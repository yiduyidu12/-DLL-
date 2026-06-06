// 多线程文件扫描 DLL 核心实现
// 架构设计：采用生产者-消费者模型实现并行文件扫描
//   - WorkerThread（消费者）：从目录队列取目录，调用 ScanDirectory 扫描
//   - ScanDirectory（生产者）：扫描目录发现子目录，将子目录重新入队
// 线程同步机制：
//   - std::atomic：用于计数器和状态标志（无锁，高性能）
//   - std::mutex：保护共享数据结构（目录队列、错误队列、线程列表）
//   - std::condition_variable：实现线程等待/唤醒机制
// 性能优化策略：
//   - 线程本地日志缓冲区：减少锁争用和文件 I/O
//   - 原子操作：避免计数器的锁开销
//   - 批量统计输出：每处理100个目录输出一次统计
// 错误处理：
//   - 错误队列：保存扫描过程中的错误信息
//   - 异常捕获：捕获回调异常，不影响整体扫描
//   - 资源清理：DLL卸载时主动停止扫描
#include "FileScanner.h"
#include "Filter.h"
#include <windows.h>
#include <queue>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <stdexcept>
#include <exception>

// 匿名命名空间：封装内部实现，限制符号链接性，避免外部访问
namespace {

    // 全局状态变量
    // 扫描状态标志：true 表示扫描正在进行中（原子操作，线程安全）
    std::atomic<bool> g_isScanning(false);
    
    // 扫描停止标志：true 表示用户请求停止扫描（原子操作，线程安全）
    std::atomic<bool> g_stopRequested(false);

    // 扫描统计计数器（均为原子操作，无锁访问）
    std::atomic<unsigned long long> g_totalFiles(0);   // 扫描到的文件总数
    std::atomic<unsigned long long> g_totalSize(0);    // 扫描到的文件总大小（字节）
    std::atomic<unsigned long long> g_directories(0);  // 扫描到的目录总数
    std::atomic<unsigned long long> g_scanTimeMs(0);   // 扫描耗时（毫秒）
    std::atomic<int> g_outstandingDirs(0);             // 待处理目录数（队列中 + 正在扫描中）

    // 扫描进度回调函数指针，用于通知调用者每个扫描到的文件信息
    ScanProgressCallback g_callback = nullptr;
    
    // 同步原语
    std::mutex g_mutex;           // 保护目录队列
    std::mutex g_errorMutex;      // 保护错误队列
    std::mutex g_logMutex;        // 保护日志文件写入
    std::mutex g_threadsMutex;    // 保护工作线程列表
    std::condition_variable g_cv; // 目录队列的条件变量（队列为空时阻塞等待）
    
    // 数据结构
    std::queue<std::wstring> g_directoryQueue;  // 待扫描目录队列
    std::vector<std::thread> g_threads;         // 工作线程列表
    
    std::wstring g_lastError;                   // 最后一个错误信息
    std::deque<std::wstring> g_errorQueue;      // 错误队列（最多保存 MAX_ERRORS 条）
    static const size_t MAX_ERRORS = 100;       // 错误队列最大容量
    
    std::wstring g_scanPath;                    // 当前扫描的根路径
    std::chrono::high_resolution_clock::time_point g_startTime;  // 扫描开始时间

    // 过滤配置：用于扩展名过滤和目录排除
    ScanFilter g_filter;

    // 日志开关（默认关闭，避免大量文件时日志成为性能瓶颈）
    std::atomic<bool> g_debugLog(false);
    
    // 完成通知线程：使用 joinable 而非 detach，避免僵尸线程
    std::thread g_completionThread;
    std::mutex g_completionMutex;  // 保护完成线程

    // 辅助函数
    // 获取当前线程ID的字符串表示
    std::wstring GetThreadIdString() {
        std::wstringstream ss;
        ss << std::this_thread::get_id();
        return ss.str();
    }

    // 线程本地日志缓冲区：每个线程先写到本地 buffer，积累到一定数量后一次性刷到磁盘，
    // 大幅减少锁争用和文件 I/O 开销，提升高并发场景下的性能。
    thread_local std::vector<std::wstring> t_logBuffer;  // 线程本地日志缓冲区
    static const size_t LOG_BUFFER_SIZE = 64;            // 缓冲区大小：64条
    
