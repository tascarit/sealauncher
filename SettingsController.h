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

class SettingsController: public QObject {
    Q_OBJECT
public:
    explicit SettingsController(QObject *parent = nullptr) : QObject(parent) {}

    DWORDLONG ram() const {return m_ram;}
    DWORDLONG getNativeRam() {
        setRamNative();
        return m_ram;
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


        QQuickItem* m_backgroundVideo = view->rootObject()->findChild<QQuickItem*>("background");

        QObject::connect(view, &QWindow::visibilityChanged, [&](QWindow::Visibility v){
            const bool run = (v == QWindow::Windowed || v == QWindow::Maximized ||
                              v == QWindow::FullScreen);
            QMetaObject::invokeMethod(m_backgroundVideo, run ? "play" : "stop",
                                      Qt::QueuedConnection);
        });
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
};
