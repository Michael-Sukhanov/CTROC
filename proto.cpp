#include "proto.h"
#include "qdebug.h"
#include <iostream>

Response::Response(QByteArray &_ba):version(0),status(0), data(nullptr), contentLength(0), dataLength(0),packetFull(false){
    QString input = QString(_ba);
    QStringList tmpList = input.split("\r\n\r\n", Qt::SkipEmptyParts);

    QString preamble = tmpList.size() ? tmpList.first() : "";
    QByteArray dataPart = _ba.mid(_ba.indexOf("\r\n\r\n") + 4);

    preambleList = preamble.split("\r\n", Qt::SkipEmptyParts);
    for(auto &el : preambleList){
        if(-1 != QRegExp("PROTO*").indexIn(el))
            sscanf_s(el.toStdString().c_str(), "PROTO/%f %d", &version, &status);
        if(-1 != QRegExp("Content-Length*").indexIn(el)){
            sscanf_s(el.toStdString().c_str(), "Content-Length: %u", &contentLength);
            data = new char[contentLength];
            appendData(dataPart);
        }

    }
    packetValid = (status == 200);
}

Response::~Response(){
    if(data) delete []data;
}

float   Response::getVersion()   const {return version;    }
quint16 Response::getStatus()    const {return status;     }
char   *Response::getData()      const {return data;       }
bool    Response::isPacketFull() const {return packetFull; }
bool    Response::isPacketValid()  const {return packetValid;}

void Response::appendData(QByteArray &_ba){
    if(!packetFull && contentLength && _ba.size()){
        for(auto i = 0; i < _ba.size(); ++i)
            data[dataLength + i] = _ba[i];
        // qDebug() << data[dataLength] << _ba[0];
        dataLength += _ba.size();
        packetFull = (dataLength == contentLength);
        qDebug() << "[" << fragmentCounter++ << "]: " << dataLength << "/" << contentLength;
    }
}

quint32 Response::getContentLength() const{return contentLength;}
void Response::showData(){
    for(quint32 i = 0; i < this->contentLength; ++i){
        std::cout << data[i];
    }
}

MetaInfo::MetaInfo(QByteArray &_ba):Response(_ba){
    for(auto &el : preambleList){
        // if(-1 != QRegExp("Person-Name:*").indexIn(el)){
        //     std::string s1, s2, s3;
        //     sscanf_s(el.toStdString().c_str(), "Person-Name: %s %s %s", &s1, &s2, &s3);
        //     name = QString::fromStdString(s1) + " " + QString::fromStdString(s2) + QString::fromStdString(s3);
        // }
        if(-1 != QRegExp("Person-Age:*").indexIn(el))
            sscanf_s(el.toStdString().c_str(), "Person-Age: %u", &age);
    }
}

MetaInfo::~MetaInfo(){}

QString MetaInfo::getName() const {return name;}
quint8  MetaInfo::getAge() const {return age; }

ScanState::ScanState(QByteArray &_ba):Response(_ba){
    for(auto &el : preambleList){
        if(-1 != QRegExp("In-Progress*").indexIn(el)){
            std::string s;
            sscanf_s(el.toStdString().c_str(), "In-progress: %s", &s);
            inProgress = s == "yes";
        }
        if(-1 != QRegExp("Complete-Frames*").indexIn(el))
            sscanf_s(el.toStdString().c_str(), "Complete-Frames: %d", &completeFrames);
    }
    qDebug() << inProgress << completeFrames;
}

ScanState::~ScanState(){}

bool ScanState::getInProgress() const{return inProgress;}
quint32 ScanState::getCompleteFrames() const{return completeFrames;}

