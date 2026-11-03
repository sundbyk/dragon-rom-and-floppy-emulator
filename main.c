/*
 * dragon-rom-and-floppy-emulator
 * kernel@kernelcrash.com
 *
 *
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "defines.h"
#include "stm32f4xx.h"
#include "util.h"
#include "timer.h"

#include "stm32f4_discovery.h"
#include "stm32f4_discovery_sdio_sd.h"
#include "ff.h"
#include "diskio.h"
#include "upgrade.h"
#include "autoboot.h"



extern volatile uint8_t *rom_base;
extern volatile uint8_t *low_64k_base;
extern volatile uint8_t *high_64k_base;

extern volatile uint32_t main_thread_command;
extern volatile uint32_t main_thread_data;
extern volatile uint32_t main_thread_actual_track;

extern volatile uint8_t *track_buffer;
extern volatile uint32_t fdc_write_flush_count;
extern volatile uint32_t menu_ctrl_file_count;
extern volatile uint32_t basic_rom_state;
extern volatile uint32_t basic_rom_choose;

extern volatile uint8_t *disk_header;
extern volatile uint32_t fdc_drive;
extern volatile uint32_t fdc_head[2];
extern volatile uint8_t fdc_dirty[4];

// The disk image in drive A (0) and in drive B (1). The track that drive's head is on is kept in
// track_buffer + drive * TRACK_BUFFER_SIZE
typedef struct {
	FIL fil;
	char name[FOLDER_PATH_LENGTH + 1 + FF_MAX_LFN + 1];
	char header[SIZE_OF_VDK_DISK_HEADER];
	uint32_t header_length;
	uint32_t track_size;
	uint32_t track;		// the one in the track buffer
	uint32_t mounted;
} drive_t;
static drive_t drives[2];

#ifdef ENABLE_SEMIHOSTING
extern void initialise_monitor_handles(void);   /*rtt*/
#endif

GPIO_InitTypeDef  GPIO_InitStructure;

// FATFS stuff
FRESULT res;
FILINFO fno;
DIR dir;
FATFS fs32;
char* path;
UINT BytesRead;

#if _USE_LFN
    static char lfn[_MAX_LFN + 1];
        fno.lfname = lfn;
            fno.lfsize = sizeof lfn;
#endif


// Enable the FPU (Cortex-M4 - STM32F4xx and higher)
// http://infocenter.arm.com/help/topic/com.arm.doc.dui0553a/BEHBJHIG.html
// Also make sure lazy stacking is disabled
void enable_fpu_and_disable_lazy_stacking() {
  __asm volatile
  (
    "  ldr.w r0, =0xE000ED88    \n"  /* The FPU enable bits are in the CPACR. */
    "  ldr r1, [r0]             \n"  /* read CAPCR */
    "  orr r1, r1, #( 0xf << 20 )\n" /* Set bits 20-23 to enable CP10 and CP11 coprocessors */
    "  str r1, [r0]              \n" /* Write back the modified value to the CPACR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb                       \n" /* reset pipeline now the FPU is enabled */
    // Disable lazy stacking (the default) and effectively have no stacking since we're not really using the FPU for anything other than a fast register store
    "  ldr.w r0, =0xE000EF34    \n"  /* The FPU FPCCR. */
    "  ldr r1, [r0]             \n"  /* read FPCCR */
    "  bfc r1, #30,#2\n" /* Clear bits 30-31. ASPEN and LSPEN. This disables lazy stacking */
    "  str r1, [r0]              \n" /* Write back the modified value to the FPCCR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb"                          /* reset pipeline  */
    :::"r0","r1"
    );
}

enum sysclk_freq {
    SYSCLK_42_MHZ=0,
    SYSCLK_84_MHZ,
    SYSCLK_168_MHZ,
    SYSCLK_200_MHZ,
    SYSCLK_240_MHZ,
    SYSCLK_250_MHZ,
};
 
void rcc_set_frequency(enum sysclk_freq freq)
{
    int freqs[]   = {42, 84, 168, 200, 240, 250};
 
    /* USB freqs: 42MHz, 42Mhz, 48MHz, 50MHz, 48MHz */
    int pll_div[] = {2, 4, 7, 10, 10, 10}; 
 
    /* PLL_VCO = (HSE_VALUE / PLL_M) * PLL_N */
    /* SYSCLK = PLL_VCO / PLL_P */
    /* USB OTG FS, SDIO and RNG Clock =  PLL_VCO / PLLQ */
    uint32_t PLL_P = 2;
    uint32_t PLL_N = freqs[freq] * 2;
    uint32_t PLL_M = (HSE_VALUE/1000000);
    uint32_t PLL_Q = pll_div[freq];
 
    RCC_DeInit();
 
    /* Enable HSE osscilator */
    RCC_HSEConfig(RCC_HSE_ON);
 
    if (RCC_WaitForHSEStartUp() == ERROR) {
        return;
    }
 
    /* Configure PLL clock M, N, P, and Q dividers */
    RCC_PLLConfig(RCC_PLLSource_HSE, PLL_M, PLL_N, PLL_P, PLL_Q);
 
    /* Enable PLL clock */
    RCC_PLLCmd(ENABLE);
 
    /* Wait until PLL clock is stable */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);
 
    /* Set PLL_CLK as system clock source SYSCLK */
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
 
    /* Set AHB clock divider */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
 
    //FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
    FLASH->ACR =  FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;

    /* Set APBx clock dividers */
    switch (freq) {
        /* Max freq APB1: 42MHz APB2: 84MHz */
        case SYSCLK_42_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div1); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 42MHz */
            break;
        case SYSCLK_84_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div2); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 84MHz */
            break;
        case SYSCLK_168_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 84MHz */
            break;
        case SYSCLK_200_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 50MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 100MHz */
            break;
        case SYSCLK_240_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 60MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 120MHz */
            break;
        case SYSCLK_250_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); 
            RCC_PCLK2Config(RCC_HCLK_Div2);
            break;
    }
 
    /* Update SystemCoreClock variable */
    SystemCoreClockUpdate();
}


