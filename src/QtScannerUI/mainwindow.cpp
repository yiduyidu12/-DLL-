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
#include <windows.h>

static QMutex g_logMutex;

void logToFile(const QString& message) {
    QMutexLocker locker(&g_logMutex);

    QFile file("QtScannerUI.log");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        out << timestamp << " - " << message << "\n";
        file.close();
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    logToFile("MainWindow initialized");

    ui->setupUi(this);
    ui->stopBtn->setEnabled(false);

    scanUiTimer = new QTimer(this);
    scanUiTimer->setInterval(100);
    connect(scanUiTimer, &QTimer::timeout, this, &MainWindow::refreshScanUi);

    connect(ui->browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->startBtn, &QPushButton::clicked, this, &MainWindow::onStartScan);
    connect(ui->stopBtn, &QPushButton::clicked, this, &MainWindow::onStopScan);

    logToFile("UI setup complete");
}

MainWindow::~MainWindow() {
    if (scanUiTimer) {
        scanUiTimer->stop();
    }
    if (scanThread) {
        scanWorker->requestStop();
        scanThread->quit();
        scanThread->wait();
        delete scanThread;
        delete scanWorker;
    }
    delete ui;
}

void MainWindow::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"));
    if (!dir.isEmpty()) {
        ui->pathEdit->setText(dir);
    }
}

void MainWindow::onStartScan() {
    if (scanThread) {
        return;
    }

    QString path = ui->pathEdit->text();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a folder"));
        return;
    }

    ui->fileList->clear();
    ui->resultLabel->clear();
    ui->statusBar->showMessage(tr("Scanning..."));
    lastListSampleCount = 0;

    ui->startBtn->setEnabled(false);
    ui->stopBtn->setEnabled(true);
    ui->browseBtn->setEnabled(false);
    ui->pathEdit->setEnabled(false);

    scanWorker = new ScanWorker(path);
    scanThread = new QThread(this);
    scanWorker->moveToThread(scanThread);

    connect(scanThread, &QThread::started, scanWorker, &ScanWorker::doScan);
    connect(scanWorker, &ScanWorker::scanComplete, this, &MainWindow::onScanComplete, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanError, this, &MainWindow::onScanError, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanComplete, scanThread, &QThread::quit, Qt::QueuedConnection);
    connect(scanWorker, &ScanWorker::scanError, scanThread, &QThread::quit, Qt::QueuedConnection);
    connect(scanThread, &QThread::finished, scanWorker, &QObject::deleteLater);
    connect(scanThread, &QThread::finished, this, &MainWindow::onWorkerFinished);

    scanThread->start();
    scanUiTimer->start();
}

void MainWindow::onStopScan() {
    if (!scanWorker || !scanThread) {
        return;
    }

    ui->stopBtn->setEnabled(false);
    scanWorker->requestStop();
    ui->statusBar->showMessage(tr("Stopping..."));
}

void MainWindow::refreshScanUi() {
    if (!scanWorker) {
        return;
    }

    QString filePath;
    qulonglong fileSize = 0;
    unsigned long long lastModified = 0;
    scanWorker->getLatestProgress(filePath, fileSize, lastModified);

    const unsigned long long fileCount = scanWorker->pendingFileCount.load(std::memory_order_relaxed);
    if (fileCount == 0) {
        ui->statusBar->showMessage(tr("Scanning..."));
    } else {
        ui->statusBar->showMessage(tr("Scanning: %1 (found %2 files)").arg(filePath).arg(fileCount));
    }

    if (fileCount > lastListSampleCount && !filePath.isEmpty()) {
        lastListSampleCount = fileCount;

        QString dateStr;
        if (lastModified != 0) {
            FILETIME ft;
            ft.dwLowDateTime = static_cast<DWORD>(lastModified & 0xFFFFFFFF);
            ft.dwHighDateTime = static_cast<DWORD>(lastModified >> 32);
            SYSTEMTIME st;
            if (FileTimeToSystemTime(&ft, &st)) {
                dateStr = QString("%1-%2-%3 %4:%5:%6 ")
                              .arg(st.wYear)
                              .arg(st.wMonth, 2, 10, QChar('0'))
                              .arg(st.wDay, 2, 10, QChar('0'))
                              .arg(st.wHour, 2, 10, QChar('0'))
                              .arg(st.wMinute, 2, 10, QChar('0'))
                              .arg(st.wSecond, 2, 10, QChar('0'));
            }
        }

        ui->fileList->addItem(tr("[FILE] ") + dateStr + filePath + " - " + formatSize(fileSize));
        while (ui->fileList->count() > 500) {
            delete ui->fileList->takeItem(0);
        }
        ui->fileList->scrollToBottom();
    }
}

void MainWindow::onScanComplete(const ScanSummary& summary) {
    scanUiTimer->stop();
    refreshScanUi();

    QString resultTitle = (scanWorker && scanWorker->wasStopRequested())
                              ? tr("Scan Stopped:")
                              : tr("Scan Complete:");

    QString result = QString(tr("%1\n"
                                "Files: %2\n"
                                "Folders: %3\n"
                                "Total Size: %4\n"
                                "Time: %5 ms"))
                           .arg(resultTitle)
                           .arg(summary.totalFiles)
                           .arg(summary.directories)
                           .arg(formatSize(summary.totalSize))
                           .arg(summary.scanTimeMs);

    ui->resultLabel->setText(result);
    if (scanWorker && scanWorker->wasStopRequested()) {
        ui->statusBar->showMessage(tr("Scan Stopped"));
    } else {
        ui->statusBar->showMessage(tr("Scan Complete"));
    }
}

void MainWindow::onScanError(const QString& error) {
    scanUiTimer->stop();
    QMessageBox::critical(this, tr("Error"), error);
    ui->statusBar->showMessage(tr("Scan Failed"));
}

void MainWindow::onWorkerFinished() {
    scanUiTimer->stop();
    scanThread = nullptr;
    scanWorker = nullptr;

    ui->startBtn->setEnabled(true);
    ui->stopBtn->setEnabled(false);
    ui->browseBtn->setEnabled(true);
    ui->pathEdit->setEnabled(true);
}

QString MainWindow::formatSize(qulonglong size) const {
    if (size < 1024) return QString("%1 B").arg(size);
    if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 2);
    if (size < 1024 * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024), 0, 'f', 2);
    return QString("%1 GB").arg(size / (1024.0 * 1024 * 1024), 0, 'f', 2);
}
