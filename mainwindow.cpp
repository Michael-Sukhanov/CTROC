#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>
#include <QDebug>
#include <stdio.h>

static const QVector<float> //  0  1  2    3    4   5  6    7
    ADCrangeValues_pC        {100,50,25,12.5,6.25,150,75,37.5};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
    rMode(RANGE_SAMPLING_FRAME), darkCalibFileName(""), lightCalibFileName(""), currentframeIndex(0),
    lastScanData(nullptr)
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
        ui->pushButton_prevFrame->setEnabled(false);
    });
    connect(ui->pushButton_gls             , &QPushButton::clicked, Tcpclient, &Client::getLastScan);
    connect(ui->pushButton_run             , &QPushButton::clicked, this, &MainWindow::sendRunCommand);


    //Обработка ответов от клиентов
    connect(Tcpclient, &Client::metaInfoReady   , this, &MainWindow::getMetaInfo   );
    connect(Tcpclient, &Client::scanStateReady  , this, &MainWindow::getScanState  );
    connect(Tcpclient, &Client::scanDataReady   , this, &MainWindow::getScanData   );
    connect(Tcpclient, &Client::runResponseReady, this, &MainWindow::getRunResponse);

    connect(this, &MainWindow::scanDataLoaded, this, &MainWindow::getScanData);

    //элементы визуализации (переключение между фреймами)
    connect(ui->pushButton_prevFrame, &QPushButton::clicked, this, &MainWindow::selectedFrameChanged);
    connect(ui->pushButton_nextFrame, &QPushButton::clicked, this, &MainWindow::selectedFrameChanged);
    connect(ui->horizontalSlider    , &QSlider::valueChanged, this, &MainWindow::selectedFrameChanged);
    connect(ui->pushButton_rangeMode, &QPushButton::clicked, this, [=, this](bool checked){
        if(frames.isEmpty()) return;
        rMode = checked ? RANGE_SAMPLING_FRAME : RANGE_SINGLE_FRAME;
        updateMap(frames[currentframeIndex], ui->plotMap, colorMap, rMode == RANGE_SAMPLING_FRAME ?
                  getMapRange(frames) :
                  getMapRange(frames[currentframeIndex]));
        ui->pushButton_rangeMode->setText(checked ? "Samples" : "Single");
    });

    //обработка поведения lineEditов
    //ui->lineEdit->setValidator (new QRegExpValidator(QRegExp("^([1-9][0-9]{0,2}|1000)$")));
    ui->lineEdit->setValidator(new QIntValidator(0,99999));
    if(ADCRangeLE  ) {ADCRangeLE->setValidator (new QRegExpValidator(QRegExp(QString("^[0-7]{1,%1}$").arg(nADC))));}
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
        connect(el, &QPushButton::clicked, this, [=, this](){
            QLineEdit* le = this->findChild<QLineEdit*>("lineEdit_com_" + el->objectName().mid(el->objectName().lastIndexOf("_")+1));
            Tcpclient->sendRunCommand(getCorrespondingCommand(el),
                elCommand == Command::ADCrange   ? le->text() : "",
                elCommand == Command::Scanrate   ? le->text() : "",
                elCommand == Command::ReadStream ? le->text() : ""
            );
            runGUIControl(false);
        });
    }

    connect(ui->pushButton_disDark,  SIGNAL(clicked()), this, SLOT(disableCalibration()));
    connect(ui->pushButton_disLight, SIGNAL(clicked()), this, SLOT(disableCalibration()));

    connect(ui->pushButton_drive, &QPushButton::clicked, this, [=, this](bool checked){
        Tcpclient->sendRunCommand(checked ? Command::drive_on : Command::drive_off, "", "", "");
        ui->pushButton_drive->setText(checked ? "drive_off" : "drive_on");
    });

    //обработка поведения check-boxов
    QList<QCheckBox*> checkBoxes = this->findChildren<QCheckBox*>(QRegularExpression("checkBox_*"));
    for(auto &el : checkBoxes) connect(el, &QCheckBox::clicked, this, [=, this](){ui->pushButton_run->setEnabled(getUIcommandMask());});

    initMap(ui->plotMap,      colorMap,     colorScale    );
    initMap(ui->plotMapMean,  colorMapMean, colorScaleMean, "Mean map" );
    initMap(ui->plotMapSigma, colorMapStd,  colorScaleStd , "Stdev map");

    connect(ui->plotMap     , &QCustomPlot::plottableClick, this, &MainWindow::saveImage);
    connect(ui->plotMapMean , &QCustomPlot::plottableClick, this, &MainWindow::saveImage);
    connect(ui->plotMapSigma, &QCustomPlot::plottableClick, this, &MainWindow::saveImage);

    Bars     = new QCPBars(ui->hist->xAxis, ui->hist->yAxis);
    meanBars = new QCPBars(ui->histMean->xAxis, ui->histMean->yAxis);
    stdBars  = new QCPBars(ui->histSigma->xAxis, ui->histSigma->yAxis);

    initHisto(ui->hist, colorScale);
    initHisto(ui->histMean, colorScaleMean);
    initHisto(ui->histSigma, colorScaleStd);

    //menuBar для сохранения и загрузки пакетов и калибровок
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    connect(fileMenu->addAction("Store data ..."), &QAction::triggered, this, [=, this](bool trig){
        QString fileName = QFileDialog::getSaveFileName(this, tr("Store Data"), "Data_" + QDateTime::currentDateTime().toString("yyMMddHHmmss"), tr("*.dat"));
        if(fileName.isEmpty()) return;
        if(lastScanData) lastScanData->storePacket(fileName);
    });
    connect(fileMenu->addAction("Load data ..."), &QAction::triggered, this, [=, this](bool trig){
        QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), path, tr("*.dat"));
        if(fileName.isEmpty()) return;
        path = QFileInfo(fileName).absolutePath();
        if(lastScanData) delete lastScanData;
        lastScanData = new ScanData(fileName);
        if(lastScanData->isPacketValid()) emit scanDataLoaded(lastScanData);
        else ui->textEdit_messages->append("Unable to show data");
    });
    fileMenu->addSeparator();
    connect(fileMenu->addAction("Load and apply Dark ..."), &QAction::triggered, this, [=, this]{
        QString fileName = QFileDialog::getOpenFileName(this, tr("Select dark data to calib"), path, tr("*.dat"));
        if(fileName.isEmpty()) return;
        updateDarkCalib(fileName);
        path = QFileInfo(fileName).absolutePath();
    });
    connect(fileMenu->addAction("Load and apply Light ..."), &QAction::triggered, this, [=, this]{
        QString fileName = QFileDialog::getOpenFileName(this, tr("Select dark data to calib"), path, tr("*.dat"));
        if(fileName.isEmpty()) return;
        updateLightCalib(fileName);
        path = QFileInfo(fileName).absolutePath();
    });

    connect(fileMenu, &QMenu::aboutToShow, this, [=, this](){fileMenu->actions()[0]->setEnabled(lastScanData);});
    connect(fileMenu, &QMenu::aboutToShow, this, [=, this](){fileMenu->actions()[4]->setEnabled(!darkFrame.empty);});


    //установка калибровочных фреймов по умолчанию
    msgBox.setIcon(QMessageBox::Information);
    if(QFile::exists(darkCalibFileName)) updateDarkCalib(darkCalibFileName);
    else{
        msgBox.setText("Файл калибровки темнового поля " + darkCalibFileName + " не найден");
        msgBox.exec();
        ui->label_Dark->setText("Disabled");
        ui->pushButton_disDark->setVisible(false);
        darkFrame  = Frame(FrameDARK);
    }

    if(QFile::exists(lightCalibFileName)) updateLightCalib(lightCalibFileName);
    else{
        msgBox.setText("Файл калибровки светового поля " + lightCalibFileName + " не найден");
        msgBox.exec();
        ui->label_Light->setText("Disabled");
        ui->pushButton_disLight->setVisible(false);
        lightFrame = Frame(FrameLIGHT);
    }

    connect(ui->pushButton_play, &QPushButton::clicked, this, [=, this](bool checked){
        if(frames.isEmpty()){ui->pushButton_play->setChecked(false); return;}
        ui->pushButton_play->setText(checked ? "⏸" : "⏵");
        if(checked) timer.start(40);
        else timer.stop();
    });

    connect(&timer, &QTimer::timeout, this, [=, this](){
        currentframeIndex++;
        selectedFrameChanged();
        if(currentframeIndex == frames.size() - 1){
            ui->pushButton_play->click();
            currentframeIndex = 0;
        }
    });

}

