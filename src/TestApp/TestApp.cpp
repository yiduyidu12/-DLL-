// Win32 文件扫描器 GUI 测试程序
// 演示多线程文件扫描 DLL 的集成方式，包含实时进度展示和扫描控制
#include <windows.h>
#include <shlobj.h>
#include <process.h>
#include <tchar.h>
#include <cstdio>
#include <atomic>
#include "FileScanner.h"

#pragma execution_character_set("utf-8")

#pragma comment(lib, "comctl32.lib")  // 通用控件库
#pragma comment(lib, "FileScanner.lib")  // 扫描 DLL 导入库

// 控件 ID
#define IDC_PATH_EDIT      1001
#define IDC_BROWSE_BTN     1002
#define IDC_START_BTN      1003
#define IDC_STOP_BTN       1004
#define IDC_STATUS_LABEL   1005
#define IDC_FILE_LIST      1006
#define IDC_RESULT_LABEL   1007
#define TIMER_SCAN_UI      1

// 自定义窗口消息
#define WM_SCAN_COMPLETE   (WM_USER + 4)
#define WM_SCAN_FAILED     (WM_USER + 5)

// 界面控件集合
struct AppControls {
    HWND hWnd;
    HWND hPathEdit;
    HWND hBrowseBtn;
    HWND hStartBtn;
    HWND hStopBtn;
    HWND hStatusLabel;
    HWND hFileList;
    HWND hResultLabel;
};

AppControls g_controls = {0};  // 全局控件句柄集合

// UI 刷新定时器间隔（毫秒）
static const UINT SCAN_UI_TIMER_MS = 30;
// ListBox 最大保留条目数
static const int MAX_LIST_ITEMS = 1000;
// 环形缓冲区容量
static const int RING_SIZE = 16384;
// 流控阈值：环形缓冲区使用率超过此比例时，回调跳过写入以避免溢出
static const double RING_BACKPRESSURE_RATIO = 0.75;

// 扫描状态标志
static std::atomic<bool> g_acceptProgressUpdates(true);       // 是否接受进度回调写入
static std::atomic<bool> g_scanUiActive(false);               // UI 定时刷新是否激活
static std::atomic<unsigned long long> g_scannedFileCount(0); // 已扫描文件总数
static std::atomic<unsigned long long> g_droppedEntries(0);   // 流控丢弃的条目数

// 环形缓冲区：多生产者（工作线程）单消费者（UI 定时器）
struct FileEntry {
    wchar_t path[MAX_PATH];           // 文件完整路径
    unsigned long long size;          // 文件大小（字节）
    unsigned long long modified;      // 最后修改时间（FILETIME）
};
static FileEntry g_fileRing[RING_SIZE];  // 环形缓冲区存储
static std::atomic<int> g_ringWrite(0);  // 写入序列号（单调递增）
static std::atomic<int> g_ringRead(0);   // 读取序列号（单调递增）

// 字节数转换为可读字符串
void FormatFileSize(unsigned long long size, WCHAR* buffer, size_t bufferSize) {
    if (size < 1024) {
        swprintf_s(buffer, bufferSize, L"%llu B", size);
    } else if (size < 1024 * 1024) {
        swprintf_s(buffer, bufferSize, L"%.2f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        swprintf_s(buffer, bufferSize, L"%.2f MB", size / (1024.0 * 1024));
    } else {
        swprintf_s(buffer, bufferSize, L"%.2f GB", size / (1024.0 * 1024 * 1024));
    }
}

// FILETIME 数值转"YYYY-MM-DD HH:MM"格式
void FormatFileTime(unsigned long long ft, WCHAR* buffer, size_t bufferSize) {
    if (ft == 0) { swprintf_s(buffer, bufferSize, L"---"); return; }
    FILETIME fileTime;
    fileTime.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFF);
    fileTime.dwHighDateTime = static_cast<DWORD>(ft >> 32);
    SYSTEMTIME stLocal, stUtc;
    FileTimeToSystemTime(&fileTime, &stUtc);
    SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal);
    swprintf_s(buffer, bufferSize, L"%04d-%02d-%02d %02d:%02d",
        stLocal.wYear, stLocal.wMonth, stLocal.wDay, stLocal.wHour, stLocal.wMinute);
}

