#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{

    QCoreApplication::setApplicationName("CTROC");
    QCoreApplication::setOrganizationName("INR");

    QApplication a(argc, argv);
    a.setStyleSheet("QPushButton { margin: -1px }");
    MainWindow w;
    w.setWindowTitle("CTROC");
    w.show();
    return a.exec();
}
