// Qt Scanner UI - 主窗口实现
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <atomic>

// 日志文件互斥锁，保证线程安全
static QMutex g_logMutex;

// 写入带时间戳的日志到文件
// 参数 message: 日志消息
void logToFile(const QString& message) {
    QMutexLocker locker(&g_logMutex);

    QFile file("QtScannerUI.log");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        // 添加时间戳
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        out << timestamp << " - " << message << "\n";
        file.close();
    }
}

// 主窗口构造函数
// 参数 parent: 父窗口
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    logToFile("MainWindow initialized");

    // 初始化 UI
    ui->setupUi(this);
    // 初始状态：停止按钮禁用
    ui->stopBtn->setEnabled(false);

    // 连接按钮信号到槽函数
    connect(ui->browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->startBtn, &QPushButton::clicked, this, &MainWindow::onStartScan);
    connect(ui->stopBtn, &QPushButton::clicked, this, &MainWindow::onStopScan);

    logToFile("UI setup complete");
}

// 主窗口析构函数
// 清理扫描线程资源
MainWindow::~MainWindow() {
    // 清理扫描线程
    if (scanThread) {
        // 请求停止扫描
        scanWorker->requestStop();
        // 退出线程
        scanThread->quit();
        // 等待线程结束
        scanThread->wait();
        // 删除线程和工作对象
        delete scanThread;
        delete scanWorker;
    }
    delete ui;
}

// 浏览按钮点击事件处理
// 打开文件夹选择对话框
void MainWindow::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"));
    if (!dir.isEmpty()) {
        ui->pathEdit->setText(dir);
    }
}

// 开始扫描按钮点击事件处理
// 创建扫描线程并启动扫描
void MainWindow::onStartScan() {
    // 获取用户输入的路径
    QString path = ui->pathEdit->text();

    // 检查路径是否为空
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a folder"));
        return;
    }

    // 清除之前的扫描结果
    ui->fileList->clear();
    ui->resultLabel->clear();
    ui->statusBar->showMessage(tr("Scanning..."));

    // 更新 UI 状态：扫描期间禁用部分控件
    ui->startBtn->setEnabled(false);
    ui->stopBtn->setEnabled(true);
    ui->browseBtn->setEnabled(false);
    ui->pathEdit->setEnabled(false);

    // 创建工作线程
    scanWorker = new ScanWorker(path);
    scanThread = new QThread(this);
    scanWorker->moveToThread(scanThread);

    // 连接信号
    // 使用 Qt::QueuedConnection 确保线程安全的信号传递
    connect(scanThread, &QThread::started, scanWorker, &ScanWorker::doScan);
    connect(scanWorker, &ScanWorker::fileFound, this, &MainWindow::onFileFound, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanComplete, this, &MainWindow::onScanComplete, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanError, this, &MainWindow::onScanError, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanComplete, scanThread, &QThread::quit, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanError, scanThread, &QThread::quit, Qt::QueuedConnection);
    connect(scanThread, &QThread::finished, scanWorker, &QObject::deleteLater);
    connect(scanThread, &QThread::finished, this, &MainWindow::onWorkerFinished);

    // 启动线程
    scanThread->start();
}

// 停止扫描按钮点击事件处理
// 请求停止扫描
void MainWindow::onStopScan() {
    if (scanWorker) {
        scanWorker->requestStop();
        ui->statusBar->showMessage(tr("Stopping..."));
    }
}

// 文件发现事件处理
// 参数 filePath: 文件路径
//       fileSize: 文件大小
//       lastModified: 最后修改时间
//       isDirectory: 是否为目录
void MainWindow::onFileFound(const QString& filePath, qulonglong fileSize, 
                              unsigned long long lastModified, bool isDirectory) {
    // 原子计数器，统计文件数量
    static std::atomic<unsigned long long> fileCount(0);
    fileCount.fetch_add(1);

    // 确定文件类型
    QString type = isDirectory ? tr("[DIR] ") : tr("[FILE] ");
    
    // 格式化修改时间
    QString dateStr;
    if (lastModified != 0) {
        FILETIME ft;
        ft.dwLowDateTime = static_cast<DWORD>(lastModified & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(lastModified >> 32);
        SYSTEMTIME st;
        if (FileTimeToSystemTime(&ft, &st)) {
            dateStr = QString("%1-%2-%3 %4:%5:%6 ").arg(st.wYear).arg(st.wMonth, 2, 10, QChar('0'))
                       .arg(st.wDay, 2, 10, QChar('0')).arg(st.wHour, 2, 10, QChar('0'))
                       .arg(st.wMinute, 2, 10, QChar('0')).arg(st.wSecond, 2, 10, QChar('0'));
        }
    }

    // 添加到文件列表
    ui->fileList->addItem(type + dateStr + filePath + " - " + formatSize(fileSize));
    // 滚动到最新项
    ui->fileList->scrollToBottom();
}

// 扫描完成事件处理
// 参数 summary: 扫描汇总结果
void MainWindow::onScanComplete(const ScanSummary& summary) {
    // 格式化并显示扫描结果
    QString result = QString(tr("Scan Complete:\n"
                            "Files: %1\n"
                            "Folders: %2\n"
                            "Total Size: %3\n"
                            "Time: %4 ms"))
            .arg(summary.totalFiles)
            .arg(summary.directories)
            .arg(formatSize(summary.totalSize))
            .arg(summary.scanTimeMs);

    ui->resultLabel->setText(result);
    ui->statusBar->showMessage(tr("Scan Complete"));
}

// 扫描错误事件处理
// 参数 error: 错误信息
void MainWindow::onScanError(const QString& error) {
    QMessageBox::critical(this, tr("Error"), error);
    ui->statusBar->showMessage(tr("Scan Failed"));
}

// 工作线程完成事件处理
// 恢复 UI 状态
void MainWindow::onWorkerFinished() {
    scanThread = nullptr;
    scanWorker = nullptr;

    // 恢复 UI 状态
    ui->startBtn->setEnabled(true);
    ui->stopBtn->setEnabled(false);
    ui->browseBtn->setEnabled(true);
    ui->pathEdit->setEnabled(true);
}

// 格式化文件大小为可读字符串
// 参数 size: 文件大小（字节）
// 返回: 格式化后的字符串
QString MainWindow::formatSize(qulonglong size) const {
    if (size < 1024) return QString("%1 B").arg(size);
    if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 2);
    if (size < 1024 * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024), 0, 'f', 2);
    return QString("%1 GB").arg(size / (1024.0 * 1024 * 1024), 0, 'f', 2);
}