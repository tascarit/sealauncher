#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QQuickWindow>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setQuickWidget(QQuickWidget *qmlWidget){
    this->m_quickWidget = qmlWidget;
    this->m_backgroundVideo = qmlWidget->findChild<QQuickItem*>("background");
}

void MainWindow::changeEvent(QEvent *event){
    if (event->type() == QEvent::WindowStateChange && isMinimized() && m_backgroundVideo) {
        QMetaObject::invokeMethod(m_backgroundVideo, "stop", Qt::QueuedConnection);
    } else if (event->type() == QEvent::WindowStateChange && m_backgroundVideo) {
        QMetaObject::invokeMethod(m_backgroundVideo, "play", Qt::QueuedConnection);
    }
}