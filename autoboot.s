; Autoboot for DragonDOS disks.
;
; When a disk is presented the stm32f4 puts this at $E000 (the upper half of the cartridge space,
; which an 8K DOS rom does not use), changes the JMP $8371 at the end of DragonDOS's start up to
; JMP $E000, and puts the command to type after CMD, ending with $0D (see autoboot_patch() in
; autoboot.c). This then types the command for BASIC through the console in hook, as if it had
; been typed at the OK.
;
; autoboot_stub.h is made from this with
;   lwasm -f raw -o autoboot.bin autoboot.s
; and the bytes copied in. Update AUTOBOOT_CMD_OFFSET there if CMD moves.

	PRAGMA	6809

HOOK_CONSOLE_IN		EQU	$016A		; RAM hook. JSRed each time BASIC wants a character
SAVE			EQU	$01DA		; DOS's hook while ours is in. Cassette buffer, not
PTR			EQU	$01DD		; used while booting. Next character to type
BASIC_OK		EQU	$8371		; where DragonDOS was going: BASIC's OK

	ORG	$E000

START
	LDA	HOOK_CONSOLE_IN
	STA	SAVE
	LDX	HOOK_CONSOLE_IN+1
	STX	SAVE+1
	LEAX	CMD,PCR
	STX	PTR
	LEAX	FEED,PCR
	STX	HOOK_CONSOLE_IN+1
	LDA	#$7E				; JMP
	STA	HOOK_CONSOLE_IN
	JMP	BASIC_OK

; One character of the command back to whoever asked BASIC for one. After the $0D at the end the
; DOS hook goes back in.
FEED
	PSHS	B,X
	LDX	PTR
	LDA	,X+
	STX	PTR
	CMPA	#$0D
	BNE	L1@
	LDB	SAVE
	STB	HOOK_CONSOLE_IN
	LDX	SAVE+1
	STX	HOOK_CONSOLE_IN+1
L1@	PULS	B,X
	LEAS	2,S				; skip the rest of BASIC's console in
	RTS

CMD						; the command goes here
