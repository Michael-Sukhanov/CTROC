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

    // timeLog = new std::ofstream("timingLog.dat",std::ios_base::app);
}

void Client::getMetaInfo(){
    try{
        reconnect(sockInfo, portInfo);
        if(sockInfo->write("GET METAINFO\r\n") == -1) throw sockInfo;
    }catch(QTcpSocket* sock){
        std::cout << sock->errorString().toStdString() << std::endl;
    }
}

void Client::getScanState()
{
    try{
        reconnect(sockState, portState);
        if(sockState->write("GET SCANSTATE\r\n") == -1) throw sockState;
    }catch(QTcpSocket* sock){
        std::cout << sock->errorString().toStdString() << std::endl;
    }
}

void Client::getScanData(size_t ln, size_t ff, size_t fc){
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

void Client::getLastScan(){
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

void Client::sendRunCommand(quint32 mask, QString ranges, QString scanRate, QString readNum){
    delete runResponse;
    runResponse = nullptr;
    try{
        reconnect(sockRun, portRun);
        QString request = "RUN COMMAND\r\nCommand-Line: " + makeCommand(mask, ranges, scanRate, readNum) +  "\r\n\r\n";
        // qDebug() << request;
        if(sockRun->write(request.toUtf8()) == -1) throw sockRun;
    }catch(QTcpSocket* sock){
        std::cout << "Could not write to socket: " <<  sock->errorString().toStdString() << std::endl;
    }
}

void Client::reconnect(QTcpSocket *sock, quint16 &port){
    if(port) if(!sock->bind(port)) throw sock;
    sock->connectToHost(peerIP, peerPort);

    if(!sock->waitForConnected(connectionTimeout)) throw sock;
    if(!port) port = sock->localPort();
}

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

