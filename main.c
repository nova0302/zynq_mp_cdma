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
#include <stdint.h>
#include <stdbool.h>
#include "sleep.h"
#include "main.h"


#include "ff.h"

/******************** Important!!!! **********************************/
// when you put stdio(print) related functions like printf, puts and so on...
// Be Very Cautious!!!!
// because it will ruin interrupt service routines
/************************** Function Prototypes ******************************/

static FIL fil; /* File object */
static FATFS fatfs;
static char *SD_File;

#define INTERRUPT_ID_PL2PS_EVEN  	(122U)	//	PL to PS	even intr
#define INTERRUPT_ID_PL2PS_ODD		(123U)	//	PL to PS	odd  intr

/******************** Constant Definitions **********************************/
#define DEVICE_ID_CDMA0 	XPAR_AXICDMA_0_DEVICE_ID
#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define INTERRUPT_ID_CDMA0	XPAR_FABRIC_AXICDMA_0_VEC_ID

/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif

static void isr_cdma(void *CallBackRef, u32 IrqMask, int *IgnorePtr);
int XAxiCdma_SimpleIntrExample(XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr,	u16 DeviceId, u32 IntrId);

/************************** Variable Definitions *****************************/
static XAxiCdma AxiCdmaInstance; /* Instance of the XAxiCdma */
static XScuGic IntcController; /* Instance of the Interrupt Controller */

u8 ucWeightArr[NUM_BYTES_WEIGHT] __attribute__ ((aligned(32)));

/* Source and Destination buffer for DMA transfer. */
static u8 DesBuffer[NUM_BYTES_UL] __attribute__ ((aligned (64)));

/* Shared variables used to test the callbacks. */
volatile static u32 Done = 0U; /* Dma transfer is done */
volatile static u32 Error = 0U; /* Dma Bus Error occurs */

volatile static PL2PS_EVENT_t rd_event_type = PL2PS_EVENT_EVEN;
volatile static bool bPL2PS_READ_EVENT = false;

volatile int isr_cnt_even = 0U;
volatile int isr_cnt_odd  = 0U;
volatile int isr_cnt_cdma = 0U;
volatile int cdmaErrCount = 0U;

void isr_even_pl2ps(void *CallbackRef) {
	if (!bPL2PS_READ_EVENT) {
		isr_cnt_even++;
		bPL2PS_READ_EVENT = true;
		rd_event_type = PL2PS_EVENT_EVEN;
	}
}
void isr_odd_pl2ps(void *CallbackRef) {
	if (!bPL2PS_READ_EVENT) {
		isr_cnt_odd++;
		bPL2PS_READ_EVENT = true;
		rd_event_type = PL2PS_EVENT_ODD;
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

int main() {

	int Status;
	init_platform(); // for printf

	printf("-- Build Info --\r\nFile Name: %s\r\nDate: %s\r\nTime: %s\r\n",
	__FILE__, __DATE__, __TIME__);
	xil_printf("\r\n--- Entering main() --- \r\n");

	u32 sramBaseAddr[PL2PS_EVENT_MAX] = { SRAM_ADDR_UL_EVEN, SRAM_ADDR_UL_ODD };

	//	GIC 생성 및 초기화
	GIC_Init(&IntcController);

	//	인터럽트 설정
	//	PL to PS DP_RAM Even Interrupt 등록 (GIC에 인터럽트 연결) - PL(FPGA)에서 데이터 읽어오기 위한 인터럽트
	Enable_IntrruptSystem(&IntcController, INTERRUPT_ID_PL2PS_EVEN, isr_even_pl2ps);

	//	PL to PS DP_RAM Odd Interrupt 등록  (GIC에 인터럽트 연결) - PL(FPGA)에서 데이터 읽어오기 위한 인터럽트
	Enable_IntrruptSystem(&IntcController, INTERRUPT_ID_PL2PS_ODD, isr_odd_pl2ps);

	//	DMA 디바이스 초기화
	//	PL to PS DMA
	Status = Init_CDMA(&IntcController, &AxiCdmaInstance, DEVICE_ID_CDMA0,	INTERRUPT_ID_CDMA0);
	if (Status == XST_FAILURE)
		xil_printf("Init_CDMA Failed!!\r\n");

//	systolic_1_test(); // must call before step into while loop
	setupSram();
	u32 fileNameIndex = 0;
	while (1) {

		if (bPL2PS_READ_EVENT) {
			bPL2PS_READ_EVENT = false;

			u32 SrcAddr = sramBaseAddr[rd_event_type];

			Status = XAxiCdma_SimpleTransfer(&AxiCdmaInstance,
					(UINTPTR) SrcAddr,
					(UINTPTR) &DesBuffer[0],
					NUM_BYTES_UL,
					isr_cdma,
					(void *) &AxiCdmaInstance);

			while (!Done) {
				// Wait for CDMA transfer to complete!!
			}
			Done = 0;
			// Invalidate the DestBuffer before receiving the data, in case the Data Cache is enabled
			Xil_DCacheInvalidateRange((UINTPTR) &DesBuffer, NUM_BYTES_UL);

//			writeToFile(&DesBuffer[0], NUM_BYTES_UL, fileNameIndex);

			if (Status == XST_FAILURE) cdmaErrCount++;

			if ((++fileNameIndex % 16) == 0) {
				break;
//				Xil_DCacheInvalidateRange((UINTPTR) &DesBuffer, NUM_BYTES_UL);
//				systolic_1_test();
			}
		}
	}

	xil_printf("Successfully ran XAxiCdma_SimpleIntr Example\r\n");
	xil_printf("--- Exiting main() --- \r\n");

	return XST_SUCCESS;
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

	Status = XScuGic_CfgInitialize(hGICInstPtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler, hGICInstPtr);

	Xil_ExceptionInit();

	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

//=================================================================
//	Interrupt 연결
//=================================================================

int Enable_IntrruptSystem(XScuGic *hGICInstPtr, u16 IntrruptId,
		Xil_ExceptionHandler InterruptHandler) {
	int Status = 0;

	//	DP_RAM 인터럽트 트리거 설정
	if (IntrruptId == INTERRUPT_ID_PL2PS_EVEN
			|| IntrruptId == INTERRUPT_ID_PL2PS_ODD) {
		XScuGic_SetPriorityTriggerType(hGICInstPtr, IntrruptId, 0x00, 0x3);
//		XScuGic_SetPriorityTriggerType(hGICInstPtr, IntrruptId, 0x64, 0x3);
	}

	Status = XScuGic_Connect(hGICInstPtr, IntrruptId,
			(Xil_ExceptionHandler) InterruptHandler, (void *) hGICInstPtr);

	if (Status != XST_SUCCESS) {
		return Status;
	}

	XScuGic_Enable(hGICInstPtr, IntrruptId);

	return Status;
}

//================================================================================
//	CDMA 생성 및 인터럽트 등록
//================================================================================
int Init_CDMA(XScuGic *IntcInstancePtr, XAxiCdma *InstancePtr, u16 DeviceId,
		u32 IntrId) {
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

	Status = XScuGic_Connect(IntcInstancePtr, IntrId,
			(Xil_InterruptHandler) XAxiCdma_IntrHandler, InstancePtr);
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
	unsigned int SRAM_0_Write_Buffer[642];
//	usleep(1000000);	// 1ms

	xil_printf("### PS to PL Download start \r\n");
	sleep(1);

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
				SRAM_0_Write_Buffer[j] = k_word;	// DDR3 Test write data
			}
			//memcpy ((void*)(SRAM_0_Write_Buffer), PS_to_PL_DPRAM_Ctrl, sizeof(SRAM_0_Write_Buffer));

			int temp = (0xA0000000U + offset_addr);
			uintptr_t pSramBase =(uintptr_t)temp;
			memcpy( (void*)pSramBase
					,(SRAM_0_Write_Buffer)
					, sizeof(SRAM_0_Write_Buffer));

		}
	}
}

