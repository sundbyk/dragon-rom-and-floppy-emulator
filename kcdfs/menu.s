; KCDFS (Dragon) / KCCFS (Coco) - rom and disk menu for the dragon-rom-and-floppy-emulator
;
; Build with build.sh, which counts up the build number in the title and writes build.inc, then
;   lwasm -f raw --list=menu.lst -o menu.rom menu.s
; Copy menu.rom to the root of the SD card. The output is padded to 8K.
;
; For trying it out in an emulator without the stm32f4 board:
;   lwasm -f raw -D SIM -o menu-sim.rom menu.s
; That replaces the $FF50-$FF56 interface with a built in list of names.
;
; The stm32f4 reads one folder at a time, sorts it (".." first, then folders, then roms, disks
; and cassettes) and hands it over 32 bytes per entry (see MENU_BUFFER_* in defines.h). Up to
; 1800 entries per folder.
;
; Keys
;   UP/DOWN      move (wraps round). Held down they repeat
;   LEFT/RIGHT   page. Held down they repeat
;   letters etc  find the first name starting with what has been typed so far
;   SPACE        show or hide the file sizes (or a space in the find, once it has started)
;   @            show the cover (Game.pm4 or cover.pm4). ENTER on it starts the game
;   CLEAR        start the find again
;   ENTER        open the selected folder, or start the selected rom, disk or cassette
;   SHIFT+ENTER  on a disk: put it in drive B (or take it out again). Then pick drive A's
;   BREAK        up one folder. At the top: BASIC (reset brings the menu back)

	PRAGMA	6809

DRAGON_POLCAT		EQU	$8006		; Dragon: JMP to the keyboard poll routine
COCO_POLCAT		EQU	$A000		; Coco: address of the keyboard poll routine
TXT_SCREEN		EQU	$0400
PIA1_B			EQU	$FF22		; bit 3 is the VDG CSS (colour set) line

MENU_CTRL_COMMAND	EQU	$FF50
MENU_CTRL_FILE_COUNT	EQU	$FF52
MENU_CTRL_ADDRESS	EQU	$FF54
MENU_CTRL_DATA		EQU	$FF56

BUF_SELECTED		EQU	$0000		; where the selected entry number goes
BUF_START_CURSOR	EQU	$0002		; entry to start on
BUF_COVER_OK		EQU	$0004		; 1 if there is a cover after COMMAND_LOAD_COVER
BUF_BASIC_CHOICE	EQU	$0006		; for COMMAND_CHOOSE_BASIC: 0 built in, 1 Dragon, 2 Coco
BUF_COVER		EQU	$E200		; the cover, a PMODE 4 screen
COVER_SIZE		EQU	6144
COVER_RAM		EQU	$1000		; where it is shown from. Clear of VARS, and fine on a 16K Coco
BUF_VERSION		EQU	$0010		; firmware version string
BUF_PATH		EQU	$0020		; folder being shown, "" at the top
BUF_ENTRIES		EQU	$0100		; 32 bytes per entry. Type byte then the name
ENTRY_SIZE_K		EQU	30		; where in the entry the size in K is (little endian)
TYPE_UP			EQU	'U'		; ".."
TYPE_FOLDER		EQU	'F'
TYPE_DISK		EQU	'D'
TYPE_ROM		EQU	'R'
TYPE_CAS		EQU	'C'

; Color BASIC and Dragon BASIC both keep these here
TXTTAB			EQU	$0019		; start of the BASIC program
VARTAB			EQU	$001B		; start of the variables, just after the program
ARYTAB			EQU	$001D
ARYEND			EQU	$001F
RSTFLG			EQU	$0071		; $55 if RSTVEC is set up
RSTVEC			EQU	$0072		; warm start
DRAGON_WARM		EQU	$B44F		; Dragon 32 BASIC's warm start
COCO_WARM		EQU	$A0E8		; Color BASIC's warm start (BAWMST)
EXECJP			EQU	$009D		; where EXEC with no address goes
HOOK_CONSOLE_IN		EQU	$016A		; RAM hook. JSRed every time BASIC wants a character
PIA1_CRB		EQU	$FF23		; bit 0 lets CART cause an FIRQ
PIA0_B			EQU	$FF02		; reading it clears a waiting field sync IRQ

COMMAND_LOAD_DIRECTORY	EQU	$80
COMMAND_LOAD_ROM	EQU	$40
COMMAND_CHANGE_DIR	EQU	$20
COMMAND_LOAD_COVER	EQU	$10
COMMAND_CHOOSE_BASIC	EQU	$08
COMMAND_GO_BASIC	EQU	$04
COMMAND_DRIVE_B		EQU	$02
BUF_DRIVE_B		EQU	$0060		; name of the disk in drive B, "" if none
CURPOS			EQU	$0088		; BASIC's cursor on the text screen
STATUS_BASIC_CHOOSE	EQU	$10		; read from the command register: ask which BASIC

ROWS			EQU	12		; entries per page
LIST_ROW		EQU	2
STATUS_ROW		EQU	15
MAX_PREFIX		EQU	12

; Screen codes. $00-$3F is a light character on black, $40-$7F black on light.
; $80-$FF are coloured semigraphics blocks: %1cccqqqq, c = colour, q = quadrants
BLACK			EQU	$20
BAR			EQU	$60
DISK_MARK		EQU	$DF		; cyan block
ROM_MARK		EQU	$EF		; magenta block
FOLDER_MARK		EQU	$9F		; yellow block
CAS_MARK		EQU	$CF		; buff (white) block
HIGHLIGHT		EQU	$40
BASIC_BLANK		EQU	$60		; a space the way BASIC clears the screen

; Key repeat. Counted in turns round the key loop, each with a REPEAT_UNIT delay (about 5ms)
REPEAT_UNIT		EQU	600
REPEAT_DELAY		EQU	50		; before the first repeat
REPEAT_RATE		EQU	6		; between repeats

; Variables. Low RAM that BASIC is not using for anything we care about (the machine is cold
; started when we are done), and below $1000 so a 16K Coco is fine too.
VARS			EQU	$0E00
MODE			EQU	VARS+0		; 1 Dragon, 0 Coco
COUNT			EQU	VARS+1		; 2 bytes. Number of entries
TOP			EQU	VARS+3		; 2 bytes. Entry shown on the first row
CUR			EQU	VARS+5		; 2 bytes. Selected entry
IDX			EQU	VARS+7		; 2 bytes
TMP			EQU	VARS+9		; 2 bytes
ROW			EQU	VARS+11
HIMASK			EQU	VARS+12		; $40 to draw black on light, 0 for light on black
PLEN			EQU	VARS+13
LEAD			EQU	VARS+14
DIGIT			EQU	VARS+15
SIMPTR			EQU	VARS+16		; 2 bytes, SIM build only
SHOWSIZE		EQU	VARS+18		; not 0 to show the file sizes
ETYPE			EQU	VARS+19		; type of the entry being drawn
EADDR			EQU	VARS+20		; 2 bytes. Its address in the stm32f4 buffer
REPKEY			EQU	VARS+22		; key that repeats while held down, 0 for none
REPCNT			EQU	VARS+23
SIMCAS			EQU	VARS+24		; SIM build only. Which made up .cas is being loaded
STACKHIT		EQU	VARS+25		; 1 if the .cas loads over our stack
PREFIX			EQU	VARS+32		; MAX_PREFIX bytes
STUB			EQU	VARS+$80	; the reboot code runs from here


