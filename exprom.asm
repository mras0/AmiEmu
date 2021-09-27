RomStart=0

VERSION=0
REVISION=1
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

; exec.library
_LVOAllocMem=-198
_LVOFreeMem=-210
_LVOReplyMsg=-378
_LVOCloseLibrary=-414
_LVOOpenLibrary=-552

; expansion.library
_LVOGetCurrentBinding=-138
_LVOMakeDosNode=-144
_LVOAddDosNode=-150

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

; starts at LN_SIZE
MN_REPLYPORT=$0E ; APTR   message reply port
MN_LENGTH=$12    ; UWORD  total message length in bytes
MN_SIZE=$14
		
IOERR_OPENFAIL=-1	; device/unit failed to open
IOERR_ABORTED=-2	; request terminated early [after AbortIO()]
IOERR_NOCMD=-3	        ; command not supported by device
IOERR_BADLENGTH=-4	; not a valid length (usually IO_LENGTH)
IOERR_BADADDRESS=-5	; invalid address (misaligned or bad range)
IOERR_UNITBUSY=-6	; device opens ok, but requested unit is busy
IOERR_SELFTEST=-7	; hardware failed self-test 

IOB_QUICK=0

; starts at MN_SIZE
IO_DEVICE=$14			; APTR    device node pointer
IO_UNIT=$18			; APTR    unit (driver private)
IO_COMMAND=$1C                  ; UWORD   device command
IO_FLAGS=$1E                    ; UBYTE   special flags
IO_ERROR=$1F	                ; BYTE    error or warning code
IO_SIZE=$20

IO_ACTUAL=$20			; ULONG   actual # of bytes transfered
IO_LENGTH=$24			; ULONG   requested # of bytes transfered
IO_DATA=$28			; APTR    pointer to data area
IO_OFFSET=$2C			; ULONG   offset for seeking devices
IOSTD_SIZE=$30

PA_SIGNAL=0	; Signal task in mp_SigTask
PA_SOFTINT=1	; Signal SoftInt in mp_SoftInt/mp_SigTask
PA_IGNORE=2	; Ignore arrival

; starts at LN_SIZE
MP_FLAGS=$0E                    ; UBYTE
MP_SIGBIT=$0F		        ; UBYTE    signal bit number
MP_SIGTASK=$10		        ; APTR     object to be signalled
MP_MSGLIST=$14	                ; STRUCT   message linked list
MP_SIZE=$22

; starts at MP_SIZE
UNIT_FLAGS=$22                  ; UBYTE
UNIT_OPENCNT=$24                ; UWORD
UNIT_SIZE=$26

dev_SysLib=$22  ; LIB_SIZE
dev_SegList=$26
dev_Base=$2A

; Starts at UNIT_SIZE
devunit_Device=$26              ; APTR
devunit_Sizeof=$2A

; Parameter packet for MakeDosNode
devn_dosName=$00        ; APTR  Pointer to DOS file handler name
devn_execName=$04       ; APTR  Pointer to device driver name
devn_unit=$08           ; ULONG Unit number
devn_flags=$0C          ; ULONG OpenDevice flags
devn_tableSize=$10      ; ULONG Environment size
devn_sizeBlock=$14      ; ULONG # longwords in a block
devn_secOrg=$18         ; ULONG sector origin -- unused
devn_numHeads=$1C       ; ULONG number of surfaces
devn_secsPerBlk=$20     ; ULONG secs per logical block -- unused
devn_blkTrack=$24       ; ULONG secs per track
devn_resBlks=$28        ; ULONG reserved blocks -- MUST be at least 1!
devn_prefac=$2C	        ; ULONG unused
devn_interleave=$30     ; ULONG interleave
devn_lowCyl=$34	        ; ULONG lower cylinder
devn_upperCyl=$38       ; ULONG upper cylinder
devn_numBuffers=$3C     ; ULONG number of buffers
devn_memBufType=$40     ; ULONG Type of memory for AmigaDOS buffers
devn_dName=$44 	        ; char[4] DOS file handler name
devn_Sizeof=$48	        ; Size of this structure



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

DosName: dc.b 'DH0',0
         even

; d0 = device pointer, a0 = segment list
; a6 = exec base
initRoutine:
        movem.l d1-d7/a0-a6,-(sp)
        move.l  d0, a5   ; a5=device pointer
        move.l  a6, dev_SysLib(a5)
        move.l  a0, dev_SegList(a5)
        sub.l   a4, a4

        ; Open expansion library
        lea     ExLibName(pc), a1
        moveq   #0, d0
        jsr     _LVOOpenLibrary(a6)
        tst.l   d0
        beq     irError
        move.l  d0, a4    ; a4=expansion library

        move.l  a4, a6
        lea     dev_Base(a5), a0
        moveq   #4, d0 ; Just get address (length = 4)
        jsr     _LVOGetCurrentBinding(a6)
        move.l  dev_Base(a5), d0
        beq     irError
        move.l  d0, a0
        move.l  cd_BoardAddr(a0), dev_Base(a5) ; Save base address
        bclr.b  #CDB_CONFIGME, cd_Flags(a0)    ; Mark as configured

        ; Make dos packet
        sub.l   #devn_Sizeof, a7
        move.l  a7, a3

        lea     DosName(pc), a0
        move.l  a0, devn_dosName(a3)
        lea     DevName(pc), a0
        move.l  a0, devn_execName(a3)
        move.l  #0, devn_unit(a3)
        move.l  #0, devn_flags(a3)
        move.l  #12, devn_tableSize(a3)
        move.l  #128, devn_sizeBlock(a3) ;SECTOR/4
        move.l  #0, devn_secOrg(a3)
        move.l  #1, devn_numHeads(a3)
        move.l  #1, devn_secsPerBlk(a3)
        move.l  #10, devn_blkTrack(a3)    ;sectors per track
        move.l  #2, devn_resBlks(a3)      ;2 blocks reserved (boot sector)
        move.l  #0, devn_prefac(a3)
        move.l  #0, devn_interleave(a3)
        move.l  #0, devn_lowCyl(a3)
        move.l  #999, devn_upperCyl(a3)
        move.l  #1, devn_numBuffers(a3)
        move.l  #0, devn_memBufType(a3)
        move.l  #$44483000, devn_dName(a3) ; 'DH0\0'

        lea     RomCodeEnd(pc), a0
        move.l  a3, (a0)
        move.w  #$fedf, 4(a0)

        move.l  a3, a0
        jsr     _LVOMakeDosNode(a6)

        tst.l   d0
        beq.b   irError
        move.l  d0, a0
        moveq   #0, d0 ; Boot priority
        moveq   #ADNF_STARTPROC, d1 ; Flags
        jsr     _LVOAddDosNode(a6)
        tst.l   d0
        beq.b   irError

        add.l   #devn_Sizeof, a7 ; Free dos packet

        move.l  a5, d0
        bra.b   irExit
irError:
        moveq   #0, d0
irExit:
        move.l  dev_SysLib(a5), a6 ; restore exec base
        cmp.l   #0, a4
        beq.b   irNoClose
        move.l  d0, -(sp)
        ; Close expansion library
        move.l  a4, a1
        jsr     _LVOCloseLibrary(a6)
        move.l  (sp)+, d0
irNoClose:
        movem.l (sp)+, d1-d7/a0-a6
        rts

ExLibName:  dc.b 'expansion.library', 0
            even

msg1:   dc.b 'Debugmsg: ',0
hexchars: dc.b '0123456789abcdef'
        even
DebugMsg:
        movem.l d0-d2/a0, -(sp)
        move.l  d0, d1
        lea     msg1(pc), a0
        move.w  #$100, d0 ; Stop bit
dmLoop:
        move.b  (a0)+,d0
        beq.b   dmDone
        move.w  d0, $dff030
        bra.b   dmLoop
dmDone:
        lea     hexchars(pc), a0
        moveq   #7, d2
dmHexPrint:
        rol.l   #4, d1
        move.w  d1, d0
        and.w   #$f, d0
        move.b  0(a0,d0.w), d0
        or.w    #$100, d0
        move.w  d0, $dff030
        dbf     d2, dmHexPrint
        move.w  #$010a, $dff030
        movem.l (sp)+, d0-d2/a0
        rts

Open:   ; ( device:a6, iob:a1, unitnum:d0, flags:d1 )
        movem.l d0-d7/a0-a6, -(sp)
        addq.w  #1, LIB_OPENCNT(a6) ; Avoid expunge during open
        move.l  a1, a2 ; IOB in a2

        tst.l   d0
        bne.b   OpenError ; Invalid unit

        ; Allocate memory for unit
        move.l  #devunit_Sizeof, d0
        move.l  #$10001, d1 ; MEMF_CLEAR!MEMF_PUBLIC
        move.l  a6, a3 ; device in a3
        move.l  dev_SysLib(a3), a6
        jsr     _LVOAllocMem(a6)
        move.l  a3, a6 ; restore device pointer
        tst.l   d0
        beq     OpenError
        move.l  d0, a3 ; unit=a3

        ; Initialize it
        move.l  a6, devunit_Device(a3)
        move.b  #PA_IGNORE, MP_FLAGS(a3)
        move.b  #NT_MSGPORT, LN_TYPE(a3)
        lea     DevName(pc), a0
        move.l  a0, LN_NAME(a3)

        move.l  a3, IO_UNIT(a2)
        addq.w  #1, LIB_OPENCNT(a6)
        addq.w  #1, UNIT_OPENCNT(a3)

        moveq   #0, d0
        move.b  d0, IO_ERROR(a2)
        move.b  #NT_REPLYMSG, LN_TYPE(a2) ; Mark IORqeust as complete

OpenDone:
        subq.w  #1, LIB_OPENCNT(a6) ; remove temp count
        movem.l (sp)+, d0-d7/a0-a6
        rts
OpenError:
        moveq   #IOERR_OPENFAIL, d0
        move.b  d0, IO_ERROR(a2)
        move.l  d0, IO_DEVICE(a2)
        bra.b   OpenDone

Close:  ; ( device:a6, iob:a1 )
        movem.l d0-d7/a0-a6, -(sp)

        move.l  IO_UNIT(a1), a3
        moveq   #0, d0
        move.l  d0, IO_UNIT(a1)
        move.l  d0, IO_DEVICE(a1)

        subq.w  #1, UNIT_OPENCNT(a3)
        bne.b   CloseDevice

        ; Free unit
        move.l  a6, a5
        move.l  a3, a1
        move.l  #devunit_Sizeof, d0
        move.l  dev_SysLib(a6), a6
        jsr     _LVOFreeMem(a6)
        move.l  a5, a6

CloseDevice:
        moveq   #0, d0
        subq.w  #1, LIB_OPENCNT(a6)
        bne.b   CloseEnd
        bsr     Expunge
CloseEnd:
        movem.l (sp)+, d0-d7/a0-a6
        rts

Expunge:
        moveq   #3, d0
        bra.b   Expunge

Null:
        moveq   #4, d0
        bra.b   Null

BeginIO: ; ( iob: a1, device:a6 )
        movem.l d0-d7/a0-a6, -(sp)

        ;moveq   #0, d0
        ;move.w  IO_COMMAND(a1), d0
        ;bsr     DebugMsg

        move.b  #NT_MESSAGE, LN_TYPE(a1)

        lea     RomCodeEnd(pc), a0
        move.l  a1, (a0)
        move.w  #$fede, 4(a0)

        btst    #IOB_QUICK, IO_FLAGS(a1)
        bne.b   BeginIO_End

        ; Note: "trash" a6
        move.l  dev_SysLib(a6), a6
        ; a1=message
        jsr     _LVOReplyMsg(a6)

BeginIO_End:
        movem.l (sp)+, d0-d7/a0-a6
        rts

AbortIO:
        moveq   #6, d0
        bra.b   AbortIO

RomCodeEnd:
