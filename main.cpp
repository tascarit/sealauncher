#include "mainwindow.h"

#include <QApplication>
#include <QWidget>
#include <QQuickWidget>
#include <QQuickView>
#include <QQuickStyle>
#include <QFile>
#include <QStatusBar>

#include "SettingsController.h"

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"SeaLauncher");
#endif

    QApplication a(argc, argv);
    MainWindow w;
    QQuickView view;
    SettingsController sc;

    QQuickStyle::setStyle("Basic");
    a.setWindowIcon(QIcon(":/resources/dolphin.png"));
    a.setApplicationName(QString("SeaLauncher"));

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setMinimumSize(QSize(900, 800));
    view.setMaximumSize(QSize(900, 800));
    view.setTitle("SeaLauncher");
    view.setFlags(Qt::Window | Qt::FramelessWindowHint |
                  Qt::WindowMinimizeButtonHint | Qt::WindowSystemMenuHint);
    view.setIcon(QIcon(":/resources/dolphin.png"));

    view.setSource(QUrl("qrc:/qml/Main.qml"));
    view.show();

    sc.changeRamSlider(&view);
    sc.registerSysButtonHandlers(&view);

    return QApplication::exec();
}