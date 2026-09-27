QT += widgets quick multimedia quickcontrols2 quicklayouts quickwidgets core5compat

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_CXXFLAGS += -fstack-protector-all
QMAKE_LFLAGS += -fstack-protector-all

SOURCES += \
    NewDebug.cpp \
    SettingsManager.cpp \
    localbuildsmanager.cpp \
    main.cpp \
    minecrafthandler.cpp \
    moddependencyresolver.cpp \
    modrinthapi.cpp

HEADERS += \
    JsonUtilities.h \
    NewDebug.h \
    QmlHandler.h \
    SettingsController.h \
    SettingsManager.h \
    localbuildsmanager.h \
    minecrafthandler.h \
    moddependencyresolver.h \
    modrinthapi.h

FORMS +=

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    qml/BuildCreatorDialog.qml \
    qml/ErrorDialog.qml \
    qml/ProgressPanel.qml \
    qml/Main.qml \

RESOURCES += \
    resources.qrc

RC_ICONS = resources/dolphin.ico