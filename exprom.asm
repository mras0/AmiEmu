RomStart=0

VERSION=0
REVISION=1
RTC_MATCHWORD=$4afc
RTF_AUTOINIT=$80	; rt_Init points to data structure
RTF_COLDSTART=$01
NT_DEVICE=3

INITBYTE=$e000
INITWORD=$d000
INITLONG=$c000

LIB_FLAGS=$0E
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

LN_SUCC=$00
LN_PRED=$04
LN_TYPE=$08
LN_PRI=$09
LN_NAME=$0A
LN_SIZE=$0E

dev_SysLib=$22  ; LIB_SIZE
dev_SegList=$26
dev_Base=$2A

_LVOOpenLibrary=-552
_LVOCloseLibrary=-414

DiagStart:
            dc.b $90                 ; da_Config = DAC_WORDWIDE|DAC_CONFIGTIME
            dc.b $00                 ; da_Flags = 0
            dc.w EndCopy-DiagStart   ; da_Size (in bytes)
            dc.w DiagEntry-DiagStart ; da_DiagPoint (0 = none)
            dc.w BootEntry-DiagStart ; da_BootPoint
            dc.w DevName-DiagStart   ; da_Name (offset of ID string)
            dc.w $0000               ; da_Reserved01
            dc.w $0000               ; da_Reserved02

RomTag:
            dc.w    RTC_MATCHWORD      ; UWORD RT_MATCHWORD
rt_Match:   dc.l    Romtag-DiagStart   ; APTR  RT_MATCHTAG
rt_End:     dc.l    EndCopy-DiagStart  ; APTR  RT_ENDSKIP
            dc.b    RTF_AUTOINIT+RTF_COLDSTART ; UBYTE RT_FLAGS
            dc.b    VERSION            ; UBYTE RT_VERSION
            dc.b    NT_DEVICE          ; UBYTE RT_TYPE
            dc.b    20                 ; BYTE  RT_PRI
rt_Name:    dc.l    DevName-DiagStart  ; APTR  RT_NAME
rt_Id:      dc.l    IdString-DiagStart ; APTR  RT_IDSTRING
rt_Init:    dc.l    Init-DiagStart     ; APTR  RT_INIT


DevName:    dc.b 'hello.device', 0
IdString:   dc.b 'hello ',VERSION+48,'.',REVISION+48, 0
    even

Init:
            dc.l    $100 ; data space size
            dc.l    funcTable-DiagStart
            dc.l    dataTable-DiagStart
            dc.l    initRoutine-RomStart

funcTable:
            dc.l   Open-RomStart
            dc.l   Close-RomStart
            dc.l   Expunge-RomStart
            dc.l   Null-RomStart	    ;Reserved for future use!
            dc.l   BeginIO-RomStart
            dc.l   AbortIO-RomStart
            dc.l   -1

dataTable:
            DC.W INITBYTE, LN_TYPE
            DC.B NT_DEVICE, 0
            DC.W INITLONG, LN_NAME
dt_Name:    DC.L DevName-DiagStart
            DC.W INITBYTE, LIB_FLAGS
            DC.B LIBF_SUMUSED+LIBF_CHANGED, 0
            DC.W INITWORD, LIB_VERSION, VERSION
            DC.W INITWORD, LIB_REVISION, REVISION
            DC.W INITLONG, LIB_IDSTRING
dt_Id:      DC.L IdString-DiagStart
            DC.W 0   ; terminate list

BootEntry:
            moveq   #$66,d0
            bra BootEntry
            rts

DiagEntry:
            ;lea     patchTable-RomStart(a0), a1
            move.l  #patchTable, a1
            sub.l   #RomStart, a1
            add.l   a0, a1
            move.l  a2, d1
dloop:
            move.w  (a1)+, d0
            bmi.b   bpatches
            add.l   d1, 0(a2,d0.w)
            bra.b   dloop
bpatches:
            move.l  a0, d1
bloop:
            move.w  (a1)+, d0
            bmi.b   endpatches
            add.l   d1, 0(a2,d0.w)
            bra.b   bloop
endpatches:
            moveq   #1,d0 ; success
            rts
EndCopy:

patchTable:
; Word offsets into Diag area where pointers need Diag copy address added
            dc.w   rt_Match-DiagStart
            dc.w   rt_End-DiagStart
            dc.w   rt_Name-DiagStart
            dc.w   rt_Id-DiagStart
            dc.w   rt_Init-DiagStart
            dc.w   Init-DiagStart+$4
            dc.w   Init-DiagStart+$8
            dc.w   dt_Name-DiagStart
            dc.w   dt_Id-DiagStart
            dc.w   -1
; Word offsets into Diag area where pointers need boardbase+ROMOFFS added
            dc.w   Init-DiagStart+$c
            dc.w   funcTable-DiagStart+$00
            dc.w   funcTable-DiagStart+$04
            dc.w   funcTable-DiagStart+$08
            dc.w   funcTable-DiagStart+$0C
            dc.w   funcTable-DiagStart+$10
            dc.w   funcTable-DiagStart+$14
            dc.w   -1

; d0 = device pointer, a0 = segment list
; a6 = exec base
initRoutine:
        movem.l  d1-d7/a0-a6,-(sp)
        move.l   d0, a5   ; a5=device pointer
        move.l   a6, dev_SysLib(a5)
        move.l   a0, dev_SegList(a5)

        ; Open expansion library
        lea     ExLibName(pc), a1
        moveq   #0, d0
        jsr     _LVOOpenLibrary(a6)
        tst.l   d0
        beq     irError
        move.l  d0, a4    ; a4=expansion library

        ; Close expansion library
        move.l  a4, a1
        jsr     _LVOCloseLibrary(a6)

        move.l   a5, d0
        bra.b    irExit
irError:
        moveq    #0, d0
irExit:
        movem.l  (sp)+, d1-d7/a0-a6
        rts

ExLibName:  dc.b 'expansion.library', 0
            even

Open:
        moveq   #1, d0
        bra.b   Open

Close:
        moveq   #2, d0
        bra.b   Close

Expunge:
        moveq   #3, d0
        bra.b   Expunge

Null:
        moveq   #4, d0
        bra.b   Null

BeginIO:
        moveq   #5, d0
        bra.b   BeginIO

AbortIO:
        moveq   #6, d0
        bra.b   AbortIO
