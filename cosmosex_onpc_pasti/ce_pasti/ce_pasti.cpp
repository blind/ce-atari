// ce_pasti.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "ce_pasti.h"
#include "pasti.h"
#include "socks.h"

#include <stdio.h>
#include <stdlib.h>

void log(char *str);
void log2(char *str);

//#define PASTI_PASSTHROUGH
#define PASTI_LOGCALLBACKS
#define PASTI_LOGONESHOT

#define dmaAddrSectCnt	0xFF8604
#define dmaAddrData		0xFF8604

#define dmaAddrMode		0xFF8606
#define dmaAddrStatus	0xFF8606

#define dmaAddrHi		0xFF8609
#define dmaAddrMid		0xFF860B
#define dmaAddrLo		0xFF860D

//---------------------------------------

// Mode Register bits 
#define NOT_USED     0x0001     // not used bit 
#define A0           0x0002     // A0 line, A1 on DMA port 
#define A1           0x0004     // A1 line, not used on DMA port 
#define HDC          0x0008     // HDC / FDC register select 
#define SC_REG       0x0010     // Sector count register select 
#define RESERVED5    0x0020     // reserved for future expansion ? 
#define RESERVED6    0x0040     // bit has no function 
#define NO_DMA       0x0080     // disable / enable DMA transfer 
#define DMA_WR       0x0100     // Write to / Read from DMA port 

// Status Register bits 
#define DMA_OK       0x0001     // DMA transfer went OK 
#define SC_NOT_0     0x0002     // Sector count register not zero 
#define DATA_REQ     0x0004     // DRQ line state 

HMODULE hPasti;

struct pastiINITINFO	initInfo;
struct pastiFUNCS		ceFuncs;
struct pastiFUNCS		origFuncs;

BYTE lo,mid,hi;
BYTE selectedRegister;
BYTE sectorCount;
int index;
int xferBufOffset;
BYTE cmd[24];

void handleCmd(BYTE *cmd, DWORD addr, bool readNotWrite, int sectorCount, struct pastiIOINFO *pio);
void dumpPio(int mode, struct pastiIOINFO *pio);
WORD dataChecksum(BYTE *data, int byteCount);

BOOL haveReadXfer;
pastiDMAXFERINFO xferInfoRead;
pastiDMAXFERINFO xferInfoWrite;

CE_PASTI_API BOOL pastiInit( struct pastiINITINFO *pii)
{
    initInfo = *pii;								// store struct with init info 

	xferInfoRead.xferBuf	= (BYTE *) malloc(256 * 1024);
	xferInfoWrite.xferBuf	= (BYTE *) malloc(256 * 1024);

#ifdef PASTI_LOGONESHOT
	remove("c:\\emus\\steem\\ce_log.txt");
#endif

	log("\n---------------------------------\n");
	log("pastiInit\n");

	//-----------------------
	if (hPasti==NULL) {
		hPasti=LoadLibraryA("pasti_original.dll");											//	load dll

		if(hPasti == NULL) {
			log("Failed to load pasti_original.dll\n");
			return false;
		}
	}

	LPPASTIINITPROC pastiInit = (LPPASTIINITPROC) GetProcAddress(hPasti,"pastiInit");		// find init function

	if (pastiInit){
		pastiInit(pii);																		// call original init with original param

		origFuncs = *pii->funcs;															// store original funcs pointer
	} else {
		log("Failed to find pastiInit function\n");
		return false;
	}
  //-----------------------
													// fill struct with funcs pointers
    ceFuncs.Io                  = pastiIo;
	ceFuncs.WritePorta          = pastiWritePorta;
	ceFuncs.Config              = pastiConfig;
	ceFuncs.GetConfig           = pastiGetConfig;
	ceFuncs.HwReset             = pastiHwReset;
	ceFuncs.GetLastError        = pastiGetLastError;
	ceFuncs.ImgLoad             = pastiImgLoad;
	ceFuncs.SaveImg             = pastiSaveImg;
	ceFuncs.Eject               = pastiEject;
	ceFuncs.GetBootSector       = pastiGetBootSector;
	ceFuncs.GetFileExtensions   = pastiGetFileExtensions;
	ceFuncs.SaveState           = pastiSaveState;
	ceFuncs.LoadState           = pastiLoadState;
	ceFuncs.LoadConfig          = pastiLoadConfig;
	ceFuncs.SaveConfig          = pastiSaveConfig;
	ceFuncs.Peek                = pastiPeek;
	ceFuncs.Breakpoint          = pastiBreakpoint;
	ceFuncs.DlgConfig           = pastiDlgConfig;
	ceFuncs.DlgBreakpoint       = pastiDlgBreakpoint;
	ceFuncs.DlgStatus           = pastiDlgStatus;
	ceFuncs.DlgFileProps        = pastiDlgFileProps;
	ceFuncs.Extra               = pastiExtra;
    
	pii->funcs = &ceFuncs;							// set the pointer to our (CE) functions, which will call the original fuctions

	log("pastiInit returned wrapper functions\n");

    return true;
}