MainWindow::~MainWindow(){
    saveSettings();
    delete ui;
}

void MainWindow::getRawFrames(ScanData *response, QVector<Frame> &vec){
    vec.clear();
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
            for(auto x = 0; x < it->sizeX; ++x) (*it)(z, x) = ((*it)(z, x) - darkFrame(z, x)) / lightFrame(z, x);

    }
}

quint16 MainWindow::getCorrespondingCommand(QWidget *wgt){
    return (1 <<  wgt->objectName().split("_", Qt::SkipEmptyParts).last().toUInt());
}

quint8 MainWindow::getCorrespondingBitNo(QWidget *wgt){
    return wgt->objectName().split("_", Qt::SkipEmptyParts).last().toUInt();
}

void MainWindow::updateMap(Frame &fr, QCustomPlot *&plot, QCPColorMap *&cmap, QCPRange range){
    cmap->data()->setSize(fr.sizeX, fr.sizeZ);
    cmap->data()->setRange(QCPRange(0.5, fr.sizeX - 0.5), QCPRange(0.5, fr.sizeZ - 0.5));

    for(auto i = 0; i < fr.sizeX; ++i)
        for(auto k = 0; k < fr.sizeZ; ++k){
            cmap->data()->setCell(i, k, fr(k, i));
            // cmap->
        }
    cmap->setDataRange(range);
    plot->xAxis2->setRange(QCPRange(0, fr.sizeX));
    plot->xAxis->setRange(QCPRange(0, fr.sizeX));
    plot->yAxis->setRange(QCPRange(0, fr.sizeZ));
    plot->replot();
}

