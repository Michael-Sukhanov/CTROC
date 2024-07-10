#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>
#include <QDebug>
#include <stdio.h>

const QStringList sADCranges = {
    "100 pC", "50 pC", "25 pC", "12.5 pC", "6.25 pC", "150 pC", "75 pC", "37.5 pC"
};

MainWindow::MainWindow(QString _peer_IP, quint16 _peer_port, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), meanFrame(nullptr), stdevFrame(nullptr)
{
    ui->setupUi(this);

    Tcpclient = new Client(_peer_IP, _peer_port);

    //Обработка кнопок запросов
    connect(ui->pushButton_MetaInfo, &QPushButton::clicked, Tcpclient, &Client::getMetaInfo);
    connect(ui->pushButton_ReadStatus, &QPushButton::clicked, Tcpclient, &Client::getScanState);
    connect(ui->pushButton_getData, &QPushButton::clicked, Tcpclient, [=](){
        Tcpclient->getScanData(0, 0, ui->lineEdit->text().toUInt());
        ui->pushButton_getData->setEnabled(false);
        ui->horizontalSlider->setEnabled(false);
        ui->pushButton_nextFrame->setEnabled(false);
        ui->pushButton_prevFrame->setEnabled(false);
        }
    );

    connect(ui->pushButton_gls             , &QPushButton::clicked, Tcpclient, &Client::getLaslScan);
    connect(ui->pushButton_run             , &QPushButton::clicked, this, &MainWindow::sendRunCommand);

    //Обработка ответов от клиентов
    connect(Tcpclient, &Client::metaInfoReady   , this, &MainWindow::getMetaInfo   );
    connect(Tcpclient, &Client::scanStateReady  , this, &MainWindow::getScanState  );
    connect(Tcpclient, &Client::scanDataReady   , this, &MainWindow::getScanData   );
    connect(Tcpclient, &Client::runResponseReady, this, &MainWindow::getRunResponse);

    //элементы визуализации (переключение между фреймами)
    connect(ui->pushButton_prevFrame, &QPushButton::clicked, this, &MainWindow::selectedFrameChanged);
    connect(ui->pushButton_nextFrame, &QPushButton::clicked, this, &MainWindow::selectedFrameChanged);

    connect(ui->horizontalSlider    , &QSlider::valueChanged, this, &MainWindow::selectedFrameChanged);

    //обработка поведения lineEditов
    ui->lineEdit->setValidator (new QRegExpValidator(QRegExp("^([1-9][0-9]{0,2}|1000)$")));
    ADCRangeLE   = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_ADCrange  )),
    setRateLE    = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_Scanrate  )),
    readStreamLE = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_ReadStream));

    if(ADCRangeLE  ){ADCRangeLE->setText("5555555555"); ADCRangeLE->setValidator (new QRegExpValidator(QRegExp("^[0-7]{1,10}$")));}
    if(setRateLE   ) setRateLE->   setValidator(new QRegExpValidator(QRegExp("[0-9]+")));
    if(readStreamLE) readStreamLE->setValidator(new QRegExpValidator(QRegExp("[0-9]+")));

    QList<QLineEdit*> lineEditsList = this->findChildren<QLineEdit*>(QRegularExpression("lineEdit_com_*"));
    for(auto &el : lineEditsList){
        connect(el, &QLineEdit::textEdited, this, [=](){
            QCheckBox *cb = this->findChild<QCheckBox*>("checkBox_" + el->objectName().mid(el->objectName().lastIndexOf("_") + 1));
            QPushButton *pb = this->findChild<QPushButton*>("pushButton_com_" + el->objectName().mid(el->objectName().lastIndexOf("_") + 1));
            if(!el->text().isEmpty())cb->setChecked(false);
            cb->setEnabled(!el->text().isEmpty());
            pb->setEnabled(!el->text().isEmpty());
        });
    }
    for(auto &el : lineEditsList){el->textEdited(el->text());}

    //обработка поведения кнопок
    QList<QPushButton*> pushButtonsList = this->findChildren<QPushButton*>(QRegularExpression("pushButton_com_*"));
    for(auto &el : pushButtonsList){
        auto elCommand = getCorrespondingCommand(el);
        el->setText(COMMANDS[getCorrespondingBitNo(el)]);
        connect(el, &QPushButton::clicked, this, [=](){

            quint32* scanRate = nullptr;
            quint32* readNum = nullptr;
            Ranges* rngs = nullptr;


            QLineEdit* le = this->findChild<QLineEdit*>("lineEdit_com_" + el->objectName().mid(el->objectName().lastIndexOf("_")+1));

            if(le && !le->text().isEmpty()){
                if(elCommand == Command::ADCrange){
                        rngs = new Ranges();
                        for(auto i = 0; i < le->text().size(); ++i)
                            rngs->values[i] = static_cast<ADCrange>(le->text().mid(i, 1).toUInt());
                    }

                if(elCommand == Command::Scanrate){
                    scanRate = new quint32;
                    *scanRate = le->text().toUInt();
                }

                if(elCommand == Command::ReadStream){
                    readNum = new quint32;
                    *readNum = le->text().toUInt();
                }
            }

            Tcpclient->sendRunCommand(getCorrespondingCommand(el), rngs, scanRate, readNum);

            if(scanRate) delete scanRate;
            if(readNum) delete readNum;
            if(rngs) delete rngs;

            runGUIControl(false);
        });
    }

    //обработка поведения check-boxов
    QList<QCheckBox*> checkBoxes = this->findChildren<QCheckBox*>(QRegularExpression("checkBox_*"));
    for(auto &el : checkBoxes) connect(el, &QCheckBox::clicked, this, [=](){ui->pushButton_run->setEnabled(getUIcommandMask());});

    initMap(ui->plotMap,      colorMap,     colorScale    );
    initMap(ui->plotMapMean,  colorMapMean, colorScaleMean, "Mean map" );
    initMap(ui->plotMapSigma, colorMapStd,  colorScaleStd , "Stdev map");

    connect(ui->plotMap     , &QCustomPlot::plottableClick, this, &MainWindow::saveImage);
    connect(ui->plotMapMean , &QCustomPlot::plottableClick, this, &MainWindow::saveImage);
    connect(ui->plotMapSigma, &QCustomPlot::plottableClick, this, &MainWindow::saveImage);

    meanBars = new QCPBars(ui->histMean->xAxis, ui->histMean->yAxis);
    stdBars  = new QCPBars(ui->histSigma->xAxis, ui->histSigma->yAxis);
}