void config_backup_sram() {
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE);
  /* disable backup domain write protection */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);   // set RCC->APB1ENR.pwren
  PWR_BackupAccessCmd(ENABLE);                          // set PWR->CR.dbp = 1;
  /** enable the backup regulator (used to maintain the backup SRAM content in
    * standby and Vbat modes).  NOTE : this bit is not reset when the device
    * wakes up from standby, system reset or power reset. You can check that
    * the backup regulator is ready on PWR->CSR.brr, see rm p144 */
  PWR_BackupRegulatorCmd(ENABLE);     // set PWR->CSR.bre = 1;
}


// For some weird reason the optimizer likes to delete this whole function, hence set to O0
void __attribute__((optimize("O0")))  SD_NVIC_Configuration(void)
{
        NVIC_InitTypeDef NVIC_InitStructure;


        NVIC_InitStructure.NVIC_IRQChannel = SDIO_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = SDIO_IRQ_PREEMPTION_PRIORITY;    // This must be a lower priority (ie. higher number) than the _MREQ and _IORQ interrupts
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

	// DMA2 STREAMx Interrupt ENABLE
	NVIC_InitStructure.NVIC_IRQChannel = SD_SDIO_DMA_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = SDIO_DMA_PREEMPTION_PRIORITY;
	NVIC_Init(&NVIC_InitStructure);

}

void SDIO_IRQHandler(void)
{
	/* Process All SDIO Interrupt Sources */
	SD_ProcessIRQSrc();
}

void SD_SDIO_DMA_IRQHANDLER(void)
{
	SD_ProcessDMAIRQ();
}

//
void config_PC0_event(void) {
        EXTI_InitTypeDef EXTI_InitStruct;

        /* Enable clock for SYSCFG */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource0);

        EXTI_InitStruct.EXTI_Line = EXTI_Line0;
        /* Enable interrupt */
        EXTI_InitStruct.EXTI_LineCmd = ENABLE;
        /* Interrupt mode */
        EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Event;
        /* Triggers on rising and falling edge */
        EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
        /* Add to EXTI */
        EXTI_Init(&EXTI_InitStruct);
}

void config_PC0_int(void) {
        EXTI_InitTypeDef EXTI_InitStruct;
        NVIC_InitTypeDef NVIC_InitStruct;

        /* Enable clock for SYSCFG */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource0);

        EXTI_InitStruct.EXTI_Line = EXTI_Line0;
        /* Enable interrupt */
        EXTI_InitStruct.EXTI_LineCmd = ENABLE;
        /* Interrupt mode */
        EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
        /* Triggers on rising and falling edge */
        EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;
        /* Add to EXTI */
        EXTI_Init(&EXTI_InitStruct);

        /* Add IRQ vector to NVIC */
        /* PC0 is connected to EXTI_Line0, which has EXTI0_IRQn vector */
        NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
        /* Set priority */
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = E_PREEMPTION_PRIORITY;
        /* Set sub priority */
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0x00;
        /* Enable interrupt */
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        /* Add to NVIC */
        NVIC_Init(&NVIC_InitStruct);
}

void config_PC4_int(void) {
        EXTI_InitTypeDef EXTI_InitStruct;
        NVIC_InitTypeDef NVIC_InitStruct;

        /* Enable clock for SYSCFG */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource4);

        EXTI_InitStruct.EXTI_Line = EXTI_Line4;
        /* Enable interrupt */
        EXTI_InitStruct.EXTI_LineCmd = ENABLE;
        /* Interrupt mode */
        EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
        /* Triggers on rising and falling edge */
        EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;
        /* Add to EXTI */
        EXTI_Init(&EXTI_InitStruct);

        /* Add IRQ vector to NVIC */
        /* PC0 is connected to EXTI_Line0, which has EXTI0_IRQn vector */
        NVIC_InitStruct.NVIC_IRQChannel = EXTI4_IRQn;
        /* Set priority */
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = E_PREEMPTION_PRIORITY;
        /* Set sub priority */
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0x00;
        /* Enable interrupt */
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        /* Add to NVIC */
        NVIC_Init(&NVIC_InitStruct);
}

/*
 * PC0(in)         - _E (p6)
   PC1(in)         - _CTS (p32) (the cartridge chip select)
   PC2(in)         - _P2 (p36)  (the $FF40-$FF5F chip select)

   PC3(in)         - R/_W (p18)
   PC4(in)         - OnePulse early interrupt
   PC5(out)        -  _NMI
   PC6(out)        - CART (PIA1 CB1)

   PC13(out)       - _HALT (only used by the Coco
*/
void config_gpio_portc(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOC Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	/* Configure GPIO Settings */
	// Make sure to init the PS2 keyboard pins here.
	GPIO_InitStructure.GPIO_Pin = GPIO_DRAGON_E | GPIO_DRAGON_CTS | GPIO_DRAGON_P2 | GPIO_DRAGON_RW | GPIO_ONE_PULSE | GPIO_DRAGON_RESET;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIOC->PUPDR |= 0x00000002;	// TODO. not sure why I am adding a pullup to _CTS ???
	GPIO_InitStructure.GPIO_Pin =   GPIO_DRAGON_NMI | GPIO_DRAGON_CART | GPIO_TANDY_HALT;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	//GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
	// NB: Do not set CART high. It should be low.
	GPIOC->ODR = NMI_HIGH  | HALT_HIGH;
}

/* Input/Output data GPIO pins on PD{8..15}. Also PD2 is used fo MOSI on the STM32F407VET6 board I have */
void config_gpio_data(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOD Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

}

/* Input Address GPIO pins on PE{0..15} */
void config_gpio_addr(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOE Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = 
		GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
		GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | 
		GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
}


