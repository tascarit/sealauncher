#ifndef MINECRAFTHANDLER_H
#define MINECRAFTHANDLER_H

#include <QObject>
#include <QQuickItem>
#include <QWidget>
#include <QQuickView>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

class MinecraftHandler: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool buildExists READ buildExists WRITE setBuildExists NOTIFY buildExistsChanged)
public:
    explicit MinecraftHandler(QObject *parent = nullptr);

    bool buildExists() { return m_buildExists; }

    void setBuildExists(bool);

    void Initialize(QQuickView*);

    Q_INVOKABLE void reCheckBuilds(const QString&);
    Q_INVOKABLE void mainButtonClick();

signals:
    void buildExistsChanged();

private:
    QQuickView *view;

    bool m_buildExists;
    QString m_buildName;
};

#endif // MINECRAFTHANDLER_H