MainWindow::~MainWindow(){
    delete ui;
}

void MainWindow::getFrames(ScanData *response, QVector<Frame> &vec){
    vec.clear();
    qDebug() << response->getFramesCount();
    frames.reserve(response->getFramesCount());
    for(auto i = 0; i < response->getFramesCount(); ++i){
        vec.append(Frame(response, i));
    }
}

void MainWindow::runGUIControl(bool enabled){
    QList<QWidget*> runWidgetsList = this->findChildren<QWidget*>(QRegularExpression("(_com_*)|(checkBox*)|(pushButton_run)"));
    for(auto &el : runWidgetsList){
        el->setEnabled(enabled);
    }
}

quint16 MainWindow::getCorrespondingCommand(QWidget *wgt){
    return (1 <<  wgt->objectName().split("_", Qt::SkipEmptyParts).last().toUInt());
}

quint8 MainWindow::getCorrespondingBitNo(QWidget *wgt){
    return wgt->objectName().split("_", Qt::SkipEmptyParts).last().toUInt();
}


void MainWindow::updateMap(Frame &fr, QCustomPlot *&plot, QCPColorMap *&cmap, float* sampMin, float* sampMax){
    cmap->data()->setSize(fr.sizeZ, fr.sizeX);
    cmap->data()->setRange(QCPRange(0.5, 31.5), QCPRange(0.5, 79.5));

        for(auto i = 0; i < fr.sizeZ; ++i)
            for(auto k = 0; k < fr.sizeX; ++k)
                cmap->data()->setCell(i, k, fr(i, k));

    cmap->setDataRange(QCPRange(sampMin ? *sampMin : fr.min(), sampMax ? *sampMax : fr.max()));
    plot->xAxis->setRange(QCPRange(0, fr.sizeZ));
    plot->yAxis->setRange(QCPRange(0, fr.sizeX));
    plot->replot();
}


