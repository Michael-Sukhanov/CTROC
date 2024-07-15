#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{

    QCoreApplication::setApplicationName("CTROC");
    QCoreApplication::setOrganizationName("INR");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