ScanData::ScanData(QByteArray &_ba):Response(_ba),sizeX(80),sizeZ(32),bytesPerPixel(2){
    framesCount = contentLength/ (2* sizeX * sizeZ + 32); //по умолчанию, если не известно реальное количество фреймов
    for(const auto &el : preambleList){
        if(-1 != el.indexOf("Bytes-Per-Pixel:"))
            sscanf_s(el.toStdString().c_str(), "Bytes-Per-Pixel: %u", &bytesPerPixel);
        else if(-1 != el.indexOf("Matrix-Size-X:"))
            sscanf_s(el.toStdString().c_str(), "Matrix-Size-X: %u", &sizeX);
        else if(-1 != el.indexOf("Matrix-Size-Z:")){
            sscanf_s(el.toStdString().c_str(), "Matrix-Size-Z: %u", &sizeZ);}
        else if(-1 != el.indexOf("Frames-Per-Loop:")){
            sscanf_s(el.toStdString().c_str(), "Frames-Per-Loop: %u", &framesPerLoop);}
        else if(-1 != el.indexOf("Loop-Number:")){
            sscanf_s(el.toStdString().c_str(), "Loop-Number: %u", &loopNumber);}
        else if(-1 != el.indexOf("First-Frame:")){
            sscanf_s(el.toStdString().c_str(), "First-Frame: %u", &firstFrame);}
        else if(-1 != el.indexOf("Frames-Count:")){
            sscanf_s(el.toStdString().c_str(), "Frames-Count: %u", &framesCount);}
    }
}

ScanData::~ScanData(){}

quint8 ScanData::getBytesPerPixel() const{
    return bytesPerPixel;}
quint16 ScanData::getSizeX() const { return sizeX;}
quint16 ScanData::getSizeZ() const {return sizeZ;}

quint16 ScanData::getFramesCount() const{return framesCount;}



Frame::Frame(ScanData *sd, quint16 frameNo){
    bpp = sd->getBytesPerPixel(), sizeX = sd->getSizeX(), sizeZ = sd->getSizeZ();
    quint32 step = bpp * sizeX * sizeZ + 32;
    header = *reinterpret_cast<FrameHeader*>(sd->getData() + frameNo * step);
    data = sd->getData() + frameNo * step + 32;
    // bpp = sd->getBytesPerPixel();
}

quint32 Frame::max(){
    quint32 result = 0;
    for(auto i = 0; i < sizeX * sizeZ; ++i){
        quint32 tmp = (*reinterpret_cast<quint32*>(data + i * bpp) & ((0x1 << bpp * 8) - 1));
        if(result < tmp)
            result = tmp;
    }
    return result;
}

void Frame::show(){
    qDebug() << header.pixels_in_frame << header.frame_flags0 << header.frame_flags1;
    for(auto i = 0; i <sizeZ * bpp; ++i)
        for(auto k = 0; k < sizeX * bpp; ++k)
            qDebug() << data[(i * sizeX + k) * bpp];
}

QString makeCommand(quint16 commandPipeline, Ranges *ranges, quint32 *scanRate, quint32 *readNum){
    QStringList retValue;
    for(auto i = 0; i < COMMANDS.size(); ++i){
        QString temp = COMMANDS[i];
        if((1 << i) == Command::ADCrange && ranges)
            for(auto i = 0; i < 10; ++i) temp += " " + QString::number(ranges->values[i]);
        if((1 << i) == Command::Scanrate && scanRate)  temp += " " + QString::number(*scanRate);
        if((1 << i) == Command::ReadStream && readNum) temp += " " + QString::number(*readNum);
        if(commandPipeline & (1 << i)) retValue.append(temp);
    }
    return retValue.join(";");
}

Run::Run(QByteArray &_ba):Response(_ba){
    qDebug() << preambleList;
}
Run::~Run(){}