void MainWindow::initMap(QCustomPlot *&plot, QCPColorMap *&cmap, QCPColorScale *&cscale, QString title){
    // configure axis rect:
    // plot->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom); // this will also allow rescaling the color scale by dragging/zooming
    // plot->plotLayout()->insertRow(0);
    plot->plotLayout()->insertColumn(1);
    //plot->plotLayout()->addElement(0, 0, new QCPTextElement(plot, title));
    plot->axisRect()->setupFullAxesBox(false);
    //plot->xAxis->setLabel("Z");
    //plot->yAxis->setLabel("X");
    plot->yAxis->setRangeReversed(true);
    plot->xAxis->setTickLabels(false);
    plot->xAxis2->setVisible(true);
    plot->xAxis2->setTickLabels(true);
    cmap = new QCPColorMap(plot->xAxis, plot->yAxis);
    cmap->setInterpolate(false);

    cscale = new QCPColorScale(plot);
    plot->plotLayout()->addElement(0, 1, cscale); // add it to the right of the main axis rect
    cscale->setType(QCPAxis::atRight); // scale shall be vertical bar with tick/axis labels right (actually atRight is already the default)
    cscale->axis()->setTickLengthOut(cscale->axis()->tickLengthIn()/2);
    cscale->axis()->setSubTickLengthOut(cscale->axis()->subTickLengthIn()/2);
    // cscale->axis()->
    cmap->setColorScale(cscale); // associate the color map with the color scale
    //cscale->axis()->setLabel("ADC units");

    cmap->setGradient(getGradient(kDarkBodyRadiator));

    // make sure the axis rect and color scale synchronize their bottom and top margins (so they line up):
    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    cscale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    cscale->setRangeDrag(false);
    cscale->setRangeZoom(false);
}

void MainWindow::initHisto(QCustomPlot *&plot, QCPColorScale* &cscale){
    plot->setInteractions(QCP::iRangeZoom | QCP::iRangeDrag);
    plot->axisRect()->setRangeDrag(Qt::Horizontal);
    plot->axisRect()->setRangeZoom(Qt::Horizontal);

    connect(plot,  &QCustomPlot::mouseWheel,   this, [=, this](QWheelEvent *ev){changeCorrespondingRange();});
    connect(plot,  &QCustomPlot::mouseRelease, this, [=, this](QMouseEvent *ev){changeCorrespondingRange();});
}

void MainWindow::updateHisto(Frame &fr, QCustomPlot *&plot, QCPBars *&bars){
    double min = fr.min(), max = fr.max(), binWidth = 1;
    if (max>min) {
        int m  = lround(std::log10(max-min)*3 - 6); //define decimal order of magnitude for bin width to be 5/3 lower then whole range magnitude, so there will be about 30-70 bins
        binWidth = QList<int>({1,2,5})[(m%3+3)%3] * std::pow(10, m/3); //make nice values: 0.1, 0.2, 0.5, 1, 2, 5, 10, 20...
    };
    plot->xAxis->setRange(min, max);
    plot->yAxis->setRange(0, fr.sizeX*fr.sizeZ);
    QMap<double,double> map;
    for(auto i = 0; i < fr.sizeX*fr.sizeZ; ++i) map[std::round(fr.data[i]/binWidth)*binWidth]+=1;
    bars->setWidth(binWidth);
    bars->setData(map.keys().toVector(), map.values().toVector(), true);
    bool ok;
    auto rngx = bars->getKeyRange(ok);
    if (ok) plot->xAxis->setRange(rngx.lower-binWidth/2, rngx.upper+binWidth/2);
    auto rngy = bars->getValueRange(ok);
    if (ok) plot->yAxis->setRangeUpper(rngy.upper);

    plot->replot();

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
    if(!resp){qDebug() << "Trying to handle scan data: nullptr"; return;}

    lastScanData = resp;
    ui->pushButton_getData->setEnabled(true);

    getRawFrames(lastScanData, frames);
    correctFrames(frames.begin(), frames.end());

    meanFrame  = Frame(frames.begin(), frames.end(), FrameMEAN , nullptr, lastScanData->getSizeX(), lastScanData->getSizeZ());
    stdevFrame = Frame(frames.begin(), frames.end(), FrameSTDEV, &meanFrame, lastScanData->getSizeX(), lastScanData->getSizeZ());

    qDebug() << frames.size();
    // qDebug() << frames.first()._max << frames.first().sizeX << frames.first().sizeZ;

    updateMap(frames.first(), ui->plotMap,       colorMap      , rMode ?
            getMapRange(frames) : getMapRange(frames[currentframeIndex]));
    updateMap(meanFrame,      ui->plotMapMean,   colorMapMean  , QCPRange(meanFrame.min(), meanFrame.max()));
    updateMap(stdevFrame,     ui->plotMapSigma,  colorMapStd   , QCPRange(stdevFrame.min(), stdevFrame.max()));

    updateHisto(frames.first(), ui->hist     , Bars    );
    updateHisto(meanFrame     , ui->histMean , meanBars);
    updateHisto(stdevFrame    , ui->histSigma, stdBars );

    currentframeIndex = 0;
    ui->pushButton_prevFrame->setEnabled(false);
    if(frames.size() > 1 ) ui->pushButton_nextFrame->setEnabled(true);
    ui->horizontalSlider->setEnabled(true);
    ui->horizontalSlider->setRange(0, frames.size() - 1);
    QString txt = QString::asprintf("Frame %u/%u", currentframeIndex + 1, frames.size());
    ui->label_frameNo->setText(txt);
}

void MainWindow::getRunResponse(Run *resp){
    runContent.update(resp);
    quint32 mask = runContent.maskUpdated;
    if(Command::Status      & mask) ui->textEdit_messages->append("Status response received");
    if(Command::CompileTime & mask) ui->textEdit_messages->append("Compilation time: " + runContent.compilationDateTime.toString("yyyy-MM-dd HH:mm:ss"));
    // if(Command::CompileTime & mask) ui->textEdit_messages->append("Compilation time: " + runContent.compilationDateTime.toString(Qt::ISODate));
    if(Command::Nlines      & mask) ui->textEdit_messages->append(QString::asprintf("Number of ADCs (lines): %d", runContent.numLines));
    if(Command::ADCrange    & mask) {
        QString st="ADCnumber:"; for(auto i = 0; i < nADC; ++i) st+=QString::asprintf("%5u"  ,                                       i  ); ui->textEdit_messages->append(st);
                st="range, pC:"; for(auto i = 0; i < nADC; ++i) st+=QString::asprintf("%5.3g",ADCrangeValues_pC[runContent.ADCranges[i]]); ui->textEdit_messages->append(st);
    }
    if(Command::Scanrate    & mask) ui->textEdit_messages->append(QString::asprintf("Exposure time set to %.1f ms", runContent.scanRate * 0.1));
    if(Command::mux_adc     & mask) ui->textEdit_messages->append("Set MUX"     );
    if(Command::ScanMode    & mask) ui->textEdit_messages->append("Scan mode response received");
    if(Command::kadr_off    & mask) ui->textEdit_messages->append("Set KADR off");
    if(Command::EndMessage  & mask) ui->textEdit_messages->append(QString::asprintf("MSG payload: %u", runContent.MSGpayload));
    if(Command::Reset       & mask) ui->textEdit_messages->append("Reset " + QString(runContent.resetSuccessful ? "successful" : "failed"));
    if(Command::Offset      & mask) ui->textEdit_messages->append("Offset correction: " + (runContent.offsetCorrection == 0 ? QString("¯\\_(ツ)_/¯") : (runContent.offsetCorrection == 1 ? QString("done") : QString("failed"))));
    if(Command::Drift       & mask) ui->textEdit_messages->append("Drift correction: " + QString(runContent.driftCorrection == 0 ? "¯\\_(ツ)_/¯" : (runContent.driftCorrection == 1 ? "done" : "failed")));
    if(Command::kadr_on     & mask) ui->textEdit_messages->append("Set KADR on" );
    if(Command::ReadStream  & mask){ui->textEdit_messages->append(QString::asprintf("Trying to read %u frames", runContent.tryReadNFrames));
        if(runContent.readerrCode) ui->textEdit_messages->append( runContent.readerrCode == 1 ? "ERROR read: FIFO is empty" : "Unknown Error");
        else ui->textEdit_messages->append(QString::asprintf("%u frames collected", runContent.framesCollected));
    }
    if(Command::Temperature & mask) {
        QString st="ADCnumber:"; for(auto i = 0; i < nADC; ++i) st+=QString::asprintf("%5u"  ,                           i) ; ui->textEdit_messages->append(st);
                st="temp., °C:"; for(auto i = 0; i < nADC; ++i) st+=QString::asprintf("%5.3g",runContent.ADCtemperatures[i]); ui->textEdit_messages->append(st);
    }
    if(Command::RemainWords & mask) ui->textEdit_messages->append(QString::asprintf("Data FIFO payload: %u 32-bit words", runContent.FIFOpayload));
    runGUIControl(true);
}

