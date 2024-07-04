#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow *w;
    if(argc == 1) w = new MainWindow();
    if(argc == 2) w = new MainWindow(QString(argv[1]));
    if(argc == 3) w = new MainWindow(QString(argv[1]), QString(argv[2]).toUInt());
    w->show();
    return a.exec();
}