    // 刷新当前线程的日志缓冲区到磁盘
    void FlushLogBuffer() {
        if (t_logBuffer.empty()) return;
        
        // 原子交换，立即清空 buffer，减少临界区长度
        std::vector<std::wstring> toFlush;
        toFlush.swap(t_logBuffer);
        
        // 加锁写入日志文件
        std::lock_guard<std::mutex> lock(g_logMutex);
        std::wofstream logFile(L"FileScanner.log", std::ios::app);
        if (logFile.is_open()) {
            for (auto& line : toFlush) {
                logFile << line << L'\n';
            }
            logFile.close();
        }
    }

    // 带调用栈信息的日志函数
    // 参数：message-日志消息，function-调用函数名，file-文件名，line-行号
    void LogWithCallStack(const std::wstring& message, const char* function, const char* file, int line) {
        // 如果日志开关未开启，直接返回
        if (!g_debugLog.load(std::memory_order_relaxed)) return;
        
        std::wstringstream ss;
        
        // 获取当前时间并格式化
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        struct tm localTime;
        localtime_s(&localTime, &time);  // 使用线程安全的 localtime_s
        
        // 格式化时间戳：YYYY-MM-DD HH:MM:SS
        ss << std::setfill(L'0')
           << std::setw(4) << localTime.tm_year + 1900 << L"-"
           << std::setw(2) << localTime.tm_mon + 1 << L"-"
           << std::setw(2) << localTime.tm_mday << L" "
           << std::setw(2) << localTime.tm_hour << L":"
           << std::setw(2) << localTime.tm_min << L":"
           << std::setw(2) << localTime.tm_sec << L" ["
           << GetThreadIdString() << L"] "
           << function << L"(" << file << L":" << line << L") - "
           << message;
        
        // 写入线程本地缓冲区
        t_logBuffer.push_back(ss.str());
        
        // 如果缓冲区已满，刷到磁盘
        if (t_logBuffer.size() >= LOG_BUFFER_SIZE) {
            FlushLogBuffer();
        }
    }

    // 扫描结束时调用，确保所有线程的日志缓冲区都被刷新
    void FlushAllLogBuffers() {
        FlushLogBuffer();  // 先刷当前线程的缓冲区
    }

    // 日志宏：自动获取调用位置信息
    #define LOG(message) LogWithCallStack(message, __FUNCTION__, __FILE__, __LINE__)

    // 设置最后错误信息
    void SetLastError(const std::wstring& error) {
        std::lock_guard<std::mutex> lock(g_errorMutex);
        
        // 更新最后错误
        g_lastError = error;
        
        // 添加到错误队列（保持队列大小不超过 MAX_ERRORS）
        if (g_errorQueue.size() >= MAX_ERRORS) {
            g_errorQueue.pop_front();  // 移除最旧的错误
        }
        g_errorQueue.push_back(error);
        
        LOG(L"LastError set: " + error);
    }

    // 线程本地性能统计：使用 thread_local 避免锁争用，每个线程独立统计，最后汇总
    thread_local unsigned long long t_totalScanTime = 0;  // 扫描总耗时（微秒）
    thread_local unsigned long long t_findFirstTime = 0;  // FindFirstFileW 耗时（微秒）
    thread_local unsigned long long t_findNextTime = 0;   // FindNextFileW 耗时（微秒）
    thread_local unsigned long long t_callbackTime = 0;   // 回调函数耗时（微秒）
    thread_local unsigned long long t_queueWaitTime = 0;  // 等待队列耗时（微秒）
    thread_local unsigned long long t_dirProcessed = 0;   // 处理的目录数
    thread_local unsigned long long t_filesProcessed = 0; // 处理的文件数

