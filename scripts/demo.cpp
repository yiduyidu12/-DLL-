// 完整功能演示
// 演示 FileScanner DLL 的主要功能：
// - 基础扫描（无回调）
// - 实时回调（显示文件信息）
// - 扩展名过滤
// - 目录排除
// - 中途停止
// - 参数校验
// - 错误队列管理
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include "FileScanner.h"

int main() {
    // 设置控制台输出编码为 UTF-8，支持中文输出
    SetConsoleOutputCP(CP_UTF8);
    
    // 测试目录
    const wchar_t* DIR = L"__test_scan_data";
    
    std::wcout << L"═══════════════════════════════════════\n";
    std::wcout << L"  FileScanner DLL 功能演示\n";
    std::wcout << L"═══════════════════════════════════════\n\n";

    // ========== 演示1：基础扫描（null 回调） ==========
    std::wcout << L"[1] 基础扫描（null 回调）\n";
    std::wcout << L"──────────────────────────────────────\n";
    // 传递 nullptr 作为回调，只收集统计信息
    StartScan(DIR, nullptr);
    // 等待扫描完成
    while (IsScanning()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // 获取扫描汇总结果
    ScanSummary s = GetScanSummary();
    std::wcout << L"  文件: " << s.totalFiles << L"  大小: " << s.totalSize
               << L" B  目录: " << s.directories << L"  耗时: " << s.scanTimeMs << L" ms\n";

    // ========== 演示2：实时回调（时间 + 大小 + 路径） ==========
    std::wcout << L"\n[2] 实时回调（时间 + 大小 + 路径）\n";
    std::wcout << L"──────────────────────────────────────\n";
    std::atomic<int> n(0);
    StartScan(DIR, [&](const ScanResult& r) -> bool {
        n++;
        // 只显示前8个文件
        if (n > 8) return true;
        
        // 将 FILETIME 转换为可读的日期时间
        FILETIME ft;
        ft.dwLowDateTime  = static_cast<DWORD>(r.lastModified & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(r.lastModified >> 32);
        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        
        // 输出文件信息：日期、大小、路径
        std::wcout << L"  " << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
                   << L"  " << std::setw(8) << r.fileSize << L" B  [" << r.filePath << L"]\n";
        return true;
    });
    while (IsScanning()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::wcout << L"  共 " << n << L" 个\n";

    // ========== 演示3：扩展名过滤（.cpp） ==========
    std::wcout << L"\n[3] 扩展名过滤（.cpp）\n";
    std::wcout << L"──────────────────────────────────────\n";
    n = 0;
    // 使用 StartScanEx，只扫描 .cpp 文件
    StartScanEx(DIR, [&](const ScanResult& r) -> bool { n++; return true; }, L".cpp", nullptr);
    while (IsScanning()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::wcout << L"  仅 .cpp: " << GetScanSummary().totalFiles << L" 个文件  |  全部: " << s.totalFiles << L" 个\n";

    // ========== 演示4：排除目录（readonly_dir） ==========
    std::wcout << L"\n[4] 排除目录（readonly_dir）\n";
    std::wcout << L"──────────────────────────────────────\n";
    n = 0;
    // 使用 StartScanEx，排除 readonly_dir 目录
    StartScanEx(DIR, [&](const ScanResult& r) -> bool { n++; return true; }, nullptr, L"readonly_dir");
    while (IsScanning()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::wcout << L"  排除后: " << GetScanSummary().totalFiles << L" 个  (原本 " << s.totalFiles << L" 个)\n";

    // ========== 演示5：中途停止 ==========
    std::wcout << L"\n[5] 中途停止\n";
    std::wcout << L"──────────────────────────────────────\n";
    n = 0;
    // 扫描到第10个文件时调用 StopScan() 停止扫描
    StartScan(DIR, [&](const ScanResult& r) -> bool { 
        n++; 
        if (n == 10) StopScan(); 
        return true; 
    });
    while (IsScanning()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::wcout << L"  提前停止: " << GetScanSummary().totalFiles << L" 个（部分）  vs  完整: " << s.totalFiles << L" 个\n";

    // ========== 演示6：参数校验 ==========
    std::wcout << L"\n[6] 参数校验\n";
    std::wcout << L"──────────────────────────────────────\n";
    wchar_t e[512];
    // 测试不存在的路径
    StartScan(L"C:\\__no_such_path__", nullptr);
    GetScanError(e, 512); std::wcout << L"  不存在: " << e << L"\n";
    // 测试空路径
    StartScan(L"", nullptr);
    GetScanError(e, 512); std::wcout << L"  空路径: " << e << L"\n";

    // ========== 演示7：错误队列 ==========
    std::wcout << L"\n[7] 错误队列\n";
    std::wcout << L"──────────────────────────────────────\n";
    // 获取错误队列中的错误数量
    int cnt = GetErrorCount();
    std::wcout << L"  共 " << cnt << L" 条:\n";
    // 逐条弹出并显示错误信息
    for (int i = 0; i < cnt; i++) {
        wchar_t e2[512]; 
        PopScanError(e2, 512); 
        std::wcout << L"    " << e2 << L"\n";
    }
    std::wcout << L"  剩余: " << GetErrorCount() << L"\n";

    std::wcout << L"\n═══════════════════════════════════════\n";
    std::wcout << L"  演示结束。详细日志见 FileScanner.log\n";
    std::wcout << L"═══════════════════════════════════════\n";
    return 0;
}