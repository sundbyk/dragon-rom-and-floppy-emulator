// defines
//
// This needs to be able to be included from C and asm. defines work in .S and .c files

// Stringify functions
#define XSTR(x) STR(x)
#define STR(x) #x

// true/false
#define TRUE 1
#define FALSE 0

// Shown at the top of the KCDFS menu, eg. "1.2.17". Bump the first part for new features.
// The last number is counted up by the Makefile each time the emulator is built (see build_number)
#ifndef FIRMWARE_BUILD
#define FIRMWARE_BUILD		0
#endif
#define FIRMWARE_VERSION	"1.2." XSTR(FIRMWARE_BUILD)

// GPIO Mapping Port A
#define GPIO_DEBUG_LED		GPIO_Pin_1
#define GPIO_LOGIC_ANALYSER_DEBUG	GPIO_Pin_8

#define GPIO_NEXT_ITEM		GPIO_Pin_2
#define GPIO_PREV_ITEM		GPIO_Pin_3
#define GPIO_DRAGON_TANDY_MODE_CONTROL	GPIO_Pin_4
#define GPIO_DRAGON_DSD		GPIO_Pin_5	// !DSD (p40), push-pull. Low keeps the computer's own ROM off the bus

// GPIO Mapping Port C
#define GPIO_DRAGON_E 		GPIO_Pin_0
#define GPIO_DRAGON_CTS		GPIO_Pin_1
#define GPIO_DRAGON_P2		GPIO_Pin_2
#define GPIO_DRAGON_RW		GPIO_Pin_3
#define GPIO_ONE_PULSE		GPIO_Pin_4	
#define GPIO_DRAGON_RESET	GPIO_Pin_5	

#define GPIO_DRAGON_CART	GPIO_Pin_6
#define GPIO_DRAGON_NMI		GPIO_Pin_7
#define GPIO_TANDY_HALT		GPIO_Pin_13	// PC13




// registers
#define reg_zero	s0
#define reg_bit0_high	s1
//#define reg_bit1_high	s2
#define reg_exit_vector s2
#define reg_gpioc_base	s3
#define reg_bit2_high	s4

#define reg_gpioa_base	s5
#define reg_high_64k_base	s6
#define reg_exti_base	s7
#define reg_moder_dataout	s8
#define reg_moder_datain	s9
// see further down. #define reg_next_fdc_irq_drq	s10
//#define reg_fdc_irq_all_off_mask s11
//#define reg_fdc_drq_all_off_mask s12
#define reg_fdc_drq_countdown	s11
#define reg_fdc_irq_countdown	s12
//#define reg_e_counter	s13
#define reg_fdc_irq_drq_state	s13
#define reg_ccmram_log	s14

#define reg_fdc_status	s15
// NOTE: FPU registers d8 and d9 are used somehow, so you cannot use s16,s17,s18 and s19


#define reg_fdc_write_length	s20
#define reg_fdc_system	s21
#define reg_bit8_high	s22
#define reg_track_buffer_ptr	s23
#define reg_fdc_track	s24
#define reg_fdc_actual_track	s25
#define reg_fdc_sector	s26
#define reg_fdc_data	s27

#define reg_track_base	s28	// track buffer of the selected drive: track_buffer, or the next one for drive B
#define reg_next_fdc_irq_drq	s10
#define reg_fdc_command	s29

#define reg_fake_stack	s30

#define reg_fdc_read_length	s31

// the thread command is a special one. Its hard to use a floating reg from C when we disable the FPU
#define reg_main_thread_command	r10

// ============================
//
#define         E_MASK             0x0001       // ie PC0
#define         CTS_MASK           0x0002       // ie PC1
#define         P2_MASK             0x0004       // ie PC2
#define         RW_MASK               0x0008       // ie PC3
#define         ONE_PULSE_HIGH       0x0010       // ie PC4
#define         RESET_HIGH           0x0020       // ie PC5

#define         CART_HIGH           0x0040       // ie PC6
#define         NMI_HIGH           0x0080       // ie PC7
#define         HALT_HIGH           0x2000       // ie PC13