/* Debug GPIO pins on PA6  / PA7 / PA8 */
void config_gpio_dbg(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOA Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4,DISABLE);


	/* Configure GPIO Settings */
	//GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 ;
	GPIO_InitStructure.GPIO_Pin = GPIO_DEBUG_LED | GPIO_LOGIC_ANALYSER_DEBUG;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/* push buttons on PA2, PA3, ROM/DISK control on PA4  */
void config_gpio_buttons(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOA Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);


	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_NEXT_ITEM| GPIO_PREV_ITEM | GPIO_DRAGON_TANDY_MODE_CONTROL;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/* !DSD (p40) on PA5, push-pull. High unless the EXTI4 handler is putting our own BASIC ROM on
   the bus. Set high before it is an output: the handler also reads it back from ODR. Not open
   drain: a pull-up is far too slow to have it high again for the very next bus cycle */
void config_gpio_dsd(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	GPIO_SetBits(GPIOA, GPIO_DRAGON_DSD);
	GPIO_InitStructure.GPIO_Pin = GPIO_DRAGON_DSD;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

// dragon.rom (or coco.rom with the Coco jumper) in the root of the SD card takes the place of the
// computer's own BASIC ROM: 16K for $8000-$BFFF, or 8K for $A000-$BFFF (Color BASIC without
// Extended BASIC, $8000-$9FFF is then $FF). It goes in high_64k_base at the same addresses, and
// the EXTI4 handler pulls !DSD low and puts it on the bus. It starts when the reset vector is next
// read; the menu sees BASIC_ROM_PENDING and does a cold start. Needs cartridge pin 40 wired to PA5,
// or the two ROMs fight over the bus
static void load_basic_rom(char *name) {
	uint8_t *rom = (uint8_t *) &high_64k_base + 0x8000;
	FIL f;
	FSIZE_t size;
	UINT n = 0;

	memset(&f, 0, sizeof(FIL));
	if (f_open(&f, name, FA_READ) != FR_OK) {
		return;
	}
	size = f_size(&f);
	if (size == 0x4000 || size == 0x2000) {
		memset(rom, 0xFF, 0x4000);
		if (f_read(&f, rom + 0x4000 - size, size, &n) == FR_OK && n == size) {
			// bas13.rom and extbas11.rom stuck together the wrong way round: Extended BASIC
			// (starts with "EX") belongs at $8000, Color BASIC with the vectors at $A000
			if (size == 0x4000 && rom[0x2000] == 'E' && rom[0x2001] == 'X' && !(rom[0] == 'E' && rom[1] == 'X')) {
				for (uint32_t i = 0; i < 0x2000; i++) {
					uint8_t t = rom[i];
					rom[i] = rom[i + 0x2000];
					rom[i + 0x2000] = t;
				}
			}
			basic_rom_state = BASIC_ROM_PENDING;
		}
	}
	f_close(&f);
}

// Is there a BASIC ROM load_basic_rom() would take?
static int basic_rom_ok(char *name) {
	FILINFO info;

	return f_stat(name, &info) == FR_OK && (info.fsize == 0x4000 || info.fsize == 0x2000);
}

// Dragon or Coco mode: which folder, which DOS ROM, what the EXTI4 handler emulates and what the
// menu sees in bit 6 of its command register. The jumper decides at power on; choosing the other
// machine's BASIC ROM in the menu changes it
static void set_machine_mode(uint32_t dragon, char *root_directory, char *dosrom) {
	if (dragon) {
		strcpy(root_directory,"dragon");
		strcpy(dosrom,"dragondos.rom");
		// bit 30 of reg_fdc_system is the tandy/dragon mode bit. 1 means Dragon mode
		__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                      "orr r0,#"XSTR(S_DRAGON_TANDY_MODE)"\n"
		       "vmov "XSTR(reg_fdc_system)",r0\n"
                       :::"r0");
	} else {
		strcpy(root_directory,"tandy");
		strcpy(dosrom,"tandydos.rom");
		// bit 30 of reg_fdc_system is the tandy/dragon mode bit. 0 means Tandy mode
		__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                      "and r0,#~"XSTR(S_DRAGON_TANDY_MODE)"\n"
		       "vmov "XSTR(reg_fdc_system)",r0\n"
                       :::"r0");
	}
	update_exti_exit_vector();
}


FRESULT load_disk_header(FIL *fil,char *fname, char *buffer) {
        UINT BytesRead;
        FRESULT res;
	FSIZE_t sz;

	// Start from a clean header so nothing is inherited from the previously loaded image
	memset(buffer, 0, SIZE_OF_VDK_DISK_HEADER);

        res = f_open(fil, fname, FA_READ);
	if (res != FR_OK) {
		// keep the geometry sane so track_size never ends up as 0
		buffer[VDK_TRACKS] = 40;
		buffer[VDK_SIDES] = 1;
		return res;
	}
	sz = f_size(fil);

	if (suffix_match(fname, DSK_SUFFIX)) {

		buffer[VDK_HEADER_SIZE_LSB] = 0x00; /// make it so it wont seek at all
		buffer[VDK_HEADER_SIZE_MSB] = 0x00;
		// default for sizes not listed below: single sided, as many 18x256 tracks as the file holds
		buffer[VDK_TRACKS] = (sz / (SIZEOF_ONE_DISK_TRACK) > 255) ? 255 : sz / (SIZEOF_ONE_DISK_TRACK);
		buffer[VDK_SIDES] = 1;

		switch (sz) {
			case DISK_40_18_256_SIZE:
				buffer[VDK_TRACKS] = 40;
				buffer[VDK_SIDES] = 1;
				break;
			case DISK_40_18_256_DOUBLE_SIDED_SIZE:
				buffer[VDK_TRACKS] = 40;
				buffer[VDK_SIDES] = 2;
				break;
			case DISK_80_18_256_DOUBLE_SIDED_SIZE:
				buffer[VDK_TRACKS] = 80;
				buffer[VDK_SIDES] = 2;
				break;
			case DISK_35_18_256_SIZE:
				buffer[VDK_TRACKS] = 35;
				buffer[VDK_SIDES] = 1;
				break;
		}

	}
	if (suffix_match(fname, VDK_SUFFIX)) {
        	if (res == FR_OK) {
               		res = f_read(fil, buffer, SIZE_OF_VDK_DISK_HEADER, &BytesRead);
			if (res != FR_OK || BytesRead != SIZE_OF_VDK_DISK_HEADER) {
				// short or failed read. Fall back to a headerless 40 track single sided disk
				memset(buffer, 0, SIZE_OF_VDK_DISK_HEADER);
				buffer[VDK_TRACKS] = 40;
			}
        	}
	}

	// Only 1 or 2 sides makes sense. Anything else would give a bogus track_size
	if (buffer[VDK_SIDES] < 1 || buffer[VDK_SIDES] > 2) {
		buffer[VDK_SIDES] = 1;
	}

        return res;
}


