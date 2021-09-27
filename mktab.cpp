#include <iostream>
#include <stdint.h>
#include <cassert>
#include <vector>
#include <utility>
#include <algorithm>
#include <string>
#include <iomanip>
#include <fstream>
#include <memory>
#include "ioutil.h"

constexpr uint8_t ea_imm = 0b00'111'100;

// Note: Sync these with instruction.cpp/h
constexpr uint8_t ea_data3 = 0b01'000'000;
constexpr uint8_t ea_data4 = 0b01'000'001;
constexpr uint8_t ea_data8 = 0b01'000'010;
constexpr uint8_t ea_disp  = 0b01'000'011;
constexpr uint8_t ea_sr    = 0b01'000'100;

constexpr uint8_t extra_cond_flag = 1 << 0; // Upper 4 bits are condition code
constexpr uint8_t extra_disp_flag = 1 << 1; // Displacement word follows


constexpr const unsigned block_Dn  = 1U << 0;
constexpr const unsigned block_An  = 1U << 1;
constexpr const unsigned block_Imm = 1U << 31;

struct inst_desc {
    const char* name;
    const char* sizes;
    const char* bits;
    const char* imm;
    uint32_t block = block_An; // By default block An
    uint16_t fixed_ea;
};

enum class opsize {
    none,
    b,
    w,
    l,
};

