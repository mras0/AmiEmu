RomStart=0

VERSION=0
REVISION=3

OP_INIT=$fedf
OP_IOREQ=$fede
OP_SETFSRES=$fee0
OP_FSINFO=$fee1
OP_FSINITSEG=$fee2

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

; starts at LN_SIZE
bn_Flags=$0E                    ; UWORD
bn_DeviceNode=$10               ; APTR
BootNode_SIZEOF=$14

dev_SysLib=$22  ; LIB_SIZE
dev_SegList=$26
dev_Base=$2A
dev_FileSysRes=$2e
dev_Sizeof=$32

; struct DeviceNode
dn_Next=$0000
dn_Type=$0004
dn_Task=$0008
dn_Lock=$000c
dn_Handler=$0010
dn_StackSize=$0014
dn_Priority=$0018
dn_Startup=$001c
dn_SegList=$0020
dn_GlobalVec=$0024
dn_Name=$0028
dn_Sizeof=$002c

; struct CurrentBinding
cb_ConfigDev=$0000
cb_FileName=$0004
cb_ProductString=$0008
cb_ToolTypes=$000c
cb_Sizeof=$0010

; Starts at UNIT_SIZE
devunit_Device=$26              ; APTR
devunit_UnitNum=$2A             ; ULONG
devunit_Sizeof=$2E

; struct FileSysResource
fsr_Node=$0000                  ; struct Node
fsr_Creator=$000e               ; char*
fsr_FileSysEntries=$0012        ; struct List
fsr_Sizeof=$0020

; struct FileSysEntry
fse_Node=$0000                  ; struct Node
fse_DosType=$000e               ; ULONG
fse_Version=$0012               ; ULONG
fse_PatchFlags=$0016            ; ULONG
fse_Type=$001a                  ; ULONG
fse_Task=$001e                  ; CPTR
fse_Lock=$0022                  ; BPTR
fse_Handler=$0026               ; BSTR
fse_StackSize=$002a             ; ULONG
fse_Priority=$002e              ; LONG
fse_Startup=$0032               ; BPTR
fse_SegList=$0036               ; BPTR
fse_GlobalVec=$003a             ; BPTR
fse_Sizeof=$003e

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
devn_memBufType=$40     ; ULONG type of memory for AmigaDOS buffers
devn_transferSize=$44   ; LONG  largest transfer size (largest signed #)
devn_addMask=$48        ; ULONG address mask
devn_bootPrio=$4c       ; ULONG boot priority
devn_dName=$50 	        ; char[4] DOS file handler name
devn_bootFlags=$54	; ULONG boot flags (filled in by host)
devn_segList=$58        ; BPTR segList (filled in by host)
devn_Sizeof=$5c	        ; Size of this structure

; Filesystem info
fsinfo_num=$00          ; UWORD Filesystem number (filled in by expansion ROM)
fsinfo_dosType=$02      ; ULONG dos type (filled by host)
fsinfo_version=$06      ; ULONG dos type (filled by host)
fsinfo_numHunks=$0a     ; ULONG number of hunks (filled by host)
fsinfo_hunk1=$0e        ; ULONG mem flags for hunk1 (filled by host)
fsinfo_hunk2=$12        ; ULONG mem flags for hunk2 (filled by host)
fsinfo_hunk3=$16        ; ULONG mem flags for hunk3 (filled by host)
fsinfo_Sizeof=$1a

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
rt_NamePtr: dc.l    DevName-DiagStart  ; APTR  RT_NAME
rt_Id:      dc.l    IdString-DiagStart ; APTR  RT_IDSTRING
rt_InitPtr: dc.l    Init-DiagStart     ; APTR  RT_INIT


; Padding to allow change to longer name
DevName:    dc.b 'virtualhd.device', 0, 0, 0, 0, 0, 0, 0, 0, 0
IdString:   dc.b 'virtualhd ',VERSION+48,'.',REVISION+48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
DosLibName: dc.b 'dos.library', 0
    even

Init:
            dc.l    dev_Sizeof ; data space size
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
        lea     DosLibName(pc), a1
        jsr     _LVOFindResident(a6)    ; find the DOS resident tag
        move.l  d0, a0
        move.l  RT_INIT(a0), a0         ; set vector to DOS INIT
        jmp     (a0)                    ; initialize DOS

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
        bra     dloop
bpatches:
        move.l  a0, d1
bloop:
        move.w  (a1)+, d0
        bmi.b   endpatches
        add.l   d1, 0(a2,d0.w)
        bra     bloop
endpatches:
        moveq   #1,d0 ; success
        rts
EndCopy:

patchTable:
; Word offsets into Diag area where pointers need Diag copy address added
        dc.w   rt_Match-DiagStart
        dc.w   rt_End-DiagStart
        dc.w   rt_NamePtr-DiagStart
        dc.w   rt_Id-DiagStart
        dc.w   rt_InitPtr-DiagStart
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

        ; Check if we need to install any filesystems
        lea     RomCodeEnd(pc), a0
        move.w  2(a0), d6       ; d6 = number of filesystems to install
        beq     irFsOK

        ; Open FileSystem.resource
        lea     FsrName(pc), a1
        jsr     _LVOOpenResource(a6)
        tst.l   d0
        beq     irNoFsResource

        ; Inform host about FileSystem.resource
        lea     RomCodeEnd(pc), a0
        move.l  d0, (a0)
        move.w  #OP_SETFSRES, 4(a0)
        ; re-read number of file systems to install after host has had chance to prune list
        move.w  2(a0), d6
        bne     irHasFsResource
        bra     irFsOK

irNoFsResource:
        bsr     CreateFileSysResource
        tst.l   d0
        beq     irError

irHasFsResource:
        move.l  d0, dev_FileSysRes(a5)
        sub.l   #fsinfo_Sizeof, a7
        moveq   #0, d5 ; Fs counter
irFsLoop:
        move.l  a7, a1
        move.w  d5, fsinfo_num(a1)
        lea     RomCodeEnd(pc), a0
        move.l  a1, (a0)
        move.w  #OP_FSINFO, 4(a0)

        sub.l   #16, a7 ; fsseginfo
        move.l  a7, a3
        move.l  fsinfo_numHunks(a1), d3
        lea     fsinfo_hunk1(a1), a2
        moveq   #0, d2
irAllocHunkLoop:
        move.l  (a2)+, d0       ; Hunk size + flags
        move.l  d0, d1
        clr.w   d1
        swap    d1
        lsr.l   #8, d1
        lsr.l   #5, d1
        or.l    #$10000, d1 ; MEMF_CLEAR
        and.l   #$3FFFFFFF, d0
        lsl.l   #2, d0
        addq.l  #8, d0
        jsr     _LVOAllocMem(a6)
        move.l  d2, d1
        lsl.l   #2, d1
        move.l  d0, 0(a3,d1.w)
        addq.l  #1, d2
        cmp.l   d2, d3
        bne     irAllocHunkLoop

        move.l  d5, 12(a3)
        lea     RomCodeEnd(pc), a0
        move.l  a3, (a0)
        move.w  #OP_FSINITSEG, 4(a0)

        move.l  0(a3), d0 ; First hunk
        addq.l  #4, d0    ; Point to link
        lsr.l   #2, d0    ; BPTR
        move.l  d0, a3

        add.l   #16, a7

        move.l  #fse_Sizeof, d0
        move.l  #$10001, d1 ; MEMF_CLEAR!MEMF_PUBLIC
        jsr     _LVOAllocMem(a6)
        tst.l   d0
        beq     irError ; TODO: Need clean up...
        move.l  d0, a0

        move.l  a7, a1
        ; a0 = FileSysEntry
        ; a1 = fsinfo
        ; a3 = seglist
        move.l  fsinfo_dosType(a1), fse_DosType(a0)
        move.l  fsinfo_version(a1), fse_Version(a0)
        move.l  #$180, fse_PatchFlags(a0) ; Needed?
        move.l  #-1, fse_GlobalVec(a0)
        move.l  a3, fse_SegList(a0)
        lea.l   IdString(pc), a3
        move.l  a3, LN_NAME(a0)
        move.l  a0, a1
        move.l  dev_FileSysRes(a5), a0
        lea.l   fsr_FileSysEntries(a0), a0
        jsr     _LVOAddHead(a6)

        addq.w  #1, d5
        cmp.w   d5, d6
        bne     irFsLoop
        add.l   #fsinfo_Sizeof, a7

irFsOK:
        move.l  a4, a6 ; a6 = expansion library
        sub.l   #cb_Sizeof, a7
        move.l  a7, a0
        moveq   #cb_Sizeof, d0 ; Don't pass in less than sizeof(CurrentBinding) for KS1.3!
        jsr     _LVOGetCurrentBinding(a6)
        move.l  cb_ConfigDev(a7), d6         ; d6 = ConfigNode
        add.l   #cb_Sizeof, a7
        tst.l   d6
        beq     irError
        move.l  d6, a0
        move.l  cd_BoardAddr(a0), dev_Base(a5) ; Save base address
        bclr.b  #CDB_CONFIGME, cd_Flags(a0)    ; Mark as configured

        moveq   #0, d5 ; Unit counter
irUnitLoop:
        ; Room for dos packet
        sub.l   #devn_Sizeof, a7
        move.l  a7, a3

        ; Room for name
        sub.l   #32, a7

        move.l  a7, devn_dosName(a3)
        lea     DevName(pc), a0
        move.l  a0, devn_execName(a3)
        move.l  d5, devn_unit(a3)
        move.l  #16, devn_tableSize(a3)

        lea     RomCodeEnd(pc), a0
        move.l  a3, (a0)
        move.w  #OP_INIT, 4(a0)

        move.l  a3, a0
        jsr     _LVOMakeDosNode(a6)
        tst.l   d0
        beq     irUnitError

        move.l  d0, a0 ; a0 = DosNode

        move.l  #-1, dn_GlobalVec(a0)
        move.l  devn_segList(a3), dn_SegList(a0) ; Seglist from host

        move.l  devn_bootFlags(a3), d0
        btst.l  #0, d0 ; Auto boot?
        bne     irBootNode

        ; a0 = dosNode
        moveq   #-128, d0 ; Boot priority (-128 = not bootable)
        moveq   #ADNF_STARTPROC, d1 ; Flags
        jsr     _LVOAddDosNode(a6)
        tst.l   d0
        beq     irUnitError

        bra     irNextUnit

irBootNode:
        cmp.w   #36, LIB_VERSION(a6)
        bcs.b   irPrev36

        ; a0 = dosNode
        move.l  d6, a1 ; configDev
        move.l  devn_bootPrio(a3), d0 ; Boot priority
        moveq   #ADNF_STARTPROC, d1 ; flags
        jsr     _LVOAddBootNode(a6)
        tst.l   d0
        beq     irUnitError

        bra     irNextUnit

irPrev36:
        ; Enqueue boot node
        move.l  a0, -(sp)
        move.l  dev_SysLib(a5), a6
        moveq   #BootNode_SIZEOF, d0
        move.l  #$10001, d1 ; MEMF_CLEAR!MEMF_PUBLIC
        move.l  dev_SysLib(a5), a6
        jsr     _LVOAllocMem(a6)
        move.l  (sp)+, a0
        tst.l   d0
        beq     irUnitError
        move.l  d0, a1
        move.b  #NT_BOOTNODE, LN_TYPE(a1)
        move.l  d6, LN_NAME(a1)
        move.w  #ADNF_STARTPROC, bn_Flags(a1)
        move.l  a0, bn_DeviceNode(a1)
        lea     eb_MountList(a4), a0
        jsr     _LVOForbid(a6)
        jsr     _LVOEnqueue(a6)
        jsr     _LVOPermit(a6)
        move.l  a4, a6 ; a6 = expansion.library again!
        bra     irNextUnit

irUnitError:
        add.l   #32, a7 ; Free name
        add.l   #devn_Sizeof, a7 ; Free dos packet
        bra     irError

irNextUnit:
        add.l   #32, a7 ; Free name
        add.l   #devn_Sizeof, a7 ; Free dos packet
        addq.w  #1, d5 ; next unit
        cmp.w   RomCodeEnd(pc), d5 ; Done?
        bne     irUnitLoop

        move.l  a5, d0
        bra     irExit
irError:
        moveq   #0, d0
irExit:
        move.l  dev_SysLib(a5), a6 ; restore exec base

        cmp.l   #0, a4
        beq     irNoClose
        move.l  d0, -(sp)
        ; Close expansion library
        move.l  a4, a1
        jsr     _LVOCloseLibrary(a6)
        move.l  (sp)+, d0
irNoClose:
        movem.l (sp)+, d1-d7/a0-a6
        rts

ExLibName:  dc.b 'expansion.library', 0
FsrName:    dc.b 'FileSystem.resource', 0
            even

;        ; a0 = List
;PrintList:
;        movem.l d0/a0/a1, -(sp)
;        move.l  a0, a1
;prLoop:
;        tst.l   LN_SUCC(a1)
;        beq     prDone
;        move.l  a1, d0
;        bsr     SerPutNum
;        move.b  #$20, d0
;        bsr     SerPutchar
;        move.l  LN_NAME(a1), a0
;        bsr     SerPutMsg
;        bsr     SerPutCrLf
;        move.l  LN_SUCC(a1), a1
;        bra     prLoop
;prDone:
;        movem.l (sp)+, d0/a0/a1
;        rts
;
;SerPutchar:
;        move.l  d0, -(sp)
;        and.w   #$ff, d0
;        or.w    #$100, d0       ; stop bit
;        move.w  d0, $dff030
;        move.l  (sp)+, d0
;        rts
;
;SerPutMsg:
;        movem.l  d0/a0, -(sp)
;spLoop:
;        move.b  (a0)+, d0
;        beq     spDone
;        bsr     SerPutchar
;        bra     spLoop
;spDone:
;        movem.l  (sp)+, d0/a0
;        rts
;
;SerPutCrLf:
;        move.l  d0, -(sp)
;        move.b  #13, d0
;        bsr     SerPutchar
;        move.b  #10, d0
;        bsr     SerPutchar
;        move.l  (sp)+, d0
;        rts
;
;SerPutNum:
;        movem.l d0-d2, -(sp)
;        move.l  d0, d1
;        moveq   #7, d2
;spnLoop:
;        rol.l   #4, d1
;        move.w  d1, d0
;        and.b   #$f, d0
;        add.b   #$30, d0
;        cmp.b   #$39, d0
;        ble.b   spnPrint
;        add.b   #39, d0
;spnPrint:
;        bsr     SerPutchar
;        dbf     d2, spnLoop
;        movem.l (sp)+, d0-d2
;        rts

Open:   ; ( device:a6, iob:a1, unitnum:d0, flags:d1 )
        movem.l d0-d7/a0-a6, -(sp)
        addq.w  #1, LIB_OPENCNT(a6) ; Avoid expunge during open
        move.l  a1, a2 ; IOB in a2

        moveq   #0, d2
        move.w  RomCodeEnd(pc), d2 ; Number of units
        cmp.l   d2, d0
        bge.b   OpenError ; Invalid unit

        move.l  d0, d4 ; UnitNum in d4

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
        move.l  d4, devunit_UnitNum(a3)
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
        bra     OpenDone

Close:  ; ( device:a6, iob:a1 )
        movem.l d0-d7/a0-a6, -(sp)

        move.l  IO_UNIT(a1), a3
        moveq   #0, d0
        move.l  d0, IO_UNIT(a1)
        move.l  d0, IO_DEVICE(a1)

        subq.w  #1, UNIT_OPENCNT(a3)
        bne     CloseDevice

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
        bne     CloseEnd
        bsr     Expunge
CloseEnd:
        movem.l (sp)+, d0-d7/a0-a6
        rts

Expunge:
        ; TODO: Support this (maybe)
        ; Just leak memory for now
        moveq   #0, d0
        rts

Null:
        moveq   #0, d0
        rts

BeginIO: ; ( iob: a1, device:a6 )
        movem.l d0-d7/a0-a6, -(sp)

        move.b  #NT_MESSAGE, LN_TYPE(a1)

        lea     RomCodeEnd(pc), a0
        move.l  a1, (a0)
        move.w  #OP_IOREQ, 4(a0)

        btst    #IOB_QUICK, IO_FLAGS(a1)
        bne     BeginIO_End

        ; Note: "trash" a6
        move.l  dev_SysLib(a6), a6
        ; a1=message
        jsr     _LVOReplyMsg(a6)

BeginIO_End:
        movem.l (sp)+, d0-d7/a0-a6
        rts

AbortIO:
        ; Shouldn't be called, but fake success anyway
        move.b  #0, IO_ERROR(a1)
        rts

; In: a6=SysBase Out: d0=Filesystem resource
CreateFileSysResource:
        move.l  #fsr_Sizeof, d0
        move.l  #$10001, d1 ; MEMF_CLEAR!MEMF_PUBLIC
        jsr     _LVOAllocMem(a6)
        tst.l   d0
        beq     cfsrOut
        move.l  d0, -(sp)
        move.l  d0, a0
        move.b  #NT_RESOURCE, LN_TYPE(a0)
        lea     FsrName(pc), a1
        move.l  a1, LN_NAME(a0)
        lea     IdString(pc), a1
        move.l  a1, fsr_Creator(a0)
        ; Init the list
        lea     fsr_FileSysEntries(a0), a0
        move.l  a0, LH_HEAD(a0)
        addq.l  #4, LH_HEAD(a0) ; Point head to tail
        clr.l   LH_TAIL(a0)     ; Clear tail
        move.l  a0, LH_TAILPRED(a0) ; TailPred points to head
        ; Add it to the system
        lea     ResourceList(a6), a0
        move.l  d0, a1
        jsr     _LVOAddTail(a6)
        move.l  (sp)+, d0
cfsrOut:
        rts

RomCodeEnd:
