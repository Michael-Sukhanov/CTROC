#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "client.h"
#include "ctrocplottables.h"
#include <qcustomplot.h>
#include <palettes.h>
#include <QSettings>
#include <QTimer>

extern quint8 nADC;
const quint32 nFramesDefault = 1024;

enum Range_Mode{
    RANGE_SINGLE_FRAME, RANGE_SAMPLING_FRAME
};


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void scanDataLoaded(ScanData*);

private:
    Ui::MainWindow *ui;
    //класс синхронизирует обновлени гистограмм и карт
    FrameMap* singleFrameMap, *meanFrameMap, *stdevFrameMap;
    FrameHist* singleFrameHist, *meanFrameHist, *stdevFrameHist;

    Range_Mode rMode;

    QString peerIP;
    quint16 peerPort;
    Client *Tcpclient;

    QLineEdit *ADCRangeLE, *setRateLE, *readStreamLE;

    QVector<Frame> frames;
    Frame meanFrame, stdevFrame, darkFrame, lightFrame;
    QString darkCalibFileName, lightCalibFileName, path;

    quint32 currentframeIndex;

    RunContent runContent;

    void getRawFrames(ScanData *response, QVector<Frame> &);
    void runGUIControl(bool enabled);
    void correctFrames(QVector<Frame>::iterator start, QVector<Frame>::iterator stop);


    quint16 getCorrespondingCommand(QWidget*);
    quint8 getCorrespondingBitNo(QWidget*);

    //для работы с ini файлом
    QSettings* settings;

    void loadSettings();
    void saveSettings();

    QMessageBox msgBox;  

    //используется для отображения, сохранения и загрузки данных с последнего скана
    ScanData *lastScanData;

    //функция получения калибровочных фреймов
    Frame getDarkFrame(ScanData*);
    Frame getLighFrame(ScanData*);

    void updateDarkCalib(QString fileName);
    void updateLightCalib(QString fileName);

    QCPRange getValueRange(QVector<Frame> &frames);
    QCPRange getValueRange(Frame&);

    QTimer timer;

private slots:

    void getMetaInfo (MetaInfo  *);
    void getScanState(ScanState *);
    void getScanData (ScanData  *);
    void getRunResponse(Run     *);
    void sendRunCommand();
    void saveImage(QCPAbstractPlottable *  plottable, int  dataIndex, QMouseEvent* evnt);

    void selectedFrameChanged();
    quint16 getUIcommandMask();

    void disableCalibration();

};
#endif // MAINWINDOW_H
