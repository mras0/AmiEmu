#include "test68k.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>

#include "ioutil.h"
#include "cpu.h"
#include "memory.h"
#include "disasm.h"
#include "asm.h"

// From Yacht (Yet Another Cycle Hunting Table)                
// With some modifications
namespace {

const char* const line0000_immediate = R"(
#<data>,<ea> :    |                
  .B or .W :      |                
    Dn            |  8(2/0)  0(0/0)
    (An)          | 12(2/1)  4(1/0)
    (An)+         | 12(2/1)  4(1/0)
    -(An)         | 12(2/1)  6(1/0)
    (d16,An)      | 12(2/1)  8(2/0)
    (d8,An,Xn)    | 12(2/1) 10(2/0)
    (xxx).W       | 12(2/1)  8(2/0)
    (xxx).L       | 12(2/1) 12(3/0)
  .L :            |
    Dn            | 16(3/0)  0(0/0)
    (An)          | 20(3/2)  8(2/0)
    (An)+         | 20(3/2)  8(2/0)
    -(An)         | 20(3/2) 10(2/0)
    (d16,An)      | 20(3/2) 12(3/0)
    (d8,An,Xn)    | 20(3/2) 14(3/0)
    (xxx).W       | 20(3/2) 12(3/0)
    (xxx).L       | 20(3/2) 16(4/0)
)";

const char* const cmpi_table = R"(
#<data>,<ea> :    |                
  .B or .W :      |                
    Dn            |  8(2/0)  0(0/0)
    (An)          |  8(2/0)  4(1/0)
    (An)+         |  8(2/0)  4(1/0)
    -(An)         |  8(2/0)  6(1/0)
    (d16,An)      |  8(2/0)  8(2/0)
    (d8,An,Xn)    |  8(2/0) 10(2/0)
    (xxx).W       |  8(2/0)  8(2/0)
    (xxx).L       |  8(2/0) 12(3/0)
  .L :            |                
    Dn            | 14(3/0)  0(0/0)
    (An)          | 12(3/0)  8(2/0)
    (An)+         | 12(3/0)  8(2/0)
    -(An)         | 12(3/0) 10(2/0)
    (d16,An)      | 12(3/0) 12(3/0)
    (d8,An,Xn)    | 12(3/0) 14(3/0)
    (xxx).W       | 12(3/0) 12(3/0)
    (xxx).L       | 12(3/0) 16(4/0)
)";

const char* const bchg_bset_table = R"(
Dn,<ea> :         |                 
  .B :            |                 
    (An)          |  8(1/1)  4(1/0) 
    (An)+         |  8(1/1)  4(1/0) 
    -(An)         |  8(1/1)  6(1/0) 
    (d16,An)      |  8(1/1)  8(2/0) 
    (d8,An,Xn)    |  8(1/1) 10(2/0) 
    (xxx).W       |  8(1/1)  8(2/0) 
    (xxx).L       |  8(1/1) 12(3/0) 
#<data>,<ea> :    |                 
  .B :            |                 
    (An)          | 12(2/1)  4(1/0) 
    (An)+         | 12(2/1)  4(1/0) 
    -(An)         | 12(2/1)  6(1/0) 
    (d16,An)      | 12(2/1)  8(2/0) 
    (d8,An,Xn)    | 12(2/1) 10(2/0) 
    (xxx).W       | 12(2/1)  8(2/0) 
    (xxx).L       | 12(2/1) 12(3/0) 
)";

const char* const bclr_table = R"(
Dn,<ea> :         |                
  .B :            |                
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
#<data>,<ea> :    |                
  .B :            |                
    (An)          | 12(2/1)  4(1/0)
    (An)+         | 12(2/1)  4(1/0)
    -(An)         | 12(2/1)  6(1/0)
    (d16,An)      | 12(2/1)  8(2/0)
    (d8,An,Xn)    | 12(2/1) 10(2/0)
    (xxx).W       | 12(2/1)  8(2/0)
    (xxx).L       | 12(2/1) 12(3/0)
)";

const char* const btst_table = R"(
Dn,<ea> :         |                
  .B :            |                
    (An)          |  4(1/0)  4(1/0)
    (An)+         |  4(1/0)  4(1/0)
    -(An)         |  4(1/0)  6(1/0)
    (d16,An)      |  4(1/0)  8(2/0)
    (d8,An,Xn)    |  4(1/0) 10(2/0)
    (xxx).W       |  4(1/0)  8(2/0)
    (xxx).L       |  4(1/0) 12(3/0)
Dn,Dm :           |                
  .L :            |  6(1/0)  0(0/0)
#<data>,<ea> :    |                
  .B :            |                
    (An)          |  8(2/0)  4(1/0)
    (An)+         |  8(2/0)  4(1/0)
    -(An)         |  8(2/0)  6(1/0)
    (d16,An)      |  8(2/0)  8(2/0)
    (d8,An,Xn)    |  8(2/0) 10(2/0)
    (xxx).W       |  8(2/0)  8(2/0)
    (xxx).L       |  8(2/0) 12(3/0)
#<data>,Dn :      |                
  .L :            | 10(2/0)  0(0/0)
)";

const char* const move_table = R"(
<ea>,Dn :         |        
  .B or .W :      |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          |  8(2/0)
    (An)+         |  8(2/0)
    -(An)         | 10(2/0)
    (d16,An)      | 12(3/0)
    (d8,An,Xn)    | 14(3/0)
    (xxx).W       | 12(3/0)
    (xxx).L       | 16(4/0)
    #<data>       |  8(2/0)
  .L :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          | 12(3/0)
    (An)+         | 12(3/0)
    -(An)         | 14(3/0)
    (d16,An)      | 16(4/0)
    (d8,An,Xn)    | 18(4/0)
    (xxx).W       | 16(4/0)
    (xxx).L       | 20(5/0)
    #<data>       | 12(3/0)
<ea>,(An) :       |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,(An)+ :      |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,-(An) :      |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,(d16,An) :   |        
  .B or .W :      |        
    Dn            | 12(2/1)
    An            | 12(2/1)
    (An)          | 16(3/1)
    (An)+         | 16(3/1)
    -(An)         | 18(3/1)
    (d16,An)      | 20(4/1)
    (d8,An,Xn)    | 22(4/1)
    (xxx).W       | 20(4/1)
    (xxx).L       | 24(5/1)
    #<data>       | 16(3/1)
  .L :            |        
    Dn            | 16(2/2)
    An            | 16(2/2)
    (An)          | 24(4/2)
    (An)+         | 24(4/2)
    -(An)         | 26(4/2)
    (d16,An)      | 28(5/2)
    (d8,An,Xn)    | 30(5/2)
    (xxx).W       | 28(5/2)
    (xxx).L       | 32(6/2)
    #<data>       | 24(4/2)
<ea>,(d8,An,Xn) : |        
  .B or .W :      |        
    Dn            | 14(2/1)
    An            | 14(2/1)
    (An)          | 18(3/1)
    (An)+         | 18(3/1)
    -(An)         | 20(3/1)
    (d16,An)      | 22(4/1)
    (d8,An,Xn)    | 24(4/1)
    (xxx).W       | 22(4/1)
    (xxx).L       | 26(5/1)
    #<data>       | 18(3/1)
  .L :            |        
    Dn            | 18(2/2)
    An            | 18(2/2)
    (An)          | 26(4/2)
    (An)+         | 26(4/2)
    -(An)         | 28(4/2)
    (d16,An)      | 30(5/2)
    (d8,An,Xn)    | 32(5/2)
    (xxx).W       | 30(5/2)
    (xxx).L       | 34(6/2)
    #<data>       | 26(4/2)
<ea>,(xxx).W :    |        
  .B or .W :      |        
    Dn            | 12(2/1)
    An            | 12(2/1)
    (An)          | 16(3/1)
    (An)+         | 16(3/1)
    -(An)         | 18(3/1)
    (d16,An)      | 20(4/1)
    (d8,An,Xn)    | 22(4/1)
    (xxx).W       | 20(4/1)
    (xxx).L       | 24(5/1)
    #<data>       | 16(3/1)
  .L :            |        
    Dn            | 16(2/2)
    An            | 16(2/2)
    (An)          | 24(4/2)
    (An)+         | 24(4/2)
    -(An)         | 26(4/2)
    (d16,An)      | 28(5/2)
    (d8,An,Xn)    | 30(5/2)
    (xxx).W       | 28(5/2)
    (xxx).L       | 32(6/2)
    #<data>       | 24(4/2)