FRESULT __attribute__((optimize("O0")))  load_track(FIL *fil, uint32_t track_number, char *track, uint32_t header_length, uint32_t track_size) {
        UINT BytesRead;
        FRESULT res;
	uint32_t offset;

	offset = header_length + (track_number * track_size);

	res = f_lseek(fil, offset);
	if (res == FR_OK) {
		// load 2 sides regardless of whether the disk is single sided or not
		res = f_read(fil, track, 2 * 18 * 256, &BytesRead);
	} else {
                // it is possible after a weird reset that it will try to seek to some random value. Just return a blank track then.
                for (uint32_t i=0; i< SIZEOF_ONE_DISK_TRACK; i++) {
                        track[i] = 0;
                }
		// fake success
		res = FR_OK;
	}

	return res;
}

FRESULT __attribute__((optimize("O0"))) save_track(FIL *fil, uint32_t track_number, char *track, uint32_t header_length, uint32_t track_size) {
        UINT BytesWritten;
        FRESULT res;
	uint32_t offset;

	offset = header_length + (track_number * track_size);

	res = f_lseek(fil, offset);
	if (res == FR_OK) {
		res = f_write(fil, track, track_size, &BytesWritten);
	} else {
                blink_debug_led(3000);
	}
	return res;

}



// Fill the menu buffer with the folder menu_dir (see MENU_BUFFER_* in defines.h).
// Returns the number of entries.
uint32_t load_menu_folder(char *menu_dir, char *root_directory, uint32_t start_cursor) {
	uint32_t count, len;
	char *relative = menu_dir + strlen(root_directory);	// "" or eg. "/games/arcade"

	count = load_directory(menu_dir, (unsigned char *) (MENU_BUFFER_BASE + MENU_BUFFER_ENTRIES), relative[0] != 0);

	// If it is too long, keep the end. That is the folder you are in
	len = strlen(relative);
	if (len > MENU_PATH_LENGTH) {
		relative += len - MENU_PATH_LENGTH;
	}
	strcpy((char *) (MENU_BUFFER_BASE + MENU_BUFFER_PATH), relative);

	*(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_START_CURSOR) = (start_cursor < count) ? start_cursor : 0;
	return count;
}

// Entry number of the folder called name in the menu buffer, or 0
uint32_t find_menu_folder(char *name, uint32_t count) {
	uint32_t i;
	unsigned char *entry;

	for (i = 0; i < count; i++) {
		entry = (unsigned char *) (MENU_BUFFER_BASE + MENU_BUFFER_ENTRIES + (i * MENU_ENTRY_SIZE));
		// menu names are cut at MENU_NAME_LENGTH
		if (entry[0] == MENU_TYPE_FOLDER && strncmp((char *) &entry[1], name, MENU_NAME_LENGTH) == 0) {
			return i;
		}
	}
	return 0;
}

// Read the picture file into the cover buffer. TRUE if it was there and the right size. Through a
// buffer in SRAM: the SD card's DMA can not reach CCM, and f_read of whole sectors goes straight
// to the destination (it never finishes)
static uint32_t read_cover(char *path) {
	static uint8_t buffer[512];
	uint8_t *to = (uint8_t *) (MENU_BUFFER_BASE + MENU_BUFFER_COVER);
	uint32_t done = 0;
	FIL f;
	UINT n = 0;

	memset(&f, 0, sizeof(FIL));
	if (f_open(&f, path, FA_READ) != FR_OK) {
		return FALSE;
	}
	if (f_size(&f) == MENU_COVER_SIZE) {
		while (done < MENU_COVER_SIZE && f_read(&f, buffer, sizeof(buffer), &n) == FR_OK && n > 0) {
			memcpy(to + done, buffer, n);
			done += n;
		}
	}
	f_close(&f);
	return done == MENU_COVER_SIZE;
}

// Cover for the menu entry at ordinal in folder: Game.pm4 for Game.cas, or cover.pm4 in the folder
// (for a folder entry, the one inside it)
uint32_t load_cover(char *folder, uint32_t ordinal) {
	static char path[FOLDER_PATH_LENGTH + 1 + FF_MAX_LFN + 1 + 16];
	static FILINFO cover_fno;
	DIR cover_dir;
	int32_t images_before;
	FRESULT r;
	char *dot;

	memset(&cover_dir, 0, sizeof(DIR));
	r = find_menu_entry(&cover_dir, folder, ordinal, &cover_fno, &images_before);
	f_closedir(&cover_dir);
	if (r != FR_OK || cover_fno.fname[0] == 0) {
		return FALSE;
	}
	strcpy(path, folder);
	strcat(path, "/");
	strcat(path, cover_fno.fname);
	if (is_folder(&cover_fno)) {
		strcat(path, "/cover" MENU_COVER_SUFFIX);
		return read_cover(path);
	}
	dot = strrchr(path, '.');
	if (dot && dot > strrchr(path, '/')) {
		strcpy(dot, MENU_COVER_SUFFIX);
		if (read_cover(path)) {
			return TRUE;
		}
	}
	strcpy(path, folder);
	strcat(path, "/cover" MENU_COVER_SUFFIX);
	return read_cover(path);
}