// =============================
#define		NEXT_ROM_OR_DISK_MASK 0x0004	   // PA2
#define		PREV_ROM_OR_DISK_MASK 0x0008	   // PA3
#define		TANDY_DRAGON_MODE_MASK 0x0010	   // PA4
#define		DSD_HIGH	0x0020	   // PA5. NOTE: the DEBUG_ writes of all of GPIOA's ODR pull it low
// =============================
// basic_rom_state (interrupt.S): dragon.rom or coco.rom in place of the computer's own BASIC ROM
#define		BASIC_ROM_ON		0x01	// in use
#define		BASIC_ROM_PENDING	0x02	// loaded, starts when the reset vector is read
#define		BASIC_ROM_SAM_TY	0x04	// the SAM's map type bit. 1 means RAM at $8000-$FEFF
#define		BASIC_ROM_SAM_TY_BIT	2
#define		BASIC_ROM_VECTOR_LOW	0x08	// $FFFE was just read, so the $FFFF that follows is the vector
#define		MENU_STATUS_BASIC_PENDING 0x20	// bit 5 of the menu command register
#define		MENU_STATUS_BASIC_CHOOSE 0x10	// bit 4: both dragon.rom and coco.rom, ask which
// MENU_BUFFER_BASIC_CHOICE, written by the menu before CHOOSE_BASIC
#define		BASIC_CHOICE_BUILT_IN	0
#define		BASIC_CHOICE_DRAGON	1
#define		BASIC_CHOICE_COCO	2
#define		MAGIC_BUTTON_SUM	1025	// you get this if you push + then - , or - then +

#define		E_PREEMPTION_PRIORITY	0
#define		SDIO_IRQ_PREEMPTION_PRIORITY	3
#define		SDIO_DMA_PREEMPTION_PRIORITY	4


// ============================
// straight from fMSX
//
                           /* Common status bits:               */
#define F_BUSY     0x01    /* Controller is executing a command */
#define F_READONLY 0x40    /* The disk is write-protected       */
#define F_NOTREADY 0x80    /* The drive is not ready            */

                           /* Type-1 command status:            */
#define F_INDEX    0x02    /* Index mark detected               */
#define F_TRACK0   0x04    /* Head positioned at track #0       */
#define F_CRCERR   0x08    /* CRC error in ID field             */
#define F_SEEKERR  0x10    /* Seek error, track not verified    */
#define F_HEADLOAD 0x20    /* Head loaded                       */

                           /* Type-2 and Type-3 command status: */
#define F_DRQ      0x02    /* Data request pending              */
#define F_LOSTDATA 0x04    /* Data has been lost (missed DRQ)   */
#define F_ERRCODE  0x18    /* Error code bits:                  */
#define F_BADDATA  0x08    /* 1 = bad data CRC                  */
#define F_NOTFOUND 0x10    /* 2 = sector not found              */
#define F_BADID    0x18    /* 3 = bad ID field CRC              */
#define F_DELETED  0x20    /* Deleted data mark (when reading)  */
#define F_WRFAULT  0x20    /* Write fault (when writing)        */

#define C_DELMARK  0x01
#define C_UPDATE_SSO 0x02
#define C_STEPRATE 0x03
#define C_VERIFY   0x04
#define C_WAIT15MS 0x04
#define C_LOADHEAD 0x08
#define C_SIDE     0x08
#define C_IRQ      0x08
#define C_SETTRACK 0x10
#define C_MULTIREC 0x10

#define S_DRAGON_DRIVE    0x03	// drive select is bits 1 and 0. 00 means drive 0
#define S_DRAGON_MOTOR    0x04 // has to be zero for motor to turn on.
#define S_DRAGON_DENSITY  0x08 // connects straight to _DDEN
#define S_DRAGON_PRECOMP  0x10 // set high to enable PRECOMP
#define S_DRAGON_NMI      0x20 // set high to enable _NMI ints

#define S_TANDY_DRIVE_1  0x01
#define S_TANDY_DRIVE_2  0x02
#define S_TANDY_DRIVE_3  0x04
#define S_TANDY_MOTOR    0x08
#define S_TANDY_PRECOMP  0x10
#define S_TANDY_DENSITY  0x20
//#define S_TANDY_DRIVE_4  0x40
#define S_TANDY_SIDE  0x40		// Not sure of the history of this, but DRIVE 4 became the Side select
#define S_TANDY_HALT     0x80

#define S_LASTSTEPDIR	0x80
#define S_SIDE          0x00000100
#define S_DRIVE_NOT_READY	0x00000200	// the selected drive has no disk: B with nothing in it, or a third/fourth drive
#define S_DRIVE_B_MOUNTED	0x00000400	// there is a disk in drive B
#define S_FDC_PRESENT	0x80000000
#define S_DRAGON_TANDY_MODE	0x40000000    // Dragon Mode = 1 (to match the state when the jumper is disconnected), Tandy Mode = 0 (to match 'pulled to GND')
#define S_CART_QUIET	0x20000000	// stop toggling CART, so a program loaded from a .cas is not interrupted by the menu starting again

