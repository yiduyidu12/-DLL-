# FileScanner - 多线程文件扫描 DLL

一个基于 Win32 API 和 C++ 标准库实现的高性能多线程文件扫描 DLL 模块。

## 📋 项目简介

本项目实现了一个高效的文件扫描 DLL，支持：
- **多线程并行扫描**：基于生产者-消费者模型，充分利用多核 CPU
- **实时进度回调**：扫描过程中实时通知调用方文件信息
- **灵活过滤**：支持扩展名过滤和目录排除
- **优雅停止**：支持随时终止扫描
- **错误处理**：完善的错误队列管理机制

## ✨ 功能特性

| 特性 | 说明 |
|-----|------|
| 多线程扫描 | 自动检测 CPU 核心数，创建对应数量的工作线程 |
| 实时回调 | 通过 `ScanProgressCallback` 实时返回扫描结果 |
| 文件过滤 | 支持按扩展名过滤（如 `.cpp;.h;.txt`） |
| 目录排除 | 支持排除指定目录（如 `.git;node_modules`） |
| 时间戳 | 返回文件修改时间（FILETIME 格式） |
| 优雅停止 | 通过原子标志实现无锁停止请求 |
| 错误队列 | 保存扫描过程中的错误信息，便于排查 |

## 📁 项目结构

```
DLL/
├── include/              # 公共头文件（DLL 接口）
│   ├── FileScanner.h     # 核心扫描 API 声明
│   └── Filter.h          # 过滤模块接口
├── src/                  # 源代码目录
│   ├── FileScanner/      # DLL 核心实现
│   │   ├── FileScanner.cpp    # 多线程扫描逻辑
│   │   ├── FileScanner.def    # DLL 导出定义
│   │   └── Filter.cpp         # 过滤模块实现
│   ├── TestApp/          # Win32 GUI 测试程序
│   │   └── TestApp.cpp        # Win32 窗口实现
│   └── QtScannerUI/      # Qt GUI 测试程序
│       ├── main.cpp           # Qt 入口
│       ├── mainwindow.cpp     # 主窗口实现
│       ├── mainwindow.h       # 主窗口头文件
│       ├── mainwindow.ui      # UI 设计文件
│       └── QtScannerUI.pro    # Qt 项目文件
├── test/                 # 测试用例
│   ├── test_full.cpp          # 全功能测试
│   ├── test_exception.cpp     # 异常处理测试
│   ├── test_crash.cpp         # 崩溃恢复测试
│   ├── test_safety.cpp        # 线程安全测试
│   ├── test_filter.cpp        # 过滤功能测试
│   └── test_timestamp.cpp     # 时间戳功能测试
├── scripts/              # 构建脚本
│   ├── build_vs.bat           # 编译 DLL
│   ├── build_test.bat         # 编译测试程序
│   ├── build_and_test.bat     # 完整构建测试
│   ├── demo.bat               # 功能演示
│   └── gen_test_data.ps1      # 生成测试数据
├── __test_scan_data/     # 测试数据目录
├── bin/                  # 编译输出（git 忽略）
├── obj/                  # 中间产物（git 忽略）
└── .gitignore            # Git 忽略配置
```

## 🛠️ 环境要求

| 软件 | 版本要求 | 说明 |
|-----|---------|------|
| Windows | 10/11 (x64) | 操作系统 |
| Visual Studio | 2022 | 开发环境 |
| Windows SDK | 10.0.22621.0+ | Windows API 支持 |
| Qt (可选) | 6.x | 编译 Qt UI 项目 |
| Git | 任意版本 | 版本控制 |

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/your-username/FileScanner.git
cd FileScanner
```

### 2. 编译项目

```bash
# 编译 DLL（核心模块）
scripts/build_vs.bat

# 编译 Win32 测试程序
scripts/build_test.bat

# 完整构建并测试
scripts/build_and_test.bat
```

### 3. 运行程序

```bash
# 运行 Win32 测试 UI
bin/Release/TestApp.exe

# 运行功能演示
scripts/demo.bat
```

## 📡 API 使用说明

### 基础扫描

```cpp
#include "FileScanner.h"
#include <iostream>