; Next byte from the stm32f4 buffer into A, without touching RAM (the .cas loader can not trust
; any of it). The SIM build has to use the stand-in READ_DATA.
GETB	MACRO
 IFDEF SIM
	LBSR	READ_DATA
 ELSE
	LDA	MENU_CTRL_DATA
 ENDC
	ENDM

; Next two bytes, big endian, into D
GETW	MACRO
	GETB
	TFR	A,B
	GETB
	EXG	A,B
	ENDM

; Interrupts on again, but not the CART one that started this menu. Uses A only. The field sync
; IRQ waiting since the copying started is cleared too, so the program gets a whole field before
; the first one
INTERRUPTS_ON MACRO
	LDA	PIA1_CRB
	ANDA	#$FE
	STA	PIA1_CRB
	LDA	PIA1_B				; clear one that is waiting
	LDA	PIA0_B
	ANDCC	#$AF
	ENDM

	ORG	$C000

	FCC	/DK/				; Dragon and Coco BASIC both look for this

START
	; text mode with the orange colour set
	LDA	PIA1_B
	ANDA	#$07
	ORA	#$08
	STA	PIA1_B

	CLR	SHOWSIZE
	CLR	REPKEY
	CLR	SIMCAS
	LBSR	GET_MODE
	LBSR	CHOOSE_BASIC
	LBSR	NEW_BASIC
	LBSR	CLEAR_SCREEN
	LBSR	DRAW_HEADER

	LDU	#TXT_SCREEN+(7*32)+7
	CLR	HIMASK
	LEAX	MSG_READING,PCR
	LBSR	PUTS

	LBSR	LOAD_DIRECTORY
	LBSR	NEW_LIST

	LDD	COUNT
	BNE	MAIN_LOOP
	; nothing to show
	LBSR	CLEAR_LIST
	LDU	#TXT_SCREEN+(7*32)+3
	LEAX	MSG_NO_FILES,PCR
	LBSR	PUTS
	LEAX	MSG_DRAGON_DIR,PCR
	TST	MODE
	BNE	L1@
	LEAX	MSG_TANDY_DIR,PCR
L1@	LBSR	PUTS
L2@	BRA	L2@

MAIN_LOOP
	LBSR	DRAW_LIST
	LBSR	DRAW_STATUS
KEY_WAIT
	LBSR	GET_KEY
	TSTA
	BNE	L1@
	LBSR	KEY_REPEAT			; nothing new. Is an arrow being held down?
	TSTA
	BEQ	KEY_WAIT
	BRA	L2@
L1@	LBSR	ARM_REPEAT
L2@	CMPA	#$0A
	LBEQ	KEY_DOWN
	CMPA	#$5E
	LBEQ	KEY_UP
	CMPA	#$09
	LBEQ	KEY_PAGE_DOWN
	CMPA	#$08
	LBEQ	KEY_PAGE_UP
	CMPA	#$0D
	LBEQ	KEY_ENTER
	CMPA	#$0C
	LBEQ	KEY_CLEAR
	CMPA	#$03
	LBEQ	KEY_BREAK
	CMPA	#'@'
	LBEQ	KEY_COVER
	CMPA	#$20
	BLO	KEY_WAIT
	BNE	L3@
	TST	PLEN				; SPACE. Part of the find once that has started
	BNE	L3@
	COM	SHOWSIZE
	BRA	MAIN_LOOP
L3@	CMPA	#$7E
	BHI	KEY_WAIT
	LBSR	FIND_TYPED
	BRA	MAIN_LOOP

; A new key in A. Arrows repeat if held down. A is kept
ARM_REPEAT
	CLR	REPKEY
	CMPA	#$0A				; down
	BEQ	L1@
	CMPA	#$5E				; up
	BEQ	L1@
	CMPA	#$09				; right
	BEQ	L1@
	CMPA	#$08				; left
	BNE	L2@
L1@	STA	REPKEY
	LDB	#REPEAT_DELAY
	STB	REPCNT
L2@	RTS

; No new key. Returns the key to repeat in A, or 0
KEY_REPEAT
	LDA	REPKEY
	BEQ	L9@
	; anything held down? All the columns low, then look for a row that is low
	CLR	$FF02
	LDA	$FF00
	LDB	#$FF
	STB	$FF02
	ORA	#$80				; bit 7 is the joystick comparator
	CMPA	#$FF
	BNE	L1@
	CLR	REPKEY				; let go
	CLRA
	RTS
L1@	LDX	#REPEAT_UNIT
L2@	LEAX	-1,X
	BNE	L2@
	DEC	REPCNT
	BNE	L8@
	LDA	#REPEAT_RATE
	STA	REPCNT
	LDA	REPKEY
	RTS
L8@	CLRA
L9@	RTS

KEY_DOWN
	LDD	CUR
	ADDD	#1
	CMPD	COUNT
	BLO	L1@
	LDD	#0				; past the last one, back to the first
	STD	TOP
L1@	STD	CUR
	LBSR	FIX_TOP
	LBRA	MAIN_LOOP

KEY_UP
	LDD	CUR
	BNE	L1@
	LDD	COUNT				; at the first one, go to the last
L1@	SUBD	#1
	STD	CUR
	LBSR	FIX_TOP
	LBRA	MAIN_LOOP

KEY_PAGE_DOWN
	LDD	CUR
	ADDD	#ROWS
	CMPD	COUNT
	BLO	L1@
	LDD	COUNT
	SUBD	#1
L1@	STD	CUR
	LDD	TOP
	ADDD	#ROWS
	STD	TOP
	LBSR	CLAMP_TOP
	LBSR	FIX_TOP
	LBRA	MAIN_LOOP

KEY_PAGE_UP
	LDD	CUR
	SUBD	#ROWS
	BCC	L1@
	LDD	#0
L1@	STD	CUR
	LDD	TOP
	SUBD	#ROWS
	BCC	L2@
	LDD	#0
L2@	STD	TOP
	LBSR	FIX_TOP
	LBRA	MAIN_LOOP

KEY_CLEAR
	CLR	PLEN
	LBRA	MAIN_LOOP

; BREAK. Same as ENTER on the ".." entry, if there is one. At the top there is not: go to BASIC
KEY_BREAK
	LDD	#BUF_ENTRIES
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	CMPA	#TYPE_UP
	BNE	GO_BASIC
	LDD	#0
	STD	CUR
	LBRA	OPEN_FOLDER

; The selected disk goes in drive B, or comes out if it is in there already. No reset, so the
; disk for drive A can be picked next
MOUNT_B
	LBSR	WRITE_SELECTED
	LBSR	SEND_DRIVE_B
	LBSR	DRAW_DRIVE_B
	LBRA	MAIN_LOOP

; Not equal (Z clear) if SHIFT is held down. Same row and column on the Dragon and the Coco
SHIFT_DOWN
	LDA	#$7F				; column 7
	STA	PIA0_B
	LDA	$FF00
	LDB	#$FF
	STB	PIA0_B
	COMA
	ANDA	#$40				; row 6
	RTS

; BASIC's OK, as if there were no menu. The stm32f4 stops CART until the next reset, which
; brings the menu back
GO_BASIC
	LBSR	SEND_GO_BASIC
	LDA	PIA1_B
	ANDA	#$F7				; CSS 0
	STA	PIA1_B
	LDU	#TXT_SCREEN
	STU	CURPOS
	LDA	#BASIC_BLANK