<ea>,(xxx).L :    |        
  .B or .W :      |        
    Dn            | 16(3/1)
    An            | 16(3/1)
    (An)          | 20(4/1)
    (An)+         | 20(4/1)
    -(An)         | 22(4/1)
    (d16,An)      | 24(5/1)
    (d8,An,Xn)    | 26(5/1)
    (xxx).W       | 24(5/1)
    (xxx).L       | 28(6/1)
    #<data>       | 20(4/1)
  .L :            |        
    Dn            | 20(3/2)
    An            | 20(3/2)
    (An)          | 28(5/2)
    (An)+         | 28(5/2)
    -(An)         | 30(5/2)
    (d16,An)      | 32(6/2)
    (d8,An,Xn)    | 34(6/2)
    (xxx).W       | 32(6/2)
    (xxx).L       | 36(7/2)
    #<data>       | 28(5/2)
<ea>,An :         |        
  .W :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          |  8(2/0)
    (An)+         |  8(2/0)
    -(An)         | 10(2/0)
    (d16,An)      | 12(3/0)
    (d8,An,Xn)    | 14(3/0)
    (xxx).W       | 12(3/0)
    (xxx).L       | 16(4/0)
    #<data>       |  8(2/0)
  .L :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          | 12(3/0)
    (An)+         | 12(3/0)
    -(An)         | 14(3/0)
    (d16,An)      | 16(4/0)
    (d8,An,Xn)    | 18(4/0)
    (xxx).W       | 16(4/0)
    (xxx).L       | 20(5/0)
    #<data>       | 12(3/0)
)";

const char* const line1111_rmw_table = R"(
<ea> :            |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    Dn            |  6(1/0)  0(0/0)
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
)";

const char* const nbcd_table = R"(
<ea> :            |                
  .B :            |                
    Dn            |  6(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
)";

const char* const move_sr_table = R"(
SR,<ea> :         |                
  .W :            |                
    Dn            |  6(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
<ea>,SR :         |                
  .W :            |                
    Dn            | 12(1/0)  0(0/0)
    (An)          | 12(1/0)  4(1/0)
    (An)+         | 12(1/0)  4(1/0)
    -(An)         | 12(1/0)  6(1/0)
    (d16,An)      | 12(1/0)  8(2/0)
    (d8,An,Xn)    | 12(1/0) 10(2/0)
    (xxx).W       | 12(1/0)  8(2/0)
    (xxx).L       | 12(1/0) 12(3/0)
    #<data>       | 12(1/0)  4(1/0)
<ea>,CCR :        |                
  .W :            |                
    Dn            | 12(1/0)  0(0/0)
    (An)          | 12(1/0)  4(1/0)
    (An)+         | 12(1/0)  4(1/0)
    -(An)         | 12(1/0)  6(1/0)
    (d16,An)      | 12(1/0)  8(2/0)
    (d8,An,Xn)    | 12(1/0) 10(2/0)
    (xxx).W       | 12(1/0)  8(2/0)
    (xxx).L       | 12(1/0) 12(3/0)
    #<data>       | 12(1/0)  4(1/0)
)";

const char* const pea_table = R"(
<ea> :            |        
  .L :            |        
    (An)          | 12(1/2)
    (d16,An)      | 16(2/2)
    (d8,An,Xn)    | 20(2/2)
    (xxx).W       | 16(2/2)
    (xxx).L       | 20(3/2)
)";

const char* const swap_table = R"(
<ea> :            |                 
  .W :            |
    Dn            |  4(1/0)
)";

const char* const ext_table = R"(
<ea> :            |                 
  .W :            |  
    Dn            |  4(1/0)         
  .L :            | 
    Dn            |  4(1/0)         
)";

const char* const tst_table = R"(
<ea> :            |                 
  .B or .W :      |                 
    Dn            |  4(1/0)  0(0/0) 
    (An)          |  4(1/0)  4(1/0) 
    (An)+         |  4(1/0)  4(1/0) 
    -(An)         |  4(1/0)  6(1/0) 
    (d16,An)      |  4(1/0)  8(2/0) 
    (d8,An,Xn)    |  4(1/0) 10(2/0) 
    (xxx).W       |  4(1/0)  8(2/0) 
    (xxx).L       |  4(1/0) 12(3/0) 
  .L :            |                 
    Dn            |  4(1/0)  0(0/0) 
    (An)          |  4(1/0)  8(2/0) 
    (An)+         |  4(1/0)  8(2/0) 
    -(An)         |  4(1/0) 10(2/0) 
    (d16,An)      |  4(1/0) 12(3/0) 
    (d8,An,Xn)    |  4(1/0) 14(3/0) 
    (xxx).W       |  4(1/0) 12(3/0) 
    (xxx).L       |  4(1/0) 16(4/0) 
)";

const char* const tas_table = R"(
<ea> :            |                
  .B :            |                
    Dn            |  4(1/0)  0(0/0)
    (An)          | 10(1/1)  4(1/0)
    (An)+         | 10(1/1)  4(1/0)
    -(An)         | 10(1/1)  6(1/0)
    (d16,An)      | 10(1/1)  8(2/0)
    (d8,An,Xn)    | 10(1/1) 10(2/0)
    (xxx).W       | 10(1/1)  8(1/0)
    (xxx).L       | 10(1/1) 12(2/0)
)";

const char* const add_sub_table = R"(
<ea>,Dn :         |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    An            |  4(1/0)  0(0/0)
    (An)          |  4(1/0)  4(1/0)
    (An)+         |  4(1/0)  4(1/0)
    -(An)         |  4(1/0)  6(1/0)
    (d16,An)      |  4(1/0)  8(2/0)
    (d8,An,Xn)    |  4(1/0) 10(2/0)
    (xxx).W       |  4(1/0)  8(2/0)
    (xxx).L       |  4(1/0) 12(3/0)
    #<data>       |  4(1/0)  4(1/0)
  .L :            |                
    Dn            |  8(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          |  6(1/0)  8(2/0)
    (An)+         |  6(1/0)  8(2/0)
    -(An)         |  6(1/0) 10(2/0)
    (d16,An)      |  6(1/0) 12(3/0)
    (d8,An,Xn)    |  6(1/0) 14(3/0)
    (xxx).W       |  6(1/0) 12(3/0)
    (xxx).L       |  6(1/0) 16(4/0)
    #<data>       |  8(1/0)  8(2/0)
Dn,<ea> :         |                
  .B or .W :      |                
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
<ea>,An :         |                
  .W :            |                
    Dn            |  8(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          |  8(1/0)  4(1/0)
    (An)+         |  8(1/0)  4(1/0)
    -(An)         |  8(1/0)  6(1/0)
    (d16,An)      |  8(1/0)  8(2/0)
    (d8,An,Xn)    |  8(1/0) 10(2/0)
    (xxx).W       |  8(1/0)  8(2/0)
    (xxx).L       |  8(1/0) 12(3/0)
    #<data>       |  8(1/0)  4(1/0)
  .L :            |                
    Dn            |  8(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          |  6(1/0)  8(2/0)
    (An)+         |  6(1/0)  8(2/0)
    -(An)         |  6(1/0) 10(2/0)
    (d16,An)      |  6(1/0) 12(3/0)
    (d8,An,Xn)    |  6(1/0) 14(3/0)
    (xxx).W       |  6(1/0) 12(3/0)
    (xxx).L       |  6(1/0) 16(4/0)
    #<data>       |  8(1/0)  8(2/0)
)";

const char* const addx_subx_table = R"(
<ea> :            |
  .B or .W :      |
    Dy,Dx         |  4(1/0)
    -(Ay),-(Ax)   | 18(3/1)
  .L :            |
    Dy,Dx         |  8(1/0)
    -(Ay),-(Ax)   | 30(5/2)
)";

const char* const lea_table = R"(
<ea>,An :         |         
  .L :            |         
    (An)          |  4(1/0) 
    (d16,An)      |  8(2/0) 
    (d8,An,Xn)    | 12(2/0) 
    (xxx).W       |  8(2/0) 
    (xxx).L       | 12(3/0) 
)";

