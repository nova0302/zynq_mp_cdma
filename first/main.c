/******************************************************************************
 *
 * Copyright (C) 2010 - 2017 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
 ******************************************************************************/
/*****************************************************************************/
/**
 *
 * @file xaxicdma_example_simple_intr.c
 *
 * This file demonstrates how to use the xaxicdma driver on the Xilinx AXI
 * CDMA core (AXICDMA) to transfer packets in simple transfer mode through
 * interrupt.
 *
 * Modify the NUMBER_OF_TRANSFER constant to have different number of simple
 * transfers done in this test.
 *
 * This example assumes that the system has an interrupt controller.
 *
 * To see the debug print, you need a Uart16550 or uartlite in your system,
 * and please set "-DDEBUG" in your compiler options for the example, also
 * comment out the "#undef DEBUG" in xdebug.h. You need to rebuild your
 * software executable.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 *  . Updated the debug print on type casting to avoid warnings on u32. Cast
 *    u32 to (unsigned int) to use the %x format.
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jz   07/30/10 First release
 * 2.01a rkv  01/28/11 Changed function prototype of XAxiCdma_SimpleIntrExample
 * 		       to a function taking arguments interrupt instance,device
 * 		       instance,device id,device interrupt id
 *		       Added interrupt support for Cortex A9
 * 2.01a srt  03/05/12 Modified interrupt support for Zynq.
 * 		       Modified Flushing and Invalidation of Caches to fix CRs
 *		       648103, 648701.
 * 4.3   ms   01/22/17 Modified xil_printf statement in main function to
 *            ensure that "Successfully ran" and "Failed" strings are
 *            available in all examples. This is a fix for CR-965028.
 *       ms   04/05/17 Modified Comment lines in functions to
 *                     recognize it as documentation block for doxygen
 *                     generation of examples.
 * 4.4   rsp  02/22/18 Support data buffers above 4GB.Use UINTPTR for
 *                     typecasting buffer address(CR-995116).
 * </pre>
 *
 ****************************************************************************/
#include "xaxicdma.h"
#include "xdebug.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "platform.h"
#include "xscugic.h"

#include "xpseudo_asm_gcc.h"
#include "xreg_cortexa53.h"

#include <stdio.h>
#include <stdbool.h>
#include "sleep.h"
#include "main.h"


/***************** Macros (Inline Functions) Definitions *********************/
/************************** Variable Definitions *****************************/

static XScuGic xScuGic; /* Instance of the Interrupt Controller */

static XAxiCdma xAxiCdma; /* Instance of the XAxiCdma */

/* Source and Destination buffer for DMA transfer.
 */
volatile static u8 DesBuffer[ROW_COUNT][TX_SIZE_BYTES] __attribute__ ((aligned (64)));

/* Shared variables used to test the callbacks.
 */
volatile static u32 Done  = 0U; /* Dma transfer is done */
volatile static u32 Error = 0U; /* Dma Bus Error occurs */

volatile static PL2PS_EVENT_t pl2ps_event =PL2PS_EVENT_EVEN ;
volatile static bool bPL2PS_READ_EVENT = false;

volatile u32 isr_cnt_even = 0U;
volatile u32 isr_cnt_odd  = 0U;
volatile u32 isr_cnt_cdma = 0U;
volatile u32 fCount       = 0U;
volatile u32 cdmaErrCount = 0U;