L1@	STA	,U+
	CMPU	#TXT_SCREEN+512
	BNE	L1@
	LBSR	CAS_INTERRUPTS
	LBSR	FIND_WARM
	JMP	,X

KEY_ENTER
	LDD	CUR
	LBSR	ENTRY_ADDRESS
	LBSR	SET_ADDRESS
	LBSR	READ_DATA			; type byte
	CMPA	#TYPE_DISK
	BNE	L0@
	LBSR	SHIFT_DOWN			; SHIFT+ENTER: in drive B instead
	LBNE	MOUNT_B
	LDA	#TYPE_DISK
L0@	CMPA	#TYPE_FOLDER
	LBEQ	OPEN_FOLDER
	CMPA	#TYPE_UP
	LBEQ	OPEN_FOLDER
	STA	ETYPE
	LBSR	WRITE_SELECTED
	LDA	ETYPE
	CMPA	#TYPE_CAS
	LBEQ	RUN_CAS
 IFDEF SIM
	; nothing to reboot into. Just show what would have been sent
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	LDA	#HIGHLIGHT
	STA	HIMASK
	LEAX	MSG_SIM_SELECTED,PCR
	LBSR	PUTS
	LDD	CUR
	ADDD	#1
	LBSR	PRINT_DEC4
	CLR	HIMASK
L1@	LBSR	GET_KEY
	TSTA
	BEQ	L1@
	LBRA	MAIN_LOOP
 ELSE
	; The stm32f4 swaps the rom out from under us, so the last bit has to run from RAM
	LEAX	STUB_START,PCR
	LDY	#STUB
L1@	LDA	,X+
	STA	,Y+
	CMPY	#STUB+(STUB_END-STUB_START)
	BNE	L1@
	JMP	STUB
 ENDC

; @. Show the cover of the entry at CUR full screen. ENTER starts it, any other key goes back
KEY_COVER
	LDD	CUR
	LBSR	ENTRY_ADDRESS
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	CMPA	#TYPE_UP
	LBEQ	KEY_WAIT
	LBSR	WRITE_SELECTED
	LBSR	LOAD_COVER
	LDD	#BUF_COVER_OK
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	BNE	L2@
	; none. Say so on the status line until a key is pressed
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	LDA	#HIGHLIGHT
	STA	HIMASK
	LEAX	MSG_NO_COVER,PCR
	LBSR	PUTS
	LDA	#BAR
L1@	STA	,U+
	CMPU	#TXT_SCREEN+((STATUS_ROW+1)*32)
	BNE	L1@
	CLR	HIMASK
	LBSR	WAIT_KEY
	LBRA	MAIN_LOOP
L2@	LDD	#BUF_COVER
	LBSR	SET_ADDRESS
	LDX	#COVER_RAM
L3@	LBSR	READ_DATA
	STA	,X+
	CMPX	#COVER_RAM+COVER_SIZE
	BNE	L3@
	LBSR	GRAPHICS_MODE
	LBSR	WAIT_KEY
	PSHS	A
	LBSR	TEXT_MODE
	PULS	A
	CLR	REPKEY
	CMPA	#$0D
	LBEQ	KEY_ENTER
	LBRA	MAIN_LOOP

WAIT_KEY
	LBSR	GET_KEY
	TSTA
	BEQ	WAIT_KEY
	RTS

; PMODE 4 with CSS 1 (black and buff), showing COVER_RAM. The SAM's V bits are $FFC0-$FFC5 and
; its display address (in 512 byte steps) is $FFC6-$FFD3, writing the odd address sets a bit
GRAPHICS_MODE
	LDA	PIA1_B
	ANDA	#$07
	ORA	#$F8				; A/G, GM2, GM1, CSS
	STA	PIA1_B
	STA	$FFC0				; V = 110
	STA	$FFC3
	STA	$FFC5
	LDB	#COVER_RAM/512
	BRA	SAM_ADDRESS

; Back to the orange text screen at $0400
TEXT_MODE
	LDA	PIA1_B
	ANDA	#$07
	ORA	#$08
	STA	PIA1_B
	STA	$FFC0				; V = 000
	STA	$FFC2
	STA	$FFC4
	LDB	#TXT_SCREEN/512
; B = display address / 512. Seven bits, F0 first
SAM_ADDRESS
	LDX	#$FFC6
L1@	LSRB
	BCC	L2@
	STA	1,X				; set
	BRA	L3@
L2@	STA	,X				; clear
L3@	LEAX	2,X
	CMPX	#$FFD4
	BNE	L1@
	RTS

; Ask the stm32f4 to open the folder (or "..") at CUR, then show it
OPEN_FOLDER
	LBSR	WRITE_SELECTED
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	CLR	HIMASK
	LEAX	MSG_READING,PCR
	LBSR	PUTS
	LBSR	CHANGE_FOLDER
	LBSR	NEW_LIST
	LBRA	MAIN_LOOP

; Tell the stm32f4 which entry. Little endian
WRITE_SELECTED
	LDD	#BUF_SELECTED
	LBSR	SET_ADDRESS
	LDD	CUR
	PSHS	A
	TFR	B,A
	LBSR	WRITE_DATA
	PULS	A
	LBRA	WRITE_DATA

; A new list is in the stm32f4 buffer. Pick up the count and where to start, and show the folder
NEW_LIST
	LBSR	GET_FILE_COUNT
	STD	COUNT
	LDD	#BUF_START_CURSOR
	LBSR	SET_ADDRESS
	LBSR	READ_DATA			; low byte
	TFR	A,B
	LBSR	READ_DATA			; high byte
	CMPD	COUNT
	BLO	L1@
	LDD	#0
L1@	STD	CUR
	STD	TOP
	CLR	PLEN
	LBSR	CLAMP_TOP
	LBRA	DRAW_PATH

; Copied to RAM. Must only use relative branches and absolute addresses outside the rom.
STUB_START
	ORCC	#$50				; no interrupts
	CLR	$0071				; make the reset a cold start
	LDA	#$40				; load the selected rom or disk
	STA	MENU_CTRL_COMMAND
	; Wait for the stm32f4 to say it is done (bit 7 low). Give up after about 10 seconds
	LDB	#8
L1@	LDX	#0
L2@	LDA	MENU_CTRL_COMMAND
	BPL	L3@
	LEAX	-1,X
	BNE	L2@
	DECB
	BNE	L1@
L3@	TST	MODE
	BEQ	L4@
	JMP	[$FFFE]				; Dragon reset
L4@	JMP	$A027				; Coco reset (Coco 1 and 2)
STUB_END

; ------------------------------------------------------------------
; .cas files. The stm32f4 turns the file into a list of what to load (see load_cas() in util.c):
;   'M' exec load length, then the bytes      machine code, as CLOADM would load it
;   'B' length, then the bytes                a BASIC program, as CLOAD would load it
;   'S' length, then the state                where to carry on from (see CAS_RESUME)
;   0                                         the end
; or 'E' and a message. The rom stays as it is, so this all runs from here.
RUN_CAS
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	CLR	HIMASK
	LEAX	MSG_LOADING,PCR
	LBSR	PUTS
	LDA	#BLACK
