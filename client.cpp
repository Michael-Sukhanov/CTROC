#include "client.h"
#include "qthread.h"
#include "qtimer.h"

Client::Client(QString _peerIP, quint16 _peer_port, int _connectionTimeout, QObject *parent)
    : QObject{parent}, peerPort(_peer_port), peerIP(_peerIP), connectionTimeout(_connectionTimeout), dataResponse(nullptr), runResponse(nullptr){

    sockInfo  = new QTcpSocket(this);
    sockState = new QTcpSocket(this);
    sockData  = new QTcpSocket(this);
    sockRun   = new QTcpSocket(this);

    connect(sockInfo , &QTcpSocket::readyRead, this, &Client::read);
    connect(sockState, &QTcpSocket::readyRead, this, &Client::read);
    connect(sockData , &QTcpSocket::readyRead, this, &Client::read);
    connect(sockRun  , &QTcpSocket::readyRead, this, &Client::read);

    timeLog = new std::ofstream("timingLog.dat",std::ios_base::app);
}

void Client::getMetaInfo(){
    try{
        reconnect(sockInfo, portInfo);
        if(sockInfo->write("GET METAINFO\r\n") == -1) throw sockInfo;
        requestMetaSent = QTime::currentTime();
    }catch(QTcpSocket* sock){
        std::cout << sock->errorString().toStdString() << std::endl;
    }
}

void Client::getScanState()
{
    try{
        reconnect(sockState, portState);
        if(sockState->write("GET SCANSTATE\r\n") == -1) throw sockState;
        requestStateSent = QTime::currentTime();
    }catch(QTcpSocket* sock){
        std::cout << sock->errorString().toStdString() << std::endl;
    }
}

void Client::getScanData(quint16 ln, quint16 ff, quint16 fc){
    delete dataResponse;
    dataResponse = nullptr;
    try{
        reconnect(sockData, portData);
        QString request = QString::asprintf("GET SCANDATA\r\nLoop-Number: %u\r\nFirst-Frame: %u\r\nFrames-Count: %u\r\n\r\n", ln, ff, fc);
        if(sockData->write(request.toUtf8()) == - 1) throw sockData;
    }catch(QTcpSocket* sock){
        std::cout << sock ->errorString().toStdString() << std::endl;
    }
}

void Client::getLaslScan(){
    delete dataResponse;
    dataResponse = nullptr;
    try{
        reconnect(sockData, portData);
        QString request = QString::asprintf("GET LASTSCAN\r\n\r\n");
        if(sockData->write(request.toUtf8()) == - 1) throw sockData;
    }catch(QTcpSocket* sock){
        std::cout << sock ->errorString().toStdString() << std::endl;
    }
}

void Client::sendRunCommand(quint16 mask, Ranges *ranges, quint32* scanRate, quint32* readNum){
    delete runResponse;
    runResponse = nullptr;
    try{
        reconnect(sockRun, portRun);
        QString request = "RUN COMMAND\r\nCommand-Line: " + makeCommand(mask, ranges, scanRate, readNum) +  "\r\n\r\n";
        // qDebug() << request;
        if(sockRun->write(request.toUtf8()) == -1) throw sockRun;
    }catch(QTcpSocket* sock){
        std::cout << sock->errorString().toStdString() << std::endl;
    }
}

void Client::reconnect(QTcpSocket *sock, quint16 &port){
    if(port) if(!sock->bind(port)) throw sock;
    sock->connectToHost(peerIP, peerPort);

    if(!sock->waitForConnected(connectionTimeout)) throw sock;
    if(!port) port = sock->localPort();
}

// void Client::read(){
//     static int fragCnt;
//     QTcpSocket* sock = qobject_cast<QTcpSocket*>(sender());
//     if(sock == sockData && responseBuffer){
//             dataBuffer.append(sock->readAll());
//             qDebug() << "Got data fragment: " << fragCnt++ << dataBuffer.size() << '/' << responseBuffer->getContentLength();
//         if(dataBuffer.size() == responseBuffer->getContentLength()){
//             responseBuffer->setData(dataBuffer);
//             emit scanDataReady(responseBuffer);
//             qDebug() << "The data  is full";
//         }else if(dataBuffer.size() > responseBuffer->getContentLength())
//             std::cout << "Data buffer bigger than expected" << std::endl;
//         return;
//     }

//     QByteArray arrTmp = sock->readAll();
//     QString tmp = QString(arrTmp);
//     QStringList list = tmp.split(QRegularExpression("(/|\\s+)"), Qt::SkipEmptyParts);
//     QStringList::Iterator start = list.begin() + 1;

//     if(sock == sockRun){
//         qDebug() << QString(arrTmp);
//     }

//     if(sock == sockInfo){
//         emit metaInfoReady(new MetaInfo(arrTmp));
//         *timeLog << "M\t" << requestMetaSent.secsTo(QTime::currentTime()) << std::endl;
//     }

//     if(sock == sockState){
//         emit scanStateReady(new ScanState(arrTmp));
//         *timeLog << "S\t" << requestStateSent.secsTo(QTime::currentTime()) << std::endl;
//     }

//     if(sock == sockData && !responseBuffer){
//         fragCnt = 0;
//         responseBuffer = new ScanData(arrTmp);
//         dataBuffer.append(arrTmp.mid(arrTmp.lastIndexOf(QByteArray("\r\n\r\n")) + 4));
//         qDebug() << "Got data fragment: " << fragCnt++;

//         if(dataBuffer.size() == responseBuffer->getContentLength()){
//             responseBuffer->setData(dataBuffer);
//             emit scanDataReady(responseBuffer);
//             qDebug() << "The data  is full";
//         }
//         if(responseBuffer->getStatus() != 200) emit scanDataReady(responseBuffer);

//         *timeLog << "D\t" << requestDataSent.secsTo(QTime::currentTime()) << std::endl;
//     }

// }

void Client::read(){
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    QByteArray receivedBytes = sock->readAll();

    if(sock == sockInfo ) emit metaInfoReady(new MetaInfo(receivedBytes));
    if(sock == sockState) emit scanStateReady(new ScanState(receivedBytes));

    if(sock == sockData ){
        if(!dataResponse){
            dataResponse = new ScanData(receivedBytes);
        }else{
            if(dataResponse->isPacketValid()) dataResponse->appendData(receivedBytes);
            else{
                delete dataResponse;
                dataResponse = nullptr;
            }

        }
        if(dataResponse && dataResponse->isPacketFull() && dataResponse->isPacketValid()){
            qDebug() << "Scan data response ready";
            // dataResponse->showData();
            emit scanDataReady(dataResponse);
        }
    }

    if(sock == sockRun){
        if(!runResponse){
            runResponse = new Run(receivedBytes);
        }else{
            if(runResponse->isPacketValid()) runResponse->appendData(receivedBytes);
            else{
                delete runResponse;
                runResponse = nullptr;
            }

        }
        if(runResponse && runResponse->isPacketFull() && runResponse->isPacketValid()){
            qDebug() << "Run response ready";
            // dataResponse->showData();
            emit runResponseReady(runResponse);
        }
    }

}

