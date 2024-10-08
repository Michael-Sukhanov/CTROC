#ifndef CLIENT_H
#define CLIENT_H

#include "qregularexpression.h"
#include <iostream>
#include <QObject>
#include <QTcpSocket>
#include <proto.h>
#include <QDebug>
#include <QDateTime>
#include <fstream>


const QString PEER_IP = "185.207.88.173";
const quint16 PEER_PORT = 8317;
const int CONNECTION_TIMEOUT = 3000;

class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(QString _peerIP        = PEER_IP,
                    quint16 _peer_port     = PEER_PORT,
                    int _connectionTimeout = CONNECTION_TIMEOUT,
                    QObject *parent        = nullptr);

public slots:
    void getMetaInfo   ();
    void getScanState  ();
    void getScanData   (size_t, size_t, size_t);
    void getLastScan   ();
    void sendRunCommand(quint32 mask, QString ranges, QString scanRate, QString readNum);

signals:
    void metaInfoReady   (MetaInfo*  );
    void scanStateReady  (ScanState* );
    void scanDataReady   (ScanData*  );
    void runResponseReady(Run*       );

private:
    quint16 portInfo = 0, portState = 0, portData = 0, portRun = 0, peerPort;
    QString peerIP;
    int connectionTimeout;
    // std::ofstream* timeLog;
    // QTime requestDataSent, requestMetaSent, requestStateSent, requestRunCommand;

    QTcpSocket *sockInfo, *sockData, *sockState, *sockRun;
    void reconnect(QTcpSocket * sock, quint16 &port);

    ScanData* dataResponse;
    Run* runResponse;
    QAbstractSocket::SocketError lastSocketError;


private slots:
    // void read();
    void read();

};

#endif // CLIENT_H