static uint8_t *drive_buffer(uint32_t d) {
	return (uint8_t *) &track_buffer + d * TRACK_BUFFER_SIZE;
}

// Write a drive's track buffer back to its image file, then reopen the image for reading
static void flush_track(uint32_t d) {
	drive_t *drive = &drives[d];

	if (drive->fil.obj.id) {
		f_close(&drive->fil);
		memset(&drive->fil, 0, sizeof(FIL));
	}
	f_open(&drive->fil, drive->name, FA_WRITE);
	save_track(&drive->fil, drive->track, (char *) drive_buffer(d), drive->header_length, drive->track_size);
	f_close(&drive->fil);
	memset(&drive->fil, 0, sizeof(FIL));
	f_open(&drive->fil, drive->name, FA_READ);
}

// Write back the tracks the computer has written to. The flag is cleared first, so a sector
// written meanwhile is caught next time
static void flush_drives(void) {
	fdc_write_flush_count = 0;
	for (uint32_t d = 0; d < 2; d++) {
		if (fdc_dirty[d]) {
			fdc_dirty[d] = 0;
			if (drives[d].mounted) {
				flush_track(d);
			}
		}
	}
}

// Make name the image in drive d. Its track buffer is filled by whoever calls this
static void mount_drive(uint32_t d, char *name) {
	drive_t *drive = &drives[d];

	if (drive->fil.obj.id) {
		f_close(&drive->fil);
	}
	memset(&drive->fil, 0, sizeof(FIL));
	strcpy(drive->name, name);
	load_disk_header(&drive->fil, drive->name, drive->header);
	drive->track_size = (uint32_t) (uint8_t) drive->header[VDK_SIDES] * 18 * 256;
	drive->header_length = ((uint32_t) (uint8_t) drive->header[VDK_HEADER_SIZE_LSB]) +
		((uint32_t) (uint8_t) drive->header[VDK_HEADER_SIZE_MSB] << 8);
	drive->mounted = TRUE;
}

// Where drive d's head is. The selected drive's is only in the EXTI4 handler's register
static uint32_t drive_head(uint32_t d) {
	if (fdc_drive == d) {
		copy_from_fdc_track_registers();
		return main_thread_data;
	}
	return fdc_head[d];
}

// Show the name of the disk in drive B (without the extension) in the menu, or nothing
static void show_drive_b(void) {
	char *to = (char *) (MENU_BUFFER_BASE + MENU_BUFFER_DRIVE_B);
	char *name, *dot;

	to[0] = 0;
	if (drives[1].mounted) {
		name = strrchr(drives[1].name, '/');
		name = name ? name + 1 : drives[1].name;
		strncpy(to, name, MENU_NAME_LENGTH);
		to[MENU_NAME_LENGTH] = 0;
		dot = strrchr(to, '.');
		if (dot) {
			*dot = 0;
		}
	}
}

void copy_rom_to_ram(uint8_t *from, uint8_t *to) {
   uint32_t *p, *q;
   // copy in 4 byte chunks
   p = (uint32_t *)from;
   q = (uint32_t *)to;

   for (int i=0; i< 0x0800;i++) {
      *q++ = *p++;
   }

}