int main() {

	int Status;
	init_platform(); // for printf

	printf("-- Build Info --\r\nFile Name: %s\r\nDate: %s\r\nTime: %s\r\n", __FILE__, __DATE__, __TIME__);
	xil_printf("\r\n--- Entering main() --- \r\n");

	const u32 uiBramBaseAddr[PL2PS_EVENT_MAX] = { BRAM_ADDR_EVEN, BRAM_ADDR_ODD };

	//	GIC 생성 및 초기화
	GIC_Init(&xScuGic);

	//	인터럽트 설정
	//	PL to PS DP_RAM Even Interrupt 등록 (GIC에 인터럽트 연결) - PL(FPGA)에서 데이터 읽어오기 위한 인터럽트
	Enable_IntrruptSystem(&xScuGic, INTERRUPT_ID_PL2PS_EVEN, isr_pl2ps_even);

	//	PL to PS DP_RAM Odd Interrupt 등록  (GIC에 인터럽트 연결) - PL(FPGA)에서 데이터 읽어오기 위한 인터럽트
	Enable_IntrruptSystem(&xScuGic, INTERRUPT_ID_PL2PS_ODD,	isr_pl2ps_odd);

	//	DMA 디바이스 초기화
	//	PL to PS DMA
	Status = Init_CDMA(&xScuGic, &xAxiCdma, DEVICE_ID_CDMA0, INTERRUPT_ID_CDMA0);
	if (Status == XST_FAILURE)
		xil_printf("Init_CDMA Failed!!\r\n");

	systolic_1_test(); // must call before step into while loop

	while (1) {

		if (bPL2PS_READ_EVENT) {
			bPL2PS_READ_EVENT = false;

			u32 SrcAddr = uiBramBaseAddr[pl2ps_event];

			Status = XAxiCdma_SimpleTransfer(
						&xAxiCdma
						, (UINTPTR) SrcAddr
						, (UINTPTR)&DesBuffer[fCount]
						, TX_SIZE_BYTES
						, isr_cdma,
					(void *) &xAxiCdma);

			if (Status == XST_FAILURE){
				cdmaErrCount++;
			}

			while(!Done){
				// Wait for CDMA to complete!!
			}
			Done = FALSE;

			if ((++fCount % 16) == 0){

				fCount = 0;

				// Invalidate the DestBuffer before receiving the data, in case the Data Cache is enabled
				Xil_DCacheInvalidateRange((UINTPTR)&DesBuffer, TX_SIZE_BYTES * ROW_COUNT);
				systolic_1_test();
			}
		}
	}

	xil_printf("Successfully ran XAxiCdma_SimpleIntr Example\r\n");
	xil_printf("--- Exiting main() --- \r\n");

	return XST_SUCCESS;
}

void isr_pl2ps_even(void *CallbackRef) {
	if(!bPL2PS_READ_EVENT){
		isr_cnt_even++;
		bPL2PS_READ_EVENT = true;
		pl2ps_event = PL2PS_EVENT_EVEN;
	}
}

void isr_pl2ps_odd(void *CallbackRef) {
	if(!bPL2PS_READ_EVENT){
		isr_cnt_odd++;
		bPL2PS_READ_EVENT = true;
		pl2ps_event = PL2PS_EVENT_ODD;
	}
}

void isr_cdma(void *CallBackRef, u32 IrqMask, int *IgnorePtr) {
//	bPL2PS_READ_EVENT = false;
	isr_cnt_cdma++;
	if (IrqMask & XAXICDMA_XR_IRQ_ERROR_MASK) {
		Error = TRUE;
	}
	if (IrqMask & XAXICDMA_XR_IRQ_IOC_MASK) {
		Done = TRUE;
	}
}

