// 多线程文件扫描 DLL 公共头文件
// 生产者-消费者模型：WorkerThread 消费目录队列，ScanDirectory 发现子目录后入队
#pragma once

#ifdef FILESCANNER_EXPORTS  // 定义导出宏，用于区分 DLL 编译和调用方编译
// 导出宏定义：当编译 DLL 本身时导出符号，供外部调用
#define FILESCANNER_API __declspec(dllexport)
#else
// 导入宏定义：当编译调用方时导入符号，使用 DLL 提供的功能
#define FILESCANNER_API __declspec(dllimport)
#endif

#include <functional>
#include <string>

// 扫描结果结构体，通过回调传给调用方
// 包含文件路径、大小、修改时间、是否为目录、父目录路径
struct ScanResult {
    std::wstring filePath;             // 文件完整路径
    unsigned long long fileSize;       // 文件大小（字节）
    unsigned long long lastModified;   // 最后修改时间（FILETIME 格式，100纳秒间隔）
    bool isDirectory;                  // 是否为目录
    std::wstring directoryPath;        // 父目录路径，便于按目录统计分组
};

// 扫描汇总，调用 GetScanSummary 获取
struct ScanSummary {
    unsigned long long totalFiles;     // 扫描到的文件总数
    unsigned long long totalSize;      // 所有文件总大小（字节）
    unsigned long long directories;    // 扫描到的目录总数
    unsigned long long scanTimeMs;     // 扫描耗时（毫秒）
};

// 进度回调，返回 false 可终止扫描
typedef std::function<bool(const ScanResult&)> ScanProgressCallback;

extern "C" {
    // 基础扫描：不限制文件类型，不排除目录
    FILESCANNER_API bool StartScan(const wchar_t* folderPath, ScanProgressCallback progressCallback);

    // 带过滤的扫描
    // extensions：仅扫描这些扩展名的文件，分号分隔，如 L".cpp;.h;.txt"，传空串或 nullptr 表示不过滤
    // excludeDirs：跳过这些名称的目录，分号分隔，如 L".git;node_modules;bin"，传空串或 nullptr 表示不排除
    FILESCANNER_API bool StartScanEx(const wchar_t* folderPath, ScanProgressCallback progressCallback,
                                     const wchar_t* extensions, const wchar_t* excludeDirs);

    FILESCANNER_API void StopScan();
    // 设置工作线程数，0 表示按 CPU 核心数自动；扫描开始前调用
    FILESCANNER_API void SetMaxWorkerThreads(unsigned int threadCount);
    FILESCANNER_API ScanSummary GetScanSummary();
    FILESCANNER_API bool IsScanning();
    FILESCANNER_API bool IsStopRequested();
    FILESCANNER_API bool GetScanError(wchar_t* buf, size_t bufSize);
    FILESCANNER_API bool PopScanError(wchar_t* buf, size_t bufSize);
    FILESCANNER_API int GetErrorCount();
    FILESCANNER_API void EnableDebugLog(bool enable);
}