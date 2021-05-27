#include <iostream>

/*
http://goldencrystal.free.fr/M68kOpcodes-v2.3.pdf

XXX: Handle D
XXX: EXG omitted

ORI to CCR      B       0 0 0 0 0 0 0 0 0 0 1 1 1 1 0 0     B I
ORI to SR        W      0 0 0 0 0 0 0 0 0 1 1 1 1 1 0 0     W I
ORI             BWL     0 0 0 0 0 0 0 0 Sx  M     Xn        / I
ANDI to CCR     B       0 0 0 0 0 0 1 0 0 0 1 1 1 1 0 0     B I
ANDI to SR       W      0 0 0 0 0 0 1 0 0 1 1 1 1 1 0 0     W I
ANDI            BWL     0 0 0 0 0 0 1 0 Sx  M     Xn        / I
SUBI            BWL     0 0 0 0 0 1 0 0 Sx  M     Xn        / I
ADDI            BWL     0 0 0 0 0 1 1 0 Sx  M     Xn        / I
EORI to CCR     B       0 0 0 0 1 0 1 0 0 0 1 1 1 1 0 0     B I
EORI to SR       W      0 0 0 0 1 0 1 0 0 1 1 1 1 1 0 0     W I
EORI            BWL     0 0 0 0 1 0 1 0 Sx  M     Xn        / I
CMPI            BWL     0 0 0 0 1 1 0 0 Sx  M     Xn        / I
BTST            B L     0 0 0 0 1 0 0 0 0 0 M     Xn        B N
BCHG            B L     0 0 0 0 1 0 0 0 0 1 M     Xn        B N
BCLR            B L     0 0 0 0 1 0 0 0 1 0 M     Xn        B N
BSET            B L     0 0 0 0 1 0 0 0 1 1 M     Xn        B N
BTST            B L     0 0 0 0 Dn    1 0 0 M     Xn        B N
BCHG            B L     0 0 0 0 Dn    1 0 1 M     Xn        B N
BCLR            B L     0 0 0 0 Dn    1 1 0 M     Xn        B N
BSET            B L     0 0 0 0 Dn    1 1 1 M     Xn        B N
MOVEP            WL     0 0 0 0 Dn    1 DxSz0 0 1 An        W D
MOVEA            WL     0 0 Sy  An    0 0 1 M     Xn
MOVE            BWL     0 0 Sy  Xn    M     M     Xn
MOVE from SR     W      0 1 0 0 0 0 0 0 1 1 M     Xn
MOVE to CCR     B       0 1 0 0 0 1 0 0 1 1 M     Xn
MOVE to SR       W      0 1 0 0 0 1 1 0 1 1 M     Xn
NEGX            BWL     0 1 0 0 0 0 0 0 Sx  M     Xn
CLRX            BWL     0 1 0 0 0 0 1 0 Sx  M     Xn
NEG             BWL     0 1 0 0 0 1 0 0 Sx  M     Xn
NOT             BWL     0 1 0 0 0 1 1 0 Sx  M     Xn
EXT              WL     0 1 0 0 1 0 0 0 1 Sz0 0 0 Dn
NBCD            B       0 1 0 0 1 0 0 0 0 0 M     Xn
SWAP             W      0 1 0 0 1 0 0 0 0 1 0 0 0 Dn
PEA               L     0 1 0 0 1 0 0 0 0 1 M     Xn
ILLEGAL                 0 1 0 0 1 0 1 0 1 1 1 1 1 1 0 0
TAS             B       0 1 0 0 1 0 1 0 1 1 M     Xn
TST             BWL     0 1 0 0 1 0 1 0 Sx  M     Xn
TRAP                    0 1 0 0 1 1 1 0 0 1 0 0 Vector
LINK             W      0 1 0 0 1 1 1 0 0 1 0 1 0 An
UNLK                    0 1 0 0 1 1 1 0 0 1 0 1 1 An
MOVE USP          L     0 1 0 0 1 1 1 0 0 1 1 0 D An
RESET                   0 1 0 0 1 1 1 0 0 1 1 1 0 0 0 0
NOP                     0 1 0 0 1 1 1 0 0 1 1 1 0 0 0 1
STOP                    0 1 0 0 1 1 1 0 0 1 1 1 0 0 1 0     W I
RTE                     0 1 0 0 1 1 1 0 0 1 1 1 0 0 1 1
RTS                     0 1 0 0 1 1 1 0 0 1 1 1 0 1 0 1
TRAPV                   0 1 0 0 1 1 1 0 0 1 1 1 0 1 1 0
RTR                     0 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1
JSR                     0 1 0 0 1 1 1 0 1 0 M     Xn
JMP                     0 1 0 0 1 1 1 0 1 1 M     Xn
MOVEM            WL     0 1 0 0 1 D 0 0 1 SzM     Xn        W M
LEA               L     0 1 0 0 An    1 1 1 M     Xn
CHK              W      0 1 0 0 Dn    1 1 0 SzM   Xn
ADDQ            BWL     0 1 0 1 Data3 0 Sx  M     Xn
SUBQ            BWL     0 1 0 1 Data3 1 Sx  M     Xn
Scc             B       0 1 0 1 Cond    1 1 M     Xn
DBcc             W      0 1 0 1 Cond    1 1 0 0 1 Dn        W D
BRA             BW      0 1 1 0 0 0 0 0 Displacment         W D
BSR             BW      0 1 1 0 0 0 0 1 Displacment         W D
Bcc             BW      0 1 1 0 Cond    Displacment         W D
MOVEQ             L     0 1 1 1 Dn    0 Data8
DIVU             W      1 0 0 0 Dn    0 1 1 M     Xn
DIVS             W      1 0 0 0 Dn    1 1 1 M     Xn
SBCD            B       1 0 0 0 Xn    1 0 0 0 0 m Xn
OR              BWL     1 0 0 0 Dn    D Sx  M     Xn
SUB             BWL     1 0 0 1 Dn    D Sx  M     Xn
SUBX            BWL     1 0 0 1 Xn    1 Sx  0 0 m Xn
SUBA             WL     1 0 0 1 An    Sz1 1 M     Xn
EOR             BWL     1 0 1 1 Dn    1 Sx  M     Xn
CMPM            BWL     1 0 1 1 An    1 Sx  M     Xn
CMP             BWL     1 0 1 1 Dn    0 Sx  M     Xn
CMPA             WL     1 0 1 1 An    Sz  1 M     Xn
MULU             W      1 1 0 0 Dn    0 1 1 M     Xn
MULS             W      1 1 0 0 Dn    1 1 1 M     Xn
ABCD            B       1 1 0 0 Xn    1 0 0 0 0 m Xn
AND             BWL     1 1 0 0 Dn    D Sx  M     Xn
ADD             BWL     1 1 0 1 Dn    D Sx  M     Xn
ADDX            BWL     1 1 0 1 Xn    1 Sx  0 0 m Xn
ADDA             WL     1 1 0 1 An    Sz1 1 M     Xn
ASd             BWL     1 1 1 0 0 0 0 D 1 1 M     Xn
LSd             BWL     1 1 1 0 0 0 0 D 1 1 M     Xn
ROXd            BWL     1 1 1 0 0 0 0 D 1 1 M     Xn
ROd             BWL     1 1 1 0 0 0 0 D 1 1 M     Xn
ASd             BWL     1 1 1 0 Rot   D Sx  Mr0 0 Dn
LSd             BWL     1 1 1 0 Rot   D Sx  Mr0 1 Dn
ROXd            BWL     1 1 1 0 Rot   D Sx  Mr1 0 Dn
ROd             BWL     1 1 1 0 Rot   D Sx  Mr1 1 Dn
*/

int main()
{
}
