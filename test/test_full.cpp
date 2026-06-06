// 完整扫描功能测试套件
// 测试内容：
// - 带回调的完整扫描
// - 提前停止扫描
// - 空回调扫描
// - 无效路径处理
// - 并发扫描拒绝

#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <cstdio>
#include "FileScanner.h"

// 获取扫描错误信息
std::wstring GetErr() {
    wchar_t buf[2048];
    if (GetScanError(buf, 2048)) return buf;
    return L"";
}

// 打印分隔线和测试标题
void print_sep(const char* title) {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(50, '=') << "\n";
}

// 测试1：带回调的完整扫描
// 验证功能：
// - StartScan 正常启动扫描
// - 回调函数正确接收文件信息
// - GetScanSummary 返回正确的统计结果
void test_full_scan(const wchar_t* path) {
    print_sep("Test 1: Full Scan with Callback");
    
    // 原子计数器，用于统计回调次数
    std::atomic<int> n(0);
    
    // 启动扫描，每5个文件输出一次进度
    bool ok = StartScan(path, [&](const ScanResult& r) -> bool {
        n++;
        // 每5个文件输出一条日志，避免输出过多
        if (n % 5 == 0) {
            std::wcout << L"  callback #" << n << L" [" << r.filePath << L"] " 
                       << r.fileSize << L" bytes, modified=" << r.lastModified << L"\n";
        }
        return true;  // 返回 true 继续扫描
    });
    
    // 检查扫描是否成功启动
    if (!ok) { 
        std::wcout << L"FAIL: " << GetErr() << L"\n"; 
        return; 
    }
    
    // 等待扫描完成（轮询 IsScanning 状态）
    while (IsScanning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 获取并打印扫描汇总结果
    ScanSummary s = GetScanSummary();
    std::cout << "  files=" << s.totalFiles << " size=" << s.totalSize 
              << " bytes dirs=" << s.directories << " time=" << s.scanTimeMs << "ms\n";
}

// 测试2：提前停止扫描
// 验证功能：
// - 回调函数返回 false 可终止扫描
// - 部分扫描结果正确统计
void test_stop_early(const wchar_t* path) {
    print_sep("Test 2: Early Stop");
    
    std::atomic<int> n(0);
    
    // 扫描到第10个文件时停止
    bool ok = StartScan(path, [&](const ScanResult&) -> bool {
        n++;
        if (n >= 10) { 
            std::cout << "  STOP at file #" << n << "\n"; 
            return false;  // 返回 false 停止扫描
        }
        return true;
    });
    
    if (!ok) { 
        std::wcout << L"FAIL: " << GetErr() << L"\n"; 
        return; 
    }
    
    while (IsScanning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 验证只扫描了部分文件
    std::cout << "  files=" << GetScanSummary().totalFiles << " (partial)\n";
}

// 测试3：空回调扫描
// 验证功能：
// - 回调参数为 nullptr 时扫描正常工作
// - 统计结果仍正确收集
void test_null_callback(const wchar_t* path) {
    print_sep("Test 3: Null Callback");
    
    // 传递 nullptr 作为回调函数
    if (!StartScan(path, nullptr)) { 
        std::wcout << L"FAIL: " << GetErr() << L"\n"; 
        return; 
    }
    
    while (IsScanning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    ScanSummary s = GetScanSummary();
    std::cout << "  files=" << s.totalFiles << " size=" << s.totalSize 
              << " bytes time=" << s.scanTimeMs << "ms\n";
}

// 测试4：无效路径处理
// 验证功能：
// - 空路径被正确拒绝
// - 不存在的路径被正确拒绝
// - 错误信息正确设置
void test_bad_paths() {
    print_sep("Test 4: Invalid Paths");
    
    // 测试空路径
    std::cout << "  empty path: " << (StartScan(L"", nullptr) ? "started" : "rejected") << "\n";
    std::wcout << L"    error: " << GetErr() << L"\n";
    
    // 测试不存在的路径
    std::cout << "  missing:    " << (StartScan(L"C:\\__nope__", nullptr) ? "started" : "rejected") << "\n";
    std::wcout << L"    error: " << GetErr() << L"\n";
}

// 测试5：并发扫描拒绝
// 验证功能：
// - 同一时间只能有一个扫描任务
// - 第二个扫描请求被正确拒绝
void test_concurrent_reject(const wchar_t* path) {
    print_sep("Test 5: Concurrent Rejection");
    
    // 启动第一个扫描
    bool ok1 = StartScan(path, nullptr);
    std::cout << "  scan1: " << (ok1 ? "started" : "failed") << "\n";
    
    // 尝试启动第二个扫描（应该被拒绝）
    bool ok2 = StartScan(path, nullptr);
    std::cout << "  scan2: " << (ok2 ? "started" : "rejected") << "\n";
    std::wcout << L"  error: " << GetErr() << L"\n";
    
    // 等待第一个扫描完成
    while (IsScanning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// 打印日志文件最后50行
// 用于调试时查看扫描过程中的详细日志
void print_log_tail() {
    print_sep("Log File (Last 50 lines)");
    
    FILE* fp = _wfopen(L"FileScanner.log", L"r");
    if (!fp) { 
        std::cout << "Missing FileScanner.log\n"; 
        return; 
    }
    
    // 定位到文件末尾附近（最多显示5000字节）
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, sz > 5000 ? -5000 : 0, SEEK_END);
    
    // 如果不是文件开头，跳过第一行（可能不完整）
    if (sz > 5000) {
        while (fgetc(fp) != '\n');
    }
    
    // 逐行输出
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        std::cout << buf;
    }
    
    fclose(fp);
}

// 全局变量：存储命令行传入的扫描路径
static std::wstring g_path;

// 主函数：运行所有测试用例
// 测试流程：
// 1. 解析命令行参数（可选的扫描路径）
// 2. 依次运行5个测试用例
// 3. 打印日志文件末尾内容
// 4. 输出完成信息
int main(int argc, char* argv[]) {
    // 解析命令行参数：如果提供了路径参数，转换为宽字符
    if (argc > 1) {
        int n = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0);
        g_path.resize(n);
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, &g_path[0], n);
    }
    
    // 使用提供的路径或当前目录
    const wchar_t* path = argc > 1 ? g_path.c_str() : L".";
    
    std::wcout << L"Test path: " << path << L"\n";
    
    // 依次运行所有测试，每个测试间有短暂延迟
    test_full_scan(path);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    test_stop_early(path);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    test_null_callback(path);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    test_bad_paths();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    test_concurrent_reject(path);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 打印日志文件末尾
    print_log_tail();
    
    std::cout << "\n==================================================\n";
    std::cout << "  Done. Full log: FileScanner.log\n";
    std::cout << "==================================================\n";
    
    return 0;
}