void MainWindow::initMap(QCustomPlot *&plot, QCPColorMap *&cmap, QCPColorScale *&cscale, QString title){
    // configure axis rect:
    // plot->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom); // this will also allow rescaling the color scale by dragging/zooming
    plot->plotLayout()->insertRow(0);
    plot->plotLayout()->addElement(0, 0, new QCPTextElement(plot, title));
    plot->axisRect()->setupFullAxesBox(true);
    plot->xAxis->setLabel("Z");
    plot->yAxis->setLabel("X");

    cmap = new QCPColorMap(plot->xAxis, plot->yAxis);
    cmap->setInterpolate(false);

    cscale = new QCPColorScale(plot);
    plot->plotLayout()->addElement(1, 1, cscale); // add it to the right of the main axis rect
    cscale->setType(QCPAxis::atRight); // scale shall be vertical bar with tick/axis labels right (actually atRight is already the default)
    cmap->setColorScale(cscale); // associate the color map with the color scale
    cscale->axis()->setLabel("ADC units");

    cmap->setGradient(getGradient(kDarkBodyRadiator));

    // make sure the axis rect and color scale synchronize their bottom and top margins (so they line up):
    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    cscale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    cscale->setRangeDrag(false);
    cscale->setRangeZoom(false);

}

void MainWindow::getMetaInfo(MetaInfo *resp){
    MetaInfo *mi = resp;
    qDebug() << mi->getStatus() <<  mi->getName() << mi->getAge();
    delete mi;
}

void MainWindow::getScanState(ScanState *resp){
    ScanState *ss = resp;
    qDebug() << ss->getStatus() << ss->getCompleteFrames() << ss->getInProgress();
    delete ss;
}

void MainWindow::getScanData(ScanData *resp){
    ui->pushButton_getData->setEnabled(true);

    if(meanFrame)  delete meanFrame;
    if(stdevFrame) delete stdevFrame;

    ScanData *sd = resp;
    getFrames(sd, frames);
    writeToFile(sd);

    if(!frames.empty()){
        sampleMax = frames.first().max();
        sampleMin = frames.first().min();
    }

    for(auto &fr : frames){
        if(sampleMin > fr.min()) sampleMin = fr.min();
        if(sampleMax < fr.max()) sampleMax = fr.max();
    }

    meanFrame  = new Frame(frames.begin(), frames.end(), FrameMEAN            );
    stdevFrame = new Frame(frames.begin(), frames.end(), FrameSTDEV, meanFrame);

    updateMap(frames.first(), ui->plotMap,   colorMap    , &sampleMin, &sampleMax);
    updateMap(*meanFrame, ui->plotMapMean,   colorMapMean);
    updateMap(*stdevFrame, ui->plotMapSigma, colorMapStd );

    // writeToFile(frames); ASCII writing to file
    currentframeIndex = 0;
    ui->pushButton_prevFrame->setEnabled(false);
    if(frames.size() > 1 ) ui->pushButton_nextFrame->setEnabled(true);
    ui->horizontalSlider->setEnabled(true);
    ui->horizontalSlider->setRange(0, frames.size() - 1);
    QString txt = QString::asprintf("Frame %d/%d", currentframeIndex + 1, frames.size());
    ui->label_frameNo->setText(txt);

    // QCPTextElement *title = dynamic_cast<QCPTextElement*>(ui->plotMap->plotLayout()->element(0, 0));
    // title->setText(txt);
}

