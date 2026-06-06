// 时间戳测试套件
// 测试内容：
// - 验证 lastModified 字段包含有效的文件时间戳
// - 演示如何将 FILETIME 格式转换为可读的日期时间字符串
// - 验证时间戳转换的正确性
//
// 文件时间戳格式说明：
// - ScanResult.lastModified 是一个 64 位整数，表示 Windows FILETIME 结构
// - FILETIME 是自 1601 年 1 月 1 日以来的 100 纳秒间隔数
// - 需要通过 FileTimeToSystemTime 和 SystemTimeToTzSpecificLocalTime 转换为本地时间

#include <windows.h>
#include <iostream>
#include <cstdio>
#include "FileScanner.h"

// 主函数：时间戳测试
// 测试流程：
// 1. 设置控制台输出编码为 UTF-8（支持中文）
// 2. 删除旧日志文件
// 3. 启动扫描，扫描前10个文件
// 4. 对每个文件，将 lastModified 转换为可读的本地时间
// 5. 输出文件路径、大小和修改时间
int main() {
    // 设置控制台输出编码为 UTF-8，支持中文路径输出
    SetConsoleOutputCP(CP_UTF8);
    
    // 删除旧日志文件，确保本次测试日志干净
    std::remove("FileScanner.log");

    // 计数器：记录扫描到的文件数
    int count = 0;
    
    // 启动扫描，处理前10个文件
    bool ok = StartScan(L".", [&](const ScanResult& r) {
        count++;
        
        // 只输出前10个文件的详细信息
        if (count <= 10) {
            // 将 64 位 FILETIME 转换为 FILETIME 结构
            FILETIME ft;
            ft.dwLowDateTime = static_cast<DWORD>(r.lastModified & 0xFFFFFFFF);
            ft.dwHighDateTime = static_cast<DWORD>(r.lastModified >> 32);
            
            // 转换为系统时间（UTC）和本地时间
            SYSTEMTIME stLocal, stUtc;
            FileTimeToSystemTime(&ft, &stUtc);
            SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal);
            
            // 输出文件信息：序号、路径、大小、修改时间
            std::wcout << L"  #" << count
                       << L" [" << r.filePath << L"]"           // 文件完整路径
                       << L" " << r.fileSize << L" bytes"       // 文件大小（字节）
                       << L" " << stLocal.wYear << L"-"         // 年份
                       << stLocal.wMonth << L"-"                // 月份
                       << stLocal.wDay << L" "                  // 日期
                       << stLocal.wHour << L":"                 // 小时
                       << stLocal.wMinute << L"\n";             // 分钟
        }
        
        // 扫描到10个文件后停止
        return count < 10;
    });

    // 检查扫描是否成功启动
    if (!ok) {
        wchar_t buf[512];
        GetScanError(buf, 512);
        std::wcout << L"StartScan failed: " << buf << L"\n";
        return 1;
    }

    // 等待扫描完成
    while (IsScanning()) {
        Sleep(100);
    }
    
    // 输出最终结果
    std::wcout << L"\nDone. Scanned " << count << L" files.\n";
    return 0;
}