int __attribute__((optimize("O0")))  main(void) {

	// folder + "/" + a long filename of up to FF_MAX_LFN chars + trailing 0
	TCHAR full_filename[FOLDER_PATH_LENGTH + 1 + FF_MAX_LFN + 1];
	// NEXT/PREV step through the roms and disks in button_dir. The menu shows menu_dir.
	// Both start as root_directory, and are folders below it after the menu has been used.
	static char button_dir[FOLDER_PATH_LENGTH];
	static char menu_dir[FOLDER_PATH_LENGTH];
	static char came_from[FF_MAX_LFN + 1];
	uint32_t menu_start_cursor;
	int next_button_debounce;
	int first_time;
	int special_reset_mode;
	uint32_t	button_state;
	int32_t	file_counter;
	uint32_t reset_counter;
	uint32_t dragon_mode = FALSE;

	char dosrom[15];

        TCHAR root_directory[15];
        DIR dir;
        static FILINFO fno;


	// You have to disable lazy stacking BEFORE initialising the scratch fpu registers
	enable_fpu_and_disable_lazy_stacking();
	init_fpu_regs();

	register uint32_t main_thread_command_reg asm("r10") __attribute__((unused)) = 0;
	uint32_t main_cmd;

// main_thread_command lives in r10 and the EXTI4 handler writes it too. Every update from here
// must be one instruction, otherwise the handler can write r10 in the middle of a
// read-modify-write and its new command is lost.
#define MAIN_CMD_SET(v)	__asm volatile("mov %0,%1" : "=r"(main_thread_command_reg) : "i"(v))
#define MAIN_CMD_OR(v)	__asm volatile("orr %0,%0,%1" : "+r"(main_thread_command_reg) : "i"(v))
// Set or clear bits of reg_fdc_system
#define FDC_SYSTEM_SET(bits)	__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n orr r0,#"XSTR(bits)"\n vmov "XSTR(reg_fdc_system)",r0" ::: "r0")
#define FDC_SYSTEM_CLEAR(bits)	__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n bic r0,#"XSTR(bits)"\n vmov "XSTR(reg_fdc_system)",r0" ::: "r0")

	rcc_set_frequency(SYSCLK_240_MHZ);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_CCMDATARAMEN, ENABLE);

	/* PD{8..15}  and PD2 for SD card MOSI*/
	config_gpio_data();
	/* PE{0..15} */
	config_gpio_addr();
	
	config_gpio_portc();

	config_gpio_dbg();

	config_gpio_buttons();

	config_gpio_dsd();

	config_backup_sram();


	// Make sure _RESET is high and stable before enabling interrupts
	reset_counter=0;
	while (reset_counter < 20) {
		reset_counter = (GPIOC->IDR & RESET_HIGH)? reset_counter+1 : 0;
		delay_us(2);
	}

	setupOnePulse();

	// Determine Tandy or Dargon mode via jumper. Do this before we turn interrupts on
	dragon_mode = (GPIOA->IDR & TANDY_DRAGON_MODE_MASK) ? TRUE : FALSE;
	set_machine_mode(dragon_mode, root_directory, dosrom);

	//copy_rom_to_ram((uint8_t*) &rom_base, (uint8_t*) &high_64k_base);

	  // switch on compensation cell
	RCC->APB2ENR |= 0 |  RCC_APB2ENR_SYSCFGEN ;
	SYSCFG->CMPCR |= SYSCFG_CMPCR_CMP_PD; // enable compensation cell
	while ((SYSCFG->CMPCR & SYSCFG_CMPCR_READY) == 0);  // wait until ready

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); 
	SysTick->CTRL  = 0;

	SD_NVIC_Configuration();




	config_PC0_event();
	config_PC4_int();

        memset(&fs32, 0, sizeof(FATFS));
        res = f_mount(&fs32, "",0);
        if (res != FR_OK) {
                blink_debug_led(250);
        }

	// upgrade.bin in the root of the SD card? Let the bootloader flash it (see bootloader/bl_main.c).
	// The bootloader sets UPGRADE_NO_RETRY when it has had a go, so a file it could not use
	// does not send us round in circles.
	if (*(volatile uint32_t *) UPGRADE_FLAG_ADDR != UPGRADE_NO_RETRY && f_stat(UPGRADE_FILENAME, &fno) == FR_OK) {
		*(volatile uint32_t *) UPGRADE_FLAG_ADDR = UPGRADE_MAGIC;
		NVIC_SystemReset();
	}

	// So the menu can show which firmware is running
	strcpy((char *) (MENU_BUFFER_BASE + MENU_BUFFER_VERSION), FIRMWARE_VERSION);
	show_drive_b();


	init_fdc();
	// This next call just 'primes' the main_thread_data so that when the first disk image is loaded it loads track 0 rather than some unknown track
	copy_from_fdc_track_registers();



	strcpy(button_dir, root_directory);
	strcpy(menu_dir, root_directory);
	menu_start_cursor = 0;

        res = f_opendir(&dir, button_dir);
        if (res != FR_OK) {
                blink_debug_led(100);
        }


	first_time=TRUE;
	next_button_debounce=0;
	file_counter=-1;

	// With both on the card the menu asks which one (MAIN_THREAD_COMMAND_CHOOSE_BASIC)
	if (basic_rom_ok("dragon.rom") && basic_rom_ok("coco.rom")) {
		basic_rom_choose = TRUE;
	} else {
		load_basic_rom(dragon_mode ? "dragon.rom" : "coco.rom");
	}

	// attempt to load the special menu rom. See menu/. Note the menu will talk to the stm32f4 and ask it whether its in Tandy or Dragon mode
	res = load_rom("menu.rom",(char *)&high_64k_base);
	if (res == FR_OK) {
		first_time=FALSE;
	}

	special_reset_mode = 0;

	while(1) {
		button_state = GPIOA->IDR;
		
		 /* doesnt work
		  if (!(GPIOC->IDR & RESET_HIGH)) {
			// _RESET is low
			if (!(button_state & NEXT_ROM_OR_DISK_MASK) || !(button_state & PREV_ROM_OR_DISK_MASK) ) {
				res = load_rom("menu.rom",(char *)&high_64k_base);
				if (res == FR_OK) {
					first_time=FALSE;
				}
				// This memset is actually really important
				memset(&fil, 0, sizeof(FIL));
				next_button_debounce=30000000;
			}
		}
		*/
		if (GPIOC->IDR & RESET_HIGH) {
			special_reset_mode=0;
		} else {
			// The Dragon/Coco has been reset. Toggle CART again so the menu comes back
			FDC_SYSTEM_CLEAR(S_CART_QUIET);
		}
	
		if (!(button_state & NEXT_ROM_OR_DISK_MASK) || !(button_state & PREV_ROM_OR_DISK_MASK) || first_time) {
			first_time = FALSE;
			if (next_button_debounce == 0 ) {
				if (!(button_state & PREV_ROM_OR_DISK_MASK)) {
					// if we hit the prev button do the worlds worst way of finding the previous file
					if (file_counter >0) {
						res = f_closedir(&dir);
        					res = f_opendir(&dir, button_dir);
						for (uint32_t i=0;i< file_counter; i++) {
							res = readdir_image(&dir, &fno);
						}
						file_counter--;
					} else {
						// file_counter is 0 (ie. first file) and we hit PREV. Just keep it to the
						// first file. Do not wrap round.
						//
						// redo getting the first file
						f_closedir(&dir);
						res = f_opendir(&dir, button_dir);
						res = readdir_image(&dir, &fno);
						file_counter=0;
					}
				} else {
					// if we hit the next button or this is the first time through
					file_counter++;
					res = readdir_image(&dir, &fno);
					if (res != FR_OK || fno.fname[0] == 0) {
						// allow buttonpushes to 'wrap round'
						f_closedir(&dir);
						res = f_opendir(&dir, button_dir);
						res = readdir_image(&dir, &fno);
						file_counter=0;
					}
				}
				// Don't let a pending write end up in the next image
				flush_drives();
				strcpy(full_filename,button_dir);
				strcat(full_filename,"/");
				strcat(full_filename,fno.fname);

				if (!(GPIOC->IDR & RESET_HIGH)) {
					// The logic here is that if you hold down reset then press + then -, then special_reset_mode == 1025. If you press + twice you get 2048. If you press - twice you get 2
					if (!(button_state & PREV_ROM_OR_DISK_MASK)) {
						special_reset_mode++;
					} else {
						special_reset_mode+=1024;
					}
				}
				if ( (special_reset_mode==0) && is_disk_image(fno.fname) ) {
					load_rom(dosrom,(char *)&high_64k_base);
					mount_drive(0, full_filename);
					if (dragon_mode) {
						autoboot_patch((uint8_t *) &high_64k_base, &drives[0].fil, drives[0].name, drives[0].header_length, drives[0].track_size);
					}

					// take whatever the fdc_actual_track is and shove in to the track param for the SEEK
					//copy_from_fdc_track_registers();
					// trigger a seek in the next block of code.
					//main_thread_command_reg = MAIN_THREAD_SEEK_COMMAND;
					MAIN_CMD_SET(MAIN_THREAD_BUTTON_COMMAND);
					__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                                               "orr r0,#"XSTR(S_FDC_PRESENT)"\n"
					       "vmov "XSTR(reg_fdc_system)",r0\n"
                                               :::);
				} else {
					// assume ROM
					if (special_reset_mode==MAGIC_BUTTON_SUM) {
						load_rom("menu.rom",(char *)&high_64k_base);
						special_reset_mode=0;
					} else {
						load_rom(full_filename,(char *)&high_64k_base);
					}
					__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                                               "and r0,#~"XSTR(S_FDC_PRESENT)"\n"
					       "vmov "XSTR(reg_fdc_system)",r0\n"
                                               :::);
					FDC_SYSTEM_CLEAR(S_CART_QUIET);
				}
				//next_button_debounce=5850000;
				next_button_debounce=3000000;
			} 
		}
		next_button_debounce = (next_button_debounce>0)?next_button_debounce-1:0;

		// take one snapshot so the checks below all see the same command
		main_cmd = main_thread_command_reg;
		if (!(main_cmd & 0xf0000000) && (main_cmd & 0xff)) {
			switch(main_cmd & 0xff) {
				case (MAIN_THREAD_BUTTON_COMMAND): 
				case (MAIN_THREAD_SEEK_COMMAND): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// Check if there is any pending write
					flush_drives();
					// main_thread_data contains the track number, for the selected drive. A new
					// disk from the NEXT/PREV buttons or the menu is always drive A, on the track
					// its head is on
					uint32_t selected = fdc_drive;
					uint32_t d = ((main_cmd & 0xff) == MAIN_THREAD_BUTTON_COMMAND) ? 0 : selected;
					uint32_t track = (d == selected) ? main_thread_data : fdc_head[d];

					if (d < 2 && drives[d].mounted) {
						load_track(&drives[d].fil, track, (char *) drive_buffer(d), drives[d].header_length, drives[d].track_size);
						drives[d].track = track;
					}
					delay_ms(1);
					if (d == selected) {
						main_thread_actual_track = track;
						update_fdc_track_registers();
					}
					if ((main_cmd & 0xff) == MAIN_THREAD_SEEK_COMMAND) {
						MAIN_CMD_OR(MAIN_COMMAND_SEEK_COMPLETE);
					} else {
						// must be a next/prev button push so make sure we don't feed an INTRQ in
					   	MAIN_CMD_SET(0);
					}
					break;
				}
				case (MAIN_THREAD_COMMAND_LOAD_DIRECTORY): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// The menu comes back in the folder it was last in, on the entry last started
					menu_ctrl_file_count = load_menu_folder(menu_dir, root_directory, menu_start_cursor);
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_DIRECTORY_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_CHANGE_DIR): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// The menu writes the number of the ".." or folder entry it wants to open
					uint32_t selected = *(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_SELECTED_INDEX);
					unsigned char type = (selected < menu_ctrl_file_count) ?
						*(unsigned char *) (MENU_BUFFER_BASE + MENU_BUFFER_ENTRIES + (selected * MENU_ENTRY_SIZE)) : 0;
					char *slash = strrchr(menu_dir, '/');

					came_from[0] = 0;
					menu_start_cursor = 0;
					if (type == MENU_TYPE_UP && slash && strlen(menu_dir) > strlen(root_directory)) {
						strcpy(came_from, slash + 1);
						*slash = 0;
					} else if (type == MENU_TYPE_FOLDER) {
						DIR folder_dir;
						int32_t images_before;

						memset(&folder_dir, 0, sizeof(DIR));
						res = find_menu_entry(&folder_dir, menu_dir, menu_entry_ordinal(selected), &fno, &images_before);
						f_closedir(&folder_dir);
						if (res == FR_OK && is_folder(&fno) && strlen(menu_dir) + 1 + strlen(fno.fname) < FOLDER_PATH_LENGTH) {
							strcat(menu_dir, "/");
							strcat(menu_dir, fno.fname);
							menu_start_cursor = 1;		// first thing after ".."
						}
					}
					menu_ctrl_file_count = load_menu_folder(menu_dir, root_directory, menu_start_cursor);
					if (came_from[0]) {
						// Going up. Start on the folder we just left
						menu_start_cursor = find_menu_folder(came_from, menu_ctrl_file_count);
						*(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_START_CURSOR) = menu_start_cursor;
					}
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_DIRECTORY_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_LOAD_COVER): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					uint32_t selected = *(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_SELECTED_INDEX);

					*(volatile uint8_t *) (MENU_BUFFER_BASE + MENU_BUFFER_COVER_OK) =
						(selected < menu_ctrl_file_count) ? load_cover(menu_dir, menu_entry_ordinal(selected)) : FALSE;
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_DIRECTORY_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_CHOOSE_BASIC): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// The answer to the menu's question. The other machine's ROM brings its mode
					// with it, or the menu would call the wrong BASIC's keyboard routine after the reset
					uint8_t choice = *(volatile uint8_t *) (MENU_BUFFER_BASE + MENU_BUFFER_BASIC_CHOICE);

					if (choice == BASIC_CHOICE_DRAGON || choice == BASIC_CHOICE_COCO) {
						dragon_mode = (choice == BASIC_CHOICE_DRAGON) ? TRUE : FALSE;
						set_machine_mode(dragon_mode, root_directory, dosrom);
						strcpy(button_dir, root_directory);
						strcpy(menu_dir, root_directory);
						menu_start_cursor = 0;
						f_closedir(&dir);
						res = f_opendir(&dir, button_dir);
						file_counter = -1;
						load_basic_rom(dragon_mode ? "dragon.rom" : "coco.rom");
					}
					basic_rom_choose = FALSE;
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_ROM_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_GO_BASIC): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// The menu goes to BASIC's OK. Stop CART until the next reset, the same as for
					// a .cas, or the menu would start again
					FDC_SYSTEM_SET(S_CART_QUIET);
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_ROM_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_DRIVE_B): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// SHIFT+ENTER on a disk in the menu: it goes in drive B, without a reset. The
					// one that is in there already comes out again
					static FILINFO b_fno;
					static char b_name[FOLDER_PATH_LENGTH + 1 + FF_MAX_LFN + 1];
					DIR b_dir;
					int32_t images_before;
					uint32_t selected = *(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_SELECTED_INDEX);

					memset(&b_dir, 0, sizeof(DIR));
					res = (selected < menu_ctrl_file_count) ?
						find_menu_entry(&b_dir, menu_dir, menu_entry_ordinal(selected), &b_fno, &images_before) : FR_NO_FILE;
					f_closedir(&b_dir);
					if (res == FR_OK && is_disk_image(b_fno.fname) &&
					    strlen(menu_dir) + 1 + strlen(b_fno.fname) < sizeof(b_name)) {
						strcpy(b_name, menu_dir);
						strcat(b_name, "/");
						strcat(b_name, b_fno.fname);
						flush_drives();
						FDC_SYSTEM_CLEAR(S_DRIVE_B_MOUNTED);
						if (drives[1].mounted && strcmp(b_name, drives[1].name) == 0) {
							f_close(&drives[1].fil);
							memset(&drives[1].fil, 0, sizeof(FIL));
							drives[1].mounted = FALSE;
							if (fdc_drive == 1) {
								FDC_SYSTEM_SET(S_DRIVE_NOT_READY);
							}
						} else {
							uint32_t track = drive_head(1);

							mount_drive(1, b_name);
							load_track(&drives[1].fil, track, (char *) drive_buffer(1), drives[1].header_length, drives[1].track_size);
							drives[1].track = track;
							FDC_SYSTEM_SET(S_DRIVE_B_MOUNTED);
							if (fdc_drive == 1) {
								FDC_SYSTEM_CLEAR(S_DRIVE_NOT_READY);
							}
						}
						show_drive_b();
					}
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_ROM_COMPLETE);
					break;
				}
				case (MAIN_THREAD_COMMAND_LOAD_ROM): {
					MAIN_CMD_OR(MAIN_COMMAND_IN_PROGRESS);
					// The menu writes the number of the (sorted) entry it wants
					uint32_t selected = *(volatile uint16_t *) (MENU_BUFFER_BASE + MENU_BUFFER_SELECTED_INDEX);
					uint32_t ordinal = (selected < menu_ctrl_file_count) ? menu_entry_ordinal(selected) : 0;

					flush_drives();
					menu_start_cursor = selected;
					// Walk the menu folder to that image. This also leaves the directory pointer at the
					// right spot, such that hitting NEXT will advance to the next image in that folder
					res = find_menu_entry(&dir, menu_dir, ordinal, &fno, &file_counter);
					if (res == FR_OK && is_cas_file(&fno)) {
						// The menu stays where it is and loads the program itself. Stop CART so BASIC
						// does not start the menu again when it turns interrupts back on
						strcpy(full_filename, menu_dir);
						strcat(full_filename, "/");
						strcat(full_filename, fno.fname);
						if (load_cas(full_filename, (uint8_t *) (MENU_BUFFER_BASE + MENU_BUFFER_ENTRIES), (uint8_t *) (MENU_BUFFER_BASE + MENU_BUFFER_END)) == 0) {
							FDC_SYSTEM_SET(S_CART_QUIET);
						}
						// NEXT/PREV carry on from the first rom or disk in this folder
						f_closedir(&dir);
						res = f_opendir(&dir, menu_dir);
						file_counter = -1;
						strcpy(button_dir, menu_dir);
						MAIN_CMD_OR(MAIN_COMMAND_LOAD_ROM_COMPLETE);
						break;
					}
					if (res != FR_OK || fno.fname[0] == 0 || !is_image_file(&fno)) {
						f_closedir(&dir);
						res = f_opendir(&dir, menu_dir);
						res = readdir_image(&dir, &fno);
						file_counter=0;
					}
					strcpy(button_dir, menu_dir);
					strcpy(full_filename,button_dir);
					strcat(full_filename,"/");
					strcat(full_filename,fno.fname);
					if (is_disk_image(fno.fname)) {
						load_rom(dosrom,(char *)&high_64k_base);
						mount_drive(0, full_filename);
						if (dragon_mode) {
							// type RUN"M", BOOT or whatever suits the disk once DragonDOS is up
							autoboot_patch((uint8_t *) &high_64k_base, &drives[0].fil, drives[0].name, drives[0].header_length, drives[0].track_size);
						}

						MAIN_CMD_SET(MAIN_THREAD_BUTTON_COMMAND);
						__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                                                       "orr r0,#"XSTR(S_FDC_PRESENT)"\n"
						       "vmov "XSTR(reg_fdc_system)",r0\n"
                                                       :::);
					} else {
						// assume ROM
						load_rom(full_filename,(char *)&high_64k_base);
						__asm volatile("vmov r0,"XSTR(reg_fdc_system)"\n"
                                                       "and r0,#~"XSTR(S_FDC_PRESENT)"\n"
						       "vmov "XSTR(reg_fdc_system)",r0\n"
                                                       :::);
					}
					FDC_SYSTEM_CLEAR(S_CART_QUIET);
					MAIN_CMD_OR(MAIN_COMMAND_LOAD_ROM_COMPLETE);
					break;
				}
			}
		}

		if (fdc_write_flush_count) {
			fdc_write_flush_count--;
			if (fdc_write_flush_count == 0) {
				flush_drives();
			}
		}
	}
}

