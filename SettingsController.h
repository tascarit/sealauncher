#pragma once

#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#include "NewDebug.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QMetaMethod>
#include <QFileDialog>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QDesktopServices>
#include <QPointer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shobjidl.h>

class SettingsController: public QObject {
    Q_OBJECT
public:
    explicit SettingsController(QObject *parent = nullptr) : QObject(parent) {}

    ~SettingsController() override {
        if (m_tray) m_tray->hide();
        delete m_menu;
    }

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
        if (icon.isNull()) {
            QPixmap pm(32, 32);
            pm.fill(QColor("#3C8527"));
            icon = QIcon(pm);
        }

        m_tray = new QSystemTrayIcon(icon, view);
        m_tray->setToolTip("SeaLauncher");

        m_menu = new QMenu();
        QAction* showAction = m_menu->addAction("Показать лаунчер");
        QAction* quitAction = m_menu->addAction("Выход");

        auto showView = [view]() {
            view->show();
            view->raise();
            view->requestActivate();
        };

        QObject::connect(showAction, &QAction::triggered, view, showView);
        QObject::connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
        QObject::connect(m_tray, &QSystemTrayIcon::activated, view,
                         [showView](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                                 showView();
                         });

        m_tray->setContextMenu(m_menu);
        m_tray->show();
        newDebug() << "[Tray] Tray icon created and shown";
    }

    Q_INVOKABLE void notify(const QString& title, const QString& message)
    {
        if (m_tray && m_tray->isVisible())
            m_tray->showMessage(title, message, QSystemTrayIcon::Information, 6000);
    }

    void changeRamSlider(QQuickView* view){
        newDebug() << "DEBUG: Starting RAM setting process";

        setRamNative();
        newDebug() << "DEBUG: Found native ram count - " << m_ram << " MB";

        QObject *root = view ? view->rootObject() : nullptr;
        QObject *slider = root ? root->findChild<QObject*>("ramSlider") : nullptr;

        if (slider){
            slider->setProperty("to", (int) m_ram);
            newDebug() << "DEBUG: Slider \"to\" property set to " << m_ram << " MB";
        } else {
            newDebug() << "WARN: Failed to set slider property. Only 4 GB will be available";
        }
    }

    void registerSysButtonHandlers(QQuickView* view){
        newDebug() << "DEBUG: Registering system buttons handlers";

        QObject *root = view ? view->rootObject() : nullptr;
        if (!root) {
            newDebug() << "WARN: No QML root object, system buttons not registered";
            return;
        }

        QQuickItem *minimizeButton = root->findChild<QQuickItem*>("minimizeButton");
        QQuickItem *closeButton = root->findChild<QQuickItem*>("closeButton");

        if (minimizeButton)
            QObject::connect(minimizeButton, SIGNAL(clicked()), view, SLOT(showMinimized()));
        else
            newDebug() << "WARN: minimizeButton not found";

        if (closeButton)
            QObject::connect(closeButton, SIGNAL(clicked()), view, SLOT(close()));
        else
            newDebug() << "WARN: closeButton not found";

        newDebug() << "DEBUG: Registered sys buttons handlers";
    }

    Q_INVOKABLE QString pickJavaExecutable(const QString& startDir = QString())
    {
        QFileDialog dlg(nullptr, tr("Выберите исполняемый файл Java"));
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setNameFilter(tr("Java (java.exe javaw.exe);;Исполняемые файлы (*.exe);;Все файлы (*.*)"));
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
    QMenu* m_menu = nullptr;
};

#endif