void initStSram(StSram *pStSram, u32 numElements) {

	u32 Base   = SRAM_BASE_DL;
	u32 Offset = OFFSET_DL;
	char fileName[32];

	for (int i = 0; i < numElements; i++) {
		sprintf(fileName, "fMap%d.bin", i);
		pStSram->pBase = (uintptr_t) (Base + Offset * i);
		pStSram->numBytes = NUM_BYTES_WEIGHT;
		strncpy(pStSram->FileName, fileName, sizeof(fileName));
		pStSram++;
	}
}

int writeFmapToSram(StSram *pStSram) {

	FRESULT Res;
	UINT NumBytesRead;
	SD_File = pStSram->FileName;

	Res = f_open(&fil, SD_File,  FA_READ);
	if (Res) return XST_FAILURE;

	Res = f_read(&fil, (void*) ucWeightArr, pStSram->numBytes, &NumBytesRead);
	if (Res) return XST_FAILURE;

	memcpy((void*)pStSram->pBase, ucWeightArr, pStSram->numBytes);

	Res = f_close(&fil);
	if (Res) return XST_FAILURE;

	return XST_SUCCESS;

}

int setupSram() {

	StSram StSramArr[16];
	u32 numStSram = sizeof(StSramArr) / sizeof(StSram);

	initStSram(&StSramArr[0], numStSram);

	FRESULT Res;
	TCHAR *Path = "0:/";

	Res = f_mount(&fatfs, Path, 0);

	if (Res != FR_OK) return XST_FAILURE;

	for (int i = 0; i < NUM_WEIGHT_RAMS; i++) {
		writeFmapToSram(&StSramArr[i]);
	}

	return XST_SUCCESS;
}

int writeToFile(u8 *pBuffer, u32 uiNumBytes, u8 fileNameIndex){

	FRESULT Res;
	UINT NumBytesWritten;
	char cFileName[32];
	sprintf(cFileName, "I%d", fileNameIndex);

	SD_File = &cFileName[0];

	Res = f_open(&fil, SD_File, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
	if (Res) return XST_FAILURE;

	Res = f_lseek(&fil, 0);
	if (Res) return XST_FAILURE;

	Res = f_write(&fil, (const void*)pBuffer, uiNumBytes, &NumBytesWritten);
	if (Res) return XST_FAILURE;

	Res = f_close(&fil);
	if (Res) return XST_FAILURE;

	return XST_SUCCESS;
}
