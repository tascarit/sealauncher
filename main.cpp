#include "SettingsManager.h"
#include "SettingsController.h"
#include "minecrafthandler.h"
#include "QmlHandler.h"
#include "modrinthapi.h"
#include "localbuildsmanager.h"

#include <QApplication>
#include <QWidget>
#include <QQuickWidget>
#include <QQuickView>
#include <QQuickStyle>
#include <QFile>
#include <QStatusBar>
#include <QQmlContext>
#include <QTimer>

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    SetCurrentProcessExplicitAppUserModelID(L"SeaLauncher");

    QApplication a(argc, argv);
    QQuickView view;

    QQuickStyle::setStyle("Basic");
    a.setWindowIcon(QIcon(":/resources/dolphin.png"));
    a.setApplicationName(QString("SeaLauncher"));

    SettingsManager sm;
    SettingsController sc;
    MinecraftHandler mh;
    QmlHandler q;
    ModrinthApi modrinthApi;
    LocalBuildsManager localBuildsManager(&sm, &modrinthApi);
    localBuildsManager.setMinecraftHandler(&mh);

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setMinimumSize(QSize(900, 800));
    view.setMaximumSize(QSize(900, 800));
    view.setTitle("SeaLauncher");
    view.setFlags(Qt::Window | Qt::FramelessWindowHint |
                  Qt::WindowMinimizeButtonHint | Qt::WindowSystemMenuHint);
    view.setIcon(QIcon(":/resources/dolphin.png"));
    view.rootContext()->setContextProperty("settings", &sm);
    view.rootContext()->setContextProperty("minecraftHandler", &mh);
    view.rootContext()->setContextProperty("qmlHandler", &q);
    view.rootContext()->setContextProperty("settingsController", &sc);
    view.rootContext()->setContextProperty("modrinthApi", &modrinthApi);
    view.rootContext()->setContextProperty("localBuildsManager", &localBuildsManager);

    view.setSource(QUrl("qrc:/qml/Main.qml"));

    sc.setupTrayIcon(&view);

    QObject::connect(&mh, &MinecraftHandler::minecraftStarted, [&]() {
        view.hide();
    });

    QObject::connect(&mh, &MinecraftHandler::minecraftStopped, [&]() {
        view.show();
        view.raise();
        view.requestActivate();
    });

    view.show();

    mh.Initialize(&view, &q, &sm, &sc);
    //mh.reCheckBuilds(QString("Krevetka"), QString("21.1.250"), QString("neoforge"), QString("1.21.1"), QString("https://github.com/tascarit/KrevetkaPack/releases/download/0.1/Krevetka.zip"), 1, "0.1");

    sm.setMaxRam(sc.getNativeRam());
    sc.registerSysButtonHandlers(&view);

    return QApplication::exec();
}