// 扫描进度回调（由 DLL 工作线程调用）
// 返回值：true 继续扫描，false 终止扫描
bool CALLBACK ScanProgress(const ScanResult& result) {
    if (IsStopRequested() || !g_acceptProgressUpdates.load(std::memory_order_acquire)) {
        return false;
    }

    g_scannedFileCount.fetch_add(1, std::memory_order_relaxed);

    // 流控：环形缓冲区接近写满时跳过写入，防止尚未消费的数据被覆盖
    {
        unsigned long long writePos = g_ringWrite.load(std::memory_order_acquire);
        unsigned long long readPos = g_ringRead.load(std::memory_order_acquire);
        if (writePos - readPos >= static_cast<unsigned long long>(RING_SIZE * RING_BACKPRESSURE_RATIO)) {
            g_droppedEntries.fetch_add(1, std::memory_order_relaxed);
            return true;  // 跳过写入，但继续扫描
        }
    }

    // 原子分配环形缓冲区槽位
    int idx = g_ringWrite.fetch_add(1, std::memory_order_relaxed) % RING_SIZE;
    FileEntry& entry = g_fileRing[idx];
    wcsncpy_s(entry.path, _countof(entry.path), result.filePath.c_str(), _TRUNCATE);
    entry.size = result.fileSize;
    entry.modified = result.lastModified;
    // 发布屏障：确保以上非原子写入对 UI 线程的 acquire 读可见
    std::atomic_thread_fence(std::memory_order_release);

    return true;
}

// UI 定时器回调：从环形缓冲区读取文件条目并刷新列表
void RefreshScanUi() {
    unsigned long long fileCount = g_scannedFileCount.load(std::memory_order_relaxed);

    int readPos = g_ringRead.load(std::memory_order_acquire);
    int writePos = g_ringWrite.load(std::memory_order_acquire);
    int pending = writePos - readPos;

    if (pending > 0) {
        // 只取最新 MAX_LIST_ITEMS 条，更早的直接跳过
        if (pending > MAX_LIST_ITEMS) {
            readPos = writePos - MAX_LIST_ITEMS;
        }

        // 禁用重绘期间批量重建列表，避免每次 LB_ADDSTRING 触发 WM_PAINT
        SendMessageW(g_controls.hFileList, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g_controls.hFileList, LB_RESETCONTENT, 0, 0);

        while (readPos < writePos) {
            int idx = readPos % RING_SIZE;
            FileEntry& entry = g_fileRing[idx];
            if (entry.path[0] == L'\0') { ++readPos; continue; }

            WCHAR sizeBuffer[64];
            FormatFileSize(entry.size, sizeBuffer, _countof(sizeBuffer));
            WCHAR timeBuffer[32];
            FormatFileTime(entry.modified, timeBuffer, _countof(timeBuffer));
            WCHAR itemText[MAX_PATH + 100];
            swprintf_s(itemText, _countof(itemText), L"%s - %s - %s", timeBuffer, sizeBuffer, entry.path);
            SendMessageW(g_controls.hFileList, LB_ADDSTRING, 0, (LPARAM)itemText);
            ++readPos;
        }

        SendMessageW(g_controls.hFileList, WM_SETREDRAW, TRUE, 0);
    }
    // 更新消费位置
    g_ringRead.store(readPos, std::memory_order_release);

    // 滚动到列表末尾
    int count = SendMessageW(g_controls.hFileList, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessageW(g_controls.hFileList, LB_SETTOPINDEX, count - 1, 0);
    }

    // 更新状态栏
    WCHAR status[512];
    unsigned long long dropped = g_droppedEntries.load(std::memory_order_relaxed);
    if (fileCount == 0) {
        swprintf_s(status, _countof(status), L"正在扫描...");
    } else if (dropped > 0) {
        swprintf_s(status, _countof(status), L"正在扫描: 已发现 %llu 个文件 (显示采样中...)", fileCount);
    } else {
        swprintf_s(status, _countof(status), L"正在扫描: 已发现 %llu 个文件", fileCount);
    }
    SetWindowTextW(g_controls.hStatusLabel, status);
}