void MainWindow::sendRunCommand(){
    quint16 msk = getUIcommandMask();
    if(msk) Tcpclient->sendRunCommand(msk, ADCRangeLE->text(), setRateLE->text(), readStreamLE->text());
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


    connect(pdfSave, &QAction::triggered, this, [=](bool trig){plotObj->savePdf(QDateTime::currentDateTime().toString("yyMMddHHmmss") + ".pdf");}, Qt::ConnectionType::UniqueConnection);
    connect(saveAs, &QAction::triggered, this, [=](bool trig){
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save image"), QDateTime::currentDateTime().toString("yyMMddHHmmss"), tr("*.png;;*.jpg;;*.bmp;;*.pdf;;*.dat"));
        if(fileName.isEmpty()) return;
        QString format = fileName.split(".").last();
        if(format == "jpg") plotObj->saveJpg(fileName);
        if(format == "bmp") plotObj->saveBmp(fileName);
        if(format == "png") plotObj->savePng(fileName);
        if(format == "pdf") plotObj->savePdf(fileName);
    });
    menu->popup(evnt->globalPos());

}

void MainWindow::selectedFrameChanged(){
    QPushButton *but = qobject_cast<QPushButton*>(sender());

    if(but == ui->pushButton_prevFrame && currentframeIndex > 0) --currentframeIndex;
    else if (but == ui->pushButton_nextFrame && currentframeIndex < frames.size()) ++currentframeIndex;
    else if(qobject_cast<QSlider*>(sender()) == ui->horizontalSlider && ui->horizontalSlider->value() != currentframeIndex)
        currentframeIndex = ui->horizontalSlider->value();

    updateMap(frames[currentframeIndex], ui->plotMap, colorMap, rMode ? getMapRange(frames) : getMapRange(frames[currentframeIndex]));

    ui->horizontalSlider->setValue(currentframeIndex);

    ui->pushButton_prevFrame->setEnabled(currentframeIndex);
    ui->pushButton_nextFrame->setEnabled(currentframeIndex != frames.size() - 1);
    ui->label_frameNo->setText(QString::asprintf("Frame %d/%d", currentframeIndex + 1, frames.size()));

    updateHisto(frames[currentframeIndex], ui->hist, Bars);
}

