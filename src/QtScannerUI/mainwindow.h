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
#include <atomic>
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
        // 重置停止标志
        stopRequested.store(false);

        // 启动扫描，传入回调函数
        bool success = StartScan(scanPath.toStdWString().c_str(), [this](const ScanResult& result) {
            // 通过信号发送文件信息到主线程
            emit fileFound(QString::fromStdWString(result.filePath), result.fileSize, 
                          result.lastModified, result.isDirectory);
            // 检查是否请求停止
            return !stopRequested.load();
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
        stopRequested.store(true);
    }

signals:
    // 文件发现信号
    // 参数 filePath: 文件路径
    //       fileSize: 文件大小
    //       lastModified: 最后修改时间
    //       isDirectory: 是否为目录
    void fileFound(const QString& filePath, qulonglong fileSize, 
                   unsigned long long lastModified, bool isDirectory);
    
    // 扫描完成信号
    // 参数 summary: 扫描汇总结果
    void scanComplete(const ScanSummary& summary);
    
    // 扫描错误信号
    // 参数 error: 错误信息
    void scanError(const QString& error);

private:
    QString scanPath;              // 扫描路径
    std::atomic<bool> stopRequested; // 停止请求标志
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
    
    // 文件发现事件（来自 ScanWorker）
    void onFileFound(const QString& filePath, qulonglong fileSize, 
                     unsigned long long lastModified, bool isDirectory);
    
    // 扫描完成事件（来自 ScanWorker）
    void onScanComplete(const ScanSummary& summary);
    
    // 扫描错误事件（来自 ScanWorker）
    void onScanError(const QString& error);
    
    // 工作线程完成事件
    void onWorkerFinished();

private:
    Ui::MainWindow *ui;          // UI 对象
    QThread* scanThread = nullptr;   // 扫描线程
    ScanWorker* scanWorker = nullptr; // 扫描工作对象
    QString formatSize(qulonglong size) const; // 格式化文件大小
};

#endif // MAINWINDOW_H