const char* const jsr_table = R"(
<ea> :            |         
    (An)          | 16(2/2) 
    (d16,An)      | 18(2/2) 
    (d8,An,Xn)    | 22(2/2) 
    (xxx).W       | 18(2/2) 
    (xxx).L       | 20(3/2) 
)";

const char* const jmp_table = R"(
<ea> :            |         
    (An)          |  8(2/0) 
    (d16,An)      | 10(2/0) 
    (d8,An,Xn)    | 14(2/0) 
    (xxx).W       | 10(2/0) 
    (xxx).L       | 12(3/0) 
)";

const char* const addq_subq_table = R"(
#<data>,<ea> :    |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    Dn            |  8(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
)";

const char* const and_or_table = R"(
<ea>,Dn :         |                 
  .B or .W :      |                 
    Dn            |  4(1/0)  0(0/0) 
    (An)          |  4(1/0)  4(1/0) 
    (An)+         |  4(1/0)  4(1/0) 
    -(An)         |  4(1/0)  6(1/0) 
    (d16,An)      |  4(1/0)  8(2/0) 
    (d8,An,Xn)    |  4(1/0) 10(2/0) 
    (xxx).W       |  4(1/0)  8(2/0) 
    (xxx).L       |  4(1/0) 12(3/0) 
    #<data>       |  4(1/0)  4(1/0) 
  .L :            |                 
    Dn            |  8(1/0)  0(0/0) 
    (An)          |  6(1/0)  8(2/0) 
    (An)+         |  6(1/0)  8(2/0) 
    -(An)         |  6(1/0) 10(2/0) 
    (d16,An)      |  6(1/0) 12(3/0) 
    (d8,An,Xn)    |  6(1/0) 14(3/0) 
    (xxx).W       |  6(1/0) 12(3/0) 
    (xxx).L       |  6(1/0) 16(4/0) 
    #<data>       |  8(1/0)  8(2/0) 
Dn,<ea> :         |                 
  .B or .W :      |                 
    (An)          |  8(1/1)  4(1/0) 
    (An)+         |  8(1/1)  4(1/0) 
    -(An)         |  8(1/1)  6(1/0) 
    (d16,An)      |  8(1/1)  8(2/0) 
    (d8,An,Xn)    |  8(1/1) 10(2/0) 
    (xxx).W       |  8(1/1)  8(2/0) 
    (xxx).L       |  8(1/1) 12(3/0) 
  .L :            |                 
    (An)          | 12(1/2)  8(2/0) 
    (An)+         | 12(1/2)  8(2/0) 
    -(An)         | 12(1/2) 10(2/0) 
    (d16,An)      | 12(1/2) 12(3/0) 
    (d8,An,Xn)    | 12(1/2) 14(3/0) 
    (xxx).W       | 12(1/2) 12(3/0) 
    (xxx).L       | 12(1/2) 16(4/0) 
)";

const char* const exg_table = R"(
  .L :            |        
    Dx,Dy         |  6(1/0)
    Ax,Ay         |  6(1/0)
    Dx,Ay         |  6(1/0)
)";

const char* const cmp_table = R"(
<ea>,Dn :         |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    An            |  4(1/0)  0(0/0)
    (An)          |  4(1/0)  4(1/0)
    (An)+         |  4(1/0)  4(1/0)
    -(An)         |  4(1/0)  6(1/0)
    (d16,An)      |  4(1/0)  8(2/0)
    (d8,An,Xn)    |  4(1/0) 10(2/0)
    (xxx).W       |  4(1/0)  8(2/0)
    (xxx).L       |  4(1/0) 12(3/0)
    #<data>       |  4(1/0)  4(1/0)
  .L :            |                
    Dn            |  6(1/0)  0(0/0)
    An            |  6(1/0)  0(0/0)
    (An)          |  6(1/0)  8(2/0)
    (An)+         |  6(1/0)  8(2/0)
    -(An)         |  6(1/0) 10(2/0)
    (d16,An)      |  6(1/0) 12(3/0)
    (d8,An,Xn)    |  6(1/0) 14(3/0)
    (xxx).W       |  6(1/0) 12(3/0)
    (xxx).L       |  6(1/0) 16(4/0)
    #<data>       |  6(1/0)  8(2/0)
<ea>,An :         |                
  .B or .W :      |                
    Dn            |  6(1/0)  0(0/0)
    An            |  6(1/0)  0(0/0)
    (An)          |  6(1/0)  4(1/0)
    (An)+         |  6(1/0)  4(1/0)
    -(An)         |  6(1/0)  6(1/0)
    (d16,An)      |  6(1/0)  8(2/0)
    (d8,An,Xn)    |  6(1/0) 10(2/0)
    (xxx).W       |  6(1/0)  8(2/0)
    (xxx).L       |  6(1/0) 12(3/0)
    #<data>       |  6(1/0)  4(1/0)
  .L :            |                
    Dn            |  6(1/0)  0(0/0)
    An            |  6(1/0)  0(0/0)
    (An)          |  6(1/0)  8(2/0)
    (An)+         |  6(1/0)  8(2/0)
    -(An)         |  6(1/0) 10(2/0)
    (d16,An)      |  6(1/0) 12(3/0)
    (d8,An,Xn)    |  6(1/0) 14(3/0)
    (xxx).W       |  6(1/0) 12(3/0)
    (xxx).L       |  6(1/0) 16(4/0)
    #<data>       |  6(1/0)  8(2/0)
)";

const char* const eor_table = R"(
Dn,<ea> :         |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    Dn            |  8(1/0)  0(0/0)
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
)";

const char* const abcd_sbcd_table = R"(
<ea> :            |        
  .B :            |
    Dy,Dx         |  6(1/0)
    -(Ay),-(Ax)   | 18(3/1)
)";

const char* const shift_rot_table = R"(
<ea> :            |                
  .W :            |                
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
)";

const struct {
    const char* table;
    std::vector<std::string> instructions;
} timing_tests[] = {
    { line0000_immediate , { "EOR", "OR", "AND", "SUB", "ADD" } },
    { cmpi_table         , { "CMP" } },
    { bchg_bset_table    , { "BCHG", "BSET" } },
    { bclr_table         , { "BCLR" } },
    { btst_table         , { "BTST" } },
    { move_table         , { "MOVE" } },
    { line1111_rmw_table , { "CLR", "NEGX", "NEG", "NOT" } },
    { nbcd_table         , { "NBCD" } },
    { move_sr_table      , { "MOVE" } },
    { pea_table          , { "PEA" } },
    { swap_table         , { "SWAP" } },
    { ext_table          , { "EXT" } },
    { tst_table          , { "TST" } },
    { tas_table          , { "TAS" } },
    { lea_table          , { "LEA" } },
    { jsr_table          , { "JSR" } },
    { jmp_table          , { "JMP" } },
    { addq_subq_table    , { "ADDQ", "SUBQ" } },
    { and_or_table       , { "AND", "OR" } },
    { exg_table          , { "EXG" } },
    { add_sub_table      , { "ADD", "SUB" } },
    { addx_subx_table    , { "ADDX", "SUBX" } },
    { cmp_table          , { "CMP" } },
    { eor_table          , { "EOR" } },
    { abcd_sbcd_table    , { "ABCD", "SBCD" } },
    { shift_rot_table    , { "ASL", "ASR", "LSL", "LSR", "ROL", "ROR", "ROXL", "ROXR" } },
};

std::pair<unsigned, unsigned> interpret_cycle_text(const std::string& s)
{
    unsigned ic, ir, iw, ec, er, ew;
    char sep[6];
    std::istringstream iss { s };
    if (iss >> ic >> sep[0] >> ir >> sep[1] >> iw >> sep[2]) {
        assert(sep[0] == '(');
        assert(sep[1] == '/');
        assert(sep[2] == ')');
        if (iss >> ec >> sep[3] >> er >> sep[4] >> ew >> sep[5]) {
            assert(sep[3] == '(');
            assert(sep[4] == '/');
            assert(sep[5] == ')');
            return { ic + ec, ir + iw + er + ew };
        }
        return { ic, ir + iw }; // For move
    }
    throw std::runtime_error { "Unsupported cycle specification: " + s };
}