void MainWindow::getRunResponse(Run *resp){
    RunContent rc(resp);
    quint16 mask = rc.maskUpdated;
    if(Command::Temperature & mask) ui->textEdit_messages->append(QString::asprintf("ADC temperature: %.1f °C", rc.ADCtemperature));
    if(Command::Nlines      & mask) ui->textEdit_messages->append(QString::asprintf("Total ADC lines: %d", rc.numLines));
    if(Command::CompileTime & mask) ui->textEdit_messages->append("Compilation time: " + rc.compilationDateTime.toString("dd/MM/yyyy HH:mm:ss"));
    if(Command::Status      & mask) ui->textEdit_messages->append("Status response received");
    if(Command::ADCrange    & mask) for(auto i = 0; i < 10; ++i) ui->textEdit_messages->append(QString::asprintf("ADC %u <- ", i) + sADCranges[rc.rngs.values[i]]);
    if(Command::Scanrate    & mask) ui->textEdit_messages->append(QString::asprintf("Scan period changed to %.1f ms", static_cast<float>(rc.scanRate) * 0.1));
    if(Command::ScanMode    & mask) ui->textEdit_messages->append("Scan mode response received");
    if(Command::Reset       & mask) ui->textEdit_messages->append("Reset " + QString(rc.resetSuccessful ? "successful" : "failed"));
    if(Command::Drift       & mask) ui->textEdit_messages->append("Drift correction: " + QString(rc.driftCorrection == 0 ? "¯\\_(ツ)_/¯" : (rc.driftCorrection == 1 ? "done" : "failed")));
    if(Command::Offset      & mask) ui->textEdit_messages->append("Offset correction: " + (rc.offsetCorrection == 0 ? QString("¯\\_(ツ)_/¯") : (rc.offsetCorrection == 1 ? QString("done") : QString("failed"))));
    if(Command::RemainWords & mask) ui->textEdit_messages->append(QString::asprintf("Data FIFO payload: %u 32-bit words", rc.FIFOpayload));
    if(Command::EndMessage  & mask) ui->textEdit_messages->append(QString::asprintf("MSG payload: %u", rc.MSGpayload));
    if(Command::ReadStream  & mask){ui->textEdit_messages->append(QString::asprintf("Trying to read %u frames", rc.tryReadNFrames));
        if(rc.readerrCode) ui->textEdit_messages->append( rc.readerrCode == 1 ? "ERROR read: FIFO is empty" : "Unknown Error");
        else ui->textEdit_messages->append(QString::asprintf("%u frames collected", rc.framesCollected));
    }
    if(Command::kadr_on     & mask) ui->textEdit_messages->append("Set KADR on" );
    if(Command::kadr_off    & mask) ui->textEdit_messages->append("Set KADR off");
    if(Command::mux_adc     & mask) ui->textEdit_messages->append("Set MUX"     );
    runGUIControl(true);
}

void MainWindow::writeToFile(QVector<Frame> &frames){
    if(frames.empty()) return;
    std::ofstream stream("pixel_by_pixel.dat", std::ios::out);
    for(auto it = frames.begin(); it != frames.end(); ++it){
        for(auto x = 0; x < it->sizeX; ++x){
            for(auto z = 0; z < it->sizeZ; ++z){
                stream << (*it)(z, x) << ' ';
                }
            }
        if(it != frames.end() - 1)stream << '\n';
    }
    stream.close();
}

void MainWindow::writeToFile(ScanData *sd, QString fileName){
    std::ofstream outHeader((fileName + ".hdr").toStdString(), std::ios::out);
    outHeader << QString::asprintf("Matrix-Size-X: %u\nMatrix-Size-Z: %u\nBytes-Per-Pixel: %u", sd->getSizeX(), sd->getSizeZ(), sd->getBytesPerPixel()).toStdString();
    outHeader.close();

    std::ofstream outData((fileName + ".dat").toStdString(), std::ios::out | std::ios::binary);
    outData.write(sd->getData(), sd->getContentLength());
    outData.close();
}

void MainWindow::sendRunCommand(){
    quint16 msk = getUIcommandMask();

    quint32* scanRate = nullptr;
    quint32* readNum = nullptr;
    Ranges* rngs = nullptr;

    if(msk & Command::ADCrange && !ADCRangeLE->text().isEmpty()){
        rngs = new Ranges();
        for(auto i = 0; i < ADCRangeLE->text().size(); ++i)
            rngs->values[i] = static_cast<ADCrange>(ADCRangeLE->text().mid(i, 1).toUInt());
    }

    if(msk & Command::Scanrate && !setRateLE->text().isEmpty()){
        scanRate = new quint32;
        *scanRate = setRateLE->text().toUInt();
    }

    if(msk & Command::ReadStream && !readStreamLE->text().isEmpty()){
        readNum  = new quint32;
        *readNum = readStreamLE->text().toUInt();
    }

    if(msk) Tcpclient->sendRunCommand(msk, rngs, scanRate, readNum);

    if(scanRate) delete scanRate;
    if(readNum) delete readNum;
    if(rngs) delete rngs;

    runGUIControl(false);
}

