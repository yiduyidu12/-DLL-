// Win32 图形界面测试程序，演示 FileScanner DLL 的调用方式
// 功能：
// - 浏览并选择扫描目录
// - 启动/停止文件扫描
// - 实时显示扫描进度和文件列表
// - 显示扫描汇总结果（文件数、目录数、总大小、耗时）
#include <windows.h>
#include <shlobj.h>
#include <process.h>
#include <tchar.h>
#include <cstdio>
#include "FileScanner.h"

// 设置控制台输出编码为 UTF-8，支持中文输出
#pragma execution_character_set("utf-8")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "FileScanner.lib")

// 控件 ID 定义
#define IDC_PATH_EDIT 1001      // 路径编辑框
#define IDC_BROWSE_BTN 1002     // 浏览按钮
#define IDC_START_BTN 1003      // 开始扫描按钮
#define IDC_STOP_BTN 1004       // 停止扫描按钮
#define IDC_STATUS_LABEL 1005   // 状态标签
#define IDC_FILE_LIST 1006      // 文件列表框
#define IDC_RESULT_LABEL 1007   // 结果标签

// 应用程序控件集合
struct AppControls {
    HWND hWnd;              // 主窗口句柄
    HWND hPathEdit;         // 路径编辑框
    HWND hBrowseBtn;        // 浏览按钮
    HWND hStartBtn;         // 开始扫描按钮
    HWND hStopBtn;          // 停止扫描按钮
    HWND hStatusLabel;      // 状态标签
    HWND hFileList;         // 文件列表框
    HWND hResultLabel;      // 结果标签
};

// 全局控件对象
AppControls g_controls = {0};

// 格式化文件大小为可读字符串
// 参数 size: 文件大小（字节）
//       buffer: 输出缓冲区
//       bufferSize: 缓冲区大小
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

// FILETIME 转可读时间字符串，格式 YYYY-MM-DD HH:MM
// 参数 ft: FILETIME 格式的时间戳（64位整数）
//       buffer: 输出缓冲区
//       bufferSize: 缓冲区大小
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

