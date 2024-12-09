/*
 * main.h
 *
 *  Created on: 2024. 10. 31.
 *      Author: User
 */

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_


#define INTERRUPT_ID_PL2PS_EVEN  	(122U)	//	PL to PS	even intr
#define INTERRUPT_ID_PL2PS_ODD		(123U)	//	PL to PS	odd  intr

/******************** Constant Definitions **********************************/
#define DEVICE_ID_CDMA0 	XPAR_AXICDMA_0_DEVICE_ID
#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define INTERRUPT_ID_CDMA0	XPAR_FABRIC_AXICDMA_0_VEC_ID

#define BUFFER_BYTESIZE		(8192*8)	/* Length of the buffers for DMA transfer */

#define NUMBER_OF_TRANSFERS	(4)	/* Number of simple transfers to do */

#define BRAM_SIZE            (0x40000U) //256K
#define BRAM_BASE            (0xA1000000U)
#define BRAM_ADDR_EVEN       (BRAM_BASE)
#define BRAM_ADDR_ODD        (BRAM_BASE + BRAM_SIZE/2)
#define TX_SIZE_BYTES        (102400U)
#define TX_SIZE_IN_WORDS     (TX_SIZE_BYTES/4)
#define ROW_COUNT            (16U)

/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif

void systolic_1_test(void);

void isr_pl2ps_even(void *CallbackRef);
void isr_pl2ps_odd(void *CallbackRef);
void isr_cdma(void *CallBackRef, u32 IrqMask, int *IgnorePtr);

void DisableIntrSystem(XScuGic *IntcInstancePtr, u32 IntrId);

int GIC_Init (XScuGic *hGICInstPtr);
int Init_CDMA (XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u16 DeviceId, u32 IntrId);
int Enable_IntrruptSystem(XScuGic *hGICInstPtr, u16 IntrruptId,	Xil_ExceptionHandler InterruptHandler);
int Enable_CMDA_Intrrupt (XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u32 IntrId);

/**************************** Type Definitions *******************************/
typedef enum {
	PL2PS_EVENT_EVEN, PL2PS_EVENT_ODD, PL2PS_EVENT_MAX
} PL2PS_EVENT_t;


#endif /* SRC_MAIN_H_ */
