#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#endif // SETTINGSCONTROLLER_H
#pragma once
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QQuickItem>
#include <QQuickView>
#include <QMetaMethod>
#include <QPushButton>
#include <mainwindow.h>
#include <Windows.h>
#include <shobjidl.h>

class SettingsController: public QObject {
    Q_OBJECT
public:
    explicit SettingsController(QObject *parent = nullptr) : QObject(parent) {}

    DWORDLONG ram() const {return m_ram;}
    void changeRamSlider(QQuickView* view){
        qDebug() << "DEBUG: Starting RAM setting process";

        setRamNative();
        qDebug() << "DEBUG: Found native ram count - " << m_ram << " MB";

        QObject *root = view->rootObject();
        QObject *slider = root->findChild<QObject*>("ramSlider");

        if (slider){
            slider->setProperty("to", (int) m_ram);
            qDebug() << "DEBUG: Slider \"to\" property set to " << m_ram << " MB";
        } else {
            qDebug() << "WARN: Failed to set slider property. Only 4 GB will be available";
        }
    }

    void registerSysButtonHandlers(QQuickView* view){
        qDebug() << "DEBUG: Registering system buttons handlers";

        QObject *root = view->rootObject();
        QQuickItem *minimizeButton = root->findChild<QQuickItem*>("minimizeButton");
        QQuickItem *closeButton = root->findChild<QQuickItem*>("closeButton");

        QObject::connect(minimizeButton, SIGNAL(clicked()), view, SLOT(showMinimized()));
        QObject::connect(closeButton,    SIGNAL(clicked()), view, SLOT(close()));

        qDebug() << "DEBUG: Registered sys buttons handlers";
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

signals:
    void setRam(const DWORDLONG number){ if (m_ram != number) { m_ram = number; emit ramChanged(); }}
    void setRamNative(){
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(MEMORYSTATUSEX);

        if (GlobalMemoryStatusEx(&mem)){
            m_ram = mem.ullTotalPhys / (1024*1024);
        }
    }
    void ramChanged();

private:
    DWORDLONG m_ram = 4096;
};
