#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#endif // SETTINGSCONTROLLER_H
#pragma once
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QQuickItem>
#include <QQuickWidget>
#include <QMetaMethod>
#include <QPushButton>
#include <mainwindow.h>
#include <Windows.h>

class SettingsController: public QObject {

public:
    DWORDLONG ram() const {return m_ram;}
    void changeRamSlider(QQuickWidget* widget){
        qDebug() << "DEBUG: Starting RAM setting process";

        setRamNative();
        qDebug() << "DEBUG: Found native ram count - " << m_ram << " MB";

        QObject *root = widget->rootObject();
        QObject *slider = root->findChild<QObject*>("ramSlider");

        if (slider){
            slider->setProperty("to", (int) m_ram);
            qDebug() << "DEBUG: Slider \"to\" property set to " << m_ram << " MB";
        } else {
            qDebug() << "WARN: Failed to set slider property. Only 4 GB will be available";
        }
    }

    void registerSysButtonHandlers(QQuickWidget* widget, MainWindow* wnd){
        qDebug() << "DEBUG: Registering system buttons handlers";

        QObject *root = widget->rootObject();
        QQuickItem *minimizeButton = root->findChild<QQuickItem*>("minimizeButton");
        QQuickItem *closeButton = root->findChild<QQuickItem*>("closeButton");

        QObject::connect(minimizeButton, SIGNAL(clicked()), wnd, SLOT(showMinimized()));
        QObject::connect(closeButton, SIGNAL(clicked()), wnd, SLOT(close()));

        qDebug() << "DEBUG: Registered sys buttons handlers";
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