#define WD1793_IRQ     0x80
#define WD1793_DRQ     0x40



// -------------
#define MAIN_THREAD_SEEK_COMMAND 1
#define MAIN_THREAD_CHANGE_DISK_COMMAND 2
#define MAIN_THREAD_COMMAND_LOAD_DIRECTORY 3
#define MAIN_THREAD_COMMAND_LOAD_ROM 4
#define MAIN_THREAD_BUTTON_COMMAND 5
#define MAIN_THREAD_COMMAND_CHANGE_DIR 6
#define MAIN_THREAD_COMMAND_LOAD_COVER 7
#define MAIN_THREAD_COMMAND_CHOOSE_BASIC 8
#define MAIN_THREAD_COMMAND_GO_BASIC 9
#define MAIN_THREAD_COMMAND_DRIVE_B 10

#define LOAD_DIRECTORY_COMMAND_MASK 0x80
#define LOAD_ROM_COMMAND_MASK 0x40
#define CHANGE_DIR_COMMAND_MASK 0x20
#define LOAD_COVER_COMMAND_MASK 0x10
#define CHOOSE_BASIC_COMMAND_MASK 0x08
#define GO_BASIC_COMMAND_MASK 0x04
#define DRIVE_B_COMMAND_MASK 0x02

#define MENU_CTRL_COMMAND_REG		0x00
#define MENU_CTRL_FILE_COUNT_LSB	0x02
#define MENU_CTRL_FILE_COUNT_MSB	0x03
#define MENU_CTRL_ADDRESS_REGISTER_LSB	0x04
#define MENU_CTRL_ADDRESS_REGISTER_MSB	0x05
#define MENU_CTRL_DATA_REGISTER		0x06

// The menu buffer is the whole of CCM, seen by the 6809 through the 16 bit address register at
// $FF54/$FF55 and the data register at $FF56. NOTE: ENABLE_FDC_CCM_LOGGING also logs to CCM, so
// the menu will show garbage in a debug build with that turned on.
//   0x0000  selected entry number (16 bit, little endian), written by the menu before LOAD_ROM
//           or CHANGE_DIR
//   0x0002  entry the menu should start on (16 bit, little endian)
//   0x0004  1 if LOAD_COVER found a cover, 0 if not
//   0x0006  BASIC_CHOICE_*, written by the menu before CHOOSE_BASIC
//   0x0010  firmware version, 0 terminated, at most 15 characters
//   0x0060  name of the disk in drive B (no extension), 0 terminated, at most MENU_NAME_LENGTH
//           characters. Empty if there is none
//   0x0020  folder being shown, relative to dragon/ or tandy/, eg "/GAMES/ARCADE". Empty at the
//           top. Only the last MENU_PATH_LENGTH characters, 0 terminated
//   0x0100  directory entries, MENU_ENTRY_SIZE bytes each. ".." first, then folders, then
//           roms, disks and cassettes, each sorted by name
//              byte 0     'U' up a folder (".."), 'F' folder, 'D' disk image, 'R' rom or 'C' .cas
//              byte 1-29  name without the extension, 0 terminated, at most MENU_NAME_LENGTH chars
//              byte 30-31 file size in K, rounded (16 bit, little endian). 0 for folders
//
// After LOAD_ROM of a .cas the entries are replaced by what the menu has to load (see load_cas()).
// 16 bit numbers are big endian here, which is what the 6809 wants:
//              'M' exec load length, then length bytes   a machine code file. Can be several
//              'B' length, then length bytes             a tokenised BASIC program. At most one,
//                                                        after the 'M's
//              'S' length, then length bytes             machine state to start from, after the
//                                                        'M's (see tools/cas_snapshot.py)
//              0                                         the end
//           or 'E' and a 0 terminated message if the file could not be used
//   0xE200  the cover picture after LOAD_COVER: a PMODE 4 screen, MENU_COVER_SIZE bytes. It is
//           Game.pm4 next to Game.cas (or .vdk ...), or cover.pm4 in the folder (see
//           tools/make_covers.ps1)
#define MENU_BUFFER_BASE		CCMRAM_BASE
#define MENU_BUFFER_SELECTED_INDEX	0x0000
#define MENU_BUFFER_START_CURSOR	0x0002
#define MENU_BUFFER_COVER_OK		0x0004
#define MENU_BUFFER_BASIC_CHOICE	0x0006
#define MENU_BUFFER_COVER		0xE200
#define MENU_COVER_SIZE			6144
#define MENU_COVER_SUFFIX		".pm4"
#define MENU_BUFFER_VERSION		0x0010
#define MENU_BUFFER_PATH		0x0020
#define MENU_BUFFER_DRIVE_B		0x0060
#define MENU_BUFFER_ENTRIES		0x0100
#define MENU_BUFFER_END			0xFFF0	// the upgrade flag lives above this (see upgrade.h)
#define MENU_ENTRY_SIZE			32
#define MENU_ENTRY_SIZE_K		30
#define MENU_NAME_LENGTH		28
#define MENU_PATH_LENGTH		32
#define MENU_MAX_ENTRIES		1800	// 0x0100 + 1800*32 = MENU_BUFFER_COVER