RunContent::RunContent(Run * r){
    maskUpdated = 0;
    QStringList list =  QString(QByteArray(r->getData(), r->getContentLength())).split("\n");
    qDebug() << list;
    for(auto &el : list){
        auto idxOfThisEl = list.indexOf(el);
        if(-1 != el.indexOf("Total ADC lines")){
            maskUpdated |= Command::Nlines; sscanf_s(el.toStdString().c_str(), "Total ADC lines: %d", &numLines);
        }
        if(-1 != el.indexOf("ADC drift offset correction ON")){
            maskUpdated |= Command::Drift;
            int idxOfMsgs = list.indexOf("MSG SUCSESS", list.indexOf(el));
            driftCorrection = 1;
            if(idxOfMsgs == -1){
                idxOfMsgs = list.indexOf("MSG FAILED", list.indexOf(el));
                driftCorrection = -1;
            }
            if(idxOfMsgs == -1) driftCorrection = 0;
        }
        if(-1 != el.indexOf("ADC offset correction ON")){
            maskUpdated |= Command::Offset;
            int idxOfMsgs = list.indexOf("MSG SUCSESS", list.indexOf(el));
            offsetCorrection = 1;
            if(idxOfMsgs == -1){
                idxOfMsgs = list.indexOf("MSG FAILED", list.indexOf(el));
                offsetCorrection = -1;
            }
            if(idxOfMsgs == -1) offsetCorrection = 0;
        }
        if(-1 != el.indexOf("Reset DEV")){
            maskUpdated |=  Command::Reset;
            auto idxOfMsgs = list.indexOf("MSG SUCSESS", list.indexOf(el));
            resetSuccessful = idxOfMsgs != -1;
        }
        if(-1 != el.indexOf("ADC temp")){
            maskUpdated |= Command::Temperature;
            sscanf_s(el.toStdString().c_str(), "ADC temp = %fC", &ADCtemperature);
        }
        if(-1 != el.indexOf("bit file compilation")){
            maskUpdated |= Command::CompileTime;
            quint32 Y, M, D, h, m, s;
            sscanf_s(el.toStdString().c_str(), "bit file compilation date/time: %u-%u-%u / %u:%u:%u", &Y, &M, &D, &h, &m, &s);
            compilationDateTime.setTime(QTime(h,m,s));
            compilationDateTime.setDate(QDate(Y + 2000, M, D));
        }
        if(-1 != el.indexOf("Send STATUS")) maskUpdated |= Command::Status;
        if(-1 != el.indexOf("Set ADC range")){
            maskUpdated |= Command::ADCrange;
            for(auto i = 0; i < 10; ++i) rngs.values[i] = static_cast<ADCrange>(list[idxOfThisEl + i + 1].right(1).toUInt());
        }
        if(-1 != el.indexOf("Set scan rate")){
            maskUpdated |= Command::Scanrate;
            int brackLeft = list[idxOfThisEl + 1].indexOf("(") + 1;
            scanRate = list[idxOfThisEl + 1].mid(brackLeft, list[idxOfThisEl + 1].indexOf(")") - brackLeft).toUInt();
        }
        if(-1 != el.indexOf("Reset MODE reg")) maskUpdated |= Command::ScanMode;
        if(-1 != el.indexOf("Start data reading")) maskUpdated |= Command::StartStream;
        if(-1 != el.indexOf("left in FIFO after ADC data read")){
            maskUpdated |= Command::RemainWords;
            sscanf_s(el.toStdString().c_str(), "%u data words left in FIFO after data read", &FIFOpayload);
        }
        if(-1 != el.indexOf("Messages during readout:")){
            maskUpdated |= Command::EndMessage;
            if(list[idxOfThisEl + 1] == "no messages" || list[idxOfThisEl + 1] == ""){ MSGpayload = 0; continue;}
            sscanf_s(list[idxOfThisEl + 1].toStdString().c_str(), "MSG FIFO payload count: %u (32-bit words)", &MSGpayload);
        }
        if(-1 != el.indexOf("Loop for reading")){
            maskUpdated |= Command::ReadStream;
            sscanf_s(el.toStdString().c_str(), "Loop for reading %u frames (%u IPBus reads) begin", &tryReadNFrames, &ipbusReads);
            if(-1 != list[idxOfThisEl + 1].indexOf("ERROR")){
                if(list[idxOfThisEl + 1] == "ERROR: Break read Reg 0x101 loop after 100 times read zero") readerrCode = 1;
                else readerrCode = 2;
            }else if(-1 != list[idxOfThisEl + 1].indexOf("DATA FIFO read")){
                int tmp;
                sscanf_s(list[idxOfThisEl + 1].toStdString().c_str(), "DATA FIFO read %u times, all %u frames collected", &tmp, &framesCollected);
                readerrCode = 0;
            }
        }
    }
}
