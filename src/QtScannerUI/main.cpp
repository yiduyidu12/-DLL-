// FileScanner Qt UI - 应用程序入口点
// 功能：创建 Qt 应用程序实例，显示主窗口，进入事件循环
#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    // 创建 Qt 应用程序实例
    QApplication a(argc, argv);
    
    // 创建主窗口
    MainWindow w;
    
    // 显示主窗口
    w.show();
    
    // 进入事件循环
    return a.exec();
}