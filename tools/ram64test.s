; RAM64TEST - tries out the emulated upper RAM (ram64.on on the SD card, see ram64_on_write in
; interrupt.S). Start RAM64TEST.cas from the menu.
;
; It turns the emulated RAM on ($FF5F), fills $8000-$FEFF with a pattern, reads it back, does it
; again with the pattern inverted, and turns it off again ($FF5E). Meanwhile $1000-$2FFF holds a
; pattern of its own: if writes to the upper 32K also reached the computer's own RAM, they would
; show up there. Then it checks the BASIC ROM is back, and prints what it found.
;
;   lwasm -f raw -o ram64test.bin ram64test.s
;   python bin2cas.py ram64test.bin RAM64TEST.cas RAM64TST 3000 3000
;
; With -D SAM it uses the SAM's own $FFDF/$FFDE instead, to try the program out on a machine with
; 64K of real RAM (eg. XRoar -machine dragon32 -ram 64).

	PRAGMA	6809

 IFDEF SAM
RAM_ON		EQU	$FFDF
RAM_OFF		EQU	$FFDE
 ELSE
RAM_ON		EQU	$FF5F
RAM_OFF		EQU	$FF5E
 ENDC
RAM_STATE	EQU	$FF5E		; bit 0: there is emulated RAM, bit 1: it is on
CHROUT		EQU	$A002		; BASIC's output a character, on the Dragon and the Coco
LOW_START	EQU	$1000
LOW_END		EQU	$3000
HIGH_START	EQU	$8000
HIGH_END	EQU	$FF00

	ORG	$3000

START
	ORCC	#$50			; no interrupts: their vectors go through the ROM
	LDX	#0
	STX	HIGH_ERRORS
	STX	LOW_ERRORS
	LEAX	MSG_TITLE,PCR
	LBSR	PUTS
 IFNDEF SAM
	LDA	RAM_STATE
	ANDA	#1
	BNE	L1@
	LEAX	MSG_NOT_ON,PCR
	LBSR	PUTS
	ANDCC	#$AF
	RTS
 ENDC
L1@	LDX	HIGH_START		; the first ROM bytes, to see the ROM comes back
	STX	ROM_WORD
	LDA	#$5A			; our own pattern in low RAM
	STA	MASK
	LDX	#LOW_START
	LDU	#LOW_END
	LBSR	FILL

	STA	RAM_ON
	CLR	MASK
	LDX	#HIGH_START
	LDU	#HIGH_END
	LBSR	FILL
	LDX	#HIGH_START
	LDU	#HIGH_END
	LDY	#HIGH_ERRORS
	LBSR	CHECK
	COM	MASK			; and the other way round, every bit
	LDX	#HIGH_START
	LDU	#HIGH_END
	LBSR	FILL
	LDX	#HIGH_START
	LDU	#HIGH_END
	LDY	#HIGH_ERRORS
	LBSR	CHECK
 IFDEF HOLD
L9@	BRA	L9@				; for looking at it in a debugger
 ENDC
	STA	RAM_OFF

	LDA	#$5A
	STA	MASK
	LDX	#LOW_START
	LDU	#LOW_END
	LDY	#LOW_ERRORS
	LBSR	CHECK
	ANDCC	#$AF

	LEAX	MSG_HIGH,PCR
	LDY	#HIGH_ERRORS
	LBSR	PUT_RESULT
	LEAX	MSG_LOW,PCR
	LDY	#LOW_ERRORS
	LBSR	PUT_RESULT
	LEAX	MSG_ROM,PCR
	LBSR	PUTS
	LEAX	MSG_OK,PCR
	LDD	HIGH_START
	CMPD	ROM_WORD
	BEQ	L2@
	LEAX	MSG_GONE,PCR
L2@	LBSR	PUTS
	LEAX	MSG_PASSED,PCR
	LDD	HIGH_ERRORS
	ADDD	LOW_ERRORS
	BNE	L3@
	LDD	HIGH_START
	CMPD	ROM_WORD
	BEQ	L4@
L3@	LEAX	MSG_FAILED,PCR
L4@	LBRA	PUTS

; X..U-1 gets the pattern: low byte of the address EOR high byte EOR MASK
FILL
	PSHS	A
L1@	TFR	X,D
	PSHS	A
	EORB	,S+
	EORB	MASK
	STB	,X+
	PSHS	U
	CMPX	,S++
	BNE	L1@
	PULS	A,PC

; Read X..U-1 back. Each byte that is not the pattern adds one to the count at Y. The first one's
; address goes at Y+2, and what was read there at Y+4
CHECK
	PSHS	A
L1@	TFR	X,D
	PSHS	A
	EORB	,S+
	EORB	MASK
	CMPB	,X+
	BEQ	L2@
	LDD	,Y
	BNE	L3@
	LEAX	-1,X
	STX	2,Y
	LDB	,X+
	STB	4,Y
	LDD	,Y
L3@	ADDD	#1
	STD	,Y
L2@	PSHS	U
	CMPX	,S++
	BNE	L1@
	PULS	A,PC

; The message at X, then the count of errors at Y, and the first one: " AT $1234=$56"
PUT_RESULT
	PSHS	Y				; BASIC's output does not keep Y
	LBSR	PUTS
	LDY	,S
	LDD	,Y
	LBSR	PUT_HEX4
	LDY	,S
	LDD	,Y
	BEQ	L1@
	LEAX	MSG_AT,PCR
	LBSR	PUTS
	LDY	,S
	LDD	2,Y
	LBSR	PUT_HEX4
	LEAX	MSG_WAS,PCR
	LBSR	PUTS
	LDY	,S
	LDA	4,Y
	LBSR	PUT_HEX2
L1@	PULS	Y,PC

; Print the 0 terminated string at X
PUTS
	LDA	,X+
	BEQ	L1@
	JSR	[CHROUT]
	BRA	PUTS
L1@	RTS

; Print D as 4 hex digits
PUT_HEX4
	PSHS	B
	BSR	PUT_HEX2
	PULS	A
PUT_HEX2
	PSHS	A
	LSRA
	LSRA
	LSRA
	LSRA
	BSR	PUT_HEX1
	PULS	A
PUT_HEX1
	ANDA	#$0F
	ADDA	#'0'
	CMPA	#'9'
	BLS	L1@
	ADDA	#'A'-'9'-1
L1@	JMP	[CHROUT]

MSG_TITLE	FCC	/UPPER RAM TEST/
		FCB	13,0
MSG_NOT_ON	FCC	/NO EMULATED RAM. PUT A FILE/
		FCB	13
		FCC	/CALLED RAM64.ON ON THE SD CARD/
		FCB	13
		FCC	/AND PRESS RESET MCU/
		FCB	13,0
MSG_HIGH	FCC	/8000-FEFF ERRORS /
		FCB	0
MSG_LOW		FCB	13
		FCC	/1000-2FFF ERRORS /
		FCB	0
MSG_AT		FCC	/ AT /
		FCB	0
MSG_WAS		FCC	/=/
		FCB	0
MSG_ROM		FCB	13
		FCC	/BASIC ROM BACK: /
		FCB	0
MSG_OK		FCC	/YES/
		FCB	0
MSG_GONE	FCC	/NO/
		FCB	0
MSG_PASSED	FCB	13
		FCC	/PASSED/
		FCB	13,0
MSG_FAILED	FCB	13
		FCC	/FAILED/
		FCB	13,0

MASK		RMB	1
ROM_WORD	RMB	2
HIGH_ERRORS	RMB	2		; count, first address, what was there
		RMB	3
LOW_ERRORS	RMB	2
		RMB	3