std::string replace(const std::string& templ, const std::string& needle, const std::string& repl)
{
    auto res = templ;
    for (;;) {
        auto pos = res.find(needle);
        if (pos == std::string::npos)
            return res;
        res = res.substr(0, pos) + repl + res.substr(pos + needle.length());
    }
}

struct tester {
    static constexpr uint32_t code_pos = 0x1000;
    static constexpr uint32_t code_size = 0x2000;
    memory_handler mem { code_pos + code_size };
    std::vector<uint16_t> code_words;

    bool tests_from_table(const char* table, const std::vector<std::string>& insts)
    {
        std::istringstream iss { table };
        std::string code_template;
        std::vector<std::string> sizes;

        auto& ram = mem.ram();

        bool all_ok = true;
        bool any = false;
        
        for (std::string line; std::getline(iss, line);) {
            if (line.empty())
                continue;
            assert(line.length() > 10);
            const auto divpos = line.find_first_of('|');
            assert(divpos != std::string::npos);
            //std::cout << line << "\n";
            if (line[0] != ' ') {
                code_template = trim(line.substr(0, line.find_first_of(':')));
                continue;
            }
            if (line[2] == '.') {
                if (line.compare(2, 10, ".B or .W :") == 0)
                    sizes = { ".B", ".W" };
                else if (line.compare(2, 4, ".B :") == 0)
                    sizes = { ".B" };
                else if (line.compare(2, 4, ".W :") == 0)
                    sizes = { ".W" };
                else if (line.compare(2, 4, ".L :") == 0)
                    sizes = { ".L" };
                else
                    throw std::runtime_error { "Size not recognied: " + line };
                continue;
            }
            assert(line.compare(0, 4, "    ") == 0);
            auto replacement = trim(line.substr(4, divpos - 4));
            const auto [cycles, memaccesses] = interpret_cycle_text(line.substr(divpos + 1));

            if (sizes.empty())
                sizes = { "" };

            //std::cout << "\tReplacement = \"" << replacement << "\" cycles:" << cycles << "/" << memaccesses << "\n";

            for (const auto& inst : insts) {
                const char* data = "$34";
                if (inst.back() == 'Q')
                    data = "$8";

                for (const auto& s : sizes) {
                    if (s == ".B" && (replacement == "An" || code_template.find(",An") != std::string::npos))
                        continue;

                    auto args = code_template.empty() ? replacement : replace(code_template, "<ea>", replacement);

                    args = replace(args, "-(An)", "-(A2)");
                    args = replace(args, "-(Ax)", "-(A2)");
                    args = replace(args, "-(Ay)", "-(A2)");
                    args = replace(args, "(xxx)", "$12");
                    args = replace(args, "(d16,An)", "$12(A0)");
                    args = replace(args, "(d8,An,Xn)", "$12(A0,D0.L)");
                    args = replace(args, "Dn", "D1");
                    args = replace(args, "Dx", "D2");
                    args = replace(args, "Dy", "D3");
                    args = replace(args, "An", "A3");
                    args = replace(args, "Ax", "A3");
                    args = replace(args, "Ay", "A4");
                    args = replace(args, "<data>", data);



                    //std::cout << "\tTest " << i << s << "\t" << args << "!\n";

                    const auto stmt = inst + s + "\t" + args;

                    try {
                        code_words = assemble(code_pos, stmt.c_str());
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << " while assembling:\n" << stmt << "\n";
                        return false;
                    }

                    memset(&ram[0], 0, ram.size());
                    for (size_t i = 0; i < std::size(code_words); ++i)
                        put_u16(&ram[code_pos + i * 2], code_words[i]);

                    cpu_state input_state {};
                    input_state.sr = 0x2000;
                    input_state.pc = code_pos;
                    input_state.a[2] = 8;
                    input_state.usp = 0x400;
                    input_state.ssp = 0x400;

                    m68000 cpu { mem, input_state };

                    const auto step_res = cpu.step();
                    if (step_res.clock_cycles != cycles || step_res.mem_accesses != memaccesses) {
                        std::cerr << "Test failed for:\n" << stmt << "\n";
                        std::cerr << "Expected clock cycles:    " << (int)cycles << "\tActual: " << (int)step_res.clock_cycles << "\n";
                        std::cerr << "Expected memory accesses: " << (int)memaccesses << "\tActual: " << (int)step_res.mem_accesses << "\n";
                        //return false;
                        all_ok = false;
                    }

                    any = true;
                }
            }
        }
        if (!any) {
            std::cout << "Warning: No tests for";
            for (const auto& inst : insts)
                std::cout << " " << inst;
            std::cout << "\n";
        }
        return all_ok;
    }
};

