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
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>

class CloseGuard : public QObject {
public:
    CloseGuard(MinecraftHandler* mh, SettingsController* sc, QQuickView* view)
        : QObject(view), m_mh(mh), m_sc(sc), m_view(view) {}
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == m_view && ev->type() == QEvent::Close) {
            if (m_mh->busy()) {
                ev->ignore();
                m_view->hide();
                m_sc->notify(QStringLiteral("SeaLauncher"),
                             QStringLiteral("Установка продолжается в фоне. Откройте лаунчер из значка в трее."));
                return true;
            }
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    MinecraftHandler* m_mh;
    SettingsController* m_sc;
    QQuickView* m_view;
};

namespace {
const QString kInstanceKey = QStringLiteral("SeaLauncher-single-instance");

bool notifyRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(kInstanceKey);
    if (!socket.waitForConnected(300)) return false;
    socket.write("show");
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}
}

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    SetCurrentProcessExplicitAppUserModelID(L"SeaLauncher");

    QApplication a(argc, argv);
    a.setApplicationName(QString("SeaLauncher"));
    a.setWindowIcon(QIcon(":/resources/dolphin.png"));
    a.setQuitOnLastWindowClosed(false);

    if (notifyRunningInstance()) return 0;

    QLocalServer instanceServer;
    QLocalServer::removeServer(kInstanceKey);
    instanceServer.listen(kInstanceKey);

    QQuickStyle::setStyle("Basic");

    QQuickView view;

    SettingsManager sm;
    SettingsController sc;
    MinecraftHandler mh;
    QmlHandler q;
    ModrinthApi modrinthApi;
    LocalBuildsManager localBuildsManager(&sm, &modrinthApi);
    localBuildsManager.setMinecraftHandler(&mh);

    sm.setMaxRam(qMax(2048, static_cast<int>(sc.getNativeRam()) - 1024));
    mh.Initialize(&view, &q, &sm, &sc);

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

    if (view.status() == QQuickView::Error || !view.rootObject()) {
        QString details;
        for (const auto& err : view.errors()) details += err.toString() + "\n";
        QMessageBox::critical(nullptr, "SeaLauncher",
                              "Не удалось загрузить интерфейс лаунчера.\n\n" + details);
        return 1;
    }

    sc.setupTrayIcon(&view);

    auto bringToFront = [&view]() {
        view.show();
        view.raise();
        view.requestActivate();
    };

    QObject::connect(&mh, &MinecraftHandler::minecraftStarted, &view, [&view]() { view.hide(); });
    QObject::connect(&mh, &MinecraftHandler::minecraftStopped, &view, bringToFront);

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &view, [&]() {
        while (QLocalSocket* s = instanceServer.nextPendingConnection()) {
            s->deleteLater();
            bringToFront();
        }
    });

    view.installEventFilter(new CloseGuard(&mh, &sc, &view));

    view.show();
    sc.registerSysButtonHandlers(&view);

    return QApplication::exec();
}