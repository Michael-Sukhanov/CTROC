#include "ctrocplottables.h"

FrameMap::FrameMap(QCustomPlot *&_plot):plot(_plot){
    plot->setInteractions(QCP::iSelectPlottables);
    plot->plotLayout()->insertColumn(1);
    plot->axisRect()->setupFullAxesBox(false);
    plot->yAxis->setRangeReversed(true);
    plot->xAxis->setTickLabels(false);
    plot->xAxis2->setVisible(true);
    plot->xAxis2->setTickLabels(true);

    map = new QCPColorMap(plot->xAxis, plot->yAxis);
    plot->plottable(0)->setSelectable(QCP::stSingleData);
    map -> setInterpolate(false);

    scale = new QCPColorScale(plot);
    plot->plotLayout()->addElement(0, 1, scale);
    scale->setType(QCPAxis::atRight);
    scale->axis()->setTickLengthOut(scale->axis()->tickLengthIn()/2);
    scale->axis()->setSubTickLengthOut(scale->axis()->subTickLengthIn()/2);
    scale->axis()->setNumberFormat("eb");
    scale->axis()->setNumberPrecision(0);

    map->setColorScale(scale);
    map->setGradient(getGradient(kDarkBodyRadiator));

    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    scale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    scale->setRangeDrag(false);
    scale->setRangeZoom(false);

    text = new TextInfo(plot);

    pixel = new QCPItemRect(plot);
    pixel->topLeft->setType(QCPItemPosition::ptAxisRectRatio);
    pixel->bottomRight->setType(QCPItemPosition::ptAxisRectRatio);

    pixel->setBrush(QBrush(QColor(0,255,0, 255)));
//    pixel->topLeft->setAxes( plot->axisRect()->axis(QCPAxis::atBottom),
//                           plot->axisRect()->axis(QCPAxis::atLeft));
//    pixel->bottomRight->setAxes(plot->axisRect()->axis(QCPAxis::atBottom),
//                               plot->axisRect()->axis(QCPAxis::atLeft));
    pixel->setVisible(false);

    connect(plot, &QCustomPlot::plottableClick, this, [=](QCPAbstractPlottable* ap,int idx,QMouseEvent* ev){
        double x, y;
        map->pixelsToCoords(ev->pos(), x, y);
        int ix = static_cast<int>(x), iy = static_cast<int>(y);
        text->setText(QString("{%1, %2} : %3").arg(ix).arg(iy).arg(map->data()->data(x, y)));
        text->setVisible(true);

        pixel->topLeft->setCoords(ix/plot->xAxis->range().upper, iy/plot->yAxis->range().upper);
        pixel->bottomRight->setCoords((ix+1)/plot->xAxis->range().upper, (iy+ 1)/plot->yAxis->range().upper);
        pixel->setVisible(true);
        plot->replot();
    });
}



void FrameMap::update(Frame &frame, const QCPRange range){
    map->data()->setSize(frame.sizeX, frame.sizeZ);
    map->data()->setRange(QCPRange(0.5, frame.sizeX - 0.5), QCPRange(0.5, frame.sizeZ - 0.5));
    for(auto i = 0; i < frame.sizeX; ++i) for(auto k = 0; k < frame.sizeZ; ++k) map->data()->setCell(i, k, frame(k, i));

    plot->xAxis2->setRange(QCPRange(0, frame.sizeX));
    plot->xAxis ->setRange(QCPRange(0, frame.sizeX));
    plot->yAxis ->setRange(QCPRange(0, frame.sizeZ));

    pixel->setVisible(false);
    text->setVisible(false);

    update(range);
}

void FrameMap::update(const QCPRange range){
    map->setDataRange(range);
    plot->replot();
}

QCPColorGradient getGradient(const QList<QColor> &palette){
    QCPColorGradient retValue;
    QMap<double, QColor> map;
    for(auto i = 0; i < palette.size(); ++i) map.insert(static_cast<double>(i) / palette.size(), palette.at(i));
    retValue.setColorStops(map);
    return retValue;
}