int main() {
    // 回调函数：处理扫描结果
    auto callback = [](const ScanResult& result) {
        std::wcout << L"File: " << result.filePath 
                   << L" Size: " << result.fileSize << L" bytes\n";
        return true;  // 返回 false 终止扫描
    };

    // 开始扫描
    bool success = StartScan(L"C:\\MyFolder", callback);
    
    // 等待扫描完成
    while (IsScanning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 获取扫描结果
    ScanSummary summary = GetScanSummary();
    std::wcout << L"Total: " << summary.totalFiles << L" files, "
               << summary.totalSize << L" bytes\n";

    return 0;
}
```

### 带过滤的扫描

```cpp
// 仅扫描 .cpp 和 .h 文件，排除 .git 和 node_modules
StartScanEx(L"C:\\Project", callback, 
            L".cpp;.h",           // 扩展名过滤
            L".git;node_modules"); // 目录排除
```

### API 列表

| 函数 | 说明 |
|-----|------|
| `StartScan()` | 基础扫描，不过滤 |
| `StartScanEx()` | 带扩展名和目录排除的扫描 |
| `StopScan()` | 停止扫描 |
| `GetScanSummary()` | 获取扫描汇总 |
| `IsScanning()` | 判断是否正在扫描 |
| `GetScanError()` | 获取最后错误 |
| `PopScanError()` | 弹出错误队列 |
| `GetErrorCount()` | 获取错误数量 |

## 🔧 技术实现

### 多线程模型

采用 **生产者-消费者模式**：

```
┌─────────────────────────────────────────────────────────┐
│                   主线程                                │
│  ┌──────────────┐                                       │
│  │ StartScan()  │ → 将根目录入队 → 启动工作线程         │
│  └──────────────┘                                       │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│                   目录队列 (线程安全)                   │
│  ┌──────────────────────────────────────┐               │
│  │  dir1 │ dir2 │ dir3 │ ...           │               │
│  └──────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────┘
                          │
           ┌──────────────┼──────────────┐
           ▼              ▼              ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│   WorkerThread1 │ │   WorkerThread2 │ │   WorkerThreadN │
│  ┌────────────┐ │ │  ┌────────────┐ │ │  ┌────────────┐ │
│  │ScanDirectory│ │ │  │ScanDirectory│ │ │  │ScanDirectory│ │
│  │ 发现子目录   │ │ │  │ 发现子目录   │ │ │  │ 发现子目录   │ │
│  │ 入队        │ │ │  │ 入队        │ │ │  │ 入队        │ │
│  └────────────┘ │ │  └────────────┘ │ │  └────────────┘ │
└─────────────────┘ └─────────────────┘ └─────────────────┘
```

### 线程同步机制

| 同步原语 | 用途 |
|---------|------|
| `std::atomic` | 停止标志、计数器（无锁） |
| `std::mutex` | 保护队列和共享数据 |
| `std::condition_variable` | 线程等待/唤醒 |
| `Qt::QueuedConnection` | Qt 跨线程信号安全传递 |

### 核心类说明

#### 1. ScanResult（扫描结果结构）

定义在 `include/FileScanner.h` 中，包含单个文件的扫描结果：

| 字段 | 类型 | 说明 |
|-----|------|------|
| `filePath` | `LPCWSTR` | 文件完整路径 |
| `fileSize` | `uint64_t` | 文件大小（字节） |
| `lastModified` | `FILETIME` | 修改时间戳 |

#### 2. ScanSummary（扫描汇总结构）

定义在 `include/FileScanner.h` 中，包含扫描完成后的统计信息：

| 字段 | 类型 | 说明 |
|-----|------|------|
| `totalFiles` | `uint64_t` | 扫描文件总数 |
| `totalSize` | `uint64_t` | 总大小（字节） |
| `directories` | `uint64_t` | 扫描目录数 |
| `scanTimeMs` | `uint64_t` | 扫描耗时（毫秒） |

#### 3. ScanProgressCallback（回调函数类型）

```cpp
typedef bool (*ScanProgressCallback)(const ScanResult& result);
```

- 参数：当前扫描到的文件信息
- 返回值：`true` 继续扫描，`false` 终止扫描

#### 4. Filter（过滤模块）

定义在 `include/Filter.h` 中，负责文件过滤逻辑：

| 方法 | 说明 |
|-----|------|
| `SetExtensions()` | 设置允许的扩展名列表 |
| `SetExcludedDirs()` | 设置排除的目录列表 |
| `MatchFile()` | 判断文件是否通过过滤 |
| `MatchDir()` | 判断目录是否需要排除 |

#### 5. WorkerThread（工作线程）

定义在 `src/FileScanner/FileScanner.cpp` 中，负责执行实际扫描任务：

| 职责 | 说明 |
|-----|------|
| 从队列取目录 | 线程安全地获取待扫描目录 |
| 扫描目录内容 | 遍历目录中的文件和子目录 |
| 调用回调函数 | 将文件信息传递给用户回调 |
| 子目录入队 | 发现子目录时加入队列 |

#### 6. Global State（全局状态管理）

用于协调多线程之间的状态：

| 状态变量 | 类型 | 说明 |
|---------|------|------|
| `g_isScanning` | `std::atomic<bool>` | 扫描状态标志 |
| `g_totalFiles` | `std::atomic<uint64_t>` | 文件计数 |
| `g_totalSize` | `std::atomic<uint64_t>` | 大小累加 |
| `g_directoryQueue` | `std::queue` + `mutex` | 目录队列 |
| `g_callback` | `ScanProgressCallback` | 用户回调函数 |

### 核心流程图

```
用户调用 StartScan(path, callback)
        │
        ▼
┌───────────────────┐
│ 检查是否正在扫描  │── 是 ──→ 返回 false（并发拒绝）
└────────┬──────────┘
         │ 否
         ▼
┌───────────────────┐
│ 初始化全局状态    │
│ 设置回调函数      │
└────────┬──────────┘
         ▼
┌───────────────────┐
│ 创建工作线程池    │
│ 数量 = CPU核心数  │
└────────┬──────────┘
         ▼
┌───────────────────┐
│ 根目录入队       │
└────────┬──────────┘
         ▼
┌───────────────────┐    ┌───────────────────┐
│ WorkerThread 循环│←───│ 从队列取目录      │
└────────┬──────────┘    └───────────────────┘
         │
         ▼
┌───────────────────┐
│ 扫描目录文件     │
│ FindFirstFileW   │
└────────┬──────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌───────┐ ┌─────────┐
│ 文件  │ │ 子目录  │
└───┬───┘ └────┬────┘
    │          │
    ▼          ▼
┌───────────┐ ┌───────────────┐
│ 调用回调  │ │ 子目录入队   │
│ callback  │ │ (线程安全)    │
└───────────┘ └───────────────┘
         │
         ▼
┌───────────────────┐
│ 队列空 + 无任务  │
│ 设置停止标志     │
└────────┬──────────┘
         ▼
┌───────────────────┐
│ 合并线程         │
│ 汇总统计结果     │
└───────────────────┘
```

## 📝 测试用例

| 测试文件 | 测试内容 |
|---------|---------|
| `test_full.cpp` | 完整功能测试 |
| `test_exception.cpp` | 异常捕获和处理 |
| `test_crash.cpp` | 崩溃恢复能力 |
| `test_safety.cpp` | 线程安全性 |
| `test_filter.cpp` | 过滤功能验证 |
| `test_timestamp.cpp` | 时间戳功能 |

## 🤝 Git 协作指南

### 提交规范

```bash
# 新增功能
git commit -m "feat: 添加文件时间戳功能"

# 修复 Bug
git commit -m "fix: 修复线程安全问题"

# 代码重构
git commit -m "refactor: 提取过滤模块"

# 文档更新
git commit -m "docs: 更新 README"
```

### 分支策略

- `main`：主分支，稳定版本
- `dev`：开发分支，日常开发
- `feature/*`：功能分支，新功能开发
- `fix/*`：修复分支，Bug 修复

## 📌 注意事项

1. **VC++ 运行时**：目标机器需要安装 [VC++ Redistributable 2022](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist)
2. **权限要求**：扫描系统目录需要管理员权限
3. **Unicode 支持**：支持中文路径和文件名
4. **Qt 环境**：编译 Qt UI 需要安装 Qt 6.x

## 📄 许可证

MIT License

---