// 过滤功能测试套件
// 测试内容：
// - 无过滤扫描（全部文件）
// - 按扩展名过滤（如仅扫描 .bat）
// - 目录排除（如排除 .vs、obj）
// - 混合过滤（扩展名过滤 + 目录排除）

#include <windows.h>
#include <iostream>
#include <cstdio>
#include "FileScanner.h"

// 主函数：运行过滤功能测试
// 测试流程：
// 1. 无过滤扫描（基础扫描）
// 2. 按扩展名过滤（仅 .bat 文件）
// 3. 目录排除（排除 .vs 和 obj）
// 4. 混合使用（扩展名过滤 + 目录排除）
int main() {
    // 设置控制台输出编码为 UTF-8，支持中文输出
    SetConsoleOutputCP(CP_UTF8);

    // ========== 测试1：无过滤扫描 ==========
    std::wcout << L"--- Test 1: 无过滤（全部文件通过） ---\n";
    int count = 0;
    
    // 使用 StartScan（无过滤版本），扫描前10个文件后停止
    StartScan(L".", [&](const ScanResult& r) -> bool { 
        count++; 
        return count < 10;  // 限制只扫描前10个文件
    });
    
    // 等待扫描完成
    while (IsScanning()) {
        Sleep(50);
    }
    
    std::wcout << L"  扫描到 " << count << L" 个文件\n\n";

    // ========== 测试2：按扩展名过滤 ==========
    std::wcout << L"--- Test 2: 仅扫描 .bat 文件 ---\n";
    count = 0;
    
    // 使用 StartScanEx，指定只扫描 .bat 文件
    // 参数说明：
    //   L"." - 扫描当前目录
    //   callback - 回调函数处理每个匹配的文件
    //   L".bat" - 仅扫描 .bat 扩展名的文件
    //   nullptr - 不排除任何目录
    StartScanEx(L".", [&](const ScanResult& r) -> bool {
        count++;
        std::wcout << L"  [" << r.filePath << L"] " << r.fileSize << L" bytes\n";
        return true;
    }, L".bat", nullptr);
    
    while (IsScanning()) {
        Sleep(50);
    }
    
    std::wcout << L"  总计 " << count << L" 个 .bat 文件\n\n";

    // ========== 测试3：目录排除 ==========
    std::wcout << L"--- Test 3: 排除 .vs 和 obj 目录 ---\n";
    count = 0;
    
    // 使用 StartScanEx，排除指定目录
    // 参数说明：
    //   L".vs;obj" - 排除名为 .vs 或 obj 的目录（不区分大小写）
    StartScanEx(L".", [&](const ScanResult& r) -> bool {
        count++;
        return true;
    }, nullptr, L".vs;obj");
    
    while (IsScanning()) {
        Sleep(50);
    }
    
    ScanSummary s = GetScanSummary();
    std::wcout << L"  文件数=" << s.totalFiles << L", 目录数=" << s.directories << L"\n\n";

    // ========== 测试4：混合过滤 ==========
    std::wcout << L"--- Test 4: 混合使用：仅 .cpp + .h，排除 .vs ---\n";
    count = 0;
    
    // 同时使用扩展名过滤和目录排除
    // 参数说明：
    //   L"cpp;h" - 仅扫描 .cpp 和 .h 文件（扩展名前的点可省略）
    //   L".vs;Release" - 排除 .vs 和 Release 目录
    StartScanEx(L".", [&](const ScanResult& r) -> bool {
        count++;
        std::wcout << L"  [" << r.filePath << L"] " << r.fileSize << L" bytes\n";
        return true;
    }, L"cpp;h", L".vs;Release");
    
    while (IsScanning()) {
        Sleep(50);
    }
    
    std::wcout << L"  总计 " << count << L" 个 .cpp / .h 文件（已排除 .vs 和 Release）\n";

    return 0;
}