// 显示扫描汇总结果
void UpdateResult(bool stopped) {
    ScanSummary summary = GetScanSummary();

    WCHAR sizeBuffer[64];
    FormatFileSize(summary.totalSize, sizeBuffer, _countof(sizeBuffer));

    WCHAR timeBuffer[32];
    if (summary.scanTimeMs < 1000) {
        swprintf_s(timeBuffer, _countof(timeBuffer), L"%llu ms", summary.scanTimeMs);
    } else {
        swprintf_s(timeBuffer, _countof(timeBuffer), L"%.2f sec", summary.scanTimeMs / 1000.0);
    }

    unsigned long long dropped = g_droppedEntries.load(std::memory_order_relaxed);
    WCHAR result[512];
    if (dropped > 0) {
        swprintf_s(result, _countof(result),
            L"%s\r\n文件数: %llu\r\n目录数: %llu\r\n总大小: %s\r\n耗时: %s\r\n流控丢弃: %llu 条",
            stopped ? L"扫描已停止:" : L"扫描完成:",
            summary.totalFiles, summary.directories, sizeBuffer, timeBuffer, dropped);
    } else {
        swprintf_s(result, _countof(result),
            L"%s\r\n文件数: %llu\r\n目录数: %llu\r\n总大小: %s\r\n耗时: %s",
            stopped ? L"扫描已停止:" : L"扫描完成:",
            summary.totalFiles, summary.directories, sizeBuffer, timeBuffer);
    }
    SetWindowTextW(g_controls.hResultLabel, result);
}

// 设置扫描中可操作的控件状态
void SetScanControlsEnabled(bool enabled) {
    EnableWindow(g_controls.hStartBtn, enabled ? TRUE : FALSE);
    EnableWindow(g_controls.hBrowseBtn, enabled ? TRUE : FALSE);
    EnableWindow(g_controls.hPathEdit, enabled ? TRUE : FALSE);
}

// 开始扫描：初始化 UI 状态并启动定时器
void BeginScanUi() {
    g_acceptProgressUpdates.store(true, std::memory_order_release);
    g_scannedFileCount.store(0, std::memory_order_relaxed);
    g_ringWrite.store(0, std::memory_order_release);
    g_ringRead.store(0, std::memory_order_release);
    g_droppedEntries.store(0, std::memory_order_relaxed);
    ZeroMemory(g_fileRing, sizeof(g_fileRing));

    SetScanControlsEnabled(false);
    EnableWindow(g_controls.hStopBtn, TRUE);
    SetWindowTextW(g_controls.hStatusLabel, L"正在扫描...（可按 Esc 停止）");
    SendMessageW(g_controls.hFileList, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_controls.hResultLabel, L"");

    g_scanUiActive.store(true, std::memory_order_release);
    SetTimer(g_controls.hWnd, TIMER_SCAN_UI, SCAN_UI_TIMER_MS, NULL);
    RefreshScanUi();
}

// 结束扫描：停止定时器，刷新最后数据并显示汇总
void EndScanUi(bool stopped) {
    g_scanUiActive.store(false, std::memory_order_release);
    KillTimer(g_controls.hWnd, TIMER_SCAN_UI);
    RefreshScanUi();
    UpdateResult(stopped);
    SetWindowTextW(g_controls.hStatusLabel, stopped ? L"扫描已停止" : L"扫描完成");
    EnableWindow(g_controls.hStopBtn, FALSE);
    SetScanControlsEnabled(true);
}

// 扫描工作线程入口
void StartScanThread(PVOID param) {
    (void)param;

    WCHAR path[MAX_PATH];
    GetWindowTextW(g_controls.hPathEdit, path, MAX_PATH);

    bool success = StartScan(path, ScanProgress);
    if (!success) {
        PostMessageW(g_controls.hWnd, WM_SCAN_FAILED, 0, 0);
        _endthread();
        return;
    }

    // 等待扫描完成
    while (IsScanning()) {
        Sleep(20);
    }

    const BOOL stopped = IsStopRequested() ? TRUE : FALSE;
    PostMessageW(g_controls.hWnd, WM_SCAN_COMPLETE, stopped, 0);
    _endthread();
}

// 开始按钮
void OnStartBtnClick() {
    if (IsScanning() || g_scanUiActive.load(std::memory_order_acquire)) {
        return;
    }

    SetMaxWorkerThreads(0);  // 0 = 自动检测 CPU 核心数
    BeginScanUi();
    _beginthread(StartScanThread, 0, NULL);
}

// 停止按钮
void OnStopBtnClick() {
    if (!IsScanning() && !g_scanUiActive.load(std::memory_order_acquire)) {
        return;
    }

    EnableWindow(g_controls.hStopBtn, FALSE);
    g_acceptProgressUpdates.store(false, std::memory_order_release);

    // 立即截断 UI 更新并显示当前快照
    KillTimer(g_controls.hWnd, TIMER_SCAN_UI);
    RefreshScanUi();
    UpdateResult(true);
    SetWindowTextW(g_controls.hStatusLabel, L"扫描已停止");
    g_scanUiActive.store(false, std::memory_order_release);

    StopScan();
}

