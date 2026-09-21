#include "mainwindow.h"

#include <QApplication>
#include <QWidget>
#include <QQuickWidget>
#include <QQuickView>

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    QApplication a(argc, argv);
    MainWindow w;
    QQuickWidget *qmlWidget = new QQuickWidget(&w);

    w.setMinimumSize(QSize(1000, 1000));
    w.setMaximumSize(QSize(1000, 1000));

    qmlWidget->setSource(QUrl("qrc:/qml/Main.qml"));
    qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qmlWidget->setAttribute(Qt::WA_OpaquePaintEvent);

    w.setCentralWidget(qmlWidget);
    w.show();
    return QApplication::exec();
}