L1@	STA	,U+
	CMPU	#TXT_SCREEN+((STATUS_ROW+1)*32)
	BNE	L1@
	LBSR	LOAD_CAS
	LBSR	STACK_COVERED
	STB	STACKHIT
	LDD	#BUF_ENTRIES
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	CMPA	#'E'
	BEQ	CAS_ERROR
	PSHS	A
	; looks like BASIC from here on
	LDA	PIA1_B
	ANDA	#$F7				; CSS 0
	STA	PIA1_B
	LDU	#TXT_SCREEN
	LDA	#BASIC_BLANK
L2@	STA	,U+
	CMPU	#TXT_SCREEN+512
	BNE	L2@
	PULS	A
	BRA	CAS_COPY

; Show the message from the stm32f4. Any key goes back to the list
CAS_ERROR
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	LDA	#HIGHLIGHT
	STA	HIMASK
	LDB	#32
L1@	LBSR	READ_DATA
	BEQ	L2@
	LBSR	PUTC
	DECB
	BNE	L1@
L2@	TSTB
	BEQ	L4@
	LDA	#BAR
L3@	STA	,U+
	DECB
	BNE	L3@
L4@	CLR	HIMASK
L5@	LBSR	GET_KEY
	TSTA
	BEQ	L5@
	LBSR	LOAD_DIRECTORY			; the list was overwritten
	LBSR	NEW_LIST
	LBRA	MAIN_LOOP

; A = the first type byte. The program can go anywhere in RAM, over our variables and the stack
; too, so the copying only uses registers. The stack is used again once it is all in place, the
; same as BASIC does after CLOADM. Unless the program is over the stack (games with their own
; tape loader can fill all of RAM): then it is just JMPed to, and it has to set up its own.
CAS_COPY
	LDB	STACKHIT
	TFR	B,DP				; kept here while RAM is overwritten
	ORCC	#$50
	LDY	#0				; exec address of the last machine code file
L1@	CMPA	#'M'
	BNE	L4@
	GETW
	TFR	D,Y				; exec
	GETW
	TFR	D,X				; load
	GETW
	TFR	D,U				; length
	CMPU	#0
	BEQ	L3@
L2@	GETB
	STA	,X+
	LEAU	-1,U
	CMPU	#0
	BNE	L2@
L3@	GETB					; next type byte
	BRA	L1@
;
L4@	CMPA	#'S'
	LBEQ	CAS_RESUME
	TFR	DP,B				; STACKHIT
	TSTB
	BEQ	L7@
	CMPA	#'B'
	BEQ	L7@
	CMPY	#0
	LBNE	CAS_JUMP			; machine code only, over the stack
L7@	CLRB
	TFR	B,DP				; back to 0, as BASIC has it
	CMPY	#0
	BEQ	L6@
	STY	EXECJP				; EXEC on its own runs it again
L6@	CMPA	#'B'
	BEQ	CAS_BASIC
	; Machine code only. EXEC it, with BASIC to go back to if it returns
	LBSR	CAS_INTERRUPTS
	LBSR	FIND_WARM
	CMPY	#0
	BEQ	L5@
	PSHS	X
	JMP	,Y
L5@	JMP	,X

; The BASIC program goes where BASIC keeps its program, like CLOAD
CAS_BASIC
	GETW
	TFR	D,U				; length
	LDX	TXTTAB
	CMPU	#0
	BEQ	L2@
L1@	GETB
	STA	,X+
	LEAU	-1,U
	CMPU	#0
	BNE	L1@
	; Put the line links right, in case it was saved with the program somewhere else
	; (eg. with a DOS cartridge in). Stop at the 0 link at the end, or at the end of the data.
L2@	PSHS	X				; end of the program
	LDX	TXTTAB
L3@	LDD	,X
	BEQ	L6@
	LEAY	4,X				; past the link and the line number
L4@	CMPY	,S
	BHS	L6@
	LDA	,Y+
	BNE	L4@
	STY	,X
	TFR	Y,X
	BRA	L3@
L6@	LEAX	2,X
	LEAS	2,S
	STX	VARTAB
	STX	ARYTAB
	STX	ARYEND
	; Type RUN for it, through the console in hook. Only if nothing else is using the hook
	LDA	HOOK_CONSOLE_IN
	CMPA	#$39				; RTS
	BNE	L7@
	LEAX	FEED_R,PCR
	STX	HOOK_CONSOLE_IN+1
	LDA	#$7E				; JMP
	STA	HOOK_CONSOLE_IN
L7@	BSR	CAS_INTERRUPTS
	BSR	FIND_WARM
	JMP	,X				; warm start. "OK", and then RUN arrives

; X = BASIC's warm start, which keeps the program and goes to OK. A cartridge starts before
; BASIC has set RSTVEC up, so if it is not there yet, look for it in the rom.
FIND_WARM
	LDA	RSTFLG
	CMPA	#$55
	BNE	L1@
	LDX	RSTVEC
	LDA	,X
	CMPA	#$12				; NOP. BASIC's reset looks for it too
	BEQ	L9@
L1@	LDX	#DRAGON_WARM
	LDA	,X
	CMPA	#$12
	BEQ	L9@
	LDX	#COCO_WARM
	LDA	,X
	CMPA	#$12
	BEQ	L9@
	LDX	$FFFE				; not found. Cold start, and the program is lost
L9@	RTS

; Y = exec. The program is where our stack was, so nothing more may go on it. B = 1
CAS_JUMP
	CLRB
	TFR	B,DP
	STY	EXECJP
	INTERRUPTS_ON
	JMP	,Y

CAS_INTERRUPTS
	INTERRUPTS_ON
	RTS

; A tape made from a snapshot of the machine just after the real tape had loaded (see
; tools/cas_snapshot.py): all of RAM has been copied in, and this is the rest. Registers only,
; as all of RAM is the program's now:
;   length                  (not needed)
;   n, then n times: address, byte    written in that order (PIAs and SAM)
;   S PC U Y X DP B A CC    the 6809's registers
; The registers go on the program's stack the way an interrupt would leave them, and RTI
; carries on from there. That is the 12 bytes an IRQ could have put there at any time.
CAS_RESUME
	GETW
	GETW
	TFR	D,U
L1@	CMPU	#0
	BEQ	L2@
	GETW
	TFR	D,X
	GETB
	STA	,X
	LEAU	-1,U
	BRA	L1@
L2@	GETW
	TFR	D,S
	GETW					; PC
	PSHS	D
	GETW					; U
	PSHS	D
	GETW					; Y
	PSHS	D
	GETW					; X
	PSHS	D
	GETB					; DP
	PSHS	A
	GETB					; B
	PSHS	A
	GETB					; A
	PSHS	A
	GETB					; CC
	ORA	#$80				; E, so RTI takes all of them
	PSHS	A
	LDA	PIA0_B				; clear a waiting field sync IRQ
	RTI

; Before a .cas is copied: B = 1 if one of its machine code files goes where our stack is, or
; just under it where the return address and an IRQ would go. 0 if not.
STACK_COVERED
	LEAX	2,S				; the caller's stack
	STX	TMP
	LDD	#BUF_ENTRIES
	STD	IDX				; next file in the buffer
L1@	LDD	IDX
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	CMPA	#'M'
	BNE	L8@				; the end, or BASIC, or an error
	LBSR	READ_DATA			; exec
	LBSR	READ_DATA
	BSR	READ_WORD			; load
	PSHS	D
	BSR	READ_WORD			; length
	PSHS	D
	ADDD	IDX
	ADDD	#7				; 'M', exec, load, length
	STD	IDX
	PULS	D
	ADDD	,S				; end
	BCS	L2@				; past $FFFF
	ADDD	#16
	BCS	L2@
	CMPD	TMP
	BLS	L3@				; ends 16 or more under the stack
