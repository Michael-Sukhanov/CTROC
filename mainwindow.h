#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "client.h"
#include <qcustomplot.h>
#include <palettes.h>
#include <QSettings>

QCPColorGradient getGradient(const QList<QColor> &palette);
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
    QCPColorMap *colorMap, *colorMapMean, *colorMapStd;
    QCPColorScale *colorScale, *colorScaleMean, *colorScaleStd;
    QCPBars *meanBars, *stdBars;
    Range_Mode rMode;

    QString peerIP;
    quint16 peerPort;
    Client *Tcpclient;

    QLineEdit *ADCRangeLE, *setRateLE, *readStreamLE;

    QVector<Frame> frames;
    Frame meanFrame, stdevFrame, darkFrame, lightFrame;
    QString darkCalibFileName, lightCalibFileName;

    quint32 currentframeIndex;

    RunContent runContent;

    void getRawFrames(ScanData *response, QVector<Frame> &);
    void runGUIControl(bool enabled);
    void correctFrames(QVector<Frame>::iterator start, QVector<Frame>::iterator stop);

    quint16 getCorrespondingCommand(QWidget*);
    quint8 getCorrespondingBitNo(QWidget*);

    //для работы с ini файлами
    QSettings* settings;

    void loadSettings();
    void saveSettings();

    QMessageBox msgBox;  

    //Получаем имя калибровочного файла на основе полученных в run content данных
    QString getCalibrationFileName(FramePurpose fp);

    //используется для отображения, сохранения и загрузки данных с последнего скана
    ScanData *lastScanData;

    //функция получения калибровочных фреймов
    Frame getDarkFrame(ScanData*);
    Frame getLighFrame(ScanData*);

    void updateDarkCalib(QString fileName);
    void updateLightCalib(QString fileName);

    QCPRange getMapRange(QVector<Frame>::iterator start, QVector<Frame>::iterator stop);

private slots:
    void updateMap(Frame &, QCustomPlot *&plot, QCPColorMap* &cmap);
    void initMap(QCustomPlot *&plot, QCPColorMap* &cmap, QCPColorScale* &cscale, QString title = "Title");
    void updateHisto(Frame &fr, QCustomPlot *&plot, QCPBars *&bars);

    void getMetaInfo (MetaInfo  *);
    void getScanState(ScanState *);
    void getScanData (ScanData  *);
    void getRunResponse(Run     *);
    void writeToFile (QVector<Frame> &vec, QString fname = "pixel_by_pixel.dat");
    void writeToFile (ScanData  *, QString fileName = "scan");
    void sendRunCommand();
    void saveImage(QCPAbstractPlottable *  plottable, int  dataIndex, QMouseEvent* evnt);

    void selectedFrameChanged();
    quint16 getUIcommandMask();

    //void mySlot(QCPRange newRange,QCPRange oldRange);

};
#endif // MAINWINDOW_H
