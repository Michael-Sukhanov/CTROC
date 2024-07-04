#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpSocket>
#include <QDebug>
#include <stdio.h>

const QStringList sADCranges = {
    "100 pC", "50 pC", "25 pC", "12.5 pC", "6.25 pC", "150 pC", "75 pC", "37.5 pC"
};

MainWindow::MainWindow(QString _peer_IP, quint16 _peer_port, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
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
    ui->lineEdit->setValidator       (new QRegExpValidator(QRegExp("^([1-9][0-9]{0,2}|1000)$")));
    ui->lineEdit_com_3->setText("5555555555");
    ui->lineEdit_com_3->setValidator (new QRegExpValidator(QRegExp("^[0-7]{1,10}$"           )));
    ui->lineEdit_com_4->setValidator (new QRegExpValidator(QRegExp("[0-9]+"                  )));
    ui->lineEdit_com_10->setValidator(new QRegExpValidator(QRegExp("[0-9]+"                  )));


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
        connect(el, &QPushButton::clicked, this, [=](){

            quint32* scanRate = nullptr;
            quint32* readNum = nullptr;
            Ranges* rngs = nullptr;

            QPushButton* pb = qobject_cast<QPushButton*>(el);
            if(pb == ui->pushButton_com_3 && !ui->lineEdit_com_3->text().isEmpty()){
                    rngs = new Ranges();
                    for(auto i = 0; i < ui->lineEdit_com_3->text().size(); ++i)
                        rngs->values[i] = static_cast<ADCrange>(ui->lineEdit_com_3->text().mid(i, 1).toUInt());
                }

            if(pb == ui->pushButton_com_4 && !ui->lineEdit_com_4->text().isEmpty()){
                scanRate = new quint32;
                *scanRate = ui->lineEdit_com_4->text().toUInt();
            }

            if(pb == ui->pushButton_com_10 && !ui->lineEdit_com_10->text().isEmpty()){
                readNum = new quint32;
                *readNum = ui->lineEdit_com_10->text().toUInt();
            }

            quint8 bitNo = pb->objectName().mid(pb->objectName().lastIndexOf("_") + 1).toUInt();
            Tcpclient->sendRunCommand((1 << bitNo), rngs, scanRate, readNum);

            if(scanRate) delete scanRate;
            if(readNum) delete readNum;
            if(rngs) delete rngs;

            runGUIControl(false);
        });
    }

    //обработка поведения check-boxов
    QList<QCheckBox*> checkBoxes = this->findChildren<QCheckBox*>(QRegularExpression("checkBox_*"));
    for(auto &el : checkBoxes) connect(el, &QCheckBox::clicked, this, [=](){ui->pushButton_run->setEnabled(getUIcommandMask());});

    initMap();
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

void MainWindow::updateMap(Frame &fr){
    colorMap->data()->setSize(fr.sizeZ, fr.sizeX);
    colorMap->data()->setRange(QCPRange(0, fr.sizeZ), QCPRange(0, fr.sizeX));

        for(auto i = 0; i < fr.sizeZ; ++i)
            for(auto k = 0; k < fr.sizeX; ++k){
                colorMap->data()->setCell(i, k, fr(i, k));
            }

    colorScale->setDataRange(QCPRange(0, 1 << static_cast<int>(std::ceil(std::log2(fr.max())))));

    ui->plotMap->rescaleAxes();
    ui->plotMap->replot();

    ui->statusbar->showMessage("Map updated", 10);
}