//=================================================================
//	SCU GIC (global interrupt) 초기화
//=================================================================
int GIC_Init(XScuGic *hGICInstPtr) {
	int Status;
	XScuGic_Config *IntcConfig;

	XScuGic_DeviceInitialize(0);

	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(hGICInstPtr, IntcConfig,	IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT, (Xil_ExceptionHandler) XScuGic_InterruptHandler, hGICInstPtr);

	Xil_ExceptionInit();

	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

//=================================================================
//	Interrupt 연결
//=================================================================

int Enable_IntrruptSystem(XScuGic *hGICInstPtr, u16 IntrruptId,	Xil_ExceptionHandler InterruptHandler) {
	int Status = 0;

	//	DP_RAM 인터럽트 트리거 설정
	if (IntrruptId == INTERRUPT_ID_PL2PS_EVEN || IntrruptId == INTERRUPT_ID_PL2PS_ODD) {
		XScuGic_SetPriorityTriggerType(hGICInstPtr, IntrruptId, 0x00, 0x3);
	}

	Status = XScuGic_Connect(
			hGICInstPtr
			, IntrruptId
			, (Xil_ExceptionHandler) InterruptHandler
			, (void *) hGICInstPtr);

	if (Status != XST_SUCCESS) {
		return Status;
	}

	XScuGic_Enable(hGICInstPtr, IntrruptId);

	return Status;
}

//================================================================================
//	CDMA 생성 및 인터럽트 등록
//================================================================================
int Init_CDMA(XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u16 DeviceId, u32 IntrId) {
	XAxiCdma_Config *CfgPtr;
	int Status;

	// Initialize the XAxiCdma device.
	CfgPtr = XAxiCdma_LookupConfig(DeviceId);
	if (!CfgPtr) {
		return XST_FAILURE;
	}

	Status = XAxiCdma_CfgInitialize(InstancePtr, CfgPtr, CfgPtr->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	//	CDMA를 GIC에 연결 (CDMA 인터럽트 등록)
	Enable_CMDA_Intrrupt(IntcInstancePtr, InstancePtr, IntrId);

	// Enable all (completion/error/delay) interrupts
	XAxiCdma_IntrEnable(InstancePtr, XAXICDMA_XR_IRQ_ALL_MASK);

	return XST_SUCCESS;
}

int Enable_CMDA_Intrrupt(XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr,
		u32 IntrId) {
	int Status;

	XScuGic_SetPriorityTriggerType(IntcInstancePtr, IntrId, 0xA0, 0x3);

	// Connect the device driver handler that will be called when an
	// interrupt for the device occurs, the handler defined above performs
	// the specific interrupt processing for the device.

	Status = XScuGic_Connect(IntcInstancePtr, IntrId, (Xil_InterruptHandler) XAxiCdma_IntrHandler, InstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	//Enable the interrupt for the DMA device.
	XScuGic_Enable(IntcInstancePtr, IntrId);

	return XST_SUCCESS;
}

void systolic_1_test(void) {
	unsigned int k = 0;
	unsigned int k_word = 0;
	unsigned int offset_addr = 0;
	unsigned int BRAM_0_Write_Buffer[642];
//	usleep(1000000);	// 1ms

	xil_printf("### PS to PL Download start \r\n");
	sleep(1);
//	usleep(1000);	// 1ms
#ifdef DBG_PRINT
	xil_printf("\r\n====================================================\r\n");
#endif
	for (int i = 0; i < 214; i++)				// 0~213 (642/3)
			{
		for (int b = 0; b < 3; b++)				// block_num : 0~2
				{

			if (b == 0)
				offset_addr = 0 + i * 642 * 4;
			else if (b == 1)
				offset_addr = 262144 * 4 + i * 642 * 4;
			else if (b == 2)
				offset_addr = 524288 * 4 + i * 642 * 4;

			for (int j = 0; j < 642; j++)				// 1~10
					{
				if (k >= 255)
					k = 0;
				else
					k = k + 1;						// 0~ 642*642

				k_word = k + k * 256 + k * 65536;	// B[23:16]. G[15:8]. R[7:0]
				BRAM_0_Write_Buffer[j] = k_word;	// DDR3 Test write data
			}
			//memcpy ((void*)(BRAM_0_Write_Buffer), PS_to_PL_DPRAM_Ctrl, sizeof(BRAM_0_Write_Buffer));
			memcpy((u8*) (0xA0000000 + offset_addr), (void*) (BRAM_0_Write_Buffer), sizeof(BRAM_0_Write_Buffer));
//			memcpy ((u32*)(0xA0000000 + offset_addr), BRAM_0_Write_Buffer, sizeof(BRAM_0_Write_Buffer));
//			usleep(1000);	// 1ms
		}
#ifdef DBG_PRINT
		xil_printf("### line number = %d \r\n", i);
#endif
	}
#ifdef DBG_PRINT
	xil_printf("### PS to PL Download done \r\n");
	xil_printf("### offset_addr = %d \r\n", offset_addr);
	xil_printf("### dpram_addr = 0x%x \r\n", 0xA0000000 + offset_addr);
	xil_printf("\r\n====================================================\r\n");
#endif
}