FrameHist::FrameHist(QCustomPlot *&_plot, FrameMap *_corrMap):plot(_plot),fMap(_corrMap){
    bars = new QCPBars(plot->xAxis, plot->yAxis);
    plot->setInteractions(QCP::iRangeZoom | QCP::iRangeDrag | QCP::iSelectPlottables);
    plot->plottable(0)->setSelectable(QCP::stSingleData);
    plot->axisRect()->setRangeDrag(Qt::Horizontal);
    plot->axisRect()->setRangeZoom(Qt::Horizontal);

    text = new TextInfo(plot);

    connect(plot, &QCustomPlot::mouseWheel,   this, [=](QWheelEvent *ev){fMap->update(plot->xAxis->range());});
    connect(plot, &QCustomPlot::mouseRelease, this, [=](QMouseEvent *ev){fMap->update(plot->xAxis->range());});
    connect(plot, &QCustomPlot::plottableClick, this, [=](QCPAbstractPlottable* ap,int idx,QMouseEvent* ev){
        QCPBars* bars = qobject_cast<QCPBars*>(plot->plottable(0));
        text->setText(QString("[%1, %2): %3")
                        .arg(bars->data().data()->at(idx)->key - bars->width()/2)
                        .arg(bars->data().data()->at(idx)->key + bars->width()/2)
                        .arg(bars->data().data()->at(idx)->value));
        text->setVisible(true);
    });
    connect(plot, &QCustomPlot::selectionChangedByUser, this, [=]{text->setVisible(false);});
    connect(plot, &QCustomPlot::mouseDoubleClick, this, [=](){
        bool ok;
        QCPBars* bars = qobject_cast<QCPBars*>(plot->plottable(0));
        plot->xAxis->setRange(QCPRange(bars->getKeyRange(ok).lower - bars->width() / 2, bars->getKeyRange(ok).upper + bars->width() / 2)); plot->replot();});

}

void FrameHist::update(Frame &frame){
    double min = frame.min(), max = frame.max(), binWidth = 1;
    if (max>min) {
        int m  = lround(std::log10(max-min)*3 - 6); //define decimal order of magnitude for bin width to be 5/3 lower then whole range magnitude, so there will be about 30-70 bins
        binWidth = QList<int>({1,2,5})[(m%3+3)%3] * std::pow(10, m/3); //make nice values: 0.1, 0.2, 0.5, 1, 2, 5, 10, 20...
    };
    plot->xAxis->setRange(min, max);
    plot->yAxis->setRange(0, frame.sizeX*frame.sizeZ);
    QMap<double,double> map;
    for(auto i = 0; i < frame.sizeX*frame.sizeZ; ++i) map[std::round(frame.data[i]/binWidth)*binWidth]+=1;
    bars->setWidth(binWidth);
    bars->setData(map.keys().toVector(), map.values().toVector(), true);
    bool ok;
    auto rngx = bars->getKeyRange(ok);
    if (ok) plot->xAxis->setRange(rngx.lower-binWidth/2, rngx.upper+binWidth/2);
    auto rngy = bars->getValueRange(ok);
    if (ok) plot->yAxis->setRangeUpper(rngy.upper);

    text->setVisible(false);

    plot->replot();
}

TextInfo::TextInfo(QCustomPlot *plot):QCPItemText(plot){
    this->setPositionAlignment(Qt::AlignTop | Qt::AlignRight);
    this->position->setType(QCPItemPosition::ptAxisRectRatio);
    this->position->setCoords(0.95, 0); // place position at center/top of axis rect
    this->setFont(QFont("Consolas", 8, QFont::Bold)); // make font a bit larger
    this->setPen(QPen(Qt::NoPen));
    this->setBrush(QBrush(QColor(192,192,192, 128)));
    this->setVisible(false);
}
