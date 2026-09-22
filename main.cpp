#include "mainwindow.h"

#include <QApplication>
#include <QWidget>
#include <QQuickWidget>
#include <QQuickView>
#include <QQuickStyle>
#include <QFile>
#include <QStatusBar>

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    QApplication a(argc, argv);
    MainWindow w;
    QQuickWidget *qmlWidget = new QQuickWidget(&w);

    QQuickStyle::setStyle("Basic");
    a.setWindowIcon(QIcon(":/resources/dolphin.png"));
    a.setApplicationName(QString("SeaLauncher"));

    w.setWindowTitle(QString("SeaLauncher"));
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.setMinimumSize(QSize(900, 800));
    w.setMaximumSize(QSize(900, 800));
    w.statusBar()->hide();

    qmlWidget->setSource(QUrl("qrc:/qml/Main.qml"));
    qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qmlWidget->setAttribute(Qt::WA_OpaquePaintEvent);

    w.setCentralWidget(qmlWidget);
    w.show();
    return QApplication::exec();
}
