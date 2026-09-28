#pragma once

#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#endif // SETTINGSCONTROLLER_H

#include "NewDebug.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QMetaMethod>
#include <QPushButton>
#include <Windows.h>
#include <shobjidl.h>
#include <QFileDialog>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QDesktopServices>

class SettingsController: public QObject {
    Q_OBJECT
public:
    explicit SettingsController(QObject *parent = nullptr) : QObject(parent) {}

    DWORDLONG ram() const {return m_ram;}
    DWORDLONG getNativeRam() {
        setRamNative();
        return m_ram;
    }

    void setupTrayIcon(QQuickView* view){
        if (m_tray) return;

        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            newDebug() << "[Tray] System tray is not available on this system";
            return;
        }

        QIcon icon(":/resources/dolphin.png");
        if (icon.isNull()) icon = QIcon(":/resources/wheat.png");
        if (icon.isNull()) icon = QIcon(":/resources/vanilla.png");
        if (icon.isNull()) icon = QIcon(":/resources/dolphin.png");
        if (icon.isNull()) {
            QPixmap pm(32, 32);
            pm.fill(QColor("#3C8527"));
            icon = QIcon(pm);
        }

        m_tray = new QSystemTrayIcon(icon, view);
        m_tray->setToolTip("SeaLauncher");

        QMenu* menu = new QMenu();
        QAction* showAction = menu->addAction("Показать лаунчер");
        QAction* quitAction = menu->addAction("Выход");

        QObject::connect(showAction, &QAction::triggered, view, [view]() {
            view->show();
            view->raise();
            view->requestActivate();
        });
        QObject::connect(quitAction, &QAction::triggered, view, [view]() {
            view->close();
        });
        QObject::connect(m_tray, &QSystemTrayIcon::activated, view, [view](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                view->show();
                view->raise();
                view->requestActivate();
            }
        });

        m_tray->setContextMenu(menu);
        m_tray->show();
        newDebug() << "[Tray] Tray icon created and shown";
    }

    void changeRamSlider(QQuickView* view){
        newDebug() << "DEBUG: Starting RAM setting process";

        setRamNative();
        newDebug() << "DEBUG: Found native ram count - " << m_ram << " MB";

        QObject *root = view->rootObject();
        QObject *slider = root->findChild<QObject*>("ramSlider");

        if (slider){
            slider->setProperty("to", (int) m_ram);
            newDebug() << "DEBUG: Slider \"to\" property set to " << m_ram << " MB";
        } else {
            newDebug() << "WARN: Failed to set slider property. Only 4 GB will be available";
        }
    }

    void registerSysButtonHandlers(QQuickView* view){
        newDebug() << "DEBUG: Registering system buttons handlers";

        QObject *root = view->rootObject();
        QQuickItem *minimizeButton = root->findChild<QQuickItem*>("minimizeButton");
        QQuickItem *closeButton = root->findChild<QQuickItem*>("closeButton");

        QObject::connect(minimizeButton, SIGNAL(clicked()), view, SLOT(showMinimized()));
        QObject::connect(closeButton,    SIGNAL(clicked()), view, SLOT(close()));

        newDebug() << "DEBUG: Registered sys buttons handlers";
    }

    void registerViewConnections(QQuickView* view){


        QQuickItem* backgroundVideo = view->rootObject()->findChild<QQuickItem*>("background");

        // ВАЖНО: захват по значению ([&] здесь был багом). view->visibilityChanged
        // срабатывает асинхронно, в любой момент после того, как этот метод
        // уже вернул управление и его стек-фрейм разрушен. Захват по ссылке
        // ([&]) означал, что и view, и backgroundVideo (локальные переменные
        // этого метода) становились dangling-ссылками сразу после выхода из
        // функции, а при первом же сворачивании/разворачивании окна — UB/краш.
        QObject::connect(view, &QWindow::visibilityChanged, view, [backgroundVideo](QWindow::Visibility v){
            if (!backgroundVideo) return;
            const bool run = (v == QWindow::Windowed || v == QWindow::Maximized ||
                              v == QWindow::FullScreen);
            QMetaObject::invokeMethod(backgroundVideo, run ? "play" : "stop",
                                      Qt::QueuedConnection);
        });
    }

    void closeProgressPanel(QQuickView* view){
        QObject* panel = view->findChild<QObject*>("progressPanel");

        if(panel){
            QMetaObject::invokeMethod(panel, "close");
        }
    }

    Q_INVOKABLE QString pickJavaExecutable(const QString& startDir = QString())
    {
        QString filter =
        QObject::tr("*.exe;;Все файлы (*.*)");

        QFileDialog dlg(nullptr, tr("Выберите исполняемый файл Java"));
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setNameFilter(filter);
        dlg.setOption(QFileDialog::DontUseNativeDialog, false);
        dlg.setWindowModality(Qt::ApplicationModal);
        if (!startDir.isEmpty()) dlg.setDirectory(startDir);

        if (dlg.exec() != QDialog::Accepted) return QString();
        const QStringList sel = dlg.selectedFiles();
        return sel.isEmpty() ? QString() : sel.first();
    }

    Q_INVOKABLE QString pickDirectory(const QString& startDir = QString())
    {
        QFileDialog dlg(nullptr, tr("Выберите директорию"));
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        dlg.setOption(QFileDialog::DontUseNativeDialog, false);
        dlg.setWindowModality(Qt::ApplicationModal);
        if (!startDir.isEmpty()) dlg.setDirectory(startDir);

        if (dlg.exec() != QDialog::Accepted) return QString();
        const QStringList sel = dlg.selectedFiles();
        return sel.isEmpty() ? QString() : sel.first();
    }

private:
    void setRamNative(){
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(MEMORYSTATUSEX);

        if (GlobalMemoryStatusEx(&mem)){
            m_ram = mem.ullTotalPhys / (1024*1024);
        }
    }

    DWORDLONG m_ram = 4096;
    QSystemTrayIcon* m_tray = nullptr;
};