L2@	LDD	,S
	CMPD	TMP
	BLS	L9@				; starts at or under it
L3@	LEAS	2,S
	BRA	L1@
L8@	CLRB
	RTS
L9@	LEAS	2,S
	LDB	#1
	RTS

; D = the next two bytes in the buffer, big endian
READ_WORD
	LBSR	READ_DATA
	TFR	A,B
	LBSR	READ_DATA
	EXG	A,B
	RTS

; JMPed to from the console in hook. Each returns a character to whoever asked BASIC for one,
; then moves the hook on to the next. They must stay 4 bytes each.
FEED_R	LDA	#'R'
	BRA	FEED_NEXT
FEED_U	LDA	#'U'
	BRA	FEED_NEXT
FEED_N	LDA	#'N'
	BRA	FEED_NEXT
FEED_CR	LDA	#$39				; RTS. The hook is free again
	STA	HOOK_CONSOLE_IN
	LDA	#$0D
	LEAS	2,S				; skip the rest of BASIC's console in
	RTS
FEED_NEXT
	PSHS	D
	LDD	HOOK_CONSOLE_IN+1
	ADDD	#FEED_U-FEED_R
	STD	HOOK_CONSOLE_IN+1
	PULS	D
	LEAS	2,S
	RTS

; ------------------------------------------------------------------
; Find. A is the key that was typed. Add it to the prefix and move to the first entry
; whose name starts with the prefix. If there is none, the key is ignored.
FIND_TYPED
	LDB	PLEN
	CMPB	#MAX_PREFIX
	BHS	L9@
	LBSR	TO_UPPER
	LDX	#PREFIX
	STA	B,X
	INC	PLEN
	LDD	#0
	STD	IDX
L1@	LDD	IDX
	CMPD	COUNT
	BHS	L8@
	LBSR	ENTRY_ADDRESS
	ADDD	#1				; skip the type byte
	LBSR	SET_ADDRESS
	LDX	#PREFIX
	LDB	PLEN
L2@	LBSR	READ_DATA
	LBSR	TO_UPPER
	CMPA	,X+
	BNE	L3@
	DECB
	BNE	L2@
	; found it. Show it at the top of the page
	LDD	IDX
	STD	CUR
	STD	TOP
	LBRA	CLAMP_TOP
L3@	LDD	IDX
	ADDD	#1
	STD	IDX
	BRA	L1@
L8@	DEC	PLEN				; no match
L9@	RTS

; Keep CUR on the page
FIX_TOP
	LDD	CUR
	CMPD	TOP
	BHS	L1@
	STD	TOP
	RTS
L1@	LDD	TOP
	ADDD	#ROWS
	CMPD	CUR
	BHI	L2@
	LDD	CUR
	SUBD	#ROWS-1
	STD	TOP
L2@	RTS

; Don't scroll past the last full page
CLAMP_TOP
	LDD	COUNT
	SUBD	#ROWS
	BCC	L1@
	LDD	#0
L1@	STD	TMP
	LDD	TOP
	CMPD	TMP
	BLS	L2@
	LDD	TMP
	STD	TOP
L2@	RTS

; ------------------------------------------------------------------
; Drawing

CLEAR_SCREEN
	LDU	#TXT_SCREEN
	LDA	#BLACK
	LDB	#0				; 256 x 2 bytes
L1@	STA	,U+
	STA	,U+
	DECB
	BNE	L1@
	RTS

CLEAR_LIST
	LDU	#TXT_SCREEN+(LIST_ROW*32)
	LDA	#ROWS
	STA	ROW
L1@	LBSR	BLANK_ROW
	DEC	ROW
	BNE	L1@
	RTS

BLANK_ROW
	LDA	#BLACK
	LDB	#32
L1@	STA	,U+
	DECB
	BNE	L1@
	RTS

DRAW_HEADER
	LDU	#TXT_SCREEN
	LDA	#BAR
	LDB	#32
L1@	STA	,U+
	DECB
	BNE	L1@
;
	LDA	#HIGHLIGHT
	STA	HIMASK
	LDU	#TXT_SCREEN+1
	LEAX	TITLE,PCR
	LBSR	PUTS
	TST	MODE
	BNE	L2@
	LDA	#'C'				; KCCFS on the Coco. 'C' is also its black-on-light screen code
	STA	TXT_SCREEN+3
;
L2@	LDU	#TXT_SCREEN+15
	LEAX	MSG_FW,PCR
	LBSR	PUTS
	LDD	#BUF_VERSION
	LBSR	SET_ADDRESS
	LDB	#7				; up to "1.2.123", read from the stm32f4
L3@	LBSR	READ_DATA
	TSTA
	BEQ	L4@
	LBSR	PUTC
	DECB
	BNE	L3@
;
L4@	LDU	#TXT_SCREEN+26
	LEAX	MSG_DRAGON,PCR
	TST	MODE
	BNE	L5@
	LDU	#TXT_SCREEN+28
	LEAX	MSG_COCO,PCR
L5@	LBSR	PUTS
	CLR	HIMASK
;
	LBSR	DRAW_DRIVE_B
	LDU	#TXT_SCREEN+(14*32)
	LDB	#$0C				; upper half blocks
	LBRA	STRIPE

; The row under the list shows the folder we are in, or a stripe at the top
DRAW_PATH
	LDU	#TXT_SCREEN+(14*32)
	LDD	#BUF_PATH
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	BNE	L1@
	LDB	#$0C				; upper half blocks
	BRA	STRIPE
L1@	CLR	HIMASK
	LDB	#32
L2@	LBSR	PUTC
	DECB
	BEQ	L4@
	LBSR	READ_DATA
	BNE	L2@
	LDA	#BLACK
L3@	STA	,U+
	DECB
	BNE	L3@
L4@	RTS

; The row under the title shows the disk in drive B, or a stripe if there is none
DRAW_DRIVE_B
	LDU	#TXT_SCREEN+32
	LDD	#BUF_DRIVE_B
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	BNE	L1@
	LDB	#$03				; lower half blocks
	BRA	STRIPE
L1@	PSHS	A
	CLR	HIMASK
	LEAX	MSG_DRIVE_B,PCR
	LBSR	PUTS
	PULS	A
	LDB	#32-3
L2@	LBSR	PUTC
	DECB
	BEQ	L4@
	LBSR	READ_DATA
	BNE	L2@
	LDA	#BLACK
L3@	STA	,U+
	DECB
	BNE	L3@
L4@	RTS

; A row of coloured semigraphics. U = start of the row, B = which quadrants are lit
STRIPE
	STB	TMP
	CLRB
L1@	TFR	B,A
	LSRA
	LSRA					; 4 characters per colour
	LEAX	STRIPE_COLOURS,PCR
	LDA	A,X
	ORA	TMP
	STA	,U+
	INCB
	CMPB	#32
	BNE	L1@
	RTS

DRAW_LIST
	LDU	#TXT_SCREEN+(LIST_ROW*32)
	LDD	TOP
	STD	IDX
	LDA	#ROWS
	STA	ROW
L1@	LDD	IDX
	CMPD	COUNT
	BHS	L3@
	CLR	HIMASK
	CMPD	CUR
	BNE	L2@
	LDA	#HIGHLIGHT
	STA	HIMASK
	LDD	IDX