    // 核心扫描函数
    // 扫描单个目录
    // 参数：dirPath-目录路径，threadId-线程ID（用于日志区分）
    void ScanDirectory(const std::wstring& dirPath, unsigned int threadId) {
        auto scanStart = std::chrono::high_resolution_clock::now();
        t_dirProcessed++;  // 增加目录处理计数

        WIN32_FIND_DATAW findData;
        std::wstring searchPath = dirPath + L"\\*";  // 构建搜索路径
        
        // 调用 FindFirstFileW 获取目录第一个条目
        auto findStart = std::chrono::high_resolution_clock::now();
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        auto findEnd = std::chrono::high_resolution_clock::now();
        t_findFirstTime += std::chrono::duration_cast<std::chrono::microseconds>(findEnd - findStart).count();

        // 处理 FindFirstFileW 失败的情况
        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD err = ::GetLastError();
            if (err == ERROR_ACCESS_DENIED) {
                // 访问被拒绝是常见情况，仅记录日志
                LOG(L"[ScanDirectory] 拒绝访问: [" + dirPath + L"]");
            } else if (err != ERROR_FILE_NOT_FOUND) {
                // 其他错误记录到错误队列
                std::wstringstream ss;
                ss << L"[ScanDirectory] FindFirstFileW 失败, err=" << err << L", path=[" << dirPath << L"]";
                SetLastError(ss.str());
                LOG(ss.str());
            }
            return;
        }

        // 本地统计变量
        unsigned long long filesScanned = 0;
        unsigned long long dirsFound = 0;
        unsigned long long bytesProcessed = 0;
        unsigned long long callbackCount = 0;
        unsigned long long totalCallbackUs = 0;

        try {
            // 遍历目录中的所有条目
            do {
                // 检查是否需要停止扫描
                if (g_stopRequested.load()) {
                    break;
                }
                
                std::wstring name(findData.cFileName);
                
                // 跳过当前目录(.)和上级目录(..)
                if (name == L"." || name == L"..") {
                    auto nextStart = std::chrono::high_resolution_clock::now();
                    if (!FindNextFileW(hFind, &findData)) break;
                    auto nextEnd = std::chrono::high_resolution_clock::now();
                    t_findNextTime += std::chrono::duration_cast<std::chrono::microseconds>(nextEnd - nextStart).count();
                    continue;
                }

                std::wstring fullPath = dirPath + L"\\" + name;
                bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool isReparse = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

                // 跳过重解析点（如符号链接、快捷方式）
                if (isReparse) {
                    auto nextStart = std::chrono::high_resolution_clock::now();
                    if (!FindNextFileW(hFind, &findData)) break;
                    auto nextEnd = std::chrono::high_resolution_clock::now();
                    t_findNextTime += std::chrono::duration_cast<std::chrono::microseconds>(nextEnd - nextStart).count();
                    continue;
                }

                // 处理目录
                if (isDir) {
                    // 检查是否需要排除此目录
                    if (g_filter.ExcludeDir(name)) {
                        auto nextStart = std::chrono::high_resolution_clock::now();
                        if (!FindNextFileW(hFind, &findData)) break;
                        auto nextEnd = std::chrono::high_resolution_clock::now();
                        t_findNextTime += std::chrono::duration_cast<std::chrono::microseconds>(nextEnd - nextStart).count();
                        continue;
                    }
                    
                    // 将子目录加入队列
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        if (!g_stopRequested.load()) {
                            g_directoryQueue.push(fullPath);
                            g_outstandingDirs.fetch_add(1);
                            g_directories.fetch_add(1);  // 原子增加目录计数
                            g_cv.notify_one();           // 唤醒一个等待的工作线程
                            dirsFound++;
                        }
                    }
                } 
                // 处理文件
                else {
                    // 检查文件扩展名是否匹配
                    if (!g_filter.MatchFile(name)) {
                        auto nextStart = std::chrono::high_resolution_clock::now();
                        if (!FindNextFileW(hFind, &findData)) break;
                        auto nextEnd = std::chrono::high_resolution_clock::now();
                        t_findNextTime += std::chrono::duration_cast<std::chrono::microseconds>(nextEnd - nextStart).count();
                        continue;
                    }

                    // 提取文件大小和修改时间
                    unsigned long long fileSize = (static_cast<unsigned long long>(findData.nFileSizeHigh) << 32)
                                                | findData.nFileSizeLow;
                    unsigned long long lastModified = (static_cast<unsigned long long>(findData.ftLastWriteTime.dwHighDateTime) << 32)
                                                    | findData.ftLastWriteTime.dwLowDateTime;
                    
                    // 如果设置了回调函数，调用回调
                    if (g_callback) {
                        auto cbStart = std::chrono::high_resolution_clock::now();
                        try {
                            // 调用回调函数，返回 false 表示停止扫描
                            bool shouldContinue = g_callback({fullPath, fileSize, lastModified, false});
                            if (!shouldContinue) {
                                g_stopRequested.store(true);
                                break;
                            }
                        } catch (const std::exception& e) {
                            // 捕获标准异常
                            std::wstringstream ss;
                            ss << L"[ScanDirectory] 回调异常: " << e.what() << L", file=[" << fullPath << L"]";
                            SetLastError(ss.str());
                            LOG(ss.str());
                        } catch (...) {
                            // 捕获未知异常
                            std::wstringstream ss;
                            ss << L"[ScanDirectory] 回调未知异常, file=[" << fullPath << L"]";
                            SetLastError(ss.str());
                            LOG(ss.str());
                        }
                        auto cbEnd = std::chrono::high_resolution_clock::now();
                        totalCallbackUs += std::chrono::duration_cast<std::chrono::microseconds>(cbEnd - cbStart).count();
                        callbackCount++;
                    }
                    
                    // 更新全局统计
                    g_totalFiles.fetch_add(1);
                    g_totalSize.fetch_add(fileSize);
                    filesScanned++;
                    bytesProcessed += fileSize;
                }

                // 获取下一个文件/目录
                auto nextStart = std::chrono::high_resolution_clock::now();
                if (!FindNextFileW(hFind, &findData)) {
                    break;
                }
                auto nextEnd = std::chrono::high_resolution_clock::now();
                t_findNextTime += std::chrono::duration_cast<std::chrono::microseconds>(nextEnd - nextStart).count();

            } while (true);

