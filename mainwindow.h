#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "client.h"
#include <qcustomplot.h>



QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QString _peer_IP = PEER_IP, quint16 _peer_port = PEER_PORT, QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QCPColorMap *colorMap;
    QCPColorScale *colorScale;
    Client *Tcpclient;

    QVector<Frame> frames;
    int currentframeIndex;

    void getFrames(ScanData *response, QVector<Frame> &);
    void runGUIControl(bool enabled);



private slots:
    void updateMap(Frame &);
    void initMap();

    void getMetaInfo (MetaInfo  *);
    void getScanState(ScanState *);
    void getScanData (ScanData  *);
    void getRunResponse(Run     *);
    void writeToFile (QVector<Frame> &vec);
    void sendRunCommand();

    void selectedFrameChanged();
    quint16 getUIcommandMask();

};
#endif // MAINWINDOW_H
