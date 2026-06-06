// Qt Scanner UI - 主窗口头文件
// 功能：
// - 提供 Qt 图形界面用于文件扫描
// - 使用 QThread 在后台执行扫描任务
// - 通过信号槽机制实现线程间通信
// - 支持浏览目录、启动/停止扫描、显示结果
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <mutex>
#include "FileScanner.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// 扫描工作线程类
// 在独立的 QThread 中运行扫描任务，通过信号与主窗口通信
class ScanWorker : public QObject {
    Q_OBJECT
public:
    // 构造函数
    // 参数 path: 要扫描的目录路径
    ScanWorker(const QString& path) : scanPath(path), stopRequested(false) {}

public slots:
    // 执行扫描任务
    // 流程：
    //   1. 重置停止标志
    //   2. 调用 StartScan 启动扫描，传入回调函数
    //   3. 回调函数中通过信号发送文件信息
    //   4. 轮询 IsScanning 等待扫描完成
    //   5. 发送扫描完成信号
    void doScan() {
        stopRequested.store(false);
        pendingFileCount.store(0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            latestFilePath.clear();
            latestFileSize = 0;
            latestLastModified = 0;
        }

        bool success = StartScan(scanPath.toStdWString().c_str(), [this](const ScanResult& result) {
            if (stopRequested.load(std::memory_order_acquire)) {
                return false;
            }

            pendingFileCount.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                latestFilePath = QString::fromStdWString(result.filePath);
                latestFileSize = result.fileSize;
                latestLastModified = result.lastModified;
            }
            return true;
        });

        // 检查扫描是否成功启动
        if (!success) {
            wchar_t buf[2048];
            GetScanError(buf, 2048);
            emit scanError(QString::fromStdWString(buf));
            return;
        }

        // 等待扫描完成
        while (IsScanning() && !stopRequested.load()) {
            QThread::msleep(50);
        }

        // 如果请求停止，调用 StopScan
        if (stopRequested.load()) {
            StopScan();
        }

        // 获取扫描结果并发送完成信号
        ScanSummary summary = GetScanSummary();
        emit scanComplete(summary);
    }

    // 请求停止扫描
    void requestStop() {
        if (stopRequested.exchange(true)) {
            return;
        }
        StopScan();
    }

    bool wasStopRequested() const {
        return stopRequested.load(std::memory_order_acquire);
    }

    std::atomic<unsigned long long> pendingFileCount{0};

    void getLatestProgress(QString& filePath, qulonglong& fileSize,
                           unsigned long long& lastModified) const {
        std::lock_guard<std::mutex> lock(progressMutex);
        filePath = latestFilePath;
        fileSize = latestFileSize;
        lastModified = latestLastModified;
    }

signals:
    // 扫描完成信号
    // 参数 summary: 扫描汇总结果
    void scanComplete(const ScanSummary& summary);
    
    // 扫描错误信号
    // 参数 error: 错误信息
    void scanError(const QString& error);

private:
    QString scanPath;
    std::atomic<bool> stopRequested;
    mutable std::mutex progressMutex;
    QString latestFilePath;
    qulonglong latestFileSize = 0;
    unsigned long long latestLastModified = 0;
};

// 主窗口类
// 提供用户界面，管理扫描任务
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // 构造函数
    // 参数 parent: 父窗口
    MainWindow(QWidget *parent = nullptr);
    
    // 析构函数
    ~MainWindow();

private slots:
    // 浏览按钮点击事件
    void onBrowseClicked();
    
    // 开始扫描按钮点击事件
    void onStartScan();
    
    // 停止扫描按钮点击事件
    void onStopScan();

    // 定时刷新扫描进度
    void refreshScanUi();
    
    // 扫描完成事件（来自 ScanWorker）
    void onScanComplete(const ScanSummary& summary);
    
    // 扫描错误事件（来自 ScanWorker）
    void onScanError(const QString& error);
    
    // 工作线程完成事件
    void onWorkerFinished();

private:
    Ui::MainWindow *ui;
    QThread* scanThread = nullptr;
    ScanWorker* scanWorker = nullptr;
    QTimer* scanUiTimer = nullptr;
    unsigned long long lastListSampleCount = 0;
    QString formatSize(qulonglong size) const;
};

#endif // MAINWINDOW_H