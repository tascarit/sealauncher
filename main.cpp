#include "SettingsManager.h"
#include "SettingsController.h"

#include <QApplication>
#include <QWidget>
#include <QQuickWidget>
#include <QQuickView>
#include <QQuickStyle>
#include <QFile>
#include <QStatusBar>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"SeaLauncher");
#endif

    QApplication a(argc, argv);
    QQuickView view;
    SettingsManager sm;
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
    view.rootContext()->setContextProperty("settings", &sm);

    view.setSource(QUrl("qrc:/qml/Main.qml"));
    view.show();

    sm.setMaxRam(sc.getNativeRam());
    sc.registerSysButtonHandlers(&view);

    return QApplication::exec();
}