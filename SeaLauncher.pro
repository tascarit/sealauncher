QT += widgets quick multimedia quickcontrols2 quicklayouts quickwidgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_CXXFLAGS += -fstack-protector-all
QMAKE_LFLAGS += -fstack-protector-all

SOURCES += \
    NewDebug.cpp \
    SettingsManager.cpp \
    main.cpp

HEADERS += \
    NewDebug.h \
    SettingsController.h \
    SettingsManager.h

FORMS +=

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    qml/Main.qml

RESOURCES += \
    resources.qrc