CE_PASTI_API BOOL pastiConfig( struct pastiCONFIGINFO *pci)
{
	log("pastiConfig\n");
    
	if(origFuncs.Config != NULL) {
		return origFuncs.Config(pci);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiGetConfig( struct pastiCONFIGINFO *pci)
{
	log("pastiGetConfig\n");

	if(origFuncs.GetConfig != NULL) {
		return origFuncs.GetConfig(pci);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiHwReset( BOOL bPowerUp)
{
	log("pastiHwReset\n");

	if(origFuncs.HwReset != NULL) {
		return origFuncs.HwReset(bPowerUp);
	} else {
		return false;
	}
}

CE_PASTI_API int pastiGetLastError(void)
{
	log("pastiGetLastError\n");

	if(origFuncs.GetLastError != NULL) {
		return origFuncs.GetLastError();
	} else {
		return 0;
	}
}

BYTE statusByte;

CE_PASTI_API BOOL pastiIo( int mode, struct pastiIOINFO *pio)
{
#ifdef PASTI_LOGCALLBACKS
	log2("---\n");
#else
	if (mode != 0){
		log2("---\n");
	}
#endif
#ifdef _DEBUG
	dumpPio(mode, pio);
#endif
#ifdef PASTI_PASSTHROUGH
	BOOL res = origFuncs.Io(mode, pio);
	dumpPio(mode, pio);
	return res;
#endif


	static DWORD cntr = 0;
	static BYTE intrqState = 0;

	static bool dmaReadNotWrite;
	static bool gotWriteData = false;
	
	static bool readStatusByte = false;

	static bool shouldHandleCmd			= false;
	static bool shouldRequestWriteData	= false;
	
	bool triggerirq = false;

	char tmp[1024];

	char *pModeStr;
	char mode_str[4][16] = {"PASTI_IOUPD", "PASTI_IOREAD", "PASTI_IOWRITE", "???"};

	if(mode >= 0 && mode <= 2) {
		pModeStr = mode_str[mode];
	} else {
		pModeStr = mode_str[3];
	}

//	dumpPio(mode,pio);

	//////////////////////////
	// log calls, expect PASTI_IOUPD
	if(mode != PASTI_IOUPD) {
		if(mode != PASTI_IOWRITE) {		// on PASTI_IOREAD
			if(selectedRegister == HDC && pio->addr == dmaAddrData) {
				readStatusByte = true;
			}
		} else {						// on PASTI_IOWRITE
			if (pio->addr == dmaAddrHi){
				index = 0;
			}
			if(pio->addr == dmaAddrMode) {												// handle WRITE to DMA MODE register
				BYTE val = pio->data & (NO_DMA | HDC | A0 | SC_REG);
				
				if(val == (NO_DMA | HDC)) {												// select HDC register - before 1st PIO byte (A1 low)
					selectedRegister = HDC;

					gotWriteData	= false;
					shouldHandleCmd	= false;
				} 

				if(val == (NO_DMA | HDC | A0)) {										// select HDC register, after 1st PIO byte (A1 high again)
					selectedRegister = HDC;
					//triggerirq = true;
				}

				if( (val&(NO_DMA | SC_REG)) == (NO_DMA | SC_REG) ) {											// select sector count register
					selectedRegister = SC_REG;
				}

				if((pio->data & NO_DMA) == 0 ) {										// when NO_DMA flag not set, the DMA transfer was started
					shouldHandleCmd = true;

					if((pio->data & DMA_WR) == DMA_WR) {								// DMA WRITE started
						dmaReadNotWrite = false;
						gotWriteData	= false;
					} else {															// DMA READ started
						dmaReadNotWrite = true;
					}
				}
			}

			if(pio->addr == dmaAddrData && selectedRegister == HDC) {					// write to HDC register - PIO write byte
				if(index >= 0 && index <= 24) {											// if not out of range, store this as CMD byte
					cmd[index] = pio->data;
					index++;
				}
				//triggerirq = true;

			}

			if(pio->addr == dmaAddrSectCnt && selectedRegister == SC_REG) {				// write to SC_REG - setting sector count for DMA chip before transfer
				sectorCount = pio->data;
				xferBufOffset = 0;
				sprintf(tmp, "Sector set: %d\n", sectorCount );
				log2(tmp);
			}

			if(pio->addr == dmaAddrHi) {												// setting DMA hi address
				hi = pio->data;
//>log("DMA high\n");
			}

			if(pio->addr == dmaAddrMid) {												// setting DMA mid address
				mid = pio->data;
//>log("DMA mid\n");
			}

			if(pio->addr == dmaAddrLo) {												// setting DMA lo address
				lo = pio->data;
//>log("DMA low\n");
			}
		}
	
	}

	//////////////////////////

	if(origFuncs.Io != NULL) {
		BOOL res = origFuncs.Io(mode, pio);
		pio->haveXfer = 0;																	// neutralize data transfer from original pasti.dll

		if(readStatusByte) {																// if should read status byte
			pio->data		= statusByte;
			readStatusByte	= false;
		}

		if(shouldRequestWriteData) { 														// Did request data update in previous step? Got the data, don't need to request again
			gotWriteData			= true;
			shouldRequestWriteData	= false;
		}

		bool readyForRead		= shouldHandleCmd && dmaReadNotWrite;						// ready for read, if got command and it's a read
		bool readyForWrite		= shouldHandleCmd && !dmaReadNotWrite && gotWriteData;		// ready for write, if got command, it's a write and got the data
		shouldRequestWriteData	= shouldHandleCmd && !dmaReadNotWrite && !gotWriteData;		// ready for write, if got command, but don't have the data
		DWORD dmaAddr			= (hi << 16) | (mid << 8) | lo;
		
		//pio->intrqState = intrqState;
		if ((mode == PASTI_IOWRITE || mode == PASTI_IOREAD) ) {	// read or write to ACSI bus resets INT back to inactive state
			if (pio->addr == dmaAddrData) {
				pio->intrqState = 0;
			}
		}

		if (readyForRead || readyForWrite) {													// if read for read or write
			handleCmd(cmd, dmaAddr, dmaReadNotWrite, sectorCount, pio);
			shouldHandleCmd = false;
			if (readyForRead){
				pio->intrqState = 0;
			}
			if (readyForWrite){
				pio->intrqState = 1;
			}
//			pio->intrqState = 1;
		}
		if(shouldRequestWriteData) {														// should request write data?
			memset(xferInfoWrite.xferBuf, 0, 256*1024);

			pio->haveXfer				= true;
			pio->updateCycles			= 0x1fa * ((int)sectorCount);

			pio->xferInfo.memToDisk		= 1;
			pio->xferInfo.xferBuf		= xferInfoWrite.xferBuf;
			pio->xferInfo.xferLen		= ((int) sectorCount) * 512;
			pio->xferInfo.xferSTaddr	= dmaAddr;
			pio->intrqState = 0;
		}

		//just a test for an immediate response; response on IOUPD seems to hang in certain cases
//		if () {											// if we should send #
//			pio->intrqState = 1;
//		}
		//Send data one Sector at a time
		if (mode != PASTI_IOUPD && haveReadXfer) {
			pio->intrqState = 0;
		}
		if (mode == PASTI_IOUPD && haveReadXfer) {
			sectorCount--;
			pio->intrqState = 0;
			if (sectorCount == 0){
				haveReadXfer	= false;
				pio->intrqState = 1;
			}

			pio->haveXfer				= true;
			pio->updateCycles			= 0x1fa;

			pio->xferInfo.memToDisk		= 0;
			pio->xferInfo.xferBuf		= xferInfoRead.xferBuf + (xferBufOffset);
			pio->xferInfo.xferLen		= 512;
			pio->xferInfo.xferSTaddr	= dmaAddr;

			xferBufOffset				+= 512;
			dmaAddr						+= 512;

			hi = (dmaAddr >> 16) & 0xff;
			mid = (dmaAddr >> 8) & 0xff;
			lo = (dmaAddr)&0xff;
			//intrqState = 1;
		}

		if (triggerirq){
			pio->intrqState = 1;
		}

#ifdef _DEBUG
		dumpPio(mode,pio);
#endif
		cntr++;
		return res;
	} else {
		return false;
	}
}

void dumpPio(int mode, struct pastiIOINFO *pio)
{
#ifndef PASTI_LOGCALLBACKS
	if (mode == 0){
		return;
	}
#endif

	char tmp[1024];

	if(pio->haveXfer) {
		sprintf(tmp, "dumpPio -- mode: %d, addr: %08x, data: %06x, cycles: %08x, updateCycles: %08x, intrqState: %02x, haveXfer: %d, memToDisk: %d, xferLen: %d, xferBuf: %08x, xferSTaddr: %08x\n", 
								 mode, pio->addr,  pio->data,	 pio->cycles,  pio->updateCycles,  pio->intrqState,  pio->haveXfer, pio->xferInfo.memToDisk, pio->xferInfo.xferLen, (DWORD) pio->xferInfo.xferBuf, pio->xferInfo.xferSTaddr);
		log2(tmp);

		//sprintf(tmp, "data: %02x %02x %02x %02x\n", ((BYTE *) pio->xferInfo.xferBuf)[0], ((BYTE *) pio->xferInfo.xferBuf)[1], ((BYTE *) pio->xferInfo.xferBuf)[2], ((BYTE *) pio->xferInfo.xferBuf)[3]);
		//log(tmp);
	} else {
		sprintf(tmp, "dumpPio -- mode: %d, addr: %08x, data: %06x, cycles: %08x, updateCycles: %08x, intrqState: %02x, haveXfer: %d\n", 
								 mode, pio->addr,  pio->data,	 pio->cycles,  pio->updateCycles,  pio->intrqState,  pio->haveXfer);
		log2(tmp);
	}
}

void handleCmd(BYTE *cmd, DWORD addr, bool readNotWrite, int sectorCount, struct pastiIOINFO *pio)
{
	char tmp[256];

	//------------
	// send the common header 
	BYTE outBuf[16];
	memcpy(outBuf, cmd, 14);				// bytes 0..13	- cmd
	outBuf[14] = (BYTE) readNotWrite;		// byte 14		- read not write flag
	outBuf[15] = (BYTE) sectorCount;		// byte 15		- sector count
	clientSocket_write(outBuf, 16);			// send the common header for read and write part

	sprintf(tmp, "\nhandleCmd - write 16 bytes header, haveReadXfer: %d\n", haveReadXfer);
	log2(tmp);
	//------------
	// now send or receive the data
	int byteCount = sectorCount * 512;

	if(readNotWrite) {						// on READ
		sprintf(tmp, "handleCmd - READ: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x - byteCount: %d\n", 
			cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11], byteCount);
		log2(tmp);

		int skip = 0;
		while(1) {
			BYTE a = 0;
			if (!clientSocket_read(&a, 1)){
				statusByte = 0x02;
				return;
			}

			if(a == 0xff) {
				break;
			}
			skip++;
		}

		if(skip != 0) {
			sprintf(tmp, "handleCmd - had to skip %d bytes\n", skip);
			log(tmp);
		} 

		if (!clientSocket_read(xferInfoRead.xferBuf, byteCount)){
			statusByte = 0x02;
			return;
		} 
		BYTE *p = xferInfoRead.xferBuf;

		sprintf(tmp, "xferBuf - %02x %02x %02x %02x %02x %02x %02x %02x ...\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		log2(tmp);

		WORD csReceived, csCalculated;
		clientSocket_read((BYTE *) &csReceived, 2);
		csCalculated = dataChecksum(xferInfoRead.xferBuf, byteCount);

		if (cmd[0] == 0x1f && cmd[1] == 0xa0 && cmd[2] == 0x43 && cmd[3] == 0x45 && cmd[4] == 0x03 && cmd[5] == 0x3f){
			log2("fread!\n");
		}

		if(csReceived != csCalculated) {
			log2("READ - checksum fail!\n");
		} else {
//			log("READ - checksum good\n");
		}

		haveReadXfer = TRUE;
	} else {								// on WRITE
		sprintf(tmp, "handleCmd - WRITE: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x - byteCount: %d\n", 
			cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11], byteCount);
		log2(tmp);

		BYTE *p = xferInfoWrite.xferBuf;
		sprintf(tmp, "data      -       %02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5]);
		log2(tmp);

		clientSocket_write(xferInfoWrite.xferBuf, byteCount);			// send the data	

		WORD csCalculated = dataChecksum(xferInfoWrite.xferBuf, byteCount);
		clientSocket_write((BYTE *) &csCalculated, 2);					

		//sprintf(tmp, "checksum  - %04x\n",csCalculated);
		//log(tmp);
	}

	//------------
	// receive status byte, set it
	BYTE inBuf;
	clientSocket_read(&inBuf, 1);

	sprintf(tmp, "handleCmd - status byte: %02x\n", (int) inBuf);
	log2(tmp);

	statusByte = inBuf;
}

WORD dataChecksum(BYTE *data, int byteCount)
{
    int i;
    WORD sum = 0;
    WORD *wp = (WORD *) data;
    WORD wordCount = byteCount/2;

    for(i=0; i<wordCount; i++) {                // calculate checksum
		sum += *wp;
        wp++;
    }
    
    return sum;
}

CE_PASTI_API BOOL pastiWritePorta( unsigned data, long cycles)
{
	//char tmp[128];
	//sprintf(tmp, "pastiWritePorta - data: %02x, cycles: %d\n", data, cycles);
	//log(tmp);

	if(origFuncs.WritePorta != NULL) {
		return origFuncs.WritePorta(data, cycles);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiImgLoad( int drive, BOOL bWprot, BOOL bDelay, long cycles, struct pastiDISKIMGINFO *pdii)
{
	log("pastiImgLoad\n");

	if(origFuncs.ImgLoad != NULL) {
		return origFuncs.ImgLoad(drive, bWprot, bDelay, cycles, pdii);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiSaveImg( int drive, BOOL bAlways, pastiDISKIMGINFO *pdii)
{
	log("pastiSaveImg\n");

	if(origFuncs.SaveImg != NULL) {
		return origFuncs.SaveImg(drive, bAlways, pdii);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiEject( int drive, long cycles)
{
	log("pastiEject\n");

	if(origFuncs.Eject != NULL) {
		return origFuncs.Eject(drive, cycles);
	} else {
		return false;
	}
}

CE_PASTI_API int pastiGetFileExtensions( char *newExts, int bufSize, BOOL bAll)
{
	log("pastiGetFileExtensions\n");
	
	if(origFuncs.GetFileExtensions != NULL) {
		return origFuncs.GetFileExtensions(newExts, bufSize, bAll);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiGetBootSector( int drive, struct pastiBOOTSECTINFO *pbsi)
{
	log("pastiGetBootSector\n");

	if(origFuncs.GetBootSector != NULL) {
		return origFuncs.GetBootSector(drive, pbsi);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiPeek( struct pastiPEEKINFO *ppi)
{
//>	log("pastiPeek\n");

	if(origFuncs.Peek != NULL) {
		return origFuncs.Peek(ppi);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiBreakpoint( unsigned subfunc, int n, struct pastiBREAKINFO *pbi)
{
	log("pastiBreakpoint\n");

	if(origFuncs.Breakpoint != NULL) {
		return origFuncs.Breakpoint(subfunc, n, pbi);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiSaveState( struct pastiSTATEINFO *psi)
{
	log("pastiSaveState\n");

	if(origFuncs.SaveState != NULL) {
		return origFuncs.SaveState(psi);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiLoadState( struct pastiSTATEINFO *psi)
{
	log("pastiLoadState\n");

	if(origFuncs.LoadState != NULL) {
		return origFuncs.LoadState(psi);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiLoadConfig( struct pastiLOADINI *pli, struct pastiCONFIGINFO *pci)
{
	log("pastiLoadConfig\n");

	if(origFuncs.LoadConfig != NULL) {
		return origFuncs.LoadConfig(pli, pci);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiSaveConfig( struct pastiLOADINI *pli, const struct pastiCONFIGINFO *pci)
{
	log("pastiSaveConfig\n");

	if(origFuncs.SaveConfig != NULL) {
		return origFuncs.SaveConfig(pli, pci);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiDlgConfig( HWND hWnd, unsigned flags, struct pastiCONFIGINFO *pci)
{
	log("pastiDlgConfig\n");

	if(origFuncs.DlgConfig != NULL) {
		return origFuncs.DlgConfig(hWnd, flags, pci);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiDlgBreakpoint( HWND hWnd)
{
	log("pastiDlgBreakpoint\n");

	if(origFuncs.DlgBreakpoint != NULL) {
		return origFuncs.DlgBreakpoint(hWnd);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiDlgStatus( HWND hWnd)
{
	log("pastiDlgStatus\n");

	if(origFuncs.DlgStatus != NULL) {
		return origFuncs.DlgStatus(hWnd);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiDlgFileProps( HWND hWnd, const char *fileName)
{
	log("pastiDlgFileProps\n");

	if(origFuncs.DlgFileProps != NULL) {
		return origFuncs.DlgFileProps(hWnd, fileName);
	} else {
		return false;
	}
}

CE_PASTI_API BOOL pastiExtra( unsigned code, void *ptr)
{
	log("pastiExtra\n");

	if(origFuncs.Extra != NULL) {
		return origFuncs.Extra(code, ptr);
	} else {
		return false;
	}
}

void log(char *str)
{
	return;
#ifdef _DEBUG
	FILE *f = fopen("c:\\emus\\steem\\ce_log.txt", "at");

	if(!f) {
		return;
	}

	fputs(str, f);

	fclose(f); 
#else
	return;
#endif
}

void log2(char *str)
{
#ifdef _DEBUG
	FILE *f = fopen("c:\\emus\\steem\\ce_log.txt", "at");

	if (!f) {
		return;
	}

	fputs(str, f);

	fclose(f);
#else
	return;
#endif
}