// 扫描进度回调，每个文件扫描到时调用
// 参数 result: 扫描结果结构体，包含文件路径、大小、修改时间等信息
// 返回: true 继续扫描，false 停止扫描
// 注意: 使用 PeekMessage 防止 UI 卡死
bool CALLBACK ScanProgress(const ScanResult& result) {
    // 格式化文件大小
    WCHAR sizeBuffer[64];
    FormatFileSize(result.fileSize, sizeBuffer, _countof(sizeBuffer));

    // 格式化文件修改时间
    WCHAR timeBuffer[32];
    FormatFileTime(result.lastModified, timeBuffer, _countof(timeBuffer));

    // 添加到文件列表
    WCHAR itemText[MAX_PATH + 100];
    swprintf_s(itemText, _countof(itemText), L"%s - %s - %s", timeBuffer, sizeBuffer, result.filePath.c_str());
    SendMessageW(g_controls.hFileList, LB_ADDSTRING, 0, (LPARAM)itemText);

    // 滚动到最新项
    int count = SendMessageW(g_controls.hFileList, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessageW(g_controls.hFileList, LB_SETTOPINDEX, count - 1, 0);
    }

    // 更新状态标签
    WCHAR status[512];
    swprintf_s(status, _countof(status), L"正在扫描: %s", result.filePath.c_str());
    SetWindowTextW(g_controls.hStatusLabel, status);

    // 处理消息队列，防止 UI 卡死
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

// 更新扫描结果标签
// 显示文件数、目录数、总大小、耗时等信息
void UpdateResult() {
    ScanSummary summary = GetScanSummary();

    // 格式化总大小
    WCHAR sizeBuffer[64];
    FormatFileSize(summary.totalSize, sizeBuffer, _countof(sizeBuffer));

    // 格式化耗时
    WCHAR timeBuffer[32];
    if (summary.scanTimeMs < 1000) {
        swprintf_s(timeBuffer, _countof(timeBuffer), L"%llu ms", summary.scanTimeMs);
    } else {
        swprintf_s(timeBuffer, _countof(timeBuffer), L"%.2f sec", summary.scanTimeMs / 1000.0);
    }

    // 显示汇总结果
    WCHAR result[512];
    swprintf_s(result, _countof(result),
        L"扫描完成:\r\n文件数: %llu\r\n目录数: %llu\r\n总大小: %s\r\n耗时: %s",
        summary.totalFiles, summary.directories, sizeBuffer, timeBuffer);
    SetWindowTextW(g_controls.hResultLabel, result);
}

// 扫描线程函数
// 参数 param: 未使用
// 流程:
//   1. 获取用户输入的扫描路径
//   2. 禁用开始/浏览按钮，启用停止按钮
//   3. 调用 StartScan 启动扫描
//   4. 轮询 IsScanning 等待扫描完成
//   5. 更新结果显示
//   6. 恢复按钮状态
void StartScanThread(PVOID param) {
    // 获取扫描路径
    WCHAR path[MAX_PATH];
    GetWindowTextW(g_controls.hPathEdit, path, MAX_PATH);

    // 禁用开始/浏览按钮，启用停止按钮
    EnableWindow(g_controls.hStartBtn, FALSE);
    EnableWindow(g_controls.hStopBtn, TRUE);
    EnableWindow(g_controls.hBrowseBtn, FALSE);
    EnableWindow(g_controls.hPathEdit, FALSE);

    // 初始化 UI 状态
    SetWindowTextW(g_controls.hStatusLabel, L"正在扫描...");
    SendMessageW(g_controls.hFileList, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_controls.hResultLabel, L"");

    // 启动扫描
    bool success = StartScan(path, ScanProgress);

    // 检查扫描是否成功启动
    if (!success) {
        wchar_t errorBuf[2048];
        if (GetScanError(errorBuf, 2048) && errorBuf[0]) {
            MessageBoxW(g_controls.hWnd, errorBuf, L"扫描错误", MB_ICONERROR);
        }
        SetWindowTextW(g_controls.hStatusLabel, L"扫描失败");
        EnableWindow(g_controls.hStartBtn, TRUE);
        EnableWindow(g_controls.hStopBtn, FALSE);
        EnableWindow(g_controls.hBrowseBtn, TRUE);
        EnableWindow(g_controls.hPathEdit, TRUE);
        _endthread();
        return;
    }

    // 等待扫描完成
    while (IsScanning()) {
        Sleep(50);
        // 处理消息队列，防止 UI 卡死
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // 更新结果
    UpdateResult();
    SetWindowTextW(g_controls.hStatusLabel, L"扫描完成");

    // 恢复按钮状态
    EnableWindow(g_controls.hStartBtn, TRUE);
    EnableWindow(g_controls.hStopBtn, FALSE);
    EnableWindow(g_controls.hBrowseBtn, TRUE);
    EnableWindow(g_controls.hPathEdit, TRUE);

    _endthread();
}

// 开始扫描按钮点击事件处理
void OnStartBtnClick() {
    _beginthread(StartScanThread, 0, NULL);
}

// 停止扫描按钮点击事件处理
void OnStopBtnClick() {
    StopScan();
}

// 浏览按钮点击事件处理
// 打开文件夹选择对话框，用户选择后更新路径编辑框
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

// 窗口过程函数
// 处理窗口消息
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 创建窗口时初始化所有控件
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            g_controls.hWnd = hwnd;

            // 路径编辑框
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

            // 文件列表框
            g_controls.hFileList = CreateWindowW(L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT,
                10, 110, 500, 250, hwnd, (HMENU)IDC_FILE_LIST, NULL, NULL);
            SendMessageW(g_controls.hFileList, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 结果标签
            g_controls.hResultLabel = CreateWindowW(L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                10, 375, 500, 80, hwnd, (HMENU)IDC_RESULT_LABEL, NULL, NULL);
            SendMessageW(g_controls.hResultLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

            break;
        }
        case WM_COMMAND: {
            // 处理按钮点击事件
            switch (LOWORD(wParam)) {
                case IDC_START_BTN:  OnStartBtnClick();  break;
                case IDC_STOP_BTN:   OnStopBtnClick();   break;
                case IDC_BROWSE_BTN: OnBrowseBtnClick(); break;
            }
            break;
        }
        case WM_DESTROY:
            // 窗口销毁时停止扫描
            StopScan();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 程序入口点
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 注册窗口类
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

    // 创建主窗口
    g_controls.hWnd = CreateWindowExW(0, L"FileScannerTest", L"文件扫描器",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 530, 500, NULL, NULL, hInstance, NULL);

    if (!g_controls.hWnd) {
        MessageBoxW(NULL, L"窗口创建失败", L"错误", MB_ICONERROR);
        return 1;
    }

    // 显示窗口
    ShowWindow(g_controls.hWnd, nCmdShow);
    UpdateWindow(g_controls.hWnd);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}