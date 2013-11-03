#include <QDir>
#include <QDebug>

#include "global.h"
#include "ccorethread.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define LOGFILE     "H:/acsilog.txt"
#define MEDIAFILE   "H:/datamedia.img"

QStringList dbg;

extern "C" void outDebugString(const char *format, ...);

CCoreThread::CCoreThread()
{
    shouldRun       = true;
    running         = false;

    sendSingleHalfWord = false;

    conUsb      = NULL;
    createConnectionObject();
    conUsb->tryToConnect();

    dataTrans   = new AcsiDataTrans();
    dataTrans->setCommunicationObject(conUsb);

    dataMedia   = new DataMedia();
    dataMedia->open((char *) MEDIAFILE, true);

    scsi        = new Scsi();
    scsi->setAcsiDataTrans(dataTrans);
//    scsi->setDataMedia(dataMedia);
    scsi->setDataMedia(&testMedia);
    scsi->setDeviceType(SCSI_TYPE_FULL);
}

CCoreThread::~CCoreThread()
{
    if(isRunning()) {
        stopRunning();
    }

    CCoreThread::displayDbg();

    delete conUsb;
    delete dataTrans;
    delete dataMedia;
    delete scsi;
}

void CCoreThread::displayDbg(void)
{
    for(int i=0; i<dbg.count(); i++) {
        qDebug() << dbg.at(i);
    }
}

void CCoreThread::run(void)
{
    BYTE inBuff[2];

    DWORD lastTick = GetTickCount();
    running = true;

    outDebugString("Core thread starting...");

    if(!conUsb->isConnected()) {
        outDebugString("USB not connected, quit.");
        return;
    }

    while(shouldRun) {
        if(GetTickCount() - lastTick < 10) {            // less than 10 ms ago?
            msleep(1);
            continue;
        }
        lastTick = GetTickCount();

        if(sendSingleHalfWord) {
            BYTE halfWord = 0, inHalf;
            conUsb->txRx(1, &halfWord, &inHalf, false);
            sendSingleHalfWord = false;
        }

        conUsb->getAtnWord(inBuff);

        if(inBuff[0] != 0 || inBuff[1] != 0) {
            logToFile((char *) "Waiting for ATN: \nIN:\n");
            logToFile(2, inBuff);
            logToFile((char *) "\n");
        }

        switch(inBuff[1]) {
        case 0:                 // this is valid, just empty data, skip this
            break;

        case ATN_FW_VERSION:
            handleFwVersion();
            break;

        case ATN_ACSI_COMMAND:
            handleAcsiCommand();
            break;

        default:
            logToFile((char *) "That ^^^ shouldn't happen!\n");
            break;
        }
    }

    running = false;
}

void CCoreThread::handleAcsiCommand(void)
{
    #define CMD_SIZE    14

    BYTE bufOut[CMD_SIZE], bufIn[CMD_SIZE];
    memset(bufOut, 0, CMD_SIZE);

    conUsb->txRx(14, bufOut, bufIn);        // get 14 cmd bytes
    outDebugString("\nhandleAcsiCommand: %02x %02x %02x %02x %02x %02x", bufIn[0], bufIn[1], bufIn[2], bufIn[3], bufIn[4], bufIn[5]);

    scsi->processCommand(bufIn);            // process the command
}

void CCoreThread::handleFwVersion(void)
{
    BYTE fwVer[8], oBuf[8];

    memset(oBuf, 0, 8);
    conUsb->txRx(8, oBuf, fwVer);

    logToFile((char *) "handleFwVersion: \nOUT:\n");
    logToFile(8, oBuf);
    logToFile((char *) "\nIN:\n");
    logToFile(8, fwVer);
    logToFile((char *) "\n");

    int year = bcdToInt(fwVer[1]) + 2000;
    if(fwVer[0] == 0xf0) {
        outDebugString("FW: Franz, %d-%02d-%02d", year, bcdToInt(fwVer[2]), bcdToInt(fwVer[3]));
    } else {
        outDebugString("FW: Hans,  %d-%02d-%02d", year, bcdToInt(fwVer[2]), bcdToInt(fwVer[3]));
    }
}

int CCoreThread::bcdToInt(int bcd)
{
    int a,b;

    a = bcd >> 4;       // upper nibble
    b = bcd &  0x0f;    // lower nibble

    return ((a * 10) + b);
}

void CCoreThread::sendHalfWord(void)
{
    sendSingleHalfWord = true;
}

void CCoreThread::stopRunning(void)
{
    shouldRun = false;
}

bool CCoreThread::isRunning(void)
{
    return running;
}

void CCoreThread::createConnectionObject(void)
{
    if(!conUsb) {                       // create object
        conUsb = new CConUsb();
    }

    if(!conUsb->init()) {               // load dll, if failed, delete object
        outDebugString("Failed to open USB connection - .dll not loaded.");
    } else {
        outDebugString("USB connection ready to connect to device.");
    }
}

void CCoreThread::usbConnectionCheck(void)
{
    if(!conUsb) {                           // no usb connection object?
        return;
    }

    if(!conUsb->isConnected()) {            // if not connected, try to connect
        conUsb->tryToConnect();
    } else {                                // if connected, check if still connected
        if(!conUsb->connectionWorking()) {  // connection not working?
            createConnectionObject();
        }
    }
}

void CCoreThread::appendToDbg(QString line)
{
    dbg.append(line);
}

void outDebugString(const char *format, ...)
{
    va_list args;
    va_start(args, format);

#define KJUT
#ifdef KJUT
    char tmp[1024];
    vsprintf(tmp, format, args);
    qDebug() << tmp;
//    CCoreThread::appendToDbg(QString(tmp));
#else
    vprintf(format, args);
#endif

    va_end(args);
}

void CCoreThread::logToFile(char *str)
{
    FILE *f = fopen(LOGFILE, "at");

    if(!f) {
        qDebug() << "dafuq!";
        return;
    }

    fprintf(f, str);
    fclose(f);
}

void CCoreThread::logToFile(WORD wval)
{
    FILE *f = fopen(LOGFILE, "at");

    if(!f) {
        qDebug() << "dafuq!";
        return;
    }

//    fprintf(f, "%04x\n", wval);
    fprintf(f, "%d\n", wval);
    fclose(f);
}

void CCoreThread::logToFile(int len, BYTE *bfr)
{
    FILE *f = fopen(LOGFILE, "at");

    if(!f) {
        qDebug() << "dafuq!";
        return;
    }

    fprintf(f, "buffer -- %d bytes\n", len);

    while(len > 0) {
        for(int col=0; col<16; col++) {
            if(len == 0) {
                break;
            }

            fprintf(f, "%02x ", *bfr);

            bfr++;
            len--;
        }
        fprintf(f, "\n");
    }

    fclose(f);
}