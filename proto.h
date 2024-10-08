#ifndef PROTO_H
#define PROTO_H

#include "qdatetime.h"
#include "qdebug.h"
#include <QString>
#include <QStringList>
#include <iostream>
#include <fstream>
#include <QFile>

namespace Command{
const quint32   bitNo_Status      =  0, Status      = (1 << bitNo_Status     ),
                bitNo_CompileTime =  1, CompileTime = (1 << bitNo_CompileTime),
                bitNo_Nlines      =  2, Nlines      = (1 << bitNo_Nlines     ),
                bitNo_ADCrange    =  3, ADCrange    = (1 << bitNo_ADCrange   ),
                bitNo_Scanrate    =  4, Scanrate    = (1 << bitNo_Scanrate   ),
                bitNo_mux_adc     =  5, mux_adc     = (1 << bitNo_mux_adc    ),
                bitNo_ScanMode    =  6, ScanMode    = (1 << bitNo_ScanMode   ),
                bitNo_kadr_off    =  7, kadr_off    = (1 << bitNo_kadr_off   ),
                bitNo_EndMessage  =  8, EndMessage  = (1 << bitNo_EndMessage ),
                bitNo_Reset       =  9, Reset       = (1 << bitNo_Reset      ),
                bitNo_Offset      = 10, Offset      = (1 << bitNo_Offset     ),
                bitNo_Drift       = 11, Drift       = (1 << bitNo_Drift      ),
                bitNo_kadr_on     = 12, kadr_on     = (1 << bitNo_kadr_on    ),
                bitNo_ReadStream  = 13, ReadStream  = (1 << bitNo_ReadStream ),
                bitNo_Temperature = 14, Temperature = (1 << bitNo_Temperature),
                bitNo_RemainWords = 15, RemainWords = (1 << bitNo_RemainWords),
                bitNo_drive_on    = 16, drive_on    = (1 << bitNo_drive_on   ),
                bitNo_drive_off   = 17, drive_off   = (1 << bitNo_drive_off  );
};

const QStringList COMMANDS ={
    "status"              ,// 0
    "comptime"            ,// 1
    "nlines"              ,// 2
    "adc_range"           ,// 3
    "scanrate"            ,// 4
    "mux_adc"             ,// 5
    "scan_mode"           ,// 6
    "kadr_off"            ,// 7
    "msg_after"           ,// 8
    "ipbus_timeout; reset",// 9
    "offset"              ,//10
    "drift"               ,//11
    "kadr_on"             ,//12
    "read_stream"         ,//13
    "temp"                ,//14
    "words_after"         ,//15
    "drive_on"            ,//16
    "drive_off"            //17
};

const int nADCmax = 16;
extern quint8 nADC;

QString makeCommand(quint32 commandPipeline, QString ranges, QString scanRate, QString readNum);

class Response{
public:
    Response(QByteArray &_ba);
    Response(QString fileName);
    ~Response();

    float   getVersion() const;
    quint16 getStatus() const;
    char *  getData() const;
    quint32 getContentLength() const;
    void    appendData(QByteArray &_ba);
    void    showData();

    void   storePacket(QString fname = "packet.dat");

    bool isPacketFull() const;
    bool isPacketValid() const;

protected:
    float version;
    quint32 status;
    char *data;
    quint32 contentLength, dataLength;
    QStringList preambleList;
    bool packetValid, packetFull;

private:
    quint32 fragmentCounter{0};
    //the fragment to use in other constructors;
    void _Response(QByteArray &_ba);

};

class MetaInfo : public Response{
public:
    MetaInfo(QByteArray &_ba);
    ~MetaInfo();

    QString getName() const;
    quint8  getAge() const;

private:
    QString name;
    quint8 age;
};

class ScanState : public Response{

public:
    ScanState(QByteArray &_ba);
    ~ScanState();

    bool getInProgress() const;
    quint32 getCompleteFrames() const;

private:
    bool inProgress;
    quint32 completeFrames;
};

class ScanData : public Response{

public:
    ScanData(QByteArray &_ba);
    ScanData(QString fileName);
    ~ScanData();

    quint8  getBytesPerPixel() const;
    quint16 getSizeX()         const;
    quint16 getSizeZ()         const;
    quint32 getFramesCount()   const;

private:
    bool compression;
    quint32 sizeX;
    quint32 sizeZ;
    quint32 framesPerLoop;
    quint32 loopNumber;
    quint32 firstFrame;
    quint32 bytesPerPixel;
    quint32 framesCount;

    void _ScanData();
};

class Run : public Response{

public:
    Run(QByteArray &_ba);
    ~Run();


private:
    char *data;

};

struct FrameHeader {
    uint16_t gantry_loop_no; /* 0 - 199, - 100 sec, 2 loops/sec */
    uint16_t frame_no_in_gantry_loop; /* 0 - 719 */
    uint32_t pixels_in_frame; /* Number of 16-bit or 24-bit pixels in frame */
    uint32_t computer_time_mark; /* в текущей версии протокола 0 */
    uint32_t detector_time_mark; /* в текущей версии протокола 0 */
    uint16_t offset_on_z; /* 0 - 20000 in 0.1 mm units */
    uint16_t detector_current; /* 1 mA +/- 16 bit enough */
    uint32_t frame_flags0; /* Pixel data validity is here */
    uint32_t frame_flags1; /* Pixel data validity is here */
    uint16_t reserved[2]; /* Pad to 32 bytes */
};

enum FramePurpose{
    FrameMEAN,
    FrameSTDEV,
    FrameSINGL,
    FrameDARK,
    FrameLIGHT
};

struct Frame{
    FrameHeader header;
    float data[nADCmax*8*32] = {0.};
    quint8 bpp;
    quint16 sizeX, sizeZ;
    FramePurpose fp;
    float _max = -1, _min = -1, _mean = -1;
    bool empty;

    Frame(ScanData* sd, quint32 frameNo);
    Frame(QVector<Frame>::iterator start, QVector<Frame>::iterator stop, FramePurpose purpose = FrameMEAN, Frame* mean = nullptr, quint16 _sX = 8*nADC, quint16 _sZ = 32);
    Frame(FramePurpose purpose = FrameDARK, QString fileName = "", quint16 _sX = 8*nADC, quint16 _sZ = 32);
    Frame(const Frame &fr);

    float& operator()(int z, int x){
        return data[x + z * sizeX];
    }
    Frame& operator=(const Frame &other){
        header = other.header;
        memcpy(data, other.data, other.sizeX * other.sizeZ * sizeof(float));
        bpp = other.bpp;
        sizeX = other.sizeX;
        sizeZ = other.sizeZ;
        fp = other.fp;
        _max = other._max;
        _min = other._min;
        _mean = other._mean;
        empty = other.empty;
        return *this;
    }
    ~Frame();

    bool writeToFile(QString fileName, bool withHeader = false);
    float max();
    float min();
    float mean();
    void show();
};

struct RunContent{
    float ADCtemperatures[nADCmax];
    quint32 FIFOpayload, MSGpayload, tryReadNFrames, ipbusReads, framesCollected;
    int driftCorrection, offsetCorrection, readerrCode;
    bool scanMode, resetSuccessful;
    int numLines;
    QDateTime compilationDateTime;
    quint8 ADCranges[nADCmax] = {0};
    quint16 scanRate;
    quint32 maskUpdated;

    RunContent(Run *rc = nullptr);
    void update(Run *);
};

#endif // PROTO_H