void MainWindow::saveImage(QCPAbstractPlottable *  plottable, int  dataIndex, QMouseEvent* evnt){
    Q_UNUSED(plottable)
    Q_UNUSED(dataIndex)

    if(evnt->button() != Qt::RightButton) return;
    QMenu* menu = new QMenu(this);
    QAction *pdfSave = menu->addAction("Save");
    QAction *saveAs = menu->addAction("Save as...");
    QCustomPlot* plotObj = qobject_cast<QCustomPlot*>(sender());
    QCPTextElement *te = dynamic_cast<QCPTextElement*>(plotObj->plotLayout()->element(0,0));

    connect(pdfSave, &QAction::triggered, this, [=](bool trig){plotObj->savePdf(te->text() + ".pdf");}, Qt::ConnectionType::UniqueConnection);
    connect(saveAs, &QAction::triggered, this, [=](bool trig){
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save image"), te->text(), tr("*.jpg;;*.bmp;;*.png;;*.pdf"));
        if(fileName.isEmpty()) return;
        QString format = fileName.split(".").last();
        qDebug() << fileName;
        if(format == "jpg") plotObj->saveJpg(fileName);
        if(format == "bmp") plotObj->saveBmp(fileName);
        if(format == "png") plotObj->savePng(fileName);
        if(format == "pdf") plotObj->savePdf(fileName);
    });

    menu->popup(evnt->globalPos());

}

void MainWindow::selectedFrameChanged(){
    QPushButton *but = qobject_cast<QPushButton*>(sender());
    if(but == ui->pushButton_prevFrame && currentframeIndex > 0)
        updateMap(frames[--currentframeIndex], ui->plotMap, colorMap, &sampleMin, &sampleMax);
    else if(but == ui->pushButton_nextFrame && frames.size() - 1 > currentframeIndex)
        updateMap(frames[++currentframeIndex], ui->plotMap, colorMap, &sampleMin, &sampleMax);
    else if(qobject_cast<QSlider*>(sender()) == ui->horizontalSlider){
        if(ui->horizontalSlider->value() == currentframeIndex) return;
        updateMap(frames[currentframeIndex = ui->horizontalSlider->value()], ui->plotMap, colorMap, &sampleMin, &sampleMax);
    }
    ui->horizontalSlider->setValue(currentframeIndex);

    ui->pushButton_prevFrame->setEnabled(currentframeIndex);
    ui->pushButton_nextFrame->setEnabled(currentframeIndex != frames.size() - 1);
    QString txt = QString::asprintf("Frame %d/%d", currentframeIndex + 1, frames.size());
    ui->label_frameNo->setText(txt);
    // qDebug() << txt;

    // ui->plotMap->plotLayout()->remove(ui->plotMap->plotLayout()->element(0,0));
    // ui->plotMap->plotLayout()->addElement(0,0,new QCPTextElement(ui->plotMap, txt));
}

quint16 MainWindow::getUIcommandMask(){
    QList<QCheckBox*>  cbList = this->findChildren<QCheckBox*>(QRegularExpression("checkBox_*"));
    quint16 mask = 0;
    for(const auto &el : cbList){
        quint32 bit;
        sscanf_s(el->objectName().toStdString().c_str(), "checkBox_%u", &bit);
        if(el->isChecked()) mask |= (1 << bit);
    }
    return mask;
}

QCPColorGradient getGradient(const QList<QColor> &palette){
    QCPColorGradient retValue;
    QMap<double, QColor> map;
    for(auto i = 0; i < palette.size(); ++i) map.insert(static_cast<double>(i) / palette.size(), palette.at(i));
    retValue.setColorStops(map);
    return retValue;
}
