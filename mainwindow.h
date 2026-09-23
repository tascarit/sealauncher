#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QQuickWidget>
#include <QQuickItem>
#include <QObject>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void setQuickWidget(QQuickWidget *qmlWidget);

protected:
    void changeEvent(QEvent *event) override;

private:
    QQuickWidget *m_quickWidget;
    QQuickItem *m_backgroundVideo;
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
