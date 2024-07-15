#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>
#include <QDebug>
#include <stdio.h>

const QStringList sADCranges = {
    "100 pC", "50 pC", "25 pC", "12.5 pC", "6.25 pC", "150 pC", "75 pC", "37.5 pC"
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
    meanFrame(nullptr), stdevFrame(nullptr), darkFrame(nullptr), lightFrame(nullptr),
    darkCalibFileName(""), lightCalibFileName(""),
    darkFrameFlag(false), lightFrameFlag(false)
{
    ui->setupUi(this);

    settings = new QSettings(QDir::currentPath() + "/Settings.ini", QSettings::IniFormat);

    //указатели на LineEditы нужны, чтобы загрузить настройки и связать их с команадами
    ADCRangeLE   = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_ADCrange  )),
    setRateLE    = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_Scanrate  )),
    readStreamLE = this->findChild<QLineEdit*>(QString::asprintf("lineEdit_com_%u", Command::bitNo_ReadStream));

    loadSettings();
    //Только после загрузки настроек создаем клиент, поскольку в нем находится инфа о IP и порте пира
    Tcpclient = new Client(peerIP, peerPort);

    //Обработка кнопок запросов
    connect(ui->pushButton_MetaInfo, &QPushButton::clicked, Tcpclient, &Client::getMetaInfo);
    connect(ui->pushButton_ReadStatus, &QPushButton::clicked, Tcpclient, &Client::getScanState);
    connect(ui->pushButton_getData, &QPushButton::clicked, Tcpclient, [=](){
        Tcpclient->getScanData(0, 0, ui->lineEdit->text().toUInt());
        ui->pushButton_getData->setEnabled(false);
        ui->horizontalSlider->setEnabled(false);
        ui->pushButton_nextFrame->setEnabled(false);
        ui->pushButton_prevFrame->setEnabled(false);});
    connect(ui->pushButton_gls             , &QPushButton::clicked, Tcpclient, &Client::getLastScan);
    connect(ui->pushButton_run             , &QPushButton::clicked, this, &MainWindow::sendRunCommand);
    connect(ui->pushButton_darkCalib, &QPushButton::clicked, this, [=](){
        ui->textEdit_messages->append("Performing Dark field calibration");
        darkFrameFlag = true;
        quint32 readN = 1000;
        Tcpclient->sendRunCommand(Command::ReadStream, nullptr, nullptr, &readN);
    });
    connect(ui->pushButton_lightCalib, &QPushButton::clicked, this, [=](){
        ui->textEdit_messages->append("Performing Light field calibration");
        lightFrameFlag = true;
        quint32 readN = 1000;
        Tcpclient->sendRunCommand(Command::ReadStream, nullptr, nullptr, &readN);
    });


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
    if(ADCRangeLE  ){ADCRangeLE->setValidator (new QRegExpValidator(QRegExp("^[0-7]{1,10}$")));}
    if(setRateLE   ) setRateLE->   setValidator(new QRegExpValidator(QRegExp("[0-9]+")));
    if(readStreamLE) readStreamLE->setValidator(new QRegExpValidator(QRegExp("[0-9]+")));

    QList<QLineEdit*> lineEditsList = this->findChildren<QLineEdit*>(QRegularExpression("lineEdit_com_*"));
    for(auto &el : lineEditsList){
        connect(el, &QLineEdit::textEdited, this, [=](){
            QCheckBox *cb = this->findChild<QCheckBox*>("checkBox_" + el->objectName().mid(el->objectName().lastIndexOf("_") + 1));
            QPushButton *pb = this->findChild<QPushButton*>("pushButton_com_" + el->objectName().mid(el->objectName().lastIndexOf("_") + 1));
            if(el->text().isEmpty())cb->setChecked(false);
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

    msgBox.setIcon(QMessageBox::Information);
    if(QFile::exists(darkCalibFileName)) darkFrame  = new Frame(FrameDARK , darkCalibFileName);
    else {
        msgBox.setText("Файл калибровки темнового поля " + darkCalibFileName + " не найден");
        msgBox.exec();
        darkFrame  = new Frame(FrameDARK );
        darkCalibFileName = "";
    }
    ui->label_Dark->setText(darkCalibFileName.isEmpty() ? "No calibration" : darkCalibFileName);

    if(QFile::exists(lightCalibFileName)) lightFrame = new Frame(FrameLIGHT, lightCalibFileName, 80, 32);
    else{
        msgBox.setText("Файл калибровки светового поля " + lightCalibFileName + " не найден");
        msgBox.exec();
        lightFrame = new Frame(FrameLIGHT);
        lightCalibFileName = "";
    }
    ui->label_Light->setText(lightCalibFileName.isEmpty() ? "No calibration" : lightCalibFileName);

    connect(ui->pushButton_loadLight, &QPushButton::clicked, this, [=](){
        QString fn = QFileDialog::getOpenFileName(this, tr("Select calibration file"), QDir::currentPath(), tr("*.dat"));
        if(!fn.isEmpty()) lightCalibFileName = QDir().relativeFilePath(fn);
        else return;
        if(lightFrame) delete lightFrame;
        lightFrame = new Frame(FrameLIGHT, lightCalibFileName);
        ui->label_Light->setText(lightCalibFileName);
    });

    connect(ui->pushButton_loadDark, &QPushButton::clicked, this, [=](){
        QString fn = QFileDialog::getOpenFileName(this, tr("Select calibration file"), QDir::currentPath(), tr("*.dat"));
        if(!fn.isEmpty()) darkCalibFileName = QDir().relativeFilePath(fn);
        else return;
        if(darkFrame) delete darkFrame;
        darkFrame = new Frame(FrameDARK, darkCalibFileName);
        ui->label_Dark->setText(darkCalibFileName);
    });

}

MainWindow::~MainWindow(){
    saveSettings();
    delete ui;
}

void MainWindow::getRawFrames(ScanData *response, QVector<Frame> &vec){
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

void MainWindow::correctFrames(QVector<Frame>::iterator start, QVector<Frame>::iterator stop){
    for(auto it = start; it != stop; ++it){
        for(auto z = 0; z < it->sizeZ; ++z)
            for(auto x = 0; x < it->sizeX; ++x){

                (*it)(z, x) = ((*it)(z, x) - (*darkFrame)(z, x)) * (*lightFrame)(z, x);

            }
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
    cmap->data()->setRange(QCPRange(0.5,fr.sizeZ - 0.5), QCPRange(0.5, fr.sizeX - 0.5));

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

    //сбрасываем предыдущие mean и stdev фреймы
    if(meanFrame)  delete meanFrame;
    if(stdevFrame) delete stdevFrame;

    //если проводится одна из калибровок, ее матрицу нужно очистить
    //для темной это все нули
    //для световой это все единицы
    if(darkFrameFlag){
        if(darkFrame) delete darkFrame;
        if(lightFrame) delete lightFrame;
        darkFrame = new Frame(FrameDARK);
        lightFrame = new Frame(FrameLIGHT);
    }

    if(lightFrameFlag){
        if(lightFrame) delete lightFrame;
        lightFrame = new Frame(FrameLIGHT);
    }

    ScanData *sd = resp;
    getRawFrames(sd, frames);
    correctFrames(frames.begin(), frames.end());

    if(!frames.empty()){
        sampleMax = frames.first().max();
        sampleMin = frames.first().min();}
    for(auto &fr : frames){
        if(sampleMin > fr.min()) sampleMin = fr.min();
        if(sampleMax < fr.max()) sampleMax = fr.max();
    }

    meanFrame  = new Frame(frames.begin(), frames.end(), FrameMEAN , nullptr, resp->getSizeX(), resp->getSizeZ());
    stdevFrame = new Frame(frames.begin(), frames.end(), FrameSTDEV, meanFrame, resp->getSizeX(), resp->getSizeZ());

    if(darkFrameFlag){
        *darkFrame = *meanFrame;
        darkCalibFileName = getCalibrationFileName(FrameDARK);
        darkFrame->writeToFile(darkCalibFileName);
        ui->textEdit_messages->append("Dark field calibration finished: new file " + darkCalibFileName);
    }

    if(lightFrameFlag){
        *lightFrame = *meanFrame;
        float lightMean = lightFrame->mean();
        qDebug() << lightMean;
        for(auto i = 0; i < lightFrame->sizeX * lightFrame->sizeZ; ++i)
            lightFrame->data[i] = lightFrame->data[i] / lightMean;
        lightCalibFileName = getCalibrationFileName(FrameLIGHT);
        lightFrame->writeToFile(lightCalibFileName);
        ui->textEdit_messages->append("Light field calibration finished: new file " + lightCalibFileName);
    }

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

    darkFrameFlag = false;
    lightFrameFlag = false;
}

void MainWindow::getRunResponse(Run *resp){
    runContent.update(resp);
    quint16 mask = runContent.maskUpdated;
    if(Command::Temperature & mask) ui->textEdit_messages->append(QString::asprintf("ADC temperature: %.1f °C", runContent.ADCtemperature));
    if(Command::Nlines      & mask) ui->textEdit_messages->append(QString::asprintf("Total ADC lines: %d", runContent.numLines));
    if(Command::CompileTime & mask) ui->textEdit_messages->append("Compilation time: " + runContent.compilationDateTime.toString("dd/MM/yyyy HH:mm:ss"));
    if(Command::Status      & mask) ui->textEdit_messages->append("Status response received");
    if(Command::ADCrange    & mask) for(auto i = 0; i < 10; ++i) ui->textEdit_messages->append(QString::asprintf("ADC %u <- ", i) + sADCranges[runContent.rngs.values[i]]);
    if(Command::Scanrate    & mask) ui->textEdit_messages->append(QString::asprintf("Scan period changed to %.1f ms", static_cast<float>(runContent.scanRate) * 0.1));
    if(Command::ScanMode    & mask) ui->textEdit_messages->append("Scan mode response received");
    if(Command::Reset       & mask) ui->textEdit_messages->append("Reset " + QString(runContent.resetSuccessful ? "successful" : "failed"));
    if(Command::Drift       & mask) ui->textEdit_messages->append("Drift correction: " + QString(runContent.driftCorrection == 0 ? "¯\\_(ツ)_/¯" : (runContent.driftCorrection == 1 ? "done" : "failed")));
    if(Command::Offset      & mask) ui->textEdit_messages->append("Offset correction: " + (runContent.offsetCorrection == 0 ? QString("¯\\_(ツ)_/¯") : (runContent.offsetCorrection == 1 ? QString("done") : QString("failed"))));
    if(Command::RemainWords & mask) ui->textEdit_messages->append(QString::asprintf("Data FIFO payload: %u 32-bit words", runContent.FIFOpayload));
    if(Command::EndMessage  & mask) ui->textEdit_messages->append(QString::asprintf("MSG payload: %u", runContent.MSGpayload));
    if(Command::ReadStream  & mask){ui->textEdit_messages->append(QString::asprintf("Trying to read %u frames", runContent.tryReadNFrames));
        if(runContent.readerrCode) ui->textEdit_messages->append( runContent.readerrCode == 1 ? "ERROR read: FIFO is empty" : "Unknown Error");
        else ui->textEdit_messages->append(QString::asprintf("%u frames collected", runContent.framesCollected));
        if(darkFrameFlag || lightFrameFlag) Tcpclient->getLastScan();
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

    // QAction *showDark = nullptr;
    // if(darkFrame) showDark = menu->addAction("Show dark frame");
    // QAction *showLight= nullptr;
    // if(lightFrame)showLight = menu->addAction("Show light frame");

    QCustomPlot* plotObj = qobject_cast<QCustomPlot*>(sender());
    // QCPColorMap* colMap = plotObj->objectName() == "plotMap" ? colorMap : (plotObj->objectName() == "plotMapMean" ? colorMapMean : colorMapStd);
    QCPTextElement *te = dynamic_cast<QCPTextElement*>(plotObj->plotLayout()->element(0,0));

    connect(pdfSave, &QAction::triggered, this, [=](bool trig){plotObj->savePdf(te->text() + ".pdf");}, Qt::ConnectionType::UniqueConnection);
    connect(saveAs, &QAction::triggered, this, [=](bool trig){
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save image"), te->text(), tr("*.jpg;;*.bmp;;*.png;;*.pdf"));
        if(fileName.isEmpty()) return;
        QString format = fileName.split(".").last();
        if(format == "jpg") plotObj->saveJpg(fileName);
        if(format == "bmp") plotObj->saveBmp(fileName);
        if(format == "png") plotObj->savePng(fileName);
        if(format == "pdf") plotObj->savePdf(fileName);
    });
    // if(showDark)  connect(showDark, &QAction::triggered, this, [=](bool trig){updateMap(*darkFrame, ui->plotMap, colorMap);});
    // if(showLight) connect(showLight, &QAction::triggered, this, [=](bool trig){updateMap(*lightFrame, ui->plotMap, colorMap);});

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

void MainWindow::saveSettings(){
    settings->beginGroup("Client");
    settings->setValue("PEER_IP", peerIP);
    settings->setValue("PEER_PORT", peerPort);
    settings->endGroup();

    settings->beginGroup("UI");
    if(ADCRangeLE)  settings->setValue(COMMANDS[Command::bitNo_ADCrange], ADCRangeLE  ->text());
    if(setRateLE )  settings->setValue(COMMANDS[Command::bitNo_Scanrate], setRateLE   ->text());
    if(readStreamLE)settings->setValue(COMMANDS[Command::bitNo_ReadStream], readStreamLE->text());
    settings->setValue("command_mask", getUIcommandMask());
    settings->setValue("GET_DATA", ui->lineEdit->text());
    settings->endGroup();

    settings->beginGroup("Calibration");
    settings->setValue("Dark_file", darkCalibFileName);
    settings->setValue("Light_file", lightCalibFileName);
    settings->endGroup();

    settings->sync();
}

QString MainWindow::getCalibrationFileName(FramePurpose fp){
    QString rngs = "";
    for(auto i = 0; i < 10; i++)
        rngs += QString::number(static_cast<int>(runContent.rngs.values[i]));
    return QString(fp == FrameDARK ? "Dark" : "Light") + "_" + rngs + "_" + QString::number(runContent.scanRate) + ".dat";
}

void MainWindow::loadSettings(){
    settings->beginGroup("Client");
    peerIP =   settings->value("PEER_IP", "172.20.75.16").toString();
    peerPort = settings->value("PEER_PORT", PEER_PORT).toUInt();
    settings->endGroup();

    settings->beginGroup("UI");
    if(ADCRangeLE)  ADCRangeLE->setText(settings->value(COMMANDS[Command::bitNo_ADCrange], QString(10, '5')).toString());
    if(setRateLE )  setRateLE->setText(settings->value(COMMANDS[Command::bitNo_Scanrate], 10).toString());
    if(readStreamLE)readStreamLE->setText(settings->value(COMMANDS[Command::bitNo_ReadStream], 1000).toString());
    ui->lineEdit->setText(settings->value("GET_DATA", 1000).toString());
    quint16 maskUI = settings->value("command_mask", 0).toUInt();
    for(auto i = 0; i < 16; ++i)
        this->findChild<QCheckBox*>(QString::asprintf("checkBox_%u", i))->setChecked(maskUI & (1 << i));
    settings->endGroup();

    settings->beginGroup("Calibration");
    darkCalibFileName = settings->value("Dark_file", "").toString();
    lightCalibFileName = settings->value("Light_file", "").toString();
    settings->endGroup();
}