// 浏览按钮：打开文件夹选择对话框
void OnBrowseBtnClick() {
    BROWSEINFOW bi = {0};
    bi.lpszTitle = L"选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            SetWindowTextW(g_controls.hPathEdit, path);
        }
        CoTaskMemFree(pidl);
    }
}

// 主窗口消息处理
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            g_controls.hWnd = hwnd;

            // 路径输入框
            g_controls.hPathEdit = CreateWindowW(L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                10, 10, 400, 25, hwnd, (HMENU)IDC_PATH_EDIT, NULL, NULL);
            SendMessageW(g_controls.hPathEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 浏览按钮
            g_controls.hBrowseBtn = CreateWindowW(L"BUTTON", L"浏览",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                420, 10, 80, 25, hwnd, (HMENU)IDC_BROWSE_BTN, NULL, NULL);
            SendMessageW(g_controls.hBrowseBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 开始扫描按钮
            g_controls.hStartBtn = CreateWindowW(L"BUTTON", L"开始扫描",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 45, 100, 25, hwnd, (HMENU)IDC_START_BTN, NULL, NULL);
            SendMessageW(g_controls.hStartBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 停止扫描按钮（初始禁用）
            g_controls.hStopBtn = CreateWindowW(L"BUTTON", L"停止扫描",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                120, 45, 100, 25, hwnd, (HMENU)IDC_STOP_BTN, NULL, NULL);
            SendMessageW(g_controls.hStopBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 状态标签
            g_controls.hStatusLabel = CreateWindowW(L"STATIC", L"就绪",
                WS_CHILD | WS_VISIBLE,
                10, 80, 500, 20, hwnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageW(g_controls.hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 文件列表
            g_controls.hFileList = CreateWindowW(L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                10, 110, 500, 250, hwnd, (HMENU)IDC_FILE_LIST, NULL, NULL);
            SendMessageW(g_controls.hFileList, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 结果汇总区域
            g_controls.hResultLabel = CreateWindowW(L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                10, 375, 500, 80, hwnd, (HMENU)IDC_RESULT_LABEL, NULL, NULL);
            SendMessageW(g_controls.hResultLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

            break;
        }
        case WM_TIMER: {
            if (wParam == TIMER_SCAN_UI && g_scanUiActive.load(std::memory_order_acquire)) {
                RefreshScanUi();
            }
            break;
        }
        case WM_SCAN_COMPLETE: {
            if (g_scanUiActive.load(std::memory_order_acquire)) {
                EndScanUi(wParam != 0);
            } else {
                // 手动停止后后台线程收尾，仅恢复控件状态
                EnableWindow(g_controls.hStopBtn, FALSE);
                SetScanControlsEnabled(true);
            }
            break;
        }
        case WM_SCAN_FAILED: {
            g_scanUiActive.store(false, std::memory_order_release);
            KillTimer(hwnd, TIMER_SCAN_UI);
            wchar_t errorBuf[2048];
            if (GetScanError(errorBuf, 2048) && errorBuf[0]) {
                MessageBoxW(hwnd, errorBuf, L"扫描错误", MB_ICONERROR);
            }
            SetWindowTextW(g_controls.hStatusLabel, L"扫描失败");
            EnableWindow(g_controls.hStopBtn, FALSE);
            SetScanControlsEnabled(true);
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_START_BTN:  OnStartBtnClick();  break;
                case IDC_STOP_BTN:   OnStopBtnClick();   break;
                case IDC_BROWSE_BTN: OnBrowseBtnClick(); break;
            }
            break;
        }
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_SCAN_UI);
            StopScan();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 程序入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FileScannerTest";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"窗口注册失败", L"错误", MB_ICONERROR);
        return 1;
    }

    g_controls.hWnd = CreateWindowExW(0, L"FileScannerTest", L"文件扫描器",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 530, 500, NULL, NULL, hInstance, NULL);

    if (!g_controls.hWnd) {
        MessageBoxW(NULL, L"窗口创建失败", L"错误", MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_controls.hWnd, nCmdShow);
    UpdateWindow(g_controls.hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Esc 键快速停止扫描
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (IsScanning() || g_scanUiActive.load(std::memory_order_acquire))) {
            OnStopBtnClick();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