L2@	LBSR	DRAW_ENTRY
	BRA	L4@
L3@	LBSR	BLANK_ROW
L4@	LDD	IDX
	ADDD	#1
	STD	IDX
	DEC	ROW
	BNE	L1@
	CLR	HIMASK
	RTS

; D = entry number, U = where on the screen. Draws 32 characters: the coloured mark, a space,
; then the name. With SHOWSIZE the last 6 are the size, eg. "  180K"
DRAW_ENTRY
	LBSR	ENTRY_ADDRESS
	STD	EADDR
	LBSR	SET_ADDRESS
	LBSR	READ_DATA			; type byte
	STA	ETYPE
	LDB	#DISK_MARK
	CMPA	#TYPE_DISK
	BEQ	L2@
	LDB	#ROM_MARK
	CMPA	#TYPE_ROM
	BEQ	L2@
	LDB	#CAS_MARK
	CMPA	#TYPE_CAS
	BEQ	L2@
	LDB	#FOLDER_MARK			; folder or ".."
L2@	STB	,U+
	LDA	#BLACK
	ORA	HIMASK
	STA	,U+
	LDB	#30
	TST	SHOWSIZE
	BEQ	L3@
	LDB	#24
L3@	LBSR	READ_DATA
	TSTA
	BEQ	L4@
	LBSR	PUTC
	DECB
	BNE	L3@
	BRA	L6@
L4@	LDA	#BLACK				; pad the rest of the name
	ORA	HIMASK
L5@	STA	,U+
	DECB
	BNE	L5@
L6@	TST	SHOWSIZE
	BEQ	L9@
	LDA	#' '
	LBSR	PUTC
	LDA	ETYPE
	CMPA	#TYPE_FOLDER
	BEQ	L7@
	CMPA	#TYPE_UP
	BEQ	L7@
	LDD	EADDR
	ADDD	#ENTRY_SIZE_K
	LBSR	SET_ADDRESS
	LBSR	READ_DATA			; low byte
	TFR	A,B
	LBSR	READ_DATA			; high byte
	CMPD	#9999
	BLS	L8@
	LDD	#9999
L8@	LBSR	PRINT_DEC4
	LDA	#'K'
	LBRA	PUTC
L7@	LDB	#5				; no size for folders
	LDA	#BLACK
	ORA	HIMASK
L1@	STA	,U+
	DECB
	BNE	L1@
L9@	RTS

DRAW_STATUS
	LDU	#TXT_SCREEN+(STATUS_ROW*32)
	CLR	HIMASK
	LDD	CUR
	ADDD	#1
	LBSR	PRINT_DEC4
	LDA	#'/'
	LBSR	PUTC
	LDD	COUNT
	LBSR	PRINT_DEC4
	LDA	#' '
	LBSR	PUTC
	LBSR	PUTC
	TST	PLEN
	BNE	L1@
	LEAX	MSG_HINT,PCR
	LBSR	PUTS
	BRA	L3@
L1@	LEAX	MSG_FIND,PCR
	LBSR	PUTS
	LDX	#PREFIX
	LDB	PLEN
L2@	LDA	,X+
	LBSR	PUTC
	DECB
	BNE	L2@
	LDA	#BAR				; cursor
	STA	,U+
L3@	CMPU	#TXT_SCREEN+((STATUS_ROW+1)*32)
	BHS	L4@
	LDA	#BLACK
	STA	,U+
	BRA	L3@
L4@	RTS

; Print D as 4 digits, leading zeros as spaces. U = where on the screen
PRINT_DEC4
	STD	TMP
	LEAX	POWERS_OF_TEN,PCR
	CLR	LEAD
L1@	LDA	#'0'
	STA	DIGIT
L2@	LDD	TMP
	SUBD	,X
	BCS	L3@
	STD	TMP
	INC	DIGIT
	BRA	L2@
L3@	LDA	DIGIT
	CMPA	#'0'
	BNE	L4@
	TST	LEAD
	BNE	L5@
	LDD	,X
	CMPD	#1				; always print the last digit
	BEQ	L5@
	LDA	#' '
	BRA	L6@
L4@	INC	LEAD
L5@	LDA	DIGIT
L6@	LBSR	PUTC
	LDD	,X++
	CMPD	#1
	BNE	L1@
	RTS

; Print the 0 terminated string at X. U = where on the screen
PUTS
	LDA	,X+
	BEQ	L1@
	LBSR	PUTC
	BRA	PUTS
L1@	RTS

; Put the ASCII character in A on the screen at U, in the colours HIMASK says
PUTC
	LBSR	TO_SCREEN
	ORA	HIMASK
	STA	,U+
	RTS

; ASCII to a light-on-black screen code. Lower case is shown as upper case
TO_SCREEN
	CMPA	#$20
	BLO	L8@
	CMPA	#$7F
	BHS	L8@
	CMPA	#$60
	BLO	L1@
	SUBA	#$20
L1@	ANDA	#$3F
	RTS
L8@	LDA	#'?'
	RTS

TO_UPPER
	CMPA	#'a'
	BLO	L1@
	CMPA	#'z'
	BHI	L1@
	SUBA	#$20
L1@	RTS

; D = entry number -> D = its address in the stm32f4 buffer
ENTRY_ADDRESS
	ASLB
	ROLA
	ASLB
	ROLA
	ASLB
	ROLA
	ASLB
	ROLA
	ASLB
	ROLA
	ADDD	#BUF_ENTRIES
	RTS

; ------------------------------------------------------------------
; Both dragon.rom and coco.rom are on the SD card: ask which BASIC to start. The stm32f4 makes
; the one chosen ready for NEW_BASIC, and the other machine's ROM changes the mode (folder, DOS
; and the keyboard routine used after the reset) as well. 3 keeps the computer's own
CHOOSE_BASIC
	LBSR	BASIC_CHOICE_NEEDED
	BEQ	L9@
	LBSR	CLEAR_SCREEN
	LBSR	DRAW_HEADER
	CLR	HIMASK
	LDU	#TXT_SCREEN+(5*32)+10
	LEAX	MSG_WHICH_BASIC,PCR
	LBSR	PUTS
	LDU	#TXT_SCREEN+(7*32)+10
	LEAX	MSG_BASIC_1,PCR
	LBSR	PUTS
	LDU	#TXT_SCREEN+(8*32)+10
	LEAX	MSG_BASIC_2,PCR
	LBSR	PUTS
	LDU	#TXT_SCREEN+(9*32)+10
	LEAX	MSG_BASIC_3,PCR
	LBSR	PUTS
L1@	LBSR	GET_KEY
	SUBA	#'1'
	CMPA	#2
	BHI	L1@				; not 1-3 (0 for no key wraps round too)
	INCA					; 1 Dragon, 2 Coco
	CMPA	#3
	BNE	L2@
	CLRA					; 0 built in
L2@	PSHS	A
	LDD	#BUF_BASIC_CHOICE
	LBSR	SET_ADDRESS
	PULS	A
	LBSR	WRITE_DATA
	LBSR	SEND_BASIC_CHOICE
L9@	RTS

; ------------------------------------------------------------------
; Keyboard. Returns the key in A, or 0 if none
GET_KEY
	TST	MODE
	BEQ	L1@
	JMP	DRAGON_POLCAT
L1@	JMP	[COCO_POLCAT]