            // 更新线程本地统计
            t_callbackTime += totalCallbackUs;
            t_filesProcessed += filesScanned;

            // 检查循环退出原因
            DWORD err = GetLastError();
            if (err != ERROR_NO_MORE_FILES) {
                LOG(L"[ScanDirectory] FindNextFileW 错误=" + std::to_wstring(err) + L", path=[" + dirPath + L"]");
            }
            
        } catch (const std::exception& e) {
            // 捕获扫描过程中的标准异常
            std::wstringstream ss;
            ss << L"[ScanDirectory] std::exception: " << e.what() << L", path=[" << dirPath << L"]";
            SetLastError(ss.str());
            LOG(ss.str());
        } catch (...) {
            // 捕获扫描过程中的未知异常
            std::wstringstream ss;
            ss << L"[ScanDirectory] 未知异常, path=[" << dirPath << L"]";
            SetLastError(ss.str());
            LOG(ss.str());
        }

        // 关闭文件句柄
        FindClose(hFind);
        
        // 更新扫描耗时
        auto scanEnd = std::chrono::high_resolution_clock::now();
        t_totalScanTime += std::chrono::duration_cast<std::chrono::microseconds>(scanEnd - scanStart).count();

        // 每处理100个目录输出一次统计信息
        if (t_dirProcessed % 100 == 0) {
            LOG(L"[Thread" + std::to_wstring(threadId) + L"] 累计统计 - 目录数: " + std::to_wstring(t_dirProcessed)
                + L", 文件数: " + std::to_wstring(t_filesProcessed)
                + L", 扫描耗时: " + std::to_wstring(t_totalScanTime / 1000) + L"ms"
                + L", FindFirst: " + std::to_wstring(t_findFirstTime / 1000) + L"ms"
                + L", FindNext: " + std::to_wstring(t_findNextTime / 1000) + L"ms"
                + L", 回调耗时: " + std::to_wstring(t_callbackTime / 1000) + L"ms");
        }
    }

    // 工作线程函数
    // 工作线程主循环
    // 参数：threadId-线程ID（用于日志区分）
    void WorkerThread(unsigned int threadId) {
        unsigned long long dirsProcessed = 0;

        try {
            // 循环处理目录队列，直到收到停止请求
            while (!g_stopRequested.load()) {
                std::wstring dir;
                
                // 加锁从队列获取目录
                {
                    std::unique_lock<std::mutex> lock(g_mutex);
                    
                    // 如果队列为空，等待通知（扫描中由 ScanDirectory 唤醒，完成时由 completionThread 唤醒）
                    while (g_directoryQueue.empty() && !g_stopRequested.load()) {
                        g_cv.wait(lock);
                    }
                    
                    // 检查是否需要退出
                    if (g_stopRequested.load()) {
                        break;
                    }
                    
                    // 获取目录并从队列移除
                    dir = g_directoryQueue.front();
                    g_directoryQueue.pop();
                }  // 解锁
                
                // 扫描目录（不加锁执行耗时操作）
                ScanDirectory(dir, threadId);
                dirsProcessed++;
                
                // 待处理目录计数减一，检查是否全部完成
                if (g_outstandingDirs.fetch_sub(1) == 1) {
                    // 最后一个待处理目录被扫描完毕
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_directoryQueue.empty() && !g_stopRequested.load()) {
                        g_stopRequested.store(true);
                        g_cv.notify_all();
                    }
                }
            }
        } catch (const std::exception& e) {
            // 捕获致命异常，记录后重新抛出
            std::wstringstream ss;
            ss << L"[WorkerThread] 致命 std::exception: " << e.what() << L", threadId=" << threadId;
            SetLastError(ss.str());
            LOG(ss.str());
            throw;
        } catch (...) {
            // 捕获未知致命异常，记录后重新抛出
            std::wstringstream ss;
            ss << L"[WorkerThread] 致命未知异常, threadId=" << threadId;
            SetLastError(ss.str());
            LOG(ss.str());
            throw;
        }

        // 退出前刷出本线程的日志缓冲区
        FlushLogBuffer();
    }
}

