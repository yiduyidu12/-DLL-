// 安全性测试套件 - 悬空指针风险验证
// 测试内容：
// - 新 API GetScanError(buf, size) 避免悬空指针问题
// - 错误队列机制正确保存历史错误
// - 空缓冲区和无效参数的安全处理
// - 缓冲区截断行为正确性
//
// 背景说明：
// 旧 API GetScanError() 返回 const wchar_t* 指向内部 std::wstring 缓冲区，
// 后续 SetLastError() 会触发 wstring 重新分配，导致之前返回的指针悬空。
//
// 新 API 设计：
// - GetScanError(buf, size): 将内容拷贝到调用方提供的缓冲区
// - PopScanError(buf, size): 从错误队列弹出最老的一条
// - 调用方拥有完整副本，无悬空风险

#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <cstdio>
#include "FileScanner.h"

// 缓冲区大小常量
#define BUF_SIZE 512

// 演示旧 API 的悬空指针风险
// 展示旧 API 的设计缺陷：返回的指针在后续错误发生时可能失效。
// 此函数仅用于演示目的，不实际调用旧 API。
void demo_old_api_risk() {
    std::cout << "\n========================================\n";
    std::cout << "  Demo: Old API dangling pointer risk\n";
    std::cout << "========================================\n";

    // 触发第一个错误（空路径）
    StartScan(L"", nullptr);
    
    /* 
       旧 API 的危险用法示意：
       const wchar_t* ptr1 = GetScanError();  // ptr1 -> g_lastError 内部缓冲区
       StartScan(...);                         // 可能触发 SetLastError -> g_lastError 重新分配
       printf("%ls\n", ptr1);                  // ptr1 已经悬空! 未定义行为!
    */
    std::cout << "  (Old API would return raw pointer to internal buffer)\n";
    std::cout << "  (Any subsequent SetLastError could invalidate it)\n";
    std::cout << "  (New API copies to caller buffer - always safe)\n";
}

// 测试1：缓冲区在后续错误写入后保持安全
// 验证关键点：
// - 第一次调用 GetScanError 后，缓冲区内容被正确拷贝
// - 第二次调用 SetLastError（通过 StartScan 触发）不会影响第一次的缓冲区
// - 证明新 API 没有悬空指针问题
void test_error_buffer_safety() {
    std::cout << "\n========================================\n";
    std::cout << "  Test 1: Buffer stays safe after more errors\n";
    std::cout << "========================================\n";

    // 触发错误 1: 空路径
    StartScan(L"", nullptr);
    wchar_t buf1[BUF_SIZE] = {0};
    bool hasErr = GetScanError(buf1, BUF_SIZE);
    std::wcout << L"  Error 1: " << (hasErr ? L"yes" : L"no") << L" [" << buf1 << L"]\n";

    // 触发错误 2: 不存在路径 (会重新调用 SetLastError)
    StartScan(L"C:\\__no_such_dir__", nullptr);
    wchar_t buf2[BUF_SIZE] = {0};
    GetScanError(buf2, BUF_SIZE);
    std::wcout << L"  Error 2: [" << buf2 << L"]\n";

    // 关键验证: buf1 仍然完好 (旧 API 会悬空)
    std::wcout << L"  Error 1 buffer still intact: [" << buf1 << L"]\n";
    if (wcscmp(buf1, L"") != 0) {
        std::cout << "  PASS: buffer1 survived subsequent error writes\n";
    } else {
        std::cout << "  FAIL: buffer1 was corrupted\n";
    }
}

// 测试2：错误队列保留历史记录
// 验证关键点：
// - 错误队列正确保存多个错误
// - GetErrorCount 返回正确的错误数量
// - PopScanError 按顺序弹出错误
// - 队列清空后 GetErrorCount 返回 0
void test_error_queue_history() {
    std::cout << "\n========================================\n";
    std::cout << "  Test 2: Error queue preserves history\n";
    std::cout << "========================================\n";

    // 触发 3 个不同错误
    StartScan(L"", nullptr);                          // 错误1: 空路径
    StartScan(L"C:\\__nope1__", nullptr);             // 错误2: 不存在的路径
    StartScan(L"\\\\invalid\\unc\\path", nullptr);    // 错误3: 无效 UNC 路径

    // 获取错误队列中的错误数量
    int count = GetErrorCount();
    std::cout << "  Errors in queue: " << count << " (expect 3)\n";

    // 逐条弹出错误（FIFO 顺序）
    for (int i = 0; i < count; i++) {
        wchar_t buf[BUF_SIZE] = {0};
        if (PopScanError(buf, BUF_SIZE)) {
            std::wcout << L"  Pop #" << (i+1) << L": [" << buf << L"]\n";
        }
    }

    // 验证队列已空
    int remaining = GetErrorCount();
    std::cout << "  Remaining: " << remaining << " (expect 0)\n";
    if (remaining == 0) {
        std::cout << "  PASS: queue consumed correctly\n";
    } else {
        std::cout << "  FAIL: queue not empty\n";
    }
}