; ------------------------------------------------------------------
; Everything that talks to the stm32f4 is below here

 IFNDEF SIM

; MODE = 1 for Dragon, 0 for Coco. Bit 6 of the command register is the jumper on the board
GET_MODE
	CLR	MODE
	LDA	MENU_CTRL_COMMAND
	BITA	#$40
	BEQ	L1@
	INC	MODE
L1@	RTS

; Bit 5 of the command register: the stm32f4 has a BASIC ROM of its own (dragon.rom or coco.rom
; on the SD card) that takes over when the reset vector is next read. Cold start into it
NEW_BASIC
	LDA	MENU_CTRL_COMMAND
	BITA	#$20
	BEQ	L1@
	ORCC	#$50
	CLR	RSTFLG			; not a warm start
	JMP	[$FFFE]
L1@	RTS

; Not equal (Z clear) if the menu has to ask which BASIC ROM to use (see CHOOSE_BASIC)
BASIC_CHOICE_NEEDED
	LDA	MENU_CTRL_COMMAND
	BITA	#STATUS_BASIC_CHOOSE
	RTS

; The answer, written to BUF_BASIC_CHOICE. Waits until the ROM is loaded
SEND_BASIC_CHOICE
	LDA	#COMMAND_CHOOSE_BASIC
	BRA	COMMAND_AND_WAIT

; Going to BASIC. The stm32f4 stops toggling CART until the next reset
SEND_GO_BASIC
	LDA	#COMMAND_GO_BASIC
	BRA	COMMAND_AND_WAIT

; The entry in BUF_SELECTED goes in drive B (or out). BUF_DRIVE_B then has its name
SEND_DRIVE_B
	LDA	#COMMAND_DRIVE_B
	BRA	COMMAND_AND_WAIT

; Ask for the sorted list and wait until it is ready (bit 7 goes low)
LOAD_DIRECTORY
	LDA	#COMMAND_LOAD_DIRECTORY
	BRA	COMMAND_AND_WAIT

; Ask for the .cas whose entry number has been written to BUF_SELECTED to be made ready
; (see RUN_CAS). It is the rom command, but the stm32f4 leaves the rom alone for a .cas
LOAD_CAS
	LDA	#COMMAND_LOAD_ROM
	BRA	COMMAND_AND_WAIT

; Ask for the cover of the entry in BUF_SELECTED (BUF_COVER_OK says if there is one)
LOAD_COVER
	LDA	#COMMAND_LOAD_COVER
	BRA	COMMAND_AND_WAIT

; Open the folder whose entry number has been written to BUF_SELECTED, and wait for its list
CHANGE_FOLDER
	LDA	#COMMAND_CHANGE_DIR
COMMAND_AND_WAIT
	STA	MENU_CTRL_COMMAND
	NOP
	NOP
L1@	LDA	MENU_CTRL_COMMAND
	BMI	L1@
	RTS

GET_FILE_COUNT
	LDB	MENU_CTRL_FILE_COUNT		; little endian
	LDA	MENU_CTRL_FILE_COUNT+1
	RTS

; D = address in the stm32f4 buffer. Little endian
SET_ADDRESS
	STB	MENU_CTRL_ADDRESS
	STA	MENU_CTRL_ADDRESS+1
	RTS

; Next byte from the buffer. The address register counts up by itself
READ_DATA
	LDA	MENU_CTRL_DATA
	RTS

WRITE_DATA
	STA	MENU_CTRL_DATA
	RTS

 ELSE

; Stand-ins for trying the menu out in an emulator. SIM_COUNT entries made from the
; names in SIM_TABLE over and over.
SIM_COUNT		EQU	700

GET_MODE
	LDA	#1
	STA	MODE
	RTS

NEW_BASIC
	RTS

; Build with -D SIM_ASK_BASIC as well to see the question
BASIC_CHOICE_NEEDED
 IFDEF SIM_ASK_BASIC
	ANDCC	#$FB				; Z clear: ask
 ELSE
	ORCC	#$04				; Z set: do not
 ENDC
	RTS

SEND_BASIC_CHOICE
SEND_GO_BASIC
SEND_DRIVE_B
	RTS

LOAD_DIRECTORY
	CLR	SIMCAS
	RTS

CHANGE_FOLDER
	RTS

; The first letter of the name picks one of the made up .cas files below: M, B or E
LOAD_CAS
	LDD	CUR
	LBSR	ENTRY_ADDRESS
	ADDD	#1
	LBSR	SET_ADDRESS
	LBSR	READ_DATA
	STA	SIMCAS
	RTS

GET_FILE_COUNT
	LDD	#SIM_COUNT
	RTS

; The SIM cover is whatever is in a second rom at $E000 (eg. XRoar -cart-rom2 Game.pm4)
LOAD_COVER
	RTS

SET_ADDRESS
	PSHS	D,X
	CMPD	#BUF_COVER
	BLO	L6@
	SUBD	#BUF_COVER
	LDX	#$E000
	BRA	L3@
L6@	CMPD	#BUF_ENTRIES
	BHS	L1@
	LEAX	SIM_LOW,PCR
	BRA	L3@
L1@	SUBD	#BUF_ENTRIES
	PSHS	A
	LDA	SIMCAS
	LEAX	SIM_CAS_MC,PCR
	CMPA	#'M'
	BEQ	L5@
	LEAX	SIM_CAS_BASIC,PCR
	CMPA	#'B'
	BEQ	L5@
	LEAX	SIM_CAS_GUS,PCR
	CMPA	#'G'
	BEQ	L5@
	LEAX	SIM_CAS_RESUME,PCR
	CMPA	#'R'
	BEQ	L5@
	LEAX	SIM_CAS_ERROR,PCR
	CMPA	#'E'
L5@	PULS	A
	BEQ	L3@
L2@	CMPD	#SIM_ENTRIES*32
	BLO	L4@
	SUBD	#SIM_ENTRIES*32
	BRA	L2@
L4@	LEAX	SIM_TABLE,PCR
L3@	LEAX	D,X
	STX	SIMPTR
	PULS	D,X,PC

READ_DATA
	PSHS	X
	LDX	SIMPTR
	LDA	,X+
	STX	SIMPTR
	PULS	X
	TSTA
	RTS

WRITE_DATA
	RTS

SIM_LOW
	FCB	0,0,3,0			; start on entry 3
	FCB	1				; there is a cover
	ZMB	SIM_LOW+BUF_VERSION-*
	FCC	/1.2.SIM/
	FCB	0
	ZMB	SIM_LOW+BUF_PATH-*
	FCC	"/GAMES/Arcade"
	FCB	0
	ZMB	SIM_LOW+BUF_DRIVE_B-*
	FCC	"Data disk"
	FCB	0

SIM_TABLE
SIM_E	SET	*
	FCC	/U../
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	0,0
SIM_E	SET	*
	FCC	/FAdventure/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	0,0
SIM_E	SET	*
	FCC	/FShoot em up/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	0,0
SIM_E	SET	*
	FCC	/CMC TEST/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	1,0
SIM_E	SET	*
	FCC	/CBASIC TEST/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	1,0
SIM_E	SET	*
	FCC	/CERROR TEST/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	1,0
SIM_E	SET	*
	FCC	/CGUS TEST/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	32,0
SIM_E	SET	*
	FCC	/CRESUME TEST/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	32,0
SIM_E	SET	*
	FCC	/RAIRPORT/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	8,0
