#ifndef PROTO_H
#define PROTO_H

#include "qdatetime.h"
#include "qdebug.h"
#include <QString>
#include <QStringList>
#include <iostream>

namespace Command{
const quint16 bitNo_Status      =  0, Status      = (1 << bitNo_Status     ),
              bitNo_CompileTime =  1, CompileTime = (1 << bitNo_CompileTime),
              bitNo_Nlines      =  2, Nlines      = (1 << bitNo_Nlines     ),
              bitNo_ADCrange    =  3, ADCrange    = (1 << bitNo_ADCrange   ),
              bitNo_Scanrate    =  4, Scanrate    = (1 << bitNo_Scanrate   ),
              bitNo_mux_adc     =  5, mux_adc     = (1 << bitNo_mux_adc    ),
              bitNo_ScanMode    =  6, ScanMode    = (1 << bitNo_ScanMode   ),
              bitNo_kadr_off    =  7, kadr_off    = (1 << bitNo_kadr_off   ),
              bitNo_Reset       =  8, Reset       = (1 << bitNo_Reset      ),
              bitNo_Drift       =  9, Drift       = (1 << bitNo_Drift      ),
              bitNo_Offset      = 10, Offset      = (1 << bitNo_Offset     ),
              bitNo_kadr_on     = 11, kadr_on     = (1 << bitNo_kadr_on    ),
              bitNo_ReadStream  = 12, ReadStream  = (1 << bitNo_ReadStream ),
              bitNo_Temperature = 13, Temperature = (1 << bitNo_Temperature),
              bitNo_RemainWords = 14, RemainWords = (1 << bitNo_RemainWords),
              bitNo_EndMessage  = 15, EndMessage  = (1 << bitNo_EndMessage );
};

const QStringList COMMANDS ={
    "status",       //0
    "comptime",     //1
    "nlines",       //2
    "adc_range",    //3
    "scanrate",     //4
    "mux_adc",      //5
    "scan_mode",    //6
    "kadr_off",     //7
    "reset",        //8
    "drift",        //9
    "offset",       //10
    "kadr_on",      //11
    "read_stream",  //12
    "temp",         //13
    "words_after",  //14
    "msg_after"     //15
};

enum ADCrange{
    ADC_RANGE_100pC  = 0,
    ADC_RANGE_50pC   = 1,
    ADC_RANGE_25pC   = 2,
    ADC_RANGE_12_5pC = 3,
    ADC_RANGE_6_25pC = 4,
    ADC_RANGE_150pC  = 5,
    ADC_RANGE_75pC   = 6,
    ADC_RANGE_37_5pC = 7
};

struct Ranges
{
    ADCrange values[10];
};



QString makeCommand(quint16 commandPipeline, Ranges *ranges = nullptr, quint32* scanRate = nullptr, quint32* readNum = nullptr);

class Response{
public:
    Response(QByteArray &_ba);
    ~Response();

    float getVersion() const;
    quint16 getStatus() const;
    char *getData() const;
    quint32 getContentLength() const;
    void appendData(QByteArray &_ba);
    void showData();

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
    ~ScanData();

    quint8  getBytesPerPixel() const;
    quint16 getSizeX()         const;
    quint16 getSizeZ()         const;
    quint16 getFramesCount()   const;

private:
    bool compression;
    quint32 sizeX;
    quint32 sizeZ;
    quint32 framesPerLoop;
    quint32 loopNumber;
    quint32 firstFrame;
    quint32 bytesPerPixel;
    quint32 framesCount;
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
    FrameSINGL
};

struct Frame{
    FrameHeader header;
    char *data;
    quint8 bpp;
    quint16 sizeX, sizeZ;
    FramePurpose fp;

    Frame(ScanData* sd, quint16 frameNo);
    Frame(QVector<Frame>::iterator start, QVector<Frame>::iterator stop, FramePurpose purpose = FrameMEAN, Frame* mean = nullptr);
    const float operator()(int z, int x) const {
        if(fp == FrameSINGL)
            return static_cast<float>(*reinterpret_cast<quint32*>(data + (z * sizeX + x) * bpp) & ((0x1 << bpp * 8) - 1));
        else
            return *reinterpret_cast<float*>(data + (z * sizeX + x) * sizeof(float));
    }

    float max();
    float min();
    void show();
};



struct RunContent{
    float ADCtemperature;
    quint32 FIFOpayload, MSGpayload, tryReadNFrames, ipbusReads, framesCollected;
    int driftCorrection, offsetCorrection, readerrCode;
    bool scanMode, resetSuccessful;
    int numLines;
    QDateTime compilationDateTime;
    Ranges rngs;
    quint32 scanRate;

    quint16 maskUpdated;

    RunContent(Run *);
};

#endif // PROTO_H
