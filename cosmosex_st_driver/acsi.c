/*--------------------------------------------------*/
/* #include <tos.h> */
#include <mint/sysbind.h>

#include "acsi.h"

#define MFP_ADDR 	0xFFFA00L      /* MFP device addres */
#define MFP     	((struct mfp_chip *) MFP_ADDR)

#define DMA_ADDR 0xFF8600L      /* DMA device addres */
#define DMA      ((struct dma_chip *) DMA_ADDR)

/* -------------------------------------- */
BYTE acsi_cmd(BYTE ReadNotWrite, BYTE *cmd, BYTE cmdLength, BYTE *buffer, WORD sectorCount)
{
	DWORD status;
	WORD i, wr1, wr2;
	void *OldSP;

	OldSP = (void *) Super((void *)0);  	/* supervisor mode */ 

	FLOCK = -1;                            	/* disable FDC operations */
	setdma((DWORD) buffer);                 /* setup DMA transfer address */

	/*********************************/
	/* transfer 0th cmd byte */
	DMA->MODE = NO_DMA | HDC;              	/* write 1st byte (0) with A1 low */
	DMA->DATA = cmd[0];
	DMA->MODE = NO_DMA | HDC | A0;         	/* A1 high again */

	if (qdone() != OK) {					/* wait for ack */
		hdone();                          	/* restore DMA device to normal */
		
		Super((void *)OldSP);  			    /* user mode */
		return ERROR;
	}

	/*********************************/
	/* transfer middle cmd bytes */
	for(i=1; i<(cmdLength-1); i++) {
		DMA->DATA = cmd[i];
		DMA->MODE = NO_DMA | HDC | A0;
	
		if (qdone() != OK) {				/* wait for ack */
			hdone();                        /* restore DMA device to normal */
			
			Super((void *)OldSP);  			    /* user mode */
			return ERROR;
		}
	}

	/* wr1 and wr2 are defined so we could toggle R/W bit and then setup Read / Write operation */ 
	if(ReadNotWrite==1) {						
		wr1 = DMA_WR;
		wr2 = 0;
	} else {
		wr1 = 0;
		wr2 = DMA_WR;
	}
  
    DMA->MODE = wr1 | NO_DMA | SC_REG;  		/* clear FIFO = toggle R/W bit */
    DMA->MODE = wr2 | NO_DMA | SC_REG;          /* and select sector count reg */ 

    DMA->SECT_CNT = sectorCount;				/* write sector cnt to DMA device */
    DMA->MODE = wr2 | NO_DMA | HDC | A0;        /* select DMA data register again */

    DMA->DATA = cmd[cmdLength - 1];      		/* transfer the last command byte */             
    DMA->MODE = wr2;                         	/* start DMA transfer */

    status = endcmd(wr2 | NO_DMA | HDC | A0);   /* wait for DMA completion */

	hdone();                                	/* restore DMA device to normal */
	
	Super((void *)OldSP);  			    		/* user mode */
	return status;
}
/****************************************************************************/
long endcmd(short mode)
{
 if (fdone() != OK)                   /* wait for operation done ack */
    return(ERRORL);

 DMA->MODE = mode;                    /* write mode word to mode register */
 return((long)(DMA->DATA & 0x00FF));  /* return completion byte */
}
/****************************************************************************/
long hdone(void)
{
 DMA->MODE = NO_DMA;        /* restore DMA mode register */
 FLOCK = 0;                 /* FDC operations may get going again */
 return((long)DMA->STATUS); /* read and return DMA status register */
}
/****************************************************************************/
void setdma(DWORD addr)
{
 DMA->ADDR[LOW]  = (BYTE)(addr);
 DMA->ADDR[MID]  = (BYTE)(addr >> 8);
 DMA->ADDR[HIGH] = (BYTE)(addr >> 16);
}
/****************************************************************************/
long qdone(void)
{
	return(wait_dma_cmpl(STIMEOUT));
}
/****************************************************************************/
long fdone(void)
{
	return(wait_dma_cmpl(LTIMEOUT));
}
/****************************************************************************/
long wait_dma_cmpl(unsigned long t_ticks)
{
 unsigned long to_count;

 to_count = t_ticks + HZ_200;   /* calc value timer must get to */

 do
 {
    if ( (MFP->GPIP & IO_DINT) == 0) /* Poll DMA IRQ interrupt */
         return(OK);                 /* got interrupt, then OK */

 }  while (HZ_200 <= to_count);      /* check timer */

 return(ERROR);                      /* no interrupt, and timer expired, */
}
/****************************************************************************/

