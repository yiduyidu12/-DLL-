// 崩溃恢复测试套件
// 测试内容：
// - 正常扫描流程
// - 回调函数抛出异常时的处理
// - 无效路径处理
// - 并发扫描请求处理
//
// 验证 DLL 在各种边界条件下不会崩溃，并且能够正确处理异常。

#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <exception>
#include "FileScanner.h"

// 测试1：正常扫描流程
// 验证功能：
// - StartScan 正常启动扫描
// - 回调函数正确处理文件信息
// - 提前停止扫描（返回 false）正常工作
// - IsScanning 正确报告扫描状态
// - GetScanSummary 返回正确统计结果
void TestNormalScan() {
    try {
        std::cout << "=== Test 1: Normal Scan ===" << std::endl;
        
        // 启动扫描，扫描前11个文件后停止
        bool success = StartScan(L".", [](const ScanResult& result) {
            static int count = 0;
            if (count++ > 10) {
                std::cout << "Scanned " << count << " files, stopping early" << std::endl;
                return false;  // 返回 false 停止扫描
            }
            return true;
        });
        
        // 检查扫描是否成功启动
        if (success) {
            // 轮询等待扫描完成
            while (IsScanning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // 获取扫描汇总
            ScanSummary summary = GetScanSummary();
            std::cout << "Scan complete - Files: " << summary.totalFiles 
                      << ", Size: " << summary.totalSize << std::endl;
        } else {
            std::cout << "Scan failed" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception in TestNormalScan: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception in TestNormalScan" << std::endl;
    }
}

// 测试2：回调函数抛出异常
// 验证功能：
// - DLL 能够捕获回调函数抛出的异常
// - 扫描能够安全终止
// - 不会导致整个进程崩溃
// - 异常信息被正确记录
void TestCallbackException() {
    try {
        std::cout << "\n=== Test 2: Callback Throws Exception ===" << std::endl;
        
        // 启动扫描，在第5个文件时抛出异常
        bool success = StartScan(L".", [](const ScanResult& result) {
            static int count = 0;
            if (count++ == 5) {
                std::cout << "Throwing exception at file " << count << std::endl;
                throw std::runtime_error("Test exception from callback");
            }
            return true;
        });
        
        if (success) {
            // 等待扫描结束（即使异常也应该能正常结束）
            while (IsScanning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            ScanSummary summary = GetScanSummary();
            std::cout << "Scan ended - Files: " << summary.totalFiles << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception in TestCallbackException: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception in TestCallbackException" << std::endl;
    }
}

// 测试3：无效路径处理
// 验证功能：
// - StartScan 正确拒绝无效路径
// - 返回 false 表示失败
// - 不会导致崩溃
void TestInvalidPath() {
    try {
        std::cout << "\n=== Test 3: Invalid Path ===" << std::endl;
        
        // 尝试扫描不存在的路径
        bool success = StartScan(L"C:\\Invalid_Path_12345", [](const ScanResult& result) {
            return true;
        });
        
        // 预期返回 false（路径不存在）
        if (!success) {
            std::cout << "Expected failure" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception in TestInvalidPath: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception in TestInvalidPath" << std::endl;
    }
}

// 测试4：并发扫描请求
// 验证功能：
// - 同一时间只能有一个扫描任务
// - 第二个扫描请求被正确拒绝
// - 不会导致死锁或崩溃
void TestConcurrentScan() {
    try {
        std::cout << "\n=== Test 4: Concurrent Scan ===" << std::endl;
        
        // 启动第一个扫描（慢速扫描，持续一段时间）
        bool success1 = StartScan(L".", [](const ScanResult& result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 减慢扫描速度
            return true;
        });
        
        if (success1) {
            // 尝试启动第二个扫描（应该被拒绝）
            bool success2 = StartScan(L".", [](const ScanResult& result) {
                return true;
            });
            
            if (!success2) {
                std::cout << "Expected failure - scan already in progress" << std::endl;
            }
            
            // 等待第一个扫描完成
            while (IsScanning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    } catch (const std::exception& e) {
        std::cout << "Exception in TestConcurrentScan: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception in TestConcurrentScan" << std::endl;
    }
}

// 主函数：运行所有崩溃恢复测试
// 测试流程：
// 1. 正常扫描测试
// 2. 回调异常测试
// 3. 无效路径测试
// 4. 并发扫描测试
//
// 所有测试都包裹在 try-catch 块中，确保单个测试失败不会影响其他测试。
int main() {
    try {
        std::cout << "=== FileScanner Crash Test ===" << std::endl;
        std::cout << "Log file: FileScanner.log" << std::endl << std::endl;
        
        // 删除旧日志文件，确保本次测试日志干净
        std::remove("FileScanner.log");
        
        // 运行所有测试
        TestNormalScan();
        TestCallbackException();
        TestInvalidPath();
        TestConcurrentScan();
        
        std::cout << "\n=== Test Complete ===" << std::endl;
        std::cout << "Check FileScanner.log for details" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Exception in main: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "Unknown exception in main" << std::endl;
    }
    
    return 0;
}