SIM_E	SET	*
	FCC	/DANDROID ATTACK/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/RBERSERK/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	16,0
SIM_E	SET	*
	FCC	/DBUZZARD BAIT/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/RCAVE HUNTER/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	8,0
SIM_E	SET	*
	FCC	/DCHUCKIE EGG/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/RCUTHBERT GOES WALKABOUT/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	16,0
SIM_E	SET	*
	FCC	/DDANGER RANGER/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/DDOWNLAND/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/RDRAGON CHESS/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	16,0
SIM_E	SET	*
	FCC	/DDUNGEONS OF DAGGORATH/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	104,1
SIM_E	SET	*
	FCC	/DFROGGER/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/DHUNCHBACK/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/DJET SET WILLY/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/RMEGABUG/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	8,0
SIM_E	SET	*
	FCC	/DNitrOS-9 Level 2 v3.3.0 boot/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	208,2
SIM_E	SET	*
	FCC	/DOS9 Level 1 Dragon64/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	104,1
SIM_E	SET	*
	FCC	/RPolaris/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	8,0
SIM_E	SET	*
	FCC	/DSpace Wrek/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/DZaxxon/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	180,0
SIM_E	SET	*
	FCC	/CCHUCKIE EGG TAPE/
	ZMB	SIM_E+ENTRY_SIZE_K-*
	FCB	12,0
SIM_ENTRIES		EQU	(*-SIM_TABLE)/32

; What the stm32f4 would make of three .cas files. Machine code that writes on the screen and
; stays there, a BASIC program with wrong line links, and one that failed.
SIM_CAS_MC
	FCB	'M'
	FDB	$3000,$3000,SIM_MC_END-SIM_MC_CODE
SIM_MC_CODE					; runs at $3000, so only relative addressing
	LDX	#TXT_SCREEN+(8*32)
	LEAY	SIM_MC_MSG,PCR
L1@	LDA	,Y+
	BEQ	L2@
	STA	,X+
	BRA	L1@
L2@	BRA	L2@
SIM_MC_MSG
	FCC	/MACHINE CODE LOADED AND RUN/
	FCB	0
SIM_MC_END
	FCB	0

SIM_CAS_BASIC
	FCB	'B'
	FDB	SIM_BASIC_END-SIM_BASIC
SIM_BASIC
	FDB	$1234,10			; link (wrong on purpose), line number
	FCB	$87				; PRINT
	FCC	/"BASIC LOADED AND RUN"/
	FCB	0
	FDB	$1234,20
	FCB	$87
	FCC	/"LINE 20"/
	FCB	0
	FDB	0				; the end
SIM_BASIC_END
	FCB	0

; Like Galactic Gus: fills RAM to $7FDA, over the stack, and sets up its own. Shows how many
; bytes of the fill were changed. The SIM build's own READ_DATA calls leave 2.
SIM_GUS_FILL		EQU	$7E00
SIM_GUS_LEN		EQU	$7FDB-SIM_GUS_FILL
SIM_CAS_GUS
	FCB	'M'
	FDB	0,SIM_GUS_FILL,SIM_GUS_LEN
	FILL	$A5,SIM_GUS_LEN
	FCB	'M'
	FDB	$3000,$3000,SIM_GUS_END-SIM_GUS_CODE
SIM_GUS_CODE					; runs at $3000
	LDS	#$0600
	LDX	#SIM_GUS_FILL
	CLRB
L1@	LDA	,X+
	CMPA	#$A5
	BEQ	L2@
	INCB
L2@	CMPX	#SIM_GUS_FILL+SIM_GUS_LEN
	BNE	L1@
	LDX	#TXT_SCREEN+(8*32)
	LEAY	SIM_GUS_MSG,PCR
L3@	LDA	,Y+
	BEQ	L4@
	STA	,X+
	BRA	L3@
L4@	TFR	B,A
	LSRA
	LSRA
	LSRA
	LSRA
	BSR	SIM_GUS_HEX
	TFR	B,A
	BSR	SIM_GUS_HEX
L5@	BRA	L5@
SIM_GUS_HEX
	ANDA	#$0F
	ADDA	#'0'
	CMPA	#'9'
	BLS	L9@
	ADDA	#'A'-'9'-1
L9@	STA	,X+
	RTS
SIM_GUS_MSG
	FCC	/OVER THE STACK. CHANGED $/
	FCB	0
SIM_GUS_END
	FCB	0

; A snapshot tape: a program, then the state to start it from. It keeps the registers it gets
; at $3100 (A B X Y U DP CC S) and stops
SIM_CAS_RESUME
	FCB	'M'
	FDB	0,$3000,SIM_RESUME_END-SIM_RESUME_CODE
SIM_RESUME_CODE
	PSHS	CC
	STA	$3100
	STB	$3101
	STX	$3102
	STY	$3104
	STU	$3106
	TFR	DP,A
	STA	$3108
	PULS	A
	STA	$3109
	STS	$310A
L1@	BRA	L1@
SIM_RESUME_END
	FCB	'S'
	FDB	SIM_STATE_END-SIM_STATE
SIM_STATE
	FDB	1				; one write: CSS 1
	FDB	PIA1_B
	FCB	$08
	FDB	$0600,$3000,$3333,$2222,$1111	; S PC U Y X
	FCB	$05,$34,$12,$50			; DP B A CC
SIM_STATE_END
	FCB	0

SIM_CAS_ERROR
	FCB	'E'
	FCC	/CHECKSUM ERROR IN CAS FILE/
	FCB	0

 ENDC

; ------------------------------------------------------------------

TITLE		FCC	/KCDFS 2.1./		; menu version. Bump this for new features
		INCLUDE	build.inc		; then the build number, counted up by build.sh
		FCB	0
MSG_DRIVE_B	FCC	/B: /
		FCB	0
MSG_FW		FCC	/FW /
		FCB	0
MSG_LOADING	FCC	/LOADING.../
		FCB	0
MSG_DRAGON	FCC	/DRAGON/
		FCB	0
MSG_COCO	FCC	/COCO/
		FCB	0
MSG_READING	FCC	/READING SD CARD.../
		FCB	0
MSG_NO_FILES	FCC	/NO ROMS OR DISKS IN /
		FCB	0
MSG_DRAGON_DIR	FCC	/DRAGON/
		FCB	0
MSG_TANDY_DIR	FCC	/TANDY/
		FCB	0
MSG_HINT	FCC	/SPACE SIZE  @ COVER/
		FCB	0
MSG_NO_COVER	FCC	/NO COVER FOR THIS ONE/
		FCB	0
MSG_FIND	FCC	/FIND:/
		FCB	0
MSG_WHICH_BASIC	FCC	/WHICH BASIC?/
		FCB	0
MSG_BASIC_1	FCC	/1  DRAGON.ROM/
		FCB	0
MSG_BASIC_2	FCC	/2  COCO.ROM/
		FCB	0
MSG_BASIC_3	FCC	/3  BUILT IN/
		FCB	0
 IFDEF SIM
MSG_SIM_SELECTED FCC	/SIM: WOULD START ENTRY /
		FCB	0
 ENDC

; colour << 4 | $80 for the stripes: red, orange, yellow, green, cyan, blue, magenta, buff
STRIPE_COLOURS	FCB	$B0,$F0,$90,$80,$D0,$A0,$E0,$C0

POWERS_OF_TEN	FDB	1000,100,10,1

	ZMB	$E000-*				; pad to 8K
