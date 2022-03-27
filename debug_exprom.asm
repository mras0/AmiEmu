RomStart=0

VERSION=0
REVISION=1

OP_INIT=1
OP_ADDTASK=2
OP_REMTASK=3

RT_MATCHWORD=$00		; UWORD word to match on (ILLEGAL)
RT_MATCHTAG=$02			; APTR  pointer to the above (RT_MATCHWORD)
RT_ENDSKIP=$06			; APTR  address to continue scan
RT_FLAGS=$0A			; UBYTE various tag flags
RT_VERSION=$0B			; UBYTE release version number
RT_TYPE=$0C			; UBYTE type of module (NT_XXXXXX)
RT_PRI=$0D			; BYTE  initialization priority
RT_NAME=$0E			; APTR  pointer to node name
RT_IDSTRING=$12			; APTR  pointer to identification string
RT_INIT=$16			; APTR  pointer to init code
RT_SIZE=$1A

RTC_MATCHWORD=$4afc
RTF_AUTOINIT=$80	; rt_Init points to data structure
RTF_COLDSTART=$01

NT_UNKNOWN=0
NT_TASK=1
NT_INTERRUPT=2
NT_DEVICE=3
NT_MSGPORT=4
NT_MESSAGE=5
NT_FREEMSG=6
NT_REPLYMSG=7
NT_RESOURCE=8
NT_BOOTNODE=16

; exec.library
_LVOFindResident=-96
_LVOForbid=-132
_LVOPermit=-138
_LVOAllocMem=-198
_LVOFreeMem=-210
_LVOAddHead=-240
_LVOAddTail=-246
_LVOEnqueue=-270
_LVOAddTask=-282
_LVORemTask=-288
_LVOReplyMsg=-378
_LVOCloseLibrary=-414
_LVOAddResource=-486
_LVOOpenResource=-498
_LVOOpenLibrary=-552

ResourceList=$150

; expansion.library
_LVOAddBootNode=-36
_LVOGetCurrentBinding=-138
_LVOMakeDosNode=-144
_LVOAddDosNode=-150

eb_MountList=74


INITBYTE=$e000
INITWORD=$d000
INITLONG=$c000

LN_SUCC=$00
LN_PRED=$04
LN_TYPE=$08
LN_PRI=$09
LN_NAME=$0A
LN_SIZE=$0E

LH_HEAD=$00         	; APTR	
LH_TAIL=$04         	; APTR	
LH_TAILPRED=$08     	; APTR	
LH_TYPE=$0C         	; UBYTE	
LH_SIZE=$0E

LIB_FLAGS=$0E ; LN_SIZE
LIB_NEGSIZE=$10
LIB_POSSIZE=$12
LIB_VERSION=$14
LIB_REVISION=$16
LIB_IDSTRING=$18
LIB_SUM=$1C
LIB_OPENCNT=$20
LIB_SIZE=$22

LIBF_SUMMING=$01	; we are currently checksumming
LIBF_CHANGED=$02	; we have just changed the lib
LIBF_SUMUSED=$04	; set if we should bother to sum
LIBF_DELEXP=$08         ; delayed expunge

; ConfigDev
cd_Node=$00
cd_Flags=$0E
cd_Rom=$10 ; struct ExpansionRom 16 bytes
cd_BoardAddr=$20

CDB_CONFIGME=1
ADNF_STARTPROC=1

; struct CurrentBinding
cb_ConfigDev=$0000
cb_FileName=$0004
cb_ProductString=$0008
cb_ToolTypes=$000c
cb_Sizeof=$0010

DiagStart:
            dc.b $90                 ; da_Config = DAC_WORDWIDE|DAC_CONFIGTIME
            dc.b $00                 ; da_Flags = 0
            dc.w EndCopy-DiagStart   ; da_Size (in bytes)
            dc.w DiagEntry-DiagStart ; da_DiagPoint (0 = none)
            dc.w BootEntry-DiagStart ; da_BootPoint
            dc.w DevName-DiagStart   ; da_Name (offset of ID string)
            dc.w $0000               ; da_Reserved01
            dc.w $0000               ; da_Reserved02

DevName:    dc.b 'debugdev', 0
    even

OFS_PTR=DiagStart
OFS_OP=DiagStart+4

BootEntry: ; Not needed
DiagEntry:
        move.l  #Continue, d0
        jmp     0(a0,d0.l) ; Continue in rom! (a0 = board address)
EndCopy:

Continue:
        move.l  a0, -(sp) ; Save our board base
        move.l  a0, -(sp) ; Again...

        move.l  #PatchCodeEnd-PatchCode, d0
        moveq   #0, d1
        jsr     _LVOAllocMem(a6)
        ; Don't handle failure
        move.l  d0, a0

        ; Copy patch code
        move.l  #PatchCodeEnd-PatchCode-1, d1
        move.l  (sp)+, a1 ; board address
        add.l   #PatchCode, a1
.copy:
        move.b  (a1)+, (a0)+
        dbf.w   d1, .copy

        move.l  d0, a0 ; RAM area
        move.l  (sp)+, a1 ; Board address
        move.l  a1, BoardAddress-PatchCode(a0) ; Save it for patched code

        move.l  a6, OFS_PTR(a1)
        move.w  #OP_INIT, OFS_OP(a1)

        lea     _LVOAddTask+2(a6), a1
        move.l  (a1), OldAddTask-PatchCode+2(a0) ; Save old AddTask vector
        lea     NewAddTask-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one

        move.l  d0, a0 ; RAM area
        lea     _LVORemTask+2(a6), a1
        move.l  (a1), OldRemTask-PatchCode+2(a0) ; Save old AddTask vector
        lea     NewRemTask-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one

        moveq   #0, d0
        rts


ThisTask=276

PatchCode:
BoardAddress:
        dc.l    0
OldAddTask:
        dc.w    $4ef9 ; JMP ABS.L
        dc.l    0 ; Old AddTask vector stored here
NewAddTask:
        move.l  a0, -(sp)
        move.l  BoardAddress(pc), a0
        move.l  a1, OFS_PTR(a0) ; Communicate new task to exprom
        move.w  #OP_ADDTASK, OFS_OP(a0)
        move.l  (sp)+, a0
        bra     OldAddTask
OldRemTask:
        dc.w    $4ef9 ; JMP ABS.L
        dc.l    0 ; Old AddTask vector stored here
NewRemTask:
        movem.l a0/a1, -(sp)
        cmp.l   #0, a1
        bne.b   NotCurTask
        move.l  $4.w, a1
        move.l  ThisTask(a1), a1
NotCurTask:
        move.l  BoardAddress(pc), a0
        move.l  a1, OFS_PTR(a0) ; Communicate new task to exprom
        move.w  #OP_REMTASK, OFS_OP(a0)
        movem.l (sp)+, a0/a1
        bra     OldRemTask

PatchCodeEnd:


