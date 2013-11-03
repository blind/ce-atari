#include <stdio.h>
#include <string.h>
#include "sleeper.h"

#include "acsidatatrans.h"
#include "native/scsi_defs.h"

extern "C" void outDebugString(const char *format, ...);

#define BUFFER_SIZE         (1024*1024)
#define COMMAND_SIZE        8

AcsiDataTrans::AcsiDataTrans()
{
    buffer          = new BYTE[BUFFER_SIZE];       // 1 MB buffer
    recvBuffer      = new BYTE[BUFFER_SIZE];
    count           = 0;
    status          = SCSI_ST_OK;
    statusWasSet    = false;
    com             = NULL;
    dataDirection   = DATA_DIRECTION_READ;
}

AcsiDataTrans::~AcsiDataTrans()
{
    delete []buffer;
    delete []recvBuffer;
}

void AcsiDataTrans::setCommunicationObject(CConUsb *comIn)
{
    com = comIn;
}

void AcsiDataTrans::clear(void)
{
    count           = 0;
    status          = SCSI_ST_OK;
    statusWasSet    = false;
    dataDirection   = DATA_DIRECTION_READ;
}

void AcsiDataTrans::setStatus(BYTE stat)
{
    status          = stat;
    statusWasSet    = true;
}

void AcsiDataTrans::addData(BYTE val)
{
    buffer[count] = val;
    count++;
}

void AcsiDataTrans::addData(BYTE *data, DWORD cnt)
{
    memcpy(&buffer[count], data, cnt);
    count += cnt;
}

// get data from Hans
bool AcsiDataTrans::recvData(BYTE *data, DWORD cnt)
{
    if(!com) {
        outDebugString("AcsiDataTrans::recvData -- no communication object, fail!");
        return false;
    }

    dataDirection = DATA_DIRECTION_WRITE;                   // let the higher function know that we've done data write

    // first send the command and tell Hans that we need WRITE data
    BYTE devCommand[COMMAND_SIZE];
    memset(devCommand, 0, COMMAND_SIZE);

    devCommand[3] = CMD_DATA_WRITE;                         // store command - WRITE
    devCommand[4] = cnt >> 8;                               // store data size
    devCommand[5] = cnt  & 0xff;
    devCommand[6] = 0xff;                                   // store INVALID status, because the real status will be sent on CMD_SEND_STATUS

    com->txRx(COMMAND_SIZE, devCommand, recvBuffer);        // transmit this command

    memset(txBuffer, 0, 520);                               // nothing to transmit, really...

    while(cnt > 0) {
        // request maximum 512 bytes from host
        DWORD subCount = (cnt > 512) ? 512 : cnt;
        cnt -= subCount;

        bool res = waitForATN(ATN_WRITE_MORE_DATA, 1000);   // wait for ATN_WRITE_MORE_DATA

        if(!res) {                                          // this didn't come? fuck!
            return false;
        }

        com->txRx(subCount + 8 - 4, txBuffer, rxBuffer);    // transmit data (size = subCount) + header and footer (size = 8) - already received 4 bytes
        memcpy(data, rxBuffer + 2, subCount);               // copy just the data, skip sequence number

        data += subCount;                                   // move in the buffer further
    }

    int index = 0;
    for(int i=0; i<16; i++) {
        char bfr[1024];
        char *b = &bfr[0];

        for(int j=0; j<32; j++) {
            sprintf(b, "%02x ", data[index++]);
            b += 3;
        }

        outDebugString("%s", bfr);
    }

    return true;
}

// send all data to Hans, including status
void AcsiDataTrans::sendDataAndStatus(void)
{
    if(!com) {
        outDebugString("AcsiDataTrans::sendDataAndStatus -- no communication object, fail!");
        return;
    }

    // for DATA write transmit just the status in a different way (on separate ATN)
    if(dataDirection == DATA_DIRECTION_WRITE) {
        sendStatusAfterWrite();
        return;
    }

    if(count == 0 && !statusWasSet) {       // if no data was added and no status was set, nothing to send then
        return;
    }

    // first send the command
    BYTE devCommand[COMMAND_SIZE];
    memset(devCommand, 0, COMMAND_SIZE);

    devCommand[3] = CMD_DATA_READ;                          // store command
    devCommand[4] = count >> 8;                             // store data size
    devCommand[5] = count  & 0xff;
    devCommand[6] = status;                                 // store status

    com->txRx(COMMAND_SIZE, devCommand, recvBuffer);        // transmit this command

    // then send the data
    BYTE *dataNow = buffer;

    txBuffer[0] = 0;
    txBuffer[1] = CMD_DATA_MARKER;                          // mark the start of data

    while(count > 0) {                                      // while there's something to send
        bool res = waitForATN(ATN_READ_MORE_DATA, 1000);    // wait for ATN_READ_MORE_DATA

        if(!res) {                                          // this didn't come? fuck!
            return;
        }

        DWORD cntNow = (count > 512) ? 512 : count;         // max 512 bytes per transfer
        count -= cntNow;

        memcpy(txBuffer + 2, dataNow, cntNow);              // copy the data after the header (2 bytes)
        com->txRx(cntNow + 2, txBuffer, rxBuffer);          // transmit this buffer with header

        dataNow += cntNow;                                  // move the data pointer further
    }
}

void AcsiDataTrans::sendStatusAfterWrite(void)
{
    bool res = waitForATN(ATN_GET_STATUS, 1000);

    if(!res) {
        return;
    }

    memset(txBuffer, 0, 10);                                // clear the tx buffer
    txBuffer[1] = CMD_SEND_STATUS;                          // set the command and the status
    txBuffer[2] = status;

    com->txRx(10 - 4, txBuffer, rxBuffer);                  // trasmit the status (10 bytes total, but 4 already received)
}

bool AcsiDataTrans::waitForATN(BYTE atnCode, DWORD maxLoopCount)
{
    DWORD lastTick = GetTickCount();
    BYTE inBuff[2];

    // wait for specific atn code, but maximum maxLoopCount times
    while(maxLoopCount--) {
        if(GetTickCount() - lastTick < 5) {             // less than 5 ms ago?
            Sleeper::msleep(1);
            continue;
        }
        lastTick = GetTickCount();

        com->getAtnWord(inBuff);                        // try to get ATN word

        if(inBuff[1] == atnCode) {                      // ATN code found?
            outDebugString("waitForATN %02x good.", atnCode);
            return true;
        }
    }

    outDebugString("waitForATN %02x fail!", atnCode);
    return false;
}