#define MENU_TYPE_UP			'U'
#define MENU_TYPE_FOLDER		'F'
#define MENU_TYPE_DISK			'D'
#define MENU_TYPE_ROM			'R'
#define MENU_TYPE_CAS			'C'

// Longest folder path (eg. "dragon/games/arcade") that can be opened from the menu
#define FOLDER_PATH_LENGTH		256


//
// ---------------

#define DSK_SUFFIX	".dsk"
#define VDK_SUFFIX	".vdk"
#define ROM_SUFFIX	".rom"
#define CCC_SUFFIX	".ccc"
#define BIN_SUFFIX	".bin"
#define CAS_SUFFIX	".cas"

// ------------
// VDK structure
//
//---------
#define CCMRAM_BASE	0x10000000

//------------
#define FDC_WRITE_FLUSH_DEFAULT	200000

//----------
#define MAIN_COMMAND_IN_PROGRESS		0x80000000
#define MAIN_COMMAND_SEEK_COMPLETE		0x40000000
#define MAIN_COMMAND_LOAD_DIRECTORY_COMPLETE	0x20000000
#define MAIN_COMMAND_LOAD_ROM_COMPLETE		0x10000000

#define MAIN_STRUCT_DATA		0x04
#define MAIN_STRUCT_ACTUAL_TRACK	0x08

//--------------
// VDK disk image
#define SIZE_OF_VDK_DISK_HEADER 12

#define VDK_ID				0
#define VDK_HEADER_SIZE_LSB		2
#define VDK_HEADER_SIZE_MSB		3
#define VDK_VERSION_VDK_FORMAT		4
#define VDK_BACKWARDS_COMPAT_VERSION	5
#define VDK_IDENTITY_FILE_SOURCE	6
#define VDK_VERSION_FILE_SOURCE		7
#define VDK_TRACKS			8
#define VDK_SIDES			9
#define VDK_FLAGS			10
#define VDK_COMPRESSION_FLAG_AND_NAME_LENGTH	11

#define SIZEOF_ONE_DISK_TRACK           18*256
#define TRACK_BUFFER_SIZE		0x2400	// one track, both sides (2*18*256). One per drive
#define DISK_40_18_256_SIZE		40*18*256
#define DISK_40_18_256_DOUBLE_SIDED_SIZE	2*40*18*256
#define DISK_80_18_256_DOUBLE_SIDED_SIZE	2*80*18*256
#define DISK_35_18_256_SIZE		35*18*256


// IRQ DRQ countdown delay stuff
#define DRQ_OFF				0x00
#define DRQ_ON				0x80

#define IRQ_OFF				0x00
#define IRQ_ON				0x80

#define WAIT_ZERO_CYCLES_UNTIL_CHANGE	0x01
#define WAIT_3_CYCLES_UNTIL_CHANGE	0x04	// 3 may work (ie. 2 cycles)
#define WAIT_8_CYCLES_UNTIL_CHANGE	0x09
#define WAIT_15_CYCLES_UNTIL_CHANGE	0x11

#define E_CYCLES_FOR_IMMEDIATE_CHANGE           0x01
#define E_CYCLES_FROM_COMMAND_TO_FIRST_DRQ      0x31
#define E_CYCLES_FROM_DRQ_TO_DRQ                0x31
// 4, 5 works, 6 does not work,  7 does not work, 8 does not work, 9 does not work, a does not work, b nogo, c nogo
#define DRAGON_READ_E_CYCLES_FROM_LAST_BYTE_TO_IRQ         0x31		// was 0x11
#define TANDY_READ_E_CYCLES_FROM_LAST_BYTE_TO_IRQ          0x04

#define DRAGON_WRITE_E_CYCLES_FROM_LAST_BYTE_TO_IRQ         0x11
#define TANDY_WRITE_E_CYCLES_FROM_LAST_BYTE_TO_IRQ          0x11