void MainWindow::changeCorrespondingRange(){
    QCustomPlot* sndr = qobject_cast<QCustomPlot*>(sender());
    Frame* fr; QCustomPlot* map; QCPColorMap* cmap;
    if(sndr == ui->hist     ){ fr = &frames[currentframeIndex]; map = ui->plotMap     ; cmap = colorMap    ;}
    else
    if(sndr == ui->histMean ){ fr = &meanFrame                ; map = ui->plotMapMean ; cmap = colorMapMean;}
    else
    if(sndr == ui->histSigma){ fr = &stdevFrame               ; map = ui->plotMapSigma; cmap = colorMapStd ;}
    else return;

    updateMap(*fr, map, cmap, sndr->xAxis->range());
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

void MainWindow::disableCalibration(){
    bool isDark = qobject_cast<QPushButton*>(sender()) == ui->pushButton_disDark;
    qobject_cast<QPushButton*>(sender())->setVisible(false);
    (isDark ? darkFrame : lightFrame) = Frame(isDark ? FrameDARK : FrameLIGHT);
    (isDark ? ui->label_Dark : ui->label_Light)->setText("Disabled");
    (isDark ? darkCalibFileName : lightCalibFileName) = "";
    getScanData(lastScanData);
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
    settings->setValue("Path", path);
    settings->endGroup();

    settings->sync();
}

Frame MainWindow::getDarkFrame(ScanData *sd){
    QVector<Frame> frames;
    getRawFrames(sd, frames);
    return Frame(frames.begin(),frames.end());
}

Frame MainWindow::getLighFrame(ScanData *sd){
    QVector<Frame> frames;
    getRawFrames(sd, frames);
    for(auto &frame : frames) for(auto i = 0; i < darkFrame.sizeX * darkFrame.sizeZ; ++i) frame.data[i] -= darkFrame.data[i];
    Frame lightFrame = Frame(frames.begin(), frames.end());
    for(auto i = 0; i < lightFrame.sizeX * lightFrame.sizeZ; ++i) lightFrame.data[i] /= lightFrame.mean();
    return lightFrame;
}

void MainWindow::updateDarkCalib(QString fileName){
    ui->pushButton_disDark->setVisible(true);

    ui->label_Dark->setText(QFileInfo(fileName).fileName());
    ui->label_Dark->setToolTip(QFileInfo(fileName).absoluteFilePath());
    ui->label_Dark->setToolTipDuration(-1);

    if(fileName.isEmpty()) return;
    ScanData* sd = new ScanData(fileName);
    darkFrame = getDarkFrame(sd);
    darkCalibFileName = fileName;
    delete sd;
}

void MainWindow::updateLightCalib(QString fileName){
    ui->pushButton_disLight->setVisible(true);

    ui->label_Light->setText(QFileInfo(fileName).fileName());
    ui->label_Light->setToolTip(QFileInfo(fileName).absoluteFilePath());
    ui->label_Light->setToolTipDuration(-1);

    if(fileName.isEmpty()) return;
    ScanData* sd = new ScanData(fileName);
    lightFrame = getLighFrame(sd);
    lightCalibFileName = fileName;
    delete sd;
}

QCPRange MainWindow::getMapRange(QVector<Frame> &frames){
    float sampleMax = frames.first().max();
    float sampleMin = frames.first().min();
    for(auto &fr : frames){
        if(sampleMin > fr.min()) sampleMin = fr.min();
        if(sampleMax < fr.min()) sampleMax = fr.min();
    }
    return QCPRange(sampleMin, sampleMax);
}

QCPRange MainWindow::getMapRange(Frame & frame){
    return QCPRange(frame.min(), frame.max());
}

void MainWindow::loadSettings(){
    settings->beginGroup("Client");
    peerIP =   settings->value("PEER_IP", "172.20.75.16").toString();
    peerPort = settings->value("PEER_PORT", PEER_PORT).toUInt();
    settings->endGroup();

    settings->beginGroup("UI");
    if(ADCRangeLE)  {
        ADCRangeLE->setText(settings->value(COMMANDS[Command::bitNo_ADCrange], QString(nADC, '5')).toString());
    }
    if(setRateLE )  setRateLE->setText(settings->value(COMMANDS[Command::bitNo_Scanrate], 10).toString());
    if(readStreamLE)readStreamLE->setText(settings->value(COMMANDS[Command::bitNo_ReadStream], 1024).toString());
    ui->lineEdit->setText(settings->value("GET_DATA", 1024).toString());
    quint16 maskUI = settings->value("command_mask", 0).toUInt();
    for(auto i = 0; i < 16; ++i)
        this->findChild<QCheckBox*>(QString::asprintf("checkBox_%u", i))->setChecked(maskUI & (1 << i));
    settings->endGroup();

    settings->beginGroup("Calibration");
    darkCalibFileName = settings->value("Dark_file", "").toString();
    lightCalibFileName = settings->value("Light_file", "").toString();
    path = settings->value("Path", "").toString();
    settings->endGroup();
}