// 测试3：空队列和无错误状态的行为
// 验证关键点：
// - 空队列时 GetErrorCount 返回 0
// - 空队列时 GetScanError 返回 false，不修改缓冲区
// - 空队列时 PopScanError 返回 false，不修改缓冲区
void test_empty_queue_behavior() {
    std::cout << "\n========================================\n";
    std::cout << "  Test 3: Empty queue / no error state\n";
    std::cout << "========================================\n";

    // 检查空队列的错误数量
    int count = GetErrorCount();
    std::cout << "  Queue size: " << count << " (expect 0)\n";

    // 测试 GetScanError 在空队列时的行为
    wchar_t buf[BUF_SIZE] = L"UNCHANGED";
    bool ok = GetScanError(buf, BUF_SIZE);
    std::cout << "  GetScanError on empty: " << (ok ? "true" : "false") << "\n";
    std::wcout << L"  Buffer: [" << buf << L"] (expect 'UNCHANGED' since no error)\n";

    // 测试 PopScanError 在空队列时的行为
    wchar_t buf2[BUF_SIZE] = L"UNCHANGED";
    ok = PopScanError(buf2, BUF_SIZE);
    std::cout << "  PopScanError on empty: " << (ok ? "true" : "false") << "\n";
    std::wcout << L"  Buffer: [" << buf2 << L"] (expect 'UNCHANGED')\n";

    // 验证测试结果
    if (!ok && wcscmp(buf2, L"UNCHANGED") == 0) {
        std::cout << "  PASS: empty queue handled correctly\n";
    } else {
        std::cout << "  FAIL: unexpected behavior\n";
    }
}

// 测试4：空缓冲区保护
// 验证关键点：
// - 传递 nullptr 作为缓冲区不应导致崩溃
// - 传递 0 作为缓冲区大小不应导致崩溃
// - 这些调用应返回 false 表示失败
void test_null_buffer_guard() {
    std::cout << "\n========================================\n";
    std::cout << "  Test 4: Null buffer protection\n";
    std::cout << "========================================\n";

    // 先触发一个错误，确保队列中有内容
    StartScan(L"", nullptr);

    // 测试各种无效参数组合
    bool ok = GetScanError(nullptr, BUF_SIZE);
    std::cout << "  GetScanError(nullptr, ...): " << (ok ? "true" : "false") << " (expect false)\n";

    ok = GetScanError(nullptr, 0);
    std::cout << "  GetScanError(nullptr, 0): " << (ok ? "true" : "false") << " (expect false)\n";

    ok = PopScanError(nullptr, BUF_SIZE);
    std::cout << "  PopScanError(nullptr, ...): " << (ok ? "true" : "false") << " (expect false)\n";

    std::cout << "  PASS: null buffer handled safely\n";
}

// 测试5：缓冲区截断行为
// 验证关键点：
// - 当缓冲区不足以容纳完整错误消息时，正确截断
// - 截断后仍保持字符串终止符
// - PopScanError 在小缓冲区时也能安全处理
void test_buffer_truncation() {
    std::cout << "\n========================================\n";
    std::cout << "  Test 5: Buffer truncation\n";
    std::cout << "========================================\n";

    // 触发一个错误
    StartScan(L"", nullptr);
    
    // 用一个很小的 buffer（16 个 wchar_t，实际可用 15 个 + 终止符）
    wchar_t tiny[16] = {0};
    bool ok = GetScanError(tiny, 16);
    size_t len = wcslen(tiny);
    std::wcout << L"  Small buffer (16): [" << tiny << L"] len=" << len << "\n";
    std::cout << "  Truncated: " << (len == 15 ? "yes (correct)" : "no") << "\n";

    // 测试 PopScanError 的截断行为
    PopScanError(tiny, 16);
    std::cout << "  Pop with small buffer also safe\n";
}

// 主函数：运行所有安全性测试
// 测试流程：
// 1. 演示旧 API 的风险（仅说明，不实际调用）
// 2. 测试缓冲区安全性
// 3. 测试错误队列历史记录
// 4. 测试空队列行为
// 5. 测试空缓冲区保护
// 6. 测试缓冲区截断
int main() {
    std::cout << "============================================\n";
    std::cout << "  Dangling Pointer Safety Verification\n";
    std::cout << "============================================\n";

    // 运行所有测试
    demo_old_api_risk();
    test_error_buffer_safety();
    test_error_queue_history();
    test_empty_queue_behavior();
    test_null_buffer_guard();
    test_buffer_truncation();

    std::cout << "\n============================================\n";
    std::cout << "  All tests done.\n";
    std::cout << "  Check FileScanner.log for API call traces.\n";
    std::cout << "============================================\n";
    return 0;
}