// 异常处理测试套件
// 测试内容：
// - 正常扫描流程
// - 回调函数抛出 std::exception
// - 回调函数抛出未知异常
// - 无效路径处理
// - 并发扫描请求拒绝
// - 空回调函数处理
// - 空路径处理
//
// 验证 DLL 在各种异常情况下的稳定性和错误报告能力。

#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <exception>
#include <cstdio>
#include "FileScanner.h"

// 安全包装函数：获取错误信息
// 将 GetScanError 的结果封装为 std::wstring，确保调用方拥有完整副本。
// 避免直接使用原始指针可能导致的悬空问题。
std::wstring GetError() {
    wchar_t buf[2048];
    if (GetScanError(buf, 2048)) return buf;
    return L"";
}

// 测试1：正常扫描
// 验证基本扫描功能正常工作，扫描前6个文件后停止。
void test_normal_scan() {
    std::cout << "\n[Test 1] Normal Scan\n";
    std::cout << "========================================\n";

    bool success = StartScan(L".", [](const ScanResult& result) {
        static int count = 0;
        if (count++ > 5) return false;  // 扫描前6个文件后停止
        return true;
    });

    if (success) {
        // 等待扫描完成
        while (IsScanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ScanSummary s = GetScanSummary();
        std::cout << "Scanned: " << s.totalFiles << " files\n";
    }
}

// 测试2：回调函数抛出 std::exception
// 验证 DLL 能够正确捕获并处理标准异常，不会导致进程崩溃。
void test_callback_throws_std_exception() {
    std::cout << "\n[Test 2] Callback throws std::exception\n";
    std::cout << "========================================\n";

    bool success = StartScan(L".", [](const ScanResult& result) {
        static int count = 0;
        if (count++ == 3) {
            // 在第4个文件时抛出标准异常
            throw std::runtime_error("Test std::exception from callback");
        }
        return true;
    });

    if (success) {
        while (IsScanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ScanSummary s = GetScanSummary();
        std::cout << "Scanned: " << s.totalFiles << " files\n";
    }

    // 检查是否捕获到异常信息
    std::wstring err = GetError();
    if (!err.empty()) {
        std::wcout << L"Error captured: " << err << L"\n";
    }
}

// 测试3：回调函数抛出未知异常
// 验证 DLL 能够正确捕获非标准异常（如 C 风格字符串），不会导致进程崩溃。
void test_callback_throws_unknown_exception() {
    std::cout << "\n[Test 3] Callback throws unknown exception\n";
    std::cout << "========================================\n";

    bool success = StartScan(L".", [](const ScanResult& result) {
        static int count = 0;
        if (count++ == 4) {
            // 抛出未知类型异常（C 风格字符串）
            throw "unknown exception string";
        }
        return true;
    });

    if (success) {
        while (IsScanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ScanSummary s = GetScanSummary();
        std::cout << "Scanned: " << s.totalFiles << " files\n";
    }

    // 检查是否捕获到异常信息
    std::wstring err2 = GetError();
    if (!err2.empty()) {
        std::wcout << L"Error captured: " << err2 << L"\n";
    }
}

// 测试4：无效路径
// 验证 DLL 正确处理不存在的路径，返回 false 并设置错误信息。
void test_invalid_path() {
    std::cout << "\n[Test 4] Invalid Path\n";
    std::cout << "========================================\n";

    bool success = StartScan(L"C:\\NonExistent_Path_12345", [](const ScanResult& result) {
        return true;
    });

    std::cout << "StartScan returned: " << (success ? "true" : "false") << "\n";

    std::wstring err2 = GetError();
    if (!err2.empty()) {
        std::wcout << L"Error: " << err2 << L"\n";
    }
}

// 测试5：并发扫描尝试
// 验证同一时间只能有一个扫描任务，第二个请求被正确拒绝。
void test_concurrent_scan() {
    std::cout << "\n[Test 5] Concurrent Scan Attempt\n";
    std::cout << "========================================\n";

    // 启动第一个扫描
    bool success1 = StartScan(L".", [](const ScanResult& result) {
        return true;
    });

    std::cout << "First scan started: " << (success1 ? "success" : "failed") << "\n";

    // 尝试启动第二个扫描（应该被拒绝）
    bool success2 = StartScan(L".", [](const ScanResult& result) {
        return true;
    });

    std::cout << "Second scan started: " << (success2 ? "success" : "failed") << "\n";

    // 等待第一个扫描完成
    if (success1) {
        while (IsScanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // 检查错误信息
    std::wstring err3 = GetError();
    if (!err3.empty()) {
        std::wcout << L"Error: " << err3 << L"\n";
    }
}

// 测试6：空回调函数
// 验证 DLL 正确处理 nullptr 回调，扫描仍能正常进行并收集统计信息。
void test_null_callback() {
    std::cout << "\n[Test 6] Null Callback\n";
    std::cout << "========================================\n";

    // 传递 nullptr 作为回调函数
    bool success = StartScan(L".", nullptr);

    std::cout << "StartScan with null callback: " << (success ? "success" : "failed") << "\n";

    if (success) {
        while (IsScanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ScanSummary s = GetScanSummary();
        std::cout << "Scanned: " << s.totalFiles << " files\n";
    }
}

// 测试7：空路径
// 验证 DLL 正确处理空路径输入，返回 false 并设置错误信息。
void test_empty_path() {
    std::cout << "\n[Test 7] Empty Path\n";
    std::cout << "========================================\n";

    // 传递空路径
    bool success = StartScan(L"", [](const ScanResult& result) {
        return true;
    });

    std::cout << "StartScan with empty path: " << (success ? "success" : "failed") << "\n";

    std::wstring err4 = GetError();
    if (!err4.empty()) {
        std::wcout << L"Error: " << err4 << L"\n";
    }
}

// 打印日志文件最后30行
// 用于调试时查看扫描过程中的详细日志信息。
void print_log_tail() {
    std::cout << "\n\n[Log File Content - Last 30 lines]\n";
    std::cout << "========================================\n";

    FILE* fp = _wfopen(L"FileScanner.log", L"r");
    if (!fp) {
        std::cout << "Cannot open log file\n";
        return;
    }

    // 定位到文件末尾附近
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);

    // 最多显示5000字节
    if (size > 5000) {
        fseek(fp, -5000, SEEK_END);
        while (fgetc(fp) != '\n'); // 跳过不完整的行
    } else {
        fseek(fp, 0, SEEK_SET);
    }

    // 逐行输出
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        printf("%s", buf);
    }
    fclose(fp);
}

// 主函数：运行所有异常处理测试
// 测试流程：
// 1. 删除旧日志文件
// 2. 依次运行7个测试用例
// 3. 等待扫描完全结束
// 4. 打印日志文件末尾内容
int main() {
    std::cout << "========================================\n";
    std::cout << "  FileScanner Exception Test Suite\n";
    std::cout << "========================================\n";

    // 删除旧日志文件
    std::remove("FileScanner.log");

    // 运行所有测试
    test_normal_scan();
    test_callback_throws_std_exception();
    test_callback_throws_unknown_exception();
    test_invalid_path();
    test_concurrent_scan();
    test_null_callback();
    test_empty_path();

    // 等待所有后台操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 打印日志文件末尾
    print_log_tail();

    std::cout << "\n========================================\n";
    std::cout << "  Test Complete\n";
    std::cout << "========================================\n";
    std::cout << "Check FileScanner.log for full log\n";

    return 0;
}