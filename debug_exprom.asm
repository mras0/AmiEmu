OP_INIT=1
OP_ADDTASK=2
OP_REMTASK=3
OP_LOADSEG=4

; exec.library
_LVOAllocMem=-198
_LVOAddTask=-282
_LVORemTask=-288
_LVOFindTask=-294
_LVOAddLibrary=-396

ResourceList=$150
ThisTask=$114

; dos.library
_LVOLoadSeg=-150

; struct Node
ln_Type=$0008
ln_Name=$000a

NT_PROCESS=13

; struct Library
lib_Version=$0014

; struct Process
pr_GlobVec=$0088

GLOBVEC_LOADSEG=$0144 ; $51*4

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

OFS_PTR1=DiagStart
OFS_PTR2=DiagStart+4
OFS_OP=DiagStart+16

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

        move.l  a6, OFS_PTR1(a1)
        move.w  #OP_INIT, OFS_OP(a1)

        lea     _LVOAddTask+2(a6), a1
        move.l  (a1), OldAddTask-PatchCode(a0) ; Save old AddTask vector
        lea     NewAddTask-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one

        move.l  d0, a0 ; RAM area
        lea     _LVORemTask+2(a6), a1
        move.l  (a1), OldRemTask-PatchCode(a0) ; Save old AddTask vector
        lea     NewRemTask-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one


        ; Don't patch AddLibrary if KS1.x
        cmp.w   #36, lib_Version(a6)
        blt.b   .ks1x

        move.l  d0, a0 ; RAM area
        lea     _LVOAddLibrary+2(a6), a1
        move.l  (a1), OldAddLibrary-PatchCode(a0) ; Save old AddLibrary vector
        lea     NewAddLibrary-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one
        bra.b   .out

.ks1x:
        ; For KS 1.x patch FindTask instead (TODO: There must be a better way...)
        move.l  d0, a0 ; RAM area
        lea     _LVOFindTask+2(a6), a1
        move.l  (a1), OldFindTask-PatchCode(a0) ; Save old FindTask vector
        lea     NewFindTask-PatchCode(a0), a0
        move.l  a0, (a1) ; Install new one

.out:
        moveq   #0, d0 ; Return failure to free diagarea
        rts

        ; Checks if the library (in a1) is dos.library
        ; Returns result in zero flag
IsDosLibrary:
        movem.l d0/a0-a1, -(sp)
        lea     DosName(pc), a0
        move.l  ln_Name(a1), a1
IDL_Compare:
        move.b  (a0)+, d0
        tst.b   d0
        beq.b   IDL_MaybeDone
        cmp.b   (a1)+, d0
        bne.b   IDL_Out
        bra.b   IDL_Compare
IDL_MaybeDone:
        tst.b   (a1)
IDL_Out:
        movem.l (sp)+, d0/a0-a1
        rts

DosName: dc.b 'dos.library', 0
        even

PatchCode:
BoardAddress:
        dc.l    0
NewAddTask:
        movem.l a0, -(sp)
        move.l  BoardAddress(pc), a0
        move.l  a1, OFS_PTR1(a0) ; Communicate new task to exprom
        move.w  #OP_ADDTASK, OFS_OP(a0)
        move.l  (sp)+, a0
        dc.w    $4ef9 ; JMP ABS.L
OldAddTask:
        dc.l    0 ; Old AddTask vector stored here

NewRemTask:
        movem.l a0/a1, -(sp)
        sub.l   a0, a0
        cmp.l   a0, a1
        bne.b   NotCurTask
        move.l  $4.w, a1
        move.l  ThisTask(a1), a1
NotCurTask:
        move.l  BoardAddress(pc), a0
        move.l  a1, OFS_PTR1(a0) ; Communicate new task to exprom
        move.w  #OP_REMTASK, OFS_OP(a0)
        movem.l (sp)+, a0/a1
        dc.w    $4ef9 ; JMP ABS.L
OldRemTask:
        dc.l    0 ; Old RemTask vector stored here

NewAddLibrary:
        move.l  a0, -(sp)
        move.l  BoardAddress(pc), a0
        jsr     IsDosLibrary(a0)
        bne.b   NAL_Out

        ; Remove AddLibrary patch
        move.l  OldAddLibrary(pc), _LVOAddLibrary+2(a6)

        ; Install LoadSeg patch
        lea.l   OldLoadSeg(pc), a0
        move.l  _LVOLoadSeg+2(a1), (a0)
        lea.l   NewLoadSeg(pc), a0
        move.l  a0, _LVOLoadSeg+2(a1)
        lea.l  _LVOLoadSeg(a1), a0
NAL_Out:
        move.l  (sp)+, a0
        dc.w    $4ef9 ; JMP ABS.L
OldAddLibrary:
        dc.l    0 ; Old AddLibrary vector stored here

NewLoadSeg:
        move.l  a0, -(sp)
        move.l  BoardAddress(pc), a0
        move.l  d1, OFS_PTR1(a0)
        move.l  (sp)+, a0
        dc.w    $4eb9 ; JSR ABS.L
OldLoadSeg:
        dc.l    0   ; Old LoadSeg stored here
        tst.l   d0
        bne.b   NLS_OK
        rts     ; LoadSeg failed
NLS_OK:
        move.l  a0, -(sp)
        move.l  BoardAddress(pc), a0
        move.l  d0, OFS_PTR2(a0)
        move.w  #OP_LOADSEG, OFS_OP(a0)
        move.l  (sp)+, a0
        rts

NewFindTask:
        dc.w    $4eb9 ; JSR ABS.L
OldFindTask:
        dc.l    0   ; Old LoadSeg stored here
        tst.l   d0
        bne.b   OFT_CheckIfPatchNeeded
        rts
OFT_CheckIfPatchNeeded:
        movem.l d0/a0/a1, -(sp)
        move.l  d0, a0
        cmp.b   #NT_PROCESS, ln_Type(a0)
        bne.b   OFT_Out ; Not a process
        move.l  pr_GlobVec(a0), a0
        add.w   #GLOBVEC_LOADSEG, a0
        move.l  (a0), d0
        beq.b   OFT_Out ; Global vectors not initialized
        lea     NewLoadSeg1x(pc), a1
        cmp.l   d0, a1
        beq.b   OFT_Out ; Already patched
        ; Store original (should always be the same)
        move.l  d0, OldLoadSeg1x-NewLoadSeg1x(a1)
        ; And patch
        move.l  a1, (a0)
OFT_Out:
        movem.l (sp)+, d0/a0/a1
        rts

NewLoadSeg1x:
        movem.l d1/a0, -(sp)
        move.l  BoardAddress(pc), a0
        ; BPTR -> CPTR (assume NUL-terminated)
        add.l   d1, d1
        add.l   d1, d1
        addq.l  #1, d1
        move.l  d1, OFS_PTR1(a0)
        movem.l (sp)+, d1/a0
        ; Follow BCPL caling convention..
        dc.w    $287c   ; MOVEA.L IMMEDIATE, A4
OldLoadSeg1x:
        dc.l 0 ; Old BCPL LoadSeg stored here
        jsr     (a5)
        move.l  a0, -(sp)
        move.l  BoardAddress(pc), a0
        move.l  d1, OFS_PTR2(a0)
        move.w  #OP_LOADSEG, OFS_OP(a0)
        move.l  (sp)+, a0
        jmp     (a6)

PatchCodeEnd:
