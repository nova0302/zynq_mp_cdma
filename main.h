/*
 * main.h
 *
 *  Created on: 2024. 10. 31.
 *      Author: User
 */

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

#define TPU_TEST

// for download weight, ps -> pl
#define SRAM_BASE_DL      	(0xA0000000U)
#define OFFSET_DL         	(0x00020000U)

#define NUM_BYTES_WEIGHT 	(103040U)
#define NUM_WEIGHT_RAMS     (16U)

typedef struct _StSram{
	uintptr_t pBase;
	u32 numBytes;
	char FileName[32];
} StSram;

// for upload, pl->ps
#define SRAM_BASE_UL        (0xA1000000U)
#define OFFSET_UL      		(0x00020000U)

#define SRAM_ADDR_UL_EVEN   (SRAM_BASE_UL)
#define SRAM_ADDR_UL_ODD    (SRAM_BASE_UL + OFFSET_UL)

#define NUM_BYTES_UL        (51200U)
#define NUM_WORDS_UL     	(NUM_BYTES_UL/4)

typedef enum {
	PL2PS_EVENT_EVEN, PL2PS_EVENT_ODD, PL2PS_EVENT_MAX
} PL2PS_EVENT_t;

void systolic_1_test(void);

int GIC_Init (XScuGic *hGICInstPtr);
int Init_CDMA (XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u16 DeviceId, u32 IntrId);
int Enable_IntrruptSystem(XScuGic *hGICInstPtr, u16 IntrruptId,	Xil_ExceptionHandler InterruptHandler);
int Enable_CMDA_Intrrupt (XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u32 IntrId);

void initStSram(StSram *pStSram, u32 numElements);
int writeFmapToSram(StSram *pStSram);
int setupSram();
int writeToFile(u8 *pBuffer, u32 uiNumBytes, u8 fileNameIndex);

#endif /* SRC_MAIN_H_ */