void MainWindow::initMap(){
    // configure axis rect:
    //ui->plotMap->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom); // this will also allow rescaling the color scale by dragging/zooming
    ui->plotMap->axisRect()->setupFullAxesBox(true);
    ui->plotMap->xAxis->setLabel("Z");
    ui->plotMap->yAxis->setLabel("X");

    colorMap = new QCPColorMap(ui->plotMap->xAxis, ui->plotMap->yAxis);
    colorMap->setInterpolate(false);

    colorScale = new QCPColorScale(ui->plotMap);
    ui->plotMap->plotLayout()->addElement(0, 1, colorScale); // add it to the right of the main axis rect
    colorScale->setType(QCPAxis::atRight); // scale shall be vertical bar with tick/axis labels right (actually atRight is already the default)
    colorMap->setColorScale(colorScale); // associate the color map with the color scale
    colorScale->axis()->setLabel("Amplitude");
//    colorScale->setDataScaleType(QCPAxis::stLogarithmic);

    colorMap->setGradient(QCPColorGradient::gpIon);

    // make sure the axis rect and color scale synchronize their bottom and top margins (so they line up):
    QCPMarginGroup *marginGroup = new QCPMarginGroup(ui->plotMap);
    ui->plotMap->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    colorScale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
    colorScale->setRangeDrag(false);
    colorScale->setRangeZoom(false);


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
    ScanData *sd = resp;
    ui->pushButton_getData->setEnabled(true);
    getFrames(sd, frames);
    updateMap(frames.first());
    writeToFile(frames);
    currentframeIndex = 0;
    ui->pushButton_prevFrame->setEnabled(false);
    if(frames.size() > 1 ) ui->pushButton_nextFrame->setEnabled(true);
    ui->horizontalSlider->setEnabled(true);
    ui->horizontalSlider->setRange(0, frames.size() - 1);
    ui->label_frameNo->setText(QString::asprintf("Frame %d/%d", currentframeIndex + 1, frames.size()));
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
    if(Command::StartStream & mask) ui->textEdit_messages->append("Start data reading: response received");
    if(Command::RemainWords & mask) ui->textEdit_messages->append(QString::asprintf("Data FIFO payload: %u 32-bit words", rc.FIFOpayload));
    if(Command::EndMessage  & mask) ui->textEdit_messages->append(QString::asprintf("MSG payload: %u", rc.MSGpayload));
    if(Command::ReadStream  & mask) {ui->textEdit_messages->append(QString::asprintf("Trying to read %u frames", rc.tryReadNFrames));
        if(rc.readerrCode) ui->textEdit_messages->append( rc.readerrCode == 1 ? "ERROR read: FIFO is empty" : "Unknown Error");
        else ui->textEdit_messages->append(QString::asprintf("%u frames collected", rc.framesCollected));
    }
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

void MainWindow::sendRunCommand(){
    quint16 msk = getUIcommandMask();

    quint32* scanRate = nullptr;
    quint32* readNum = nullptr;
    Ranges* rngs = nullptr;

    if(msk & Command::ADCrange && !ui->lineEdit_com_3->text().isEmpty()){
        rngs = new Ranges();
        for(auto i = 0; i < ui->lineEdit_com_3->text().size(); ++i)
            rngs->values[i] = static_cast<ADCrange>(ui->lineEdit_com_3->text().mid(i, 1).toUInt());
    }

    if(msk & Command::Scanrate && !ui->lineEdit_com_4->text().isEmpty()){
        scanRate = new quint32;
        *scanRate = ui->lineEdit_com_4->text().toUInt();
    }

    if(msk & Command::ReadStream && !ui->lineEdit_com_10->text().isEmpty()){
        readNum = new quint32;
        *readNum = ui->lineEdit_com_10->text().toUInt();
    }

    if(msk) Tcpclient->sendRunCommand(msk, rngs, scanRate, readNum);

    if(scanRate) delete scanRate;
    if(readNum) delete readNum;
    if(rngs) delete rngs;

    runGUIControl(false);
}

void MainWindow::selectedFrameChanged(){
    QPushButton *but = qobject_cast<QPushButton*>(sender());
    if(but == ui->pushButton_prevFrame && currentframeIndex > 0)
        updateMap(frames[--currentframeIndex]);
    else if(but == ui->pushButton_nextFrame && frames.size() - 1 > currentframeIndex)
        updateMap(frames[++currentframeIndex]);
    else if(qobject_cast<QSlider*>(sender()) == ui->horizontalSlider){
        if(ui->horizontalSlider->value() == currentframeIndex) return;
        updateMap(frames[currentframeIndex = ui->horizontalSlider->value()]);
    }
    ui->horizontalSlider->setValue(currentframeIndex);

    ui->pushButton_prevFrame->setEnabled(currentframeIndex);
    ui->pushButton_nextFrame->setEnabled(currentframeIndex != frames.size() - 1);
    ui->label_frameNo->setText(QString::asprintf("Frame %d/%d", currentframeIndex + 1, frames.size()));
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
