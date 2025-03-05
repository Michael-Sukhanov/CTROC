#ifndef CTROCPLOTTABLES_H
#define CTROCPLOTTABLES_H

#include <qcustomplot.h>
#include <palettes.h>
#include "client.h"

QCPColorGradient getGradient(const QList<QColor> &palette);

class TextInfo : public QCPItemText{
public:
    TextInfo(QCustomPlot *plot);
};

class FrameMap : public QObject
{
    Q_OBJECT
public:
    FrameMap(QCustomPlot *&_plot);
    void update(Frame &frame, const QCPRange range);
    void update(const QCPRange range);
    void saveImg(QMouseEvent *ev);

    void hideInfo();
    void showInfo();

private:
    QCustomPlot *plot;
    QCPColorMap *map;
    QCPColorScale *scale;
    TextInfo *text;
    QCPItemRect *pixel;

    void setSelectedPixel(QMouseEvent* ev);
    void mouseEventFilter(QMouseEvent* ev, QCPAbstractItem* item = nullptr);
};

class FrameHist : public QObject
{
    Q_OBJECT
public:
    FrameHist(QCustomPlot *&_plot, FrameMap *_corrMap);
    void update(Frame &frame);

private:
    QCustomPlot *plot;
    QCPBars *bars;
    TextInfo *text;
    FrameMap *fMap;
};



#endif // CTROCPLOTTABLES_H