constexpr const inst_desc insts[] = {
// Based off http://goldencrystal.free.fr/M68kOpcodes-v2.3.pdf
//                                  1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
//   Name                  Sizes    5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0   Immeditate
//
    //{"ORI.B #I, CCR"     , "   " , "0 0 0 0 0 0 0 0 0 0 1 1 1 1 0 0" , "B I" },
    //{"ORI.W #I, SSR"     , "   " , "0 0 0 0 0 0 0 0 0 1 1 1 1 1 0 0" , "W I" },
    {"ORI"               , "BWL" , "0 0 0 0 0 0 0 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    //{"ANDI.B #I, CCR"    , "   " , "0 0 0 0 0 0 1 0 0 0 1 1 1 1 0 0" , "B I" },
    //{"ANDI.W #I, SR"     , "   " , "0 0 0 0 0 0 1 0 0 1 1 1 1 1 0 0" , "W I" },
    {"ANDI"              , "BWL" , "0 0 0 0 0 0 1 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    {"SUBI"              , "BWL" , "0 0 0 0 0 1 0 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    {"ADDI"              , "BWL" , "0 0 0 0 0 1 1 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    //{"EORI.B #I, CCR"    , "   " , "0 0 0 0 1 0 1 0 0 0 1 1 1 1 0 0" , "B I" },
    //{"EORI.W #I, SR"     , "   " , "0 0 0 0 1 0 1 0 0 1 1 1 1 1 0 0" , "W I" },
    {"EORI"              , "BWL" , "0 0 0 0 1 0 1 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    {"CMPI"              , "BWL" , "0 0 0 0 1 1 0 0 Sx  M     Xn   " , "/ I", block_An|block_Imm },
    {"BTST"              , "B L" , "0 0 0 0 1 0 0 0 0 0 M     Xn   " , "B N" },
    {"BCHG"              , "B L" , "0 0 0 0 1 0 0 0 0 1 M     Xn   " , "B N" },
    {"BCLR"              , "B L" , "0 0 0 0 1 0 0 0 1 0 M     Xn   " , "B N" },
    {"BSET"              , "B L" , "0 0 0 0 1 0 0 0 1 1 M     Xn   " , "B N" },
    {"BTST"              , "B L" , "0 0 0 0 Dn    1 0 0 M     Xn   " , "  N" },
    {"BCHG"              , "B L" , "0 0 0 0 Dn    1 0 1 M     Xn   " , "  N" },
    {"BCLR"              , "B L" , "0 0 0 0 Dn    1 1 0 M     Xn   " , "  N" },
    {"BSET"              , "B L" , "0 0 0 0 Dn    1 1 1 M     Xn   " , "  N" },
    //{"MOVEP"             , " WL" , "0 0 0 0 Dn    1 DxSz0 0 1 An   " , "W D" },
    {"MOVEA"             , " WL" , "0 0 Sy  An    0 0 1 M     Xn   " , "   ", 0 },
    {"MOVE"              , "BWL" , "0 0 Sy  Xn    M     M     Xn   " , "   ", 0 },
    //{"MOVE.W SR, #1"     , " W " , "0 1 0 0 0 0 0 0 1 1 M     Xn   " , "   " },
    //{"MOVE.B #1, CCR"    , "B  " , "0 1 0 0 0 1 0 0 1 1 M     Xn   " , "   " },
    {"MOVE"              , " W " , "0 1 0 0 0 1 1 0 1 1 M     Xn   " , "   ", block_An, 0|ea_sr << 8 }, // Move to SR
    {"NEGX"              , "BWL" , "0 1 0 0 0 0 0 0 Sx  M     Xn   " , "   " },
    {"CLRX"              , "BWL" , "0 1 0 0 0 0 1 0 Sx  M     Xn   " , "   " },
    {"NEG"               , "BWL" , "0 1 0 0 0 1 0 0 Sx  M     Xn   " , "   " },
    {"NOT"               , "BWL" , "0 1 0 0 0 1 1 0 Sx  M     Xn   " , "   " },
    {"EXT"               , " WL" , "0 1 0 0 1 0 0 0 1 Sz0 0 0 Dn   " , "   " },
    {"NBCD"              , "B  " , "0 1 0 0 1 0 0 0 0 0 M     Xn   " , "   " },
    {"SWAP"              , " W " , "0 1 0 0 1 0 0 0 0 1 0 0 0 Dn   " , "   " },
    {"PEA"               , "  L" , "0 1 0 0 1 0 0 0 0 1 M     Xn   " , "   ", 0b0011011 },
    {"ILLEGAL"           , "   " , "0 1 0 0 1 0 1 0 1 1 1 1 1 1 0 0" , "   " },
    {"TAS"               , "B  " , "0 1 0 0 1 0 1 0 1 1 M     Xn   " , "   ", block_An | block_Imm },
    {"TST"               , "BWL" , "0 1 0 0 1 0 1 0 Sx  M     Xn   " , "   " },
    {"TRAP"              , "   " , "0 1 0 0 1 1 1 0 0 1 0 0 Data4  " , "   " },
    {"LINK"              , " W " , "0 1 0 0 1 1 1 0 0 1 0 1 0 An   " , "   " },
    {"UNLK"              , "   " , "0 1 0 0 1 1 1 0 0 1 0 1 1 An   " , "   " },
    //{"MOVE USP"          , "  L" , "0 1 0 0 1 1 1 0 0 1 1 0 DyAn   " , "   " },
    {"RESET"             , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 0 0 0" , "   " },
    {"NOP"               , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 0 0 1" , "   " },
    {"STOP"              , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 0 1 0" , "W I" },
    {"RTE"               , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 0 1 1" , "   " },
    {"RTS"               , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 1 0 1" , "   " },
    {"TRAPV"             , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 1 1 0" , "   " },
    {"RTR"               , "   " , "0 1 0 0 1 1 1 0 0 1 1 1 0 1 1 1" , "   " },
    {"JSR"               , "   " , "0 1 0 0 1 1 1 0 1 0 M     Xn   " , "   " },
    {"JMP"               , "   " , "0 1 0 0 1 1 1 0 1 1 M     Xn   " , "   " },
    //{"MOVEM"             , " WL" , "0 1 0 0 1 Dy0 0 1 SzM     Xn   " , "W M" },
    {"LEA"               , "  L" , "0 1 0 0 An    1 1 1 M     Xn   " , "   " },
    {"CHK"               , " W " , "0 1 0 0 Dn    1 1 0 M     Xn   " , "   " },
    {"ADDQ"              , "BWL" , "0 1 0 1 Data3 0 Sx  M     Xn   " , "   " },
    {"SUBQ"              , "BWL" , "0 1 0 1 Data3 1 Sx  M     Xn   " , "   " },
    {"Scc"               , "B  " , "0 1 0 1 Cond    1 1 M     Xn   " , "   " },
    {"DBcc"              , " W " , "0 1 0 1 Cond    1 1 0 0 1 Dn   " , "W D" },
    {"BRA"               , "BW " , "0 1 1 0 0 0 0 0 Displacement   " , "W d" },
    {"BSR"               , "BW " , "0 1 1 0 0 0 0 1 Displacement   " , "W d" },
    {"Bcc"               , "BW " , "0 1 1 0 Cond    Displacement   " , "W d" },
    {"MOVEQ"             , "  L" , "0 1 1 1 Dn    0 Data8          " , "   " },
    {"DIVU"              , " W " , "1 0 0 0 Dn    0 1 1 M     Xn   " , "   " },
    {"DIVS"              , " W " , "1 0 0 0 Dn    1 1 1 M     Xn   " , "   " },
    {"SBCD"              , "B  " , "1 0 0 0 Xn    1 0 0 0 0 m Xn   " , "   " },
    {"OR"                , "BWL" , "1 0 0 0 Dn    DzSx  M     Xn   " , "   " },
    {"SUB"               , "BWL" , "1 0 0 1 Dn    DzSx  M     Xn   " , "   ", 0 },
    {"SUBX"              , "BWL" , "1 0 0 1 Xn    1 Sx  0 0 m Xn   " , "   " },
    {"SUBA"              , " WL" , "1 0 0 1 An    Sz1 1 M     Xn   " , "   ", 0 },
    {"EOR"               , "BWL" , "1 0 1 1 Dn    1 Sx  M     Xn   " , "   " },
    {"CMPM"              , "BWL" , "1 0 1 1 An    1 Sx  0 0 1 An   " , "   " },
    {"CMP"               , "BWL" , "1 0 1 1 Dn    0 Sx  M     Xn   " , "   " },
    {"CMPA"              , " WL" , "1 0 1 1 An    Sz1 1 M     Xn   " , "   ", 0 },
    {"MULU"              , " W " , "1 1 0 0 Dn    0 1 1 M     Xn   " , "   " },
    {"MULS"              , " W " , "1 1 0 0 Dn    1 1 1 M     Xn   " , "   " },
    {"ABCD"              , "B  " , "1 1 0 0 Xn    1 0 0 0 0 m Xn   " , "   " },
    {"EXG"               , "  L" , "1 1 0 0 Xn    1 ME  0 0 MeXn   " , "   ", 0 },
    {"AND"               , "BWL" , "1 1 0 0 Dn    DzSx  M     Xn   " , "   " },
    {"ADD"               , "BWL" , "1 1 0 1 Dn    DzSx  M     Xn   " , "   ", 0 },
    {"ADDX"              , "BWL" , "1 1 0 1 Xn    1 Sx  0 0 m Xn   " , "   " },
    {"ADDA"              , " WL" , "1 1 0 1 An    Sz1 1 M     Xn   " , "   ", 0},
    {"ASd"               , "BWL" , "1 1 1 0 0 0 0 d 1 1 M     Xn   " , "   " },
    {"LSd"               , "BWL" , "1 1 1 0 0 0 1 d 1 1 M     Xn   " , "   " },
    {"ROXd"              , "BWL" , "1 1 1 0 0 1 0 d 1 1 M     Xn   " , "   " },
    {"ROd"               , "BWL" , "1 1 1 0 0 1 1 d 1 1 M     Xn   " , "   " },
    {"ASd"               , "BWL" , "1 1 1 0 Data3 d Sx  Mr0 0 Dn   " , "   " },
    {"LSd"               , "BWL" , "1 1 1 0 Data3 d Sx  Mr0 1 Dn   " , "   " },
    {"ROXd"              , "BWL" , "1 1 1 0 Data3 d Sx  Mr1 0 Dn   " , "   " },
    {"ROd"               , "BWL" , "1 1 1 0 Data3 d Sx  Mr1 1 Dn   " , "   " },
};

#define FIELDS(X)      \
    X(Sx, 2)           \
    X(Sy, 2)           \
    X(Sz, 1)           \
    X(Mr, 1)           \
    X(ME, 2)           \
    X(Me, 1)           \
    X(M, 3)            \
    X(m, 1)            \
    X(Xn, 3)           \
    X(Cond, 4)         \
    X(Data3, 3)        \
    X(Data4, 4)        \
    X(Data8, 8)        \
    X(Displacement, 8) \
    X(Dn, 3)           \
    X(Dx, 1)           \
    X(Dy, 1)           \
    X(Dz, 1)           \
    X(d, 1)            \
    X(An, 3)           \
    X(Unknown, 0)

enum class field_type {
#define DEF_FIELD(f, w) f,
    FIELDS(DEF_FIELD)
#undef DEF_FIELD
};

constexpr const char* const field_type_names[] = {
#define FIELD_TYPE_NAME(f, w) #f,
    FIELDS(FIELD_TYPE_NAME)
#undef FIELD_TYPE_NAME
};

constexpr const int field_type_widths[] = {
#define FIELD_TYPE_WIDTH(f, w) w,
    FIELDS(FIELD_TYPE_WIDTH)
#undef FIELD_TYPE_WIDTH
};

std::pair<field_type, unsigned> get_field_type(const char* t)
{
#define CHECK_FIELD(f, w) if (!strncmp(t, #f, sizeof(#f)-1)) return {field_type::f, static_cast<unsigned>(sizeof(#f)-1)};
    FIELDS(CHECK_FIELD)
#undef CHECK_FIELD
    return { field_type::Unknown, 0 };
}

int field_type_width(field_type t)
{
    const unsigned index = static_cast<unsigned>(t);
    assert(index < sizeof(field_type_names) / sizeof(*field_type_names));
    return field_type_widths[index];
}

std::ostream& operator<<(std::ostream& os, field_type t)
{
    const unsigned index = static_cast<unsigned>(t);
    assert(index < sizeof(field_type_names) / sizeof(*field_type_names));
    return os << field_type_names[index];
}

opsize get_opsize(const char* sizes)
{
    if (!strcmp(sizes, "B  "))
        return opsize::b;
    else if (!strcmp(sizes, " W "))
        return opsize::w;
    else if (!strcmp(sizes, "  L"))
        return opsize::l;
    assert(0);
    return opsize::w;
}

std::ostream& operator<<(std::ostream& os, opsize s)
{
    switch (s) {
    case opsize::none:
        return os << "none";
    case opsize::b:
        return os << "b";
    case opsize::w:
        return os << "w";
    case opsize::l:
        return os << "l";
    }
    assert(0);
    return os << "Unknown opsize (" << static_cast<int>(s) << ")";
}

using field_pair = std::pair<field_type, int>;

int extract_field(field_type t, std::vector<field_pair>& fs)
{
    auto it = std::find_if(fs.begin(), fs.end(), [t](const auto& p) { return p.first == t; });
    if (it == fs.end())
        return -1;
    const int p = it->second;
    fs.erase(it);
    return p;
}

struct instruction_info {
    std::string type;
    std::string name;
    opsize osize = opsize::none;
    uint8_t nea;
    uint8_t ea[2];
    uint8_t data;
    uint8_t extra;
};
instruction_info all_instructions[65536];

opsize osize;
int M; // M is always before Xn (except for MOVE)
int ME; // For EXG
int nea;
int cond;
uint8_t ea[2];
uint8_t data;
bool isbitop;
bool swap_ea;
int rot_dir;

void output_plain(uint16_t inst, const char* name)
{
    auto& ai = all_instructions[inst]; 
    assert(ai.name.empty());
    ai.type = ai.name = name;
}

void output_plain_with_imm(uint16_t inst, const char* name, const char* imm)
{
    auto& ai = all_instructions[inst];
    assert(ai.name.empty());
    ai.type = ai.name = name;
    ai.nea = 1;
    ai.ea[0] = ea_imm;
    assert(strlen(imm) == 3 && imm[2] == 'I');
    switch (imm[0]) {
//    case 'B':
//        ai.osize = opsize::b;
//        break;
    case 'W':
        ai.osize = opsize::w;
        break;
//    case 'L':
//        ai.osize = opsize::l;
//        break;
    default:
        assert(false);
    }
}



std::string fmtea(uint8_t e)
{
    if (e == ea_data3 || e == ea_data4 || e == ea_data8) {
        return "<DATA #$" + std::to_string(data) + ">";
    } else if (e == ea_disp) {
        return "<DISP #$" + std::to_string(data) + ">";
    }

    switch (e >> 3) {
    case 0b000:
        return "D" + std::to_string(e & 7);
    case 0b001:
        return "A" + std::to_string(e & 7);
    case 0b010:
        return "(A" + std::to_string(e & 7) + ")";
    case 0b011:
        return "(A" + std::to_string(e & 7) + ")+";
    case 0b100:
        return "-(A" + std::to_string(e & 7) + ")";
    case 0b101:
        return "(d16, A" + std::to_string(e & 7) + ")";
    case 0b110:
        return "(d8, A" + std::to_string(e & 7) + ", Xn)";
    case 0b111:
        switch (e & 7) {
        case 0b000:
            return "ABS.W";
        case 0b001:
            return "ABS.L";
        case 0b010:
            return "(d16, PC)";
        case 0b011:
            return "(d8, PC, Xn)";
        case 0b100:
            return "#IMM";
        default:
            goto not_implemented;
        }
    default:
    not_implemented:
        assert(!"Not implemented");
    }
    return "(Unknown EA " + std::to_string(e) + ")";
}

std::string make_cond_name(const std::string& name)
{
    const auto idx = name.find("cc");
    assert(idx != std::string::npos);
    assert(cond >= 0 && cond <= 15);

    constexpr const char* const condtab[16] = {
        "T",
        "F",
        "HI",
        "LS",
        "CC",
        "CS",
        "NE",
        "EQ",
        "VC",
        "VS",
        "PL",
        "MI",
        "GE",
        "LT",
        "GT",
        "LE",
    };

    return name.substr(0, idx) + condtab[cond];
}

std::string make_rot_name(const std::string& name)
{
    const auto idx = name.find("d");
    assert(idx != std::string::npos);
    assert(rot_dir == 0 || rot_dir == 1);
    return name.substr(0, idx) + (rot_dir ? "L" : "R");
}

void gen_insts(const inst_desc& desc, const std::vector<field_pair>& fields, unsigned fidx, uint16_t mask, uint16_t match)
{
    if (fidx == fields.size()) {
        assert(mask == 0xffff);
        auto& ai = all_instructions[match];
        if (!ai.name.empty()) {
            std::cerr << hexfmt(match) <<  ": Overwriting " << ai.name << " with " << desc.name << "\n";
            assert(false);
        }

        ai.osize = osize;

        if (isbitop) {
            assert(osize == opsize::none && nea == 2);
            ai.osize = (ea[1] >= 0b010'000 && ea[1] != ea_imm) ? opsize::b : opsize::l;
        }

        std::string name = desc.name;
        if (cond != -1)
            name = make_cond_name(name);
        else if (rot_dir != -1)
            name = make_rot_name(name);

        assert(!swap_ea || nea == 2);
        ai.type = rot_dir != -1 ? name : desc.name;
        ai.nea = static_cast<uint8_t>(nea);
        std::copy(ea, ea + nea, ai.ea);
        ai.data = data;
        if (swap_ea)
            std::swap(ai.ea[0], ai.ea[1]);

        ai.extra = 0;
        if (cond != -1)
            ai.extra |= extra_cond_flag | (cond << 4);
        
        if (desc.imm[2] == 'D') {
            assert(!strcmp(desc.name, "DBcc"));
            assert(!strcmp(desc.imm, "W D"));
            ai.extra |= extra_disp_flag;
            assert(nea == 1);
            ai.ea[ai.nea++] = ea_disp;
        } else if (desc.imm[2] == 'd') {
            assert(!strcmp(desc.imm, "W d"));
            if (data == 0) {
                ai.extra |= extra_disp_flag;
                ai.osize = opsize::w;
            } else {
                ai.osize = opsize::b;
                assert(data != 0xff);
            }
        }

        if (ai.osize != opsize::none) {
            assert(nea > 0);
            switch (ai.osize) {
            case opsize::b:
                assert(strchr(desc.sizes, 'B'));
                name += ".B";
                break;
            case opsize::w:
                assert(strchr(desc.sizes, 'W'));
                name += ".W";
                break;
            case opsize::l:
                assert(strchr(desc.sizes, 'L'));
                name += ".L";
                break;
            }
        }
        ai.name = name;

        if (desc.fixed_ea) {
            assert(ai.nea < 2);
            if (desc.fixed_ea & 0xff) {
                assert(!"Not implemented");
            } else {
                ai.ea[ai.nea++] = static_cast<uint8_t>(desc.fixed_ea >> 8);
            }
        }

        #if 0
        if (!strcmp(desc.name, "EXG")) {
            std::cout << hexfmt(match) << ": EXG";
            for (int i = 0; i < nea; ++i)
                std::cout << (i ? ", " : "\t") << fmtea(ea[i]);
            std::cout << "\n";
        }
        #endif

        return;
    }
    assert(mask != 0xffff);

    const field_type t = fields[fidx].first;
    const int fpos = fields[fidx].second;
    const int fwidth = field_type_width(t);
    const int fmask = ((1 << fwidth) - 1) << fpos;
    assert((mask & fmask) == 0);

    mask |= fmask;
    
    auto recur = [&](unsigned v) {
        v <<= fpos;
        assert((match & v) == 0);
        gen_insts(desc, fields, fidx + 1, mask, static_cast<uint16_t>(match | v));
    };

    switch (t) {
    case field_type::Sx:
        assert(osize == opsize::none);
        osize = opsize::b;
        recur(0b00);
        osize = opsize::w;
        recur(0b01);
        osize = opsize::l;
        recur(0b10);
        osize = opsize::none;
        break;
    case field_type::Sy:
        assert(osize == opsize::none);
        if (strchr(desc.sizes, 'B')) {
            osize = opsize::b;
            recur(0b01);
        }
        osize = opsize::w;
        recur(0b11);
        osize = opsize::l;
        recur(0b10);
        osize = opsize::none;
        break;
    case field_type::Sz:
        assert(osize == opsize::none);
        osize = opsize::w;
        recur(0b0);
        osize = opsize::l;
        recur(0b1);
        osize = opsize::none;
        break;
    case field_type::M:
        assert(M == -1);
        for (unsigned i = 0; i < 8; ++i) {
            if (((desc.block >> i) & 1)) {
                continue;
            }
            M = i;
            recur(i);
        }
        M = -1;
        break;
    case field_type::Xn:
        if (M == -1 && !strcmp(desc.name, "MOVE") && nea == 0 && static_cast<size_t>(fidx) + 1 < fields.size()) {
            assert(osize != opsize::none);
            swap_ea = true;
            for (unsigned m = 0; m < 8; ++m) {
                if (m == 1)
                    continue; // No An
                for (unsigned x = 0; x < 8; ++x) {
                    if (m == 0b111) {
                        if (x >= 4)
                            break;
                    }
                    ea[nea++] = static_cast<uint8_t>(m << 3 | x);
                    gen_insts(desc, fields, fidx + 2, mask | (7<<(fpos-3)), static_cast<uint16_t>(match | x << fpos | (m<<(fpos-3))));
                    --nea;
                }
            }
            break;
        }

        assert(M >= 0 && M < 8);
        assert(nea < 2);
        for (unsigned i = 0, Mact = (ME == -1 || nea == 0 ? M : ME); i < 8; ++i) {
            if (Mact == 0b111) {
                if (i > 4)
                    break;
                if (i == 4 && (desc.block & block_Imm))
                    break;
            }
            if (!swap_ea && nea == 1 && ea[0] < 8 && strcmp(desc.name, "EXG")) {
                // HACK: Block An/Dn and imm for e.g. OR
                if (Mact == 0 || Mact == 1 || (Mact == 7 && i == 4))
                    continue;
            }
            ea[nea++] = static_cast<uint8_t>(Mact << 3 | i);
            recur(i);
            --nea;
        }
        break;
    case field_type::Dn:
        assert(nea < 2);
        if (nea == 0 && fpos)
            swap_ea = true;
        for (unsigned i = 0; i < 8; ++i) {
            ea[nea++] = static_cast<uint8_t>(0b000'000 | i);
            recur(i);
            --nea;
        }
        break;
    case field_type::An:
        assert(nea < 2);
        if (nea == 0 && fpos) // Not for LINK/UNLK
            swap_ea = true;
        for (unsigned i = 0; i < 8; ++i) {
            ea[nea++] = static_cast<uint8_t>(0b001'000 | i);
            recur(i);
            --nea;
        }
        break;
    case field_type::Data3:
        assert(nea < 2);
        ea[nea++] = ea_data3;
        for (unsigned i = 0; i < 8; ++i) {
            data = static_cast<uint8_t>(i);
            recur(i);
        }
        --nea;
        break;
    case field_type::Data4:
        assert(nea < 2);
        ea[nea++] = ea_data4;
        for (unsigned i = 0; i < 16; ++i) {
            data = static_cast<uint8_t>(i);
            recur(i);
        }
        --nea;
        break;
    case field_type::Data8:
        assert(nea < 2);
        ea[nea++] = ea_data8;
        for (unsigned i = 0; i < 256; ++i) {
            data = static_cast<uint8_t>(i);
            recur(i);
        }
        --nea;
        break;
    case field_type::Cond:
        assert(cond == -1);
        for (unsigned i = 0; i < 16; ++i) {
            if (i < 2 && !strcmp(desc.name, "Bcc"))
                continue;
            cond = i;
            recur(i);
        }
        cond = -1;
        break;
    case field_type::Displacement:
        assert(nea < 2);
        ea[nea++] = ea_disp;
        // Note: 255 is not included since long word displacement is not supported on 68000
        for (unsigned i = 0; i < 255; ++i) {
            data = static_cast<uint8_t>(i);
            recur(i);
        }
        --nea;
        break;
    case field_type::Dz:
        swap_ea = true;
        recur(0);
        swap_ea = false;
        recur(1);
        break;
    case field_type::d:
        assert(rot_dir == -1);
        rot_dir = 0;
        recur(0);
        rot_dir = 1;
        recur(1);
        rot_dir = -1;
        break;
    case field_type::Mr:
        assert(nea == 1 && ea[0] == ea_data3);
        if (data == 0)
            data = 8;
        recur(0);
        ea[0] = data;
        recur(1);
        ea[0] = ea_data3;
        if (data == 8)
            data = 0;
        break;
    default:
        std::cerr << "TODO: " << t << "\n";
        assert(!"Not handled");
    }

}

int main(int argc, char* argv[])
{
    for (const auto& i : insts) {
        int pos = 16;
        uint16_t mask = 0;
        uint16_t match = 0;

        std::vector<field_pair> fields;

        for (const char* b = i.bits; *b; ++b) {
            switch (*b) {
            case ' ':
                continue;
            case '0':
                --pos;
                mask |= 1 << pos;
                continue;
            case '1':
                --pos;
                mask |= 1 << pos;
                match |= 1 << pos;
                continue;
            }

            const auto [t, nchars] = get_field_type(b);

            if (!nchars) {
                std::cerr << "Unknown patten for \"" << i.name << "\": \"" << b << "\"\n";
                return 1;
            }
            assert(nchars > 0);
            b += nchars - 1;

            pos -= field_type_width(t);
            fields.push_back({t, pos});
        }
        if (pos) {
            std::cerr << "Invalid number of bits consumed for \"" << i.name << "\": Remaining " << pos << "\n";
            return 1;
        }

        if (fields.empty()) {
            assert(mask == 0xffff);
            assert(!strcmp(i.sizes, "   "));
            if (!strcmp(i.imm, "   "))
                output_plain(match, i.name);
            else
                output_plain_with_imm(match, i.name, i.imm);
            continue;
        }

        M = -1;
        ME = -1;
        osize = opsize::none;
        nea = 0;
        cond = -1;
        isbitop = false;
        swap_ea = false;
        rot_dir = -1;
        data = 0;

        if (strchr(i.imm, 'I')) {
            ea[nea++] = ea_imm;
        }
        if (strchr(i.imm, 'N')) {
            if (i.imm[0] == 'B')
                ea[nea++] = ea_imm;
            isbitop = true;
        }

        if (!strcmp(i.sizes, "B  "))
            osize = opsize::b;
        else if (!strcmp(i.sizes, " W "))
            osize = opsize::w;
        else if (!strcmp(i.sizes, "  L"))
            osize = opsize::l;

        if (int p = extract_field(field_type::m, fields); p >= 0) {
            swap_ea = true;
            mask |= 1 << p;
            M = 0b000;
            gen_insts(i, fields, 0, mask, match);
            M = 0b100;
            gen_insts(i, fields, 0, mask, match | (1 << p));
        } else if (int MEp = extract_field(field_type::ME, fields); MEp >= 0) {
            // Hack hack
            const int Mep = extract_field(field_type::Me, fields);
            assert(Mep >= 0);
            mask |= 3 << MEp | 1 << Mep;
            M = 0b000;
            gen_insts(i, fields, 0, mask, match | 0b01 << MEp | 0b0 << Mep);
            M = 0b001;
            gen_insts(i, fields, 0, mask, match | 0b01 << MEp | 0b1 << Mep);
            M = 0b000;
            ME = 0b001;
            gen_insts(i, fields, 0, mask, match | 0b10 << MEp | 0b1 << Mep);
        } else {
            gen_insts(i, fields, 0, mask, match);
        }

        #if 0
        std::cout << i.name << ": " << hexfmt(mask) << " " << hexfmt(match) << ":";
        for (const auto [t, p] : fields) {
            switch (t) {
            case field_type::Sx:
            case field_type::Sy:
            case field_type::Sz:
                continue;
            case field_type::Dn:
            case field_type::An:
                continue;
            case field_type::D:
            case field_type::Mr:
                continue;
            case field_type::Data3:
            case field_type::Data4:
            case field_type::Data8:
            case field_type::Cond:
            case field_type::Displacement:
                continue;
            }

            const int m = (1<<field_type_width(t))-1;
            std::cout << " {" << t << ", " << hexfmt(static_cast<uint16_t>(m << p)) << "}";
        }
        std::cout << "\n";
        #endif
    }

    std::unique_ptr<std::ofstream> of;
    if (argc > 1) {
        of = std::make_unique<std::ofstream>(argv[1]);
        if (!*of || !of->is_open()) {
            std::cerr << "Error creating " << argv[1] << "\n";
            return 1;
        }
    }

    std::ostream& out = of ? *of : std::cout;

    for (unsigned i = 0; i < 65536; ++i) {
        auto& ai = all_instructions[i];
        if (ai.name.empty()) {
            ai.type = ai.name = "ILLEGAL";
        }

        auto n = "\"" + ai.name + "\"";
        out << "/* " << hexfmt(static_cast<uint16_t>(i)) << " */ { ";
        out << "inst_type::" << std::left << std::setw(10) << ai.type << ", " << std::left << std::setw(12) << n << ", opsize::" << std::left << std::setw(5) << ai.osize;
        out << ", " << static_cast<int>(ai.nea);
        out << ", { 0x" << hexfmt(ai.ea[0]) << ", 0x" << hexfmt(ai.ea[1]) << "}, 0x" << hexfmt(ai.data) << ", 0x" << hexfmt(ai.extra);
        out << " },\n";
    }
}