bool run_timing_tests()
{
    const struct {
        const char* test_inst;
        uint8_t clock_cycles, memory_accesses;
    } test_cases[] = {
        { "MOVE.B D0, D1"                   ,  4, 1 },
        { "MOVE.B (A0), D1"                 ,  8, 2 },
        { "MOVE.B (A0)+, D0"                ,  8, 2 },
        { "MOVE.B -(A0), D0"                , 10, 2 },
        { "MOVE.B 10(A0), D0"               , 12, 3 },
        { "MOVE.B 2(A0,D0.L), D0"           , 14, 3 },
        { "MOVE.B 0.W, D0"                  , 12, 3 },
        { "MOVE.B 0.L, D0"                  , 16, 4 },
        { "MOVE.B 0(pc), D0"                , 12, 3 },
        { "MOVE.B 0(pc,D0.L), D0"           , 14, 3 },
        { "MOVE.B #$12, D0"                 ,  8, 2 },

        { "MOVE.W D0, D1"                   ,  4, 1 },
        { "MOVE.W A0, D1"                   ,  4, 1 },
        { "MOVE.W (A0), D1"                 ,  8, 2 },
        { "MOVE.W (A0)+, D0"                ,  8, 2 },
        { "MOVE.W -(A0), D0"                , 10, 2 },
        { "MOVE.W 10(A0), D0"               , 12, 3 },
        { "MOVE.W 2(A0,D0.L), D0"           , 14, 3 },
        { "MOVE.W 0.W, D0"                  , 12, 3 },
        { "MOVE.W 0.L, D0"                  , 16, 4 },
        { "MOVE.W 0(pc), D0"                , 12, 3 },
        { "MOVE.W 0(pc,D0.L), D0"           , 14, 3 },
        { "MOVE.W #$12, D0"                 ,  8, 2 },

        { "MOVE.L D0, D1"                   ,  4, 1 },
        { "MOVE.L A0, D1"                   ,  4, 1 },
        { "MOVE.L (A0), D1"                 , 12, 3 },
        { "MOVE.L (A0)+, D0"                , 12, 3 },
        { "MOVE.L -(A0), D0"                , 14, 3 },
        { "MOVE.L 10(A0), D0"               , 16, 4 },
        { "MOVE.L 2(A0,D0.L), D0"           , 18, 4 },
        { "MOVE.L 0.W, D0"                  , 16, 4 },
        { "MOVE.L 0.L, D0"                  , 20, 5 },
        { "MOVE.L 0(pc), D0"                , 16, 4 },
        { "MOVE.L 0(pc,D0.L), D0"           , 18, 4 },
        { "MOVE.L #$12, D0"                 , 12, 3 },

        { "MOVE.W D0, (A0)"                 ,  8, 2 },
        { "MOVE.W A0, (A0)"                 ,  8, 2 },
        { "MOVE.W (A0), (A0)"               , 12, 3 },
        { "MOVE.W (A0)+, (A0)"              , 12, 3 },
        { "MOVE.W -(A1), (A0)"              , 14, 3 },
        { "MOVE.W 10(A0), (A0)"             , 16, 4 },
        { "MOVE.W 2(A0,D0.L), (A0)"         , 18, 4 },
        { "MOVE.W 0.W, (A0)"                , 16, 4 },
        { "MOVE.W 0.L, (A0)"                , 20, 5 },
        { "MOVE.W #$12, (A0)"               , 12, 3 },

        { "MOVE.L D0, (A0)"                 , 12, 3 },
        { "MOVE.L A0, (A0)"                 , 12, 3 },
        { "MOVE.L (A0), (A0)"               , 20, 5 },
        { "MOVE.L (A0)+, (A0)"              , 20, 5 },
        { "MOVE.L -(A1), (A0)"              , 22, 5 },
        { "MOVE.L 10(A0), (A0)"             , 24, 6 },
        { "MOVE.L 2(A0,D0.L), (A0)"         , 26, 6 },
        { "MOVE.L 0.W, (A0)"                , 24, 6 },
        { "MOVE.L 0.L, (A0)"                , 28, 7 },
        { "MOVE.L #$12, (A0)"               , 20, 5 },

        { "MOVE.W D0, (A2)+"                ,  8, 2 },
        { "MOVE.W A0, (A2)+"                ,  8, 2 },
        { "MOVE.W (A0), (A2)+"              , 12, 3 },
        { "MOVE.W (A0), (A2)+"              , 12, 3 },
        { "MOVE.W -(A1), (A2)+"             , 14, 3 },
        { "MOVE.W 10(A0), (A2)+"            , 16, 4 },
        { "MOVE.W 2(A0,D0.L), (A2)+"        , 18, 4 },
        { "MOVE.W 0.W, (A2)+"               , 16, 4 },
        { "MOVE.W 0.L, (A2)+"               , 20, 5 },
        { "MOVE.W #$12, (A2)+"              , 12, 3 },

        { "MOVE.L D0, (A2)+"                , 12, 3 },
        { "MOVE.L A0, (A2)+"                , 12, 3 },
        { "MOVE.L (A0), (A2)+"              , 20, 5 },
        { "MOVE.L (A0), (A2)+"              , 20, 5 },
        { "MOVE.L -(A1), (A2)+"             , 22, 5 },
        { "MOVE.L 10(A2), (A2)+"            , 24, 6 },
        { "MOVE.L 2(A0,D0.L), (A2)+"        , 26, 6 },
        { "MOVE.L 0.W, (A2)+"               , 24, 6 },
        { "MOVE.L 0.L, (A2)+"               , 28, 7 },
        { "MOVE.L #$12, (A2)+"              , 20, 5 },

        { "MOVE.W D0, -(A2)"                ,  8, 2 },
        { "MOVE.W A0, -(A2)"                ,  8, 2 },
        { "MOVE.W (A0), -(A2)"              , 12, 3 },
        { "MOVE.W (A0), -(A2)"              , 12, 3 },
        { "MOVE.W -(A1), -(A2)"             , 14, 3 },
        { "MOVE.W 10(A0), -(A2)"            , 16, 4 },
        { "MOVE.W 2(A0,D0.L), -(A2)"        , 18, 4 },
        { "MOVE.W 0.W, -(A2)"               , 16, 4 },
        { "MOVE.W 0.L, -(A2)"               , 20, 5 },
        { "MOVE.W #$12, -(A2)"              , 12, 3 },

        { "MOVE.L D0, -(A2)"                , 12, 3 },
        { "MOVE.L A0, -(A2)"                , 12, 3 },
        { "MOVE.L (A0), -(A2)"              , 20, 5 },
        { "MOVE.L (A0), -(A2)"              , 20, 5 },
        { "MOVE.L -(A1), -(A2)"             , 22, 5 },
        { "MOVE.L 10(A2), -(A2)"            , 24, 6 },
        { "MOVE.L 2(A0,D0.L), -(A2)"        , 26, 6 },
        { "MOVE.L 0.W, -(A2)"               , 24, 6 },
        { "MOVE.L 0.L, -(A2)"               , 28, 7 },
        { "MOVE.L #$12, -(A2)"              , 20, 5 },

        { "MOVE.W D0, 2(A2)"                , 12, 3 },
        { "MOVE.W A0, 2(A2)"                , 12, 3 },
        { "MOVE.W (A0), 2(A2)"              , 16, 4 },
        { "MOVE.W (A0), 2(A2)"              , 16, 4 },
        { "MOVE.W -(A1), 2(A2)"             , 18, 4 },
        { "MOVE.W 10(A0), 2(A2)"            , 20, 5 },
        { "MOVE.W 2(A0,D0.L), 2(A2)"        , 22, 5 },
        { "MOVE.W 0.W, 2(A2)"               , 20, 5 },
        { "MOVE.W 0.L, 2(A2)"               , 24, 6 },
        { "MOVE.W #$12, 2(A2)"              , 16, 4 },

        { "MOVE.L D0, 2(A2)"                , 16, 4 },
        { "MOVE.L A0, 2(A2)"                , 16, 4 },
        { "MOVE.L (A0), 2(A2)"              , 24, 6 },
        { "MOVE.L (A0), 2(A2)"              , 24, 6 },
        { "MOVE.L -(A1), 2(A2)"             , 26, 6 },
        { "MOVE.L 10(A2), 2(A2)"            , 28, 7 },
        { "MOVE.L 2(A0,D0.L), 2(A2)"        , 30, 7 },
        { "MOVE.L 0.W, 2(A2)"               , 28, 7 },
        { "MOVE.L 0.L, 2(A2)"               , 32, 8 },
        { "MOVE.L #$12, 2(A2)"              , 24, 6 },

        { "MOVE.W D0, 2(A2,D1.L)"           , 14, 3 },
        { "MOVE.W A0, 2(A2,D1.L)"           , 14, 3 },
        { "MOVE.W (A0), 2(A2,D1.L)"         , 18, 4 },
        { "MOVE.W (A0), 2(A2,D1.L)"         , 18, 4 },
        { "MOVE.W -(A1), 2(A2,D1.L)"        , 20, 4 },
        { "MOVE.W 10(A0), 2(A2,D1.L)"       , 22, 5 },
        { "MOVE.W 2(A0,D0.L), 2(A2,D1.L)"   , 24, 5 },
        { "MOVE.W 0.W, 2(A2,D1.L)"          , 22, 5 },
        { "MOVE.W 0.L, 2(A2,D1.L)"          , 26, 6 },
        { "MOVE.W #$12, 2(A2,D1.L)"         , 18, 4 },

        { "MOVE.L D0, 2(A2,D1.L)"           , 18, 4 },
        { "MOVE.L A0, 2(A2,D1.L)"           , 18, 4 },
        { "MOVE.L (A0), 2(A2,D1.L)"         , 26, 6 },
        { "MOVE.L (A0), 2(A2,D1.L)"         , 26, 6 },
        { "MOVE.L -(A1), 2(A2,D1.L)"        , 28, 6 },
        { "MOVE.L 10(A2), 2(A2,D1.L)"       , 30, 7 },
        { "MOVE.L 2(A0,D0.L), 2(A2,D1.L)"   , 32, 7 },
        { "MOVE.L 0.W, 2(A2,D1.L)"          , 30, 7 },
        { "MOVE.L 0.L, 2(A2,D1.L)"          , 34, 8 },
        { "MOVE.L #$12, 2(A2,D1.L)"         , 26, 6 },

        { "MOVE.W D0, $4.W"                 , 12, 3 },
        { "MOVE.W A0, $4.W"                 , 12, 3 },
        { "MOVE.W (A0), $4.W"               , 16, 4 },
        { "MOVE.W (A0), $4.W"               , 16, 4 },
        { "MOVE.W -(A1), $4.W"              , 18, 4 },
        { "MOVE.W 10(A0), $4.W"             , 20, 5 },
        { "MOVE.W 2(A0,D0.L), $4.W"         , 22, 5 },
        { "MOVE.W 0.W, $4.W"                , 20, 5 },
        { "MOVE.W 0.L, $4.W"                , 24, 6 },
        { "MOVE.W #$12, $4.W"               , 16, 4 },

        { "MOVE.L D0, $4.W"                 , 16, 4 },
        { "MOVE.L A0, $4.W"                 , 16, 4 },
        { "MOVE.L (A0), $4.W"               , 24, 6 },
        { "MOVE.L (A0), $4.W"               , 24, 6 },
        { "MOVE.L -(A1), $4.W"              , 26, 6 },
        { "MOVE.L 10(A2), $4.W"             , 28, 7 },
        { "MOVE.L 2(A0,D0.L), $4.W"         , 30, 7 },
        { "MOVE.L 0.W, $4.W"                , 28, 7 },
        { "MOVE.L 0.L, $4.W"                , 32, 8 },
        { "MOVE.L #$12, $4.W"               , 24, 6 },

        { "MOVE.W D0, $4.L"                 , 16, 4 },
        { "MOVE.W A0, $4.L"                 , 16, 4 },
        { "MOVE.W (A0), $4.L"               , 20, 5 },
        { "MOVE.W (A0), $4.L"               , 20, 5 },
        { "MOVE.W -(A1), $4.L"              , 22, 5 },
        { "MOVE.W 10(A0), $4.L"             , 24, 6 },
        { "MOVE.W 2(A0,D0.L), $4.L"         , 26, 6 },
        { "MOVE.W 0.W, $4.L"                , 24, 6 },
        { "MOVE.W 0.L, $4.L"                , 28, 7 },
        { "MOVE.W #$12, $4.L"               , 20, 5 },

        { "MOVE.L D0, $4.L"                 , 20, 5 },
        { "MOVE.L A0, $4.L"                 , 20, 5 },
        { "MOVE.L (A0), $4.L"               , 28, 7 },
        { "MOVE.L (A0), $4.L"               , 28, 7 },
        { "MOVE.L -(A1), $4.L"              , 30, 7 },
        { "MOVE.L 10(A2), $4.L"             , 32, 8 },
        { "MOVE.L 2(A0,D0.L), $4.L"         , 34, 8 },
        { "MOVE.L 0.W, $4.L"                , 32, 8 },
        { "MOVE.L 0.L, $4.L"                , 36, 9 },
        { "MOVE.L #$12, $4.L"               , 28, 7 },

        { "MOVE.W D0, A0"                   ,  4, 1 },
        { "MOVE.W A0, A0"                   ,  4, 1 },
        { "MOVE.W (A0), A0"                 ,  8, 2 },
        { "MOVE.W (A0), A0"                 ,  8, 2 },
        { "MOVE.W -(A1), A0"                , 10, 2 },
        { "MOVE.W 10(A0), A0"               , 12, 3 },
        { "MOVE.W 2(A0,D0.L), A0"           , 14, 3 },
        { "MOVE.W 0.W, A0"                  , 12, 3 },
        { "MOVE.W 0.L, A0"                  , 16, 4 },
        { "MOVE.W #$12, A0"                 ,  8, 2 },

        { "MOVE.L D0, A0"                   ,  4, 1 },
        { "MOVE.L A0, A0"                   ,  4, 1 },
        { "MOVE.L (A0), A0"                 , 12, 3 },
        { "MOVE.L (A0), A0"                 , 12, 3 },
        { "MOVE.L -(A1), A0"                , 14, 3 },
        { "MOVE.L 10(A2), A0"               , 16, 4 },
        { "MOVE.L 2(A0,D0.L), A0"           , 18, 4 },
        { "MOVE.L 0.W, A0"                  , 16, 4 },
        { "MOVE.L 0.L, A0"                  , 20, 5 },
        { "MOVE.L #$12, A0"                 , 12, 3 },

        // Line 0000: ADDI (should be same for EORI, ORI, ANDI and SUBI)
        { "ADD.B #$12, D0"                  ,  8, 2 },
        { "ADD.W #$12, (A0)"                , 16, 4 },
        { "ADD.B #$12, (A0)+"               , 16, 4 },
        { "ADD.W #$12, -(A2)"               , 18, 4 },
        { "ADD.W #$12, 2(A0)"               , 20, 5 },
        { "ADD.B #$12, 2(A0,D2.L)"          , 22, 5 },
        { "ADD.W #$12, $10.W"               , 20, 5 },
        { "ADD.W #$12, $10.L"               , 24, 6 },

        { "ADD.L #$12, D0"                  , 16, 3 },
        { "ADD.L #$12, (A0)"                , 28, 7 },
        { "ADD.L #$12, (A0)+"               , 28, 7 },
        { "ADD.L #$12, -(A2)"               , 30, 7 },
        { "ADD.L #$12, 2(A0,D2.L)"          , 34, 8 },
        { "ADD.L #$12, $10.W"               , 32, 8 },
        { "ADD.L #$12, $10.L"               , 36, 9 },

        { "EOR.L #$12, 2(A0)"               , 32, 8 },
        { "EOR.W #$12, $10.W"               , 20, 5 },
        { "OR.L #$12, 2(A0)"                , 32, 8 },
        { "OR.W #$12, $10.W"                , 20, 5 },
        { "AND.L #$12, 2(A0)"               , 32, 8 },
        { "AND.W #$12, $10.W"               , 20, 5 },
        { "SUB.L #$12, 2(A0)"               , 32, 8 },
        { "SUB.W #$12, $10.W"               , 20, 5 },

        { "NOP"                             ,  4, 1 },
        { "RTE"                             , 20, 5 },
        { "RTR"                             , 20, 5 },
        { "RTS"                             , 16, 4 },
        { "LINK A2, #$1234"                 , 16, 4 },
        { "UNLK A2"                         , 12, 3 },
        { "MOVEQ #$12, d0"                  ,  4, 1 },
        { "l BRA.B l"                       , 10, 2 },
        { "l BRA.W l"                       , 10, 2 },
        { "l BSR.B l"                       , 18, 4 },
        { "l BSR.W l"                       , 18, 4 },
        { "l BEQ.B l"                       , 10, 2 }, // Taken
        { "l BEQ.W l"                       , 10, 2 }, // Taken
        { "l BNE.B l"                       , 8,  1 }, // Not taken
        { "l BNE.W l"                       , 12, 2 }, // Not taken
        { "l DBEQ d2, l"                    , 12, 2 }, // Condition true
        { "l DBNE d2, l"                    , 10, 2 }, // Condition false, count not expired
        { "l DBNE d0, l"                    , 14, 3 }, // Condition false, count expired

        { "OR.B #$12, CCR"                  , 20, 3 },
        { "AND.B #$12, CCR"                 , 20, 3 },
        { "EOR.B #$12, CCR"                 , 20, 3 },
        { "OR.W #$12, SR"                   , 20, 3 },
        { "AND.W #$12, SR"                  , 20, 3 },
        { "EOR.W #$12, SR"                  , 20, 3 },

        // CC false
        { "SNE D0"                          ,  4, 1 },
        { "SNE (A0)"                        , 12, 3 },
        { "SNE (A0)+"                       , 12, 3 },
        { "SNE -(A2)"                       , 14, 3 },
        { "SNE 2(A0)"                       , 16, 4 },
        { "SNE 2(A0,D2.L)"                  , 18, 4 },
        { "SNE $10.W"                       , 16, 4 },
        { "SNE $10.L"                       , 20, 5 },
        // CC true
        { "SEQ D0"                          ,  6, 1 },
        { "SEQ (A0)"                        , 12, 3 },
        { "SEQ (A0)+"                       , 12, 3 },
        { "SEQ -(A2)"                       , 14, 3 },
        { "SEQ 2(A0)"                       , 16, 4 },
        { "SEQ 2(A0,D2.L)"                  , 18, 4 },
        { "SEQ $10.W"                       , 16, 4 },
        { "SEQ $10.L"                       , 20, 5 },

        { "BCHG D3, D0"                     ,  6, 1 },
        { "BCHG D4, D0"                     ,  8, 1 },
        { "BCHG #4, D0"                     , 10, 2 },
        { "BCHG #18, D0"                    , 12, 2 },
        { "BSET D3, D0"                     ,  6, 1 },
        { "BSET D4, D0"                     ,  8, 1 },
        { "BSET #4, D0"                     , 10, 2 },
        { "BSET #18, D0"                    , 12, 2 },
        { "BCLR D3, D0"                     ,  8, 1 },
        { "BCLR D4, D0"                     , 10, 1 },
        { "BCLR #4, D0"                     , 12, 2 },
        { "BCLR #18, D0"                    , 14, 2 },

        { "MOVEP.W D0, $12(A0)"             , 16, 4 },
        { "MOVEP.L D0, $12(A0)"             , 24, 6 },
        { "MOVEP.W $12(A0), D0"             , 16, 4 },
        { "MOVEP.L $12(A0), D0"             , 24, 6 },

        { "MOVE A0, USP"                    ,  4, 1 },
        { "MOVE USP, A0"                    ,  4, 1 },

        { "CMPM.B (A0)+, (A1)+"             , 12, 3 },
        { "CMPM.W (A0)+, (A1)+"             , 12, 3 },
        { "CMPM.L (A0)+, (A1)+"             , 20, 5 },

        { "ASL.B D0, D0"                    ,  6, 1 },
        { "ASL.B D3, D0"                    , 16, 1 },
        { "ASL.B D4, D0"                    , 40, 1 },
        { "ASL.W D0, D0"                    ,  6, 1 },
        { "ASL.W D3, D0"                    , 16, 1 },
        { "ASL.W D4, D0"                    , 40, 1 },
        { "ASL.L D0, D0"                    ,  8, 1 },
        { "ASL.L D3, D0"                    , 18, 1 },
        { "ASL.L D4, D0"                    , 42, 1 },
        { "ASL.B #1, D0"                    ,  8, 1 },
        { "ASL.L #1, D0"                    , 10, 1 },
        { "ASL.B #2, D0"                    , 10, 1 },
        { "ASL.L #2, D0"                    , 12, 1 },
        { "ASL.B #3, D0"                    , 12, 1 },
        { "ASL.L #3, D0"                    , 14, 1 },
        { "ASL.B #4, D0"                    , 14, 1 },
        { "ASL.L #4, D0"                    , 16, 1 },
        { "ASL.B #5, D0"                    , 16, 1 },
        { "ASL.L #5, D0"                    , 18, 1 },
        { "ASL.B #6, D0"                    , 18, 1 },
        { "ASL.L #6, D0"                    , 20, 1 },
        { "ASL.B #7, D0"                    , 20, 1 },
        { "ASL.L #7, D0"                    , 22, 1 },
        { "ASL.B #8, D0"                    , 22, 1 },
        { "ASL.L #8, D0"                    , 24, 1 },

        { "ASR.B D0, D0"                    ,  6, 1 },
        { "ASR.B D3, D0"                    , 16, 1 },
        { "ASR.B D4, D0"                    , 40, 1 },
        { "ASR.W D0, D0"                    ,  6, 1 },
        { "ASR.W D3, D0"                    , 16, 1 },
        { "ASR.W D4, D0"                    , 40, 1 },
        { "ASR.L D0, D0"                    ,  8, 1 },
        { "ASR.L D3, D0"                    , 18, 1 },
        { "ASR.L D4, D0"                    , 42, 1 },
        { "ASR.B #1, D0"                    ,  8, 1 },
        { "ASR.L #1, D0"                    , 10, 1 },
        { "ASR.B #4, D0"                    , 14, 1 },
        { "ASR.L #4, D0"                    , 16, 1 },
        { "ASR.B #7, D0"                    , 20, 1 },
        { "ASR.L #7, D0"                    , 22, 1 },
        { "ASR.B #8, D0"                    , 22, 1 },
        { "ASR.L #8, D0"                    , 24, 1 },

        { "LSL.B D0, D0"                    ,  6, 1 },
        { "LSL.B D3, D0"                    , 16, 1 },
        { "LSL.B D4, D0"                    , 40, 1 },
        { "LSL.W D0, D0"                    ,  6, 1 },
        { "LSL.W D3, D0"                    , 16, 1 },
        { "LSL.W D4, D0"                    , 40, 1 },
        { "LSL.L D0, D0"                    ,  8, 1 },
        { "LSL.L D3, D0"                    , 18, 1 },
        { "LSL.L D4, D0"                    , 42, 1 },
        { "LSL.B #1, D0"                    ,  8, 1 },
        { "LSL.L #1, D0"                    , 10, 1 },
        { "LSL.B #4, D0"                    , 14, 1 },
        { "LSL.L #4, D0"                    , 16, 1 },
        { "LSL.B #7, D0"                    , 20, 1 },
        { "LSL.L #7, D0"                    , 22, 1 },
        { "LSL.B #8, D0"                    , 22, 1 },
        { "LSL.L #8, D0"                    , 24, 1 },

        { "LSR.B D0, D0"                    ,  6, 1 },
        { "LSR.B D3, D0"                    , 16, 1 },
        { "LSR.B D4, D0"                    , 40, 1 },
        { "LSR.W D0, D0"                    ,  6, 1 },
        { "LSR.W D3, D0"                    , 16, 1 },
        { "LSR.W D4, D0"                    , 40, 1 },
        { "LSR.L D0, D0"                    ,  8, 1 },
        { "LSR.L D3, D0"                    , 18, 1 },
        { "LSR.L D4, D0"                    , 42, 1 },
        { "LSR.B #1, D0"                    ,  8, 1 },
        { "LSR.L #1, D0"                    , 10, 1 },
        { "LSR.B #4, D0"                    , 14, 1 },
        { "LSR.L #4, D0"                    , 16, 1 },
        { "LSR.B #7, D0"                    , 20, 1 },
        { "LSR.L #7, D0"                    , 22, 1 },
        { "LSR.B #8, D0"                    , 22, 1 },
        { "LSR.L #8, D0"                    , 24, 1 },

        { "ROL.B D0, D0"                    ,  6, 1 },
        { "ROL.B D3, D0"                    , 16, 1 },
        { "ROL.B D4, D0"                    , 40, 1 },
        { "ROL.W D0, D0"                    ,  6, 1 },
        { "ROL.W D3, D0"                    , 16, 1 },
        { "ROL.W D4, D0"                    , 40, 1 },
        { "ROL.L D0, D0"                    ,  8, 1 },
        { "ROL.L D3, D0"                    , 18, 1 },
        { "ROL.L D4, D0"                    , 42, 1 },
        { "ROL.B #1, D0"                    ,  8, 1 },
        { "ROL.L #1, D0"                    , 10, 1 },
        { "ROL.B #4, D0"                    , 14, 1 },
        { "ROL.L #4, D0"                    , 16, 1 },
        { "ROL.B #7, D0"                    , 20, 1 },
        { "ROL.L #7, D0"                    , 22, 1 },
        { "ROL.B #8, D0"                    , 22, 1 },
        { "ROL.L #8, D0"                    , 24, 1 },

        { "ROR.B D0, D0"                    ,  6, 1 },
        { "ROR.B D3, D0"                    , 16, 1 },
        { "ROR.B D4, D0"                    , 40, 1 },
        { "ROR.W D0, D0"                    ,  6, 1 },
        { "ROR.W D3, D0"                    , 16, 1 },
        { "ROR.W D4, D0"                    , 40, 1 },
        { "ROR.L D0, D0"                    ,  8, 1 },
        { "ROR.L D3, D0"                    , 18, 1 },
        { "ROR.L D4, D0"                    , 42, 1 },
        { "ROR.B #1, D0"                    ,  8, 1 },
        { "ROR.L #1, D0"                    , 10, 1 },
        { "ROR.B #4, D0"                    , 14, 1 },
        { "ROR.L #4, D0"                    , 16, 1 },
        { "ROR.B #7, D0"                    , 20, 1 },
        { "ROR.L #7, D0"                    , 22, 1 },
        { "ROR.B #8, D0"                    , 22, 1 },
        { "ROR.L #8, D0"                    , 24, 1 },

        { "ROXL.B D0, D0"                   ,  6, 1 },
        { "ROXL.B D3, D0"                   , 16, 1 },
        { "ROXL.B D4, D0"                   , 40, 1 },
        { "ROXL.W D0, D0"                   ,  6, 1 },
        { "ROXL.W D3, D0"                   , 16, 1 },
        { "ROXL.W D4, D0"                   , 40, 1 },
        { "ROXL.L D0, D0"                   ,  8, 1 },
        { "ROXL.L D3, D0"                   , 18, 1 },
        { "ROXL.L D4, D0"                   , 42, 1 },
        { "ROXL.B #1, D0"                   ,  8, 1 },
        { "ROXL.L #1, D0"                   , 10, 1 },
        { "ROXL.B #4, D0"                   , 14, 1 },
        { "ROXL.L #4, D0"                   , 16, 1 },
        { "ROXL.B #7, D0"                   , 20, 1 },
        { "ROXL.L #7, D0"                   , 22, 1 },
        { "ROXL.B #8, D0"                   , 22, 1 },
        { "ROXL.L #8, D0"                   , 24, 1 },

        { "ROXR.B D0, D0"                   ,  6, 1 },
        { "ROXR.B D3, D0"                   , 16, 1 },
        { "ROXR.B D4, D0"                   , 40, 1 },
        { "ROXR.W D0, D0"                   ,  6, 1 },
        { "ROXR.W D3, D0"                   , 16, 1 },
        { "ROXR.W D4, D0"                   , 40, 1 },
        { "ROXR.L D0, D0"                   ,  8, 1 },
        { "ROXR.L D3, D0"                   , 18, 1 },
        { "ROXR.L D4, D0"                   , 42, 1 },
        { "ROXR.B #1, D0"                   ,  8, 1 },
        { "ROXR.L #1, D0"                   , 10, 1 },
        { "ROXR.B #4, D0"                   , 14, 1 },
        { "ROXR.L #4, D0"                   , 16, 1 },
        { "ROXR.B #7, D0"                   , 20, 1 },
        { "ROXR.L #7, D0"                   , 22, 1 },
        { "ROXR.B #8, D0"                   , 22, 1 },
        { "ROXR.L #8, D0"                   , 24, 1 },

        // M->R
        { "MOVEM.W (A0), D0"                , 16, 4 },
        { "MOVEM.W (A0)+, D0"               , 16, 4 },
        { "MOVEM.W $12(A0), D0"             , 20, 5 },
        { "MOVEM.W $12(A0,D1), D0"          , 22, 5 },
        { "MOVEM.W $12.W, D0"               , 20, 5 },
        { "MOVEM.W $12.L, D0"               , 24, 6 },
        { "MOVEM.L (A0), D0"                , 20, 5 },
        { "MOVEM.L (A0)+, D0"               , 20, 5 },
        { "MOVEM.L $12(A0), D0"             , 24, 6 },
        { "MOVEM.L $12(A0,D1), D0"          , 26, 6 },
        { "MOVEM.L $12.W, D0"               , 24, 6 },
        { "MOVEM.L $12.L, D0"               , 28, 7 },
        { "MOVEM.W (A0), D0-D7"             , 44, 11 },
        { "MOVEM.W (A0)+, D0-D7"            , 44, 11 },
        { "MOVEM.W $12(A0), D0-D7"          , 48, 12 },
        { "MOVEM.W $12(A0,D1), D0-D7"       , 50, 12 },
        { "MOVEM.W $12.W, D0-D7"            , 48, 12 },
        { "MOVEM.W $12.L, D0-D7"            , 52, 13 },
        { "MOVEM.L (A0), D0-D7"             , 76, 19 },
        { "MOVEM.L (A0)+, D0-D7"            , 76, 19 },
        { "MOVEM.L $12(A0), D0-D7"          , 80, 20 },
        { "MOVEM.L $12(A0,D1), D0-D7"       , 82, 20 },
        { "MOVEM.L $12.W, D0-D7"            , 80, 20 },
        { "MOVEM.L $12.L, D0-D7"            , 84, 21 },

        // R->M
        { "MOVEM.W D0, (A0)"                , 12, 3 },
        { "MOVEM.W D0, -(A2)"               , 12, 3 },
        { "MOVEM.W D0, $12(A0)"             , 16, 4 },
        { "MOVEM.W D0, $12(A0,D1)"          , 18, 4 },
        { "MOVEM.W D0, $12.W"               , 16, 4 },
        { "MOVEM.W D0, $12.L"               , 20, 5 },
        { "MOVEM.L D0, (A0)"                , 16, 4 },
        { "MOVEM.L D0, -(A2)"               , 16, 4 },
        { "MOVEM.L D0, $12(A0)"             , 20, 5 },
        { "MOVEM.L D0, $12(A0,D1)"          , 22, 5 },
        { "MOVEM.L D0, $12.W"               , 20, 5 },
        { "MOVEM.L D0, $12.L"               , 24, 6 },
        { "MOVEM.W D0-D7, (A0)"             , 40, 10 },
        { "MOVEM.W D0-D7, -(A2)"            , 40, 10 },
        { "MOVEM.W D0-D7, $12(A0)"          , 44, 11 },
        { "MOVEM.W D0-D7, $12(A0,D1)"       , 46, 11 },
        { "MOVEM.W D0-D7, $12.W"            , 44, 11 },
        { "MOVEM.W D0-D7, $12.L"            , 48, 12 },
        { "MOVEM.L D0-D7, (A0)"             , 72, 18 },
        { "MOVEM.L D0-D7, -(A2)"            , 72, 18 },
        { "MOVEM.L D0-D7, $12(A0)"          , 76, 19 },
        { "MOVEM.L D0-D7, $12(A0,D1)"       , 78, 19 },
        { "MOVEM.L D0-D7, $12.W"            , 76, 19 },
        { "MOVEM.L D0-D7, $12.L"            , 80, 20 },

        // The following is not 100% correct
        { "DIVU D5, D0"                     , 76, 1 },
        { "DIVU #123, D0"                   , 80, 2 },
        { "DIVS D5, D0"                     , 120, 1 },
        { "DIVS #123, D0"                   , 124, 2 },

        { "MULS D5, D0"                     , 38, 1 },
        { "MULS D4, D0"                     , 46, 1 },
        { "MULS #123, D0"                   , 50, 2 },
        { "MULS #$0000, D0"                 , 42, 2 }, 
        { "MULS #$FFFF, D0"                 , 44, 2 }, 
        { "MULS #$5555, D0"                 , 74, 2 }, 
        { "MULS #$AAAA, D0"                 , 72, 2 },
        // MULU: 38+2*number of ones in source
        { "MULU D5, D0"                     , 38, 1 },
        { "MULU D4, D0"                     , 42, 1 },
        { "MULU #123, D0"                   , 54, 2 },
        { "MULU #$0000, D0"                 , 42, 2 }, 
        { "MULU #$FFFF, D0"                 , 74, 2 }, 
        { "MULU #$5555, D0"                 , 58, 2 }, 
        { "MULU #$AAAA, D0"                 , 58, 2 }, 
    };

    const uint32_t code_pos = 0x1000;
    const uint32_t code_size = 0x2000;
    memory_handler mem { code_pos + code_size };
    auto& ram = mem.ram();
    std::vector<uint16_t> insts;
    bool all_ok = true;

    for (const auto& tc : test_cases) {
        try {
            insts = assemble(code_pos, tc.test_inst);
        } catch (const std::exception& e) {
            std::cerr << e.what() << " while assembling:\n" << tc.test_inst << "\n";
            return false;
        }

        memset(&ram[0], 0, ram.size());
        for (size_t i = 0; i < std::size(insts); ++i)
            put_u16(&ram[code_pos + i * 2], insts[i]);

        cpu_state input_state {};
        input_state.sr = 0x2000 | srm_z;
        input_state.pc = code_pos;
        input_state.a[2] = 100;
        input_state.d[2] = 0x100;
        input_state.d[3] = 5;
        input_state.d[4] = 17;
        input_state.usp = 64;
        input_state.ssp = 32;

        m68000 cpu { mem, input_state };

        const auto step_res = cpu.step();
        if (step_res.clock_cycles != tc.clock_cycles || step_res.mem_accesses != tc.memory_accesses) {
            std::cerr << "Test failed for:\n" << tc.test_inst << "\n";
            std::cerr << "Expected clock cycles:    " << (int)tc.clock_cycles << "\tActual: " << (int)step_res.clock_cycles << "\n";
            std::cerr << "Expected memory accesses: " << (int)tc.memory_accesses << "\tActual: " << (int)step_res.mem_accesses << "\n";
            all_ok = false;
        }
    }

    return all_ok;
}

} // unnamed namespace


// TODO:
// CHK
// RESET
// STOP
// TRAPV
// ILLEGAL
// TRAP
// DIVU
// DIVS
// MULU
// MULS
// Exceptions/interrupts

bool test_timing()
{
    bool all_ok = true;
    all_ok &= run_timing_tests();

    tester t;
    for (const auto& tc : timing_tests) {
        const bool ok = t.tests_from_table(tc.table, tc.instructions);
        all_ok &= ok;
    }

    return all_ok;
}
