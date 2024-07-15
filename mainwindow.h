#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "client.h"
#include <qcustomplot.h>
#include <palettes.h>
#include <QSettings>

QCPColorGradient getGradient(const QList<QColor> &palette);

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QCPColorMap *colorMap, *colorMapMean, *colorMapStd;
    QCPColorScale *colorScale, *colorScaleMean, *colorScaleStd;
    QCPBars *meanBars, *stdBars;

    QString peerIP;
    quint16 peerPort;
    Client *Tcpclient;

    QLineEdit *ADCRangeLE, *setRateLE, *readStreamLE;

    QVector<Frame> frames;
    Frame *meanFrame, *stdevFrame, *darkFrame, *lightFrame;
    QString darkCalibFileName, lightCalibFileName;

    int currentframeIndex;
    float sampleMin, sampleMax;

    bool darkFrameFlag, lightFrameFlag;
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

private slots:
    void updateMap(Frame &, QCustomPlot *&plot, QCPColorMap* &cmap, float* sampMin = nullptr, float* sampMax = nullptr);
    void initMap(QCustomPlot *&plot, QCPColorMap* &cmap, QCPColorScale* &cscale, QString title = "Title");

    void getMetaInfo (MetaInfo  *);
    void getScanState(ScanState *);
    void getScanData (ScanData  *);
    void getRunResponse(Run     *);
    void writeToFile (QVector<Frame> &vec);
    void writeToFile (ScanData  *, QString fileName = "scan");
    void sendRunCommand();
    void saveImage(QCPAbstractPlottable *  plottable, int  dataIndex, QMouseEvent* evnt);

    void selectedFrameChanged();
    quint16 getUIcommandMask();

};
#endif // MAINWINDOW_H