// 导出函数实现
extern "C" {
    // StartScan 和 StartScanEx 共享的初始化+线程启动逻辑
    static bool DoStartScan(const wchar_t* folderPath, ScanProgressCallback progressCallback,
                            const wchar_t* extensions, const wchar_t* excludeDirs) {
        LOG(L"[StartScan] 开始扫描");
        LOG(L"[StartScan] 调用, path=[" + std::wstring(folderPath ? folderPath : L"null") + L"], callback=" + (progressCallback ? L"有效" : L"空"));
        LOG(L"[StartScan] 扩展名过滤=[" + std::wstring(extensions ? extensions : L"") + L"], 排除目录=[" + std::wstring(excludeDirs ? excludeDirs : L"") + L"]");

        // 检查是否已有扫描任务在进行
        if (g_isScanning.load()) {
            SetLastError(L"已有扫描任务在进行中");
            LOG(L"[StartScan] 拒绝：已有扫描任务在进行中");
            return false;
        }
        
        // 检查路径有效性
        if (!folderPath || !*folderPath) {
            SetLastError(L"文件夹路径无效（空指针或空字符串）");
            LOG(L"[StartScan] 拒绝：文件夹路径无效");
            return false;
        }
        
        LOG(L"[StartScan] 检查路径: [" + std::wstring(folderPath) + L"]");
        DWORD attr = GetFileAttributesW(folderPath);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            DWORD err = GetLastError();
            std::wstringstream ss;
            ss << L"文件夹不存在或无法访问: [" << folderPath << L"], attr=" << attr << L", err=" << err;
            SetLastError(ss.str());
            LOG(ss.str());
            return false;
        }
        LOG(L"[StartScan] 路径有效, attr=0x" + std::to_wstring(attr));

        // 设置过滤条件
        g_filter.Clear();
        g_filter.SetExtensions(extensions);
        g_filter.SetExcludeDirs(excludeDirs);
        LOG(L"[StartScan] 过滤已解析: 扩展名=" + std::to_wstring(g_filter.ExtCount()) + L" 个, 排除目录=" + std::to_wstring(g_filter.ExcludeCount()) + L" 个");

        // 清除旧日志文件
        LOG(L"[StartScan] 清除旧日志");
        std::remove("FileScanner.log");

        // 初始化全局状态
        g_totalFiles.store(0);
        g_totalSize.store(0);
        g_directories.store(1);  // 根目录算一个
        g_outstandingDirs.store(1); // 根目录待处理
        g_scanTimeMs.store(0);
        g_callback = progressCallback;
        g_stopRequested.store(false);
        g_isScanning.store(true);
        g_lastError.clear();
        { std::lock_guard<std::mutex> lock(g_errorMutex); g_errorQueue.clear(); }
        g_scanPath = folderPath;
        g_startTime = std::chrono::high_resolution_clock::now();
        LOG(L"[StartScan] 状态初始化完成, scanning=" + std::to_wstring(g_isScanning.load()) + L", stopRequested=" + std::to_wstring(g_stopRequested.load()));

        // 清空旧队列并加入根目录
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            while (!g_directoryQueue.empty()) {
                g_directoryQueue.pop();
            }
            g_directoryQueue.push(folderPath);
            LOG(L"[StartScan] 根目录入队: [" + std::wstring(folderPath) + L"], 队列大小=1");
        }

        // 获取 CPU 核心数，创建对应数量的工作线程
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) { 
            numThreads = 4; 
            LOG(L"[StartScan] hardware_concurrency 返回 0, 回退到 4 线程"); 
        } else { 
            LOG(L"[StartScan] CPU 核心数=" + std::to_wstring(numThreads) + L", 创建 " + std::to_wstring(numThreads) + L" 个工作线程"); 
        }

        // 创建工作线程
        try {
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            for (unsigned int i = 0; i < numThreads; ++i) {
                g_threads.emplace_back(WorkerThread, i + 1);
                LOG(L"[StartScan] 线程 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(numThreads) + L" 已创建, 总数=" + std::to_wstring(g_threads.size()));
            }
        } catch (const std::exception& e) {
            std::wstringstream ss;
            ss << L"[StartScan] 线程创建失败: " << e.what() << L", 已创建 " << g_threads.size() << L" / " << numThreads;
            SetLastError(ss.str()); 
            LOG(ss.str());
            g_isScanning.store(false);
            return false;
        } catch (...) {
            std::wstringstream ss;
            ss << L"[StartScan] 线程创建失败（未知错误）, 已创建 " << g_threads.size() << L" / " << numThreads;
            SetLastError(ss.str()); 
            LOG(ss.str());
            g_isScanning.store(false);
            return false;
        }
        
        {
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            LOG(L"[StartScan] 全部 " + std::to_wstring(g_threads.size()) + L" 个线程已创建, 启动完成");
        }

        // 先 join 前一次扫描可能残留的 completion thread
        {
            std::lock_guard<std::mutex> lock(g_completionMutex);
            if (g_completionThread.joinable()) {
                g_completionThread.join();
            }
        }

        // 创建完成线程：等待所有工作线程结束并汇总结果
        std::thread completionThread([]() {
            try {
                // 等待所有工作线程结束
                {
                    std::lock_guard<std::mutex> lock(g_threadsMutex);
                    for (auto& t : g_threads) {
                        if (t.joinable()) {
                            t.join();
                        }
                    }
                    g_threads.clear();
                }

                // 计算扫描耗时
                auto endTime = std::chrono::high_resolution_clock::now();
                g_scanTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - g_startTime).count());
                
                // 刷新所有日志缓冲区
                FlushAllLogBuffers();
                
                // 标记扫描结束
                g_isScanning.store(false);

                // 输出扫描完成日志
                LOG(L"[CompletionThread] 扫描完成: 文件=" + std::to_wstring(g_totalFiles.load())
                    + L", 大小=" + std::to_wstring(g_totalSize.load() / 1048576) + L" MB"
                    + L", 目录=" + std::to_wstring(g_directories.load())
                    + L", 耗时=" + std::to_wstring(g_scanTimeMs.load()) + L" ms");
            } catch (const std::exception& e) {
                std::wstringstream ss; 
                ss << L"[CompletionThread] 异常: " << e.what();
                LOG(ss.str());
                g_isScanning.store(false);
            } catch (...) {
                LOG(L"[CompletionThread] 未知异常");
                g_isScanning.store(false);
            }
        });

        // 保存完成线程
        {
            std::lock_guard<std::mutex> lock(g_completionMutex);
            g_completionThread = std::move(completionThread);
        }

        LOG(L"[StartScan] => 返回 true, 扫描已启动");
        return true;
    }

    // 基础扫描：不限制文件类型，不排除目录
    FILESCANNER_API bool StartScan(const wchar_t* folderPath, ScanProgressCallback progressCallback) {
        return DoStartScan(folderPath, progressCallback, nullptr, nullptr);
    }

    // 带过滤的扫描
    // 参数：
    //   folderPath - 要扫描的目录路径
    //   progressCallback - 扫描进度回调函数
    //   extensions - 仅扫描这些扩展名的文件，分号分隔（如 L".cpp;.h;.txt"），nullptr 表示不过滤
    //   excludeDirs - 跳过这些名称的目录，分号分隔（如 L".git;node_modules;bin"），nullptr 表示不排除
    FILESCANNER_API bool StartScanEx(const wchar_t* folderPath, ScanProgressCallback progressCallback,
                                     const wchar_t* extensions, const wchar_t* excludeDirs) {
        return DoStartScan(folderPath, progressCallback, extensions, excludeDirs);
    }

    // 停止扫描
    FILESCANNER_API void StopScan() {
        // 设置停止标志
        g_stopRequested.store(true);
        // 唤醒所有等待的线程
        g_cv.notify_all();
        // 等待完成线程结束，确保资源清理完毕
        {
            std::lock_guard<std::mutex> lock(g_completionMutex);
            if (g_completionThread.joinable()) {
                g_completionThread.join();
            }
        }
    }

    // 获取扫描汇总结果
    FILESCANNER_API ScanSummary GetScanSummary() {
        ScanSummary summary = { 
            g_totalFiles.load(), 
            g_totalSize.load(), 
            g_directories.load(), 
            g_scanTimeMs.load() 
        };
        return summary;
    }

    // 判断是否正在扫描
    FILESCANNER_API bool IsScanning() { 
        return g_isScanning.load(); 
    }

    // 获取最后一个错误信息
    // 参数：buf-输出缓冲区，bufSize-缓冲区大小（字符数）
    // 返回：true-有错误，false-无错误
    FILESCANNER_API bool GetScanError(wchar_t* buf, size_t bufSize) {
        if (!buf || bufSize == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_errorMutex);
        if (g_lastError.empty()) { 
            buf[0] = L'\0'; 
            return false; 
        }
        size_t len = g_lastError.length();
        size_t copyLen = (len < bufSize - 1) ? len : (bufSize - 1);
        wcsncpy_s(buf, bufSize, g_lastError.c_str(), copyLen);
        buf[copyLen] = L'\0';
        return true;
    }

    // 弹出错误队列中的第一个错误
    // 参数：buf-输出缓冲区，bufSize-缓冲区大小（字符数）
    // 返回：true-成功弹出，false-队列为空
    FILESCANNER_API bool PopScanError(wchar_t* buf, size_t bufSize) {
        if (!buf || bufSize == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_errorMutex);
        if (g_errorQueue.empty()) { 
            buf[0] = L'\0'; 
            return false; 
        }
        std::wstring err = g_errorQueue.front();
        g_errorQueue.pop_front();
        size_t copyLen = (err.length() < bufSize - 1) ? err.length() : (bufSize - 1);
        wcsncpy_s(buf, bufSize, err.c_str(), copyLen);
        buf[copyLen] = L'\0';
        return true;
    }

    // 获取错误队列中的错误数量
    FILESCANNER_API int GetErrorCount() {
        std::lock_guard<std::mutex> lock(g_errorMutex);
        return static_cast<int>(g_errorQueue.size());
    }
}

// DLL 入口函数：处理加载和卸载事件
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // 禁用线程通知以减少开销（DLL_THREAD_ATTACH/DLL_THREAD_DETACH）
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // DLL 卸载时主动停止扫描，确保资源正确释放
        StopScan();
        break;
    }
    return TRUE;
}