#include <cassert>
#include <iostream>
#include <fstream>
#include <optional>
#include <sstream>
#include <map>
#include <iomanip>
#include <algorithm>
#include <cstring>

#include "disasm.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"

static bool verbose_disasm = false;
static bool show_bytes = false;

static constexpr int line_width = 40;

class fixed_width_line {
public:
    fixed_width_line(std::ostream& os)
        : os_ { os }
        , old_streambuf_ { os_.rdbuf() }
    {
        assert(old_streambuf_);
        os_.rdbuf(oss_.rdbuf());
    }

    ~fixed_width_line()
    {
        if (old_streambuf_)
            flush(false);
    }

    void flush(bool enforce_width)
    {
        assert(old_streambuf_);
        os_.rdbuf(old_streambuf_);
        old_streambuf_ = nullptr;
        auto s = oss_.str();
        os_.write(s.c_str(), s.length());
        if (enforce_width) {
            int col = 0;
            const int tw = 8;
            for (const auto ch : s) {
                if (ch == '\t')
                    col += tw - col % tw;
                else
                    ++col;
            }
            while (col < line_width) {
                col += tw - col % tw;
                os_.put('\t');
            }
        }
    }

    fixed_width_line(const fixed_width_line&) = delete;
    fixed_width_line& operator=(const fixed_width_line&) = delete;
private:
    std::ostream& os_;
    std::streambuf* old_streambuf_;
    std::ostringstream oss_;
};

void disasm_stmts(const std::vector<uint8_t>& data, uint32_t offset, uint32_t end, uint32_t pcoffset = 0)
{
    auto get_word = [&]() -> uint16_t {
        if (offset + 2 > data.size())
            return 0;
        const auto w = get_u16(&data[offset]);
        offset += 2;
        return w;
    };
    while (offset < end) {
        uint16_t iw[max_instruction_words];
        const auto pc = pcoffset + offset;
        iw[0] = get_word();
        for (uint16_t i = 1; i < instructions[iw[0]].ilen; ++i)
            iw[i] = get_word();
        disasm(std::cout, pc, iw, max_instruction_words);
        std::cout << "\n";
    }
}

uint32_t hex_or_die(const char* s)
{
    auto [valid, val] = from_hex(s);
    if (!valid)
        throw std::runtime_error { "Invalid hex number: " + std::string { s } };
    return val;
}

constexpr uint32_t interrupts_end = 0xC0; // Up to and including all trap vectors

constexpr const char* const cia_regname[16] = {
    "ciapra",
    "ciaprb",
    "ciaddra",
    "ciaddrb",
    "ciatalo",
    "ciatahi",
    "ciatblo",
    "ciatbhi",
    "ciatodlow",
    "ciatodmid",
    "ciatodhi",
    "$B00", // Unused
    "ciasdr",
    "ciaicr",
    "ciacra",
    "ciacrb",
};

constexpr const char* const ciaapra_bitnames[8] = {
    "OVL",
    "/LED",
    "/CHNG",
    "/WPRO",
    "/TK0",
    "/RDY",
    "/FIR0",
    "/FIR1",
};

constexpr const char* const ciabpra_bitnames[8] = {
    "BUSY",
    "POUT",
    "SEL",
    "/DSR",
    "/CTS",
    "/CD",
    "/RTS",
    "/DTS",
};

constexpr const char* const ciabprb_bitnames[8] = {
    "/STEP",
    "DIR",
    "/SIDE",
    "/SEL0",
    "/SEL1",
    "/SEL2",
    "/SEL3",
    "/MTR",
};

constexpr const char* const ciaicr_bitnames[8] = {
    "TA",
    "TB",
    "ALRM",
    "SP",
    "FLG",
    "BIT5",
    "BIT6",
    "IR/SETCLR",
};

constexpr const char* const ciacra_bitnames[8] = {
    "START",
    "PBON",
    "OUTMODE",
    "RUNMODE",
    "LOAD",
    "INMODE",
    "SPMODE",
    "TODIN",
};

constexpr const char* const ciacrb_bitnames[8] = {
    "START",
    "PBON",
    "OUTMODE",
    "RUNMODE",
    "LOAD",
    "INMODE0",
    "INMODE1",
    "ALARM",
};

constexpr const char* const custom_regname[0x100] = {
    "bltddat",
    "dmaconr",
    "vposr",
    "vhposr",
    "dskdatr",
    "joy0dat",
    "joy1dat",
    "clxdat",
    "adkconr",
    "pot0dat",
    "pot1dat",
    "potinp",
    "serdatr",
    "dskbytr",
    "intenar",
    "intreqr",
    "dskpt",
    "dskpt+2",
    "dsklen",
    "dskdat",
    "refptr",
    "vposw",
    "vhposw",
    "copcon",
    "serdat",
    "serper",
    "potgo",
    "joytest",
    "strequ",
    "strvbl",
    "strhor",
    "strlong",
    "bltcon0",
    "bltcon1",
    "bltafwm",
    "bltalwm",
    "bltcpt",
    "bltcptl",
    "bltbpt",
    "bltbptl",
    "bltapt",
    "bltaptl",
    "bltdpt",
    "bltdptl",
    "bltsize",
    "bltcon0l",
    "bltsizv",
    "bltsizh",
    "bltcmod",
    "bltbmod",
    "bltamod",
    "bltdmod",
    "reserved_068",
    "reserved_06a",
    "reserved_06c",
    "reserved_06e",
    "bltcdat",
    "bltbdat",
    "bltadat",
    "reserved_076",
    "sprhdat",
    "bplhdat",
    "lisaid",
    "dsksync",
    "cop1lc",
    "cop1lc+2",
    "cop2lc",
    "cop2lc+2",
    "copjmp1",
    "copjmp2",
    "copins",
    "diwstrt",
    "diwstop",
    "ddfstrt",
    "ddfstop",
    "dmacon",
    "clxcon",
    "intena",
    "intreq",
    "adkcon",
    "aud0+ac_ptr",
    "aud0+ac_ptr+2",
    "aud0+ac_len",
    "aud0+ac_per",
    "aud0+ac_vol",
    "aud0+ac_dat",
    "reserved_0ac",
    "reserved_0ae",
    "aud1+ac_ptr",
    "aud1+ac_ptr+2",
    "aud1+ac_len",
    "aud1+ac_per",
    "aud1+ac_vol",
    "aud1+ac_dat",
    "reserved_0bc",
    "reserved_0be",
    "aud2+ac_ptr",
    "aud2+ac_ptr+2",
    "aud2+ac_len",
    "aud2+ac_per",
    "aud2+ac_vol",
    "aud2+ac_dat",
    "reserved_0cc",
    "reserved_0ce",
    "aud3+ac_ptr",
    "aud3+ac_ptr+2",
    "aud3+ac_len",
    "aud3+ac_per",
    "aud3+ac_vol",
    "aud3+ac_dat",
    "reserved_0dc",
    "reserved_0de",
    "bplpt+0*2+0",
    "bplpt+0*2+2",
    "bplpt+1*2+0",
    "bplpt+1*2+2",
    "bplpt+2*2+0",
    "bplpt+2*2+2",
    "bplpt+3*2+0",
    "bplpt+3*2+2",
    "bplpt+4*2+0",
    "bplpt+4*2+2",
    "bplpt+5*2+0",
    "bplpt+5*2+2",
    "bplpt+6*2+0",
    "bplpt+6*2+2",
    "bplpt+7*2+0",
    "bplpt+7*2+1",
    "bplcon0",
    "bplcon1",
    "bplcon2",
    "bplcon3",
    "bpl1mod",
    "bpl2mod",
    "bplcon4",
    "clxcon2",
    "bpl1dat",
    "bpl2dat",
    "bpl3dat",
    "bpl4dat",
    "bpl5dat",
    "bpl6dat",
    "bpl7dat",
    "bpl8dat",
    "sprpt+4*0",
    "sprpt+4*0+2",
    "sprpt+4*1",
    "sprpt+4*1+2",
    "sprpt+4*2",
    "sprpt+4*2+2",
    "sprpt+4*3",
    "sprpt+4*3+2",
    "sprpt+4*4",
    "sprpt+4*4+2",
    "sprpt+4*5",
    "sprpt+4*5+2",
    "sprpt+4*6",
    "sprpt+4*6+2",
    "sprpt+4*7",
    "sprpt+4*7+2",
    "spr+0*sd_SIZEOF+sd_pos",
    "spr+0*sd_SIZEOF+sd_ctl",
    "spr+0*sd_SIZEOF+sd_dataa",
    "spr+0*sd_SIZEOF+sd_dataB",
    "spr+1*sd_SIZEOF+sd_pos",
    "spr+1*sd_SIZEOF+sd_ctl",
    "spr+1*sd_SIZEOF+sd_dataa",
    "spr+1*sd_SIZEOF+sd_dataB",
    "spr+2*sd_SIZEOF+sd_pos",
    "spr+2*sd_SIZEOF+sd_ctl",
    "spr+2*sd_SIZEOF+sd_dataa",
    "spr+2*sd_SIZEOF+sd_dataB",
    "spr+3*sd_SIZEOF+sd_pos",
    "spr+3*sd_SIZEOF+sd_ctl",
    "spr+3*sd_SIZEOF+sd_dataa",
    "spr+3*sd_SIZEOF+sd_dataB",
    "spr+4*sd_SIZEOF+sd_pos",
    "spr+4*sd_SIZEOF+sd_ctl",
    "spr+4*sd_SIZEOF+sd_dataa",
    "spr+4*sd_SIZEOF+sd_dataB",
    "spr+5*sd_SIZEOF+sd_pos",
    "spr+5*sd_SIZEOF+sd_ctl",
    "spr+5*sd_SIZEOF+sd_dataa",
    "spr+5*sd_SIZEOF+sd_dataB",
    "spr+6*sd_SIZEOF+sd_pos",
    "spr+6*sd_SIZEOF+sd_ctl",
    "spr+6*sd_SIZEOF+sd_dataa",
    "spr+6*sd_SIZEOF+sd_dataB",
    "spr+7*sd_SIZEOF+sd_pos",
    "spr+7*sd_SIZEOF+sd_ctl",
    "spr+7*sd_SIZEOF+sd_dataa",
    "spr+7*sd_SIZEOF+sd_dataB",
    "color",
    "color+1*2",
    "color+2*2",
    "color+3*2",
    "color+4*2",
    "color+5*2",
    "color+6*2",
    "color+7*2",
    "color+8*2",
    "color+9*2",
    "color+10*2",
    "color+11*2",
    "color+12*2",
    "color+13*2",
    "color+14*2",
    "color+15*2",
    "color+16*2",
    "color+17*2",
    "color+18*2",
    "color+19*2",
    "color+20*2",
    "color+21*2",
    "color+22*2",
    "color+23*2",
    "color+24*2",
    "color+25*2",
    "color+26*2",
    "color+27*2",
    "color+28*2",
    "color+29*2",
    "color+30*2",
    "color+31*2",
    "htotal",
    "hsstop",
    "hbstrt",
    "hbstop",
    "vtotal",
    "vsstop",
    "vbstrt",
    "vbstop",
    "sprhstrt",
    "sprhstop",
    "bplhstrt",
    "bplhstop",
    "hhposw",
    "hhposr",
    "beamcon0",
    "hsstrt",
    "vsstrt",
    "hcenter",
    "diwhigh",
    "bplhmod",
    "sprhpth",
    "sprhptl",
    "bplhpth",
    "bplhptl",
    "reserved_1f0",
    "reserved_1f2",
    "reserved_1f4",
    "reserved_1f6",
    "reserved_1f8",
    "reserved_1fa",
    "fmode",
    "custom_noop",
};

constexpr const char* const dmacon_bitnames[16] = {
    "AUD0EN",
    "AUD1EN",
    "AUD2EN",
    "AUD3EN",
    "DSKEN",
    "SPREN",
    "BLTEN",
    "COPEN",
    "BPLEN",
    "DMAEN",
    "BLTPRI",
    "BIT11",
    "BIT12",
    "BZERO",
    "BBUSY",
    "SETCLR",
};

constexpr const char* const int_bitnames[16] = {
    "TBE",
    "DSKBLK",
    "SOFT",
    "PORTS",
    "COPER",
    "VERTB",
    "BLIT",
    "AUD0",
    "AUD1",
    "AUD2",
    "AUD3",
    "RBF",
    "DSKSYN",
    "EXTER",
    "INTEN",
    "SETCLR",
};

constexpr const char* const adkcon_bitnames[16] = {
    "USE0V1",
    "USE1V2",
    "USE2V3",
    "USE3VN",
    "USE0P1",
    "USE1P2",
    "USE2P3",
    "USE3PN",
    "FAST",
    "MSBSYNC",
    "WORDSYNC",
    "UARTBRK",
    "MFMPREC",
    "PRECOMP0",
    "PRECOMP1",
    "SETCLR",
};

std::string interrupt_name(uint8_t vec)
{
    switch (vec) {
    case 2:
        return "BusError";
    case 3:
        return "AddressError";
    case 4:
        return "IllegalInstruction";
    case 5:
        return "ZeroDivide";
    case 6:
        return "ChkException";
    case 7:
        return "TrapVException";
    case 8:
        return "PrivililegeViolation";
    case 9:
        return "TraceException";
    case 10:
        return "Line1010Exception";
    case 11:
        return "Line1111Exception";
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
        return "Level" + std::to_string(vec - 24);
    }
    if (vec >= 32 && vec < 48)
        return "Trap" + hexstring(vec - 32, 1);
    return "Interrupt" + hexstring(vec, 2);
}

class simval {
public:
    simval()
        : raw_ { 0 }
        , state_ { STATE_UNKNOWN }
    {
    }

    explicit simval(uint32_t val)
        : raw_ { val }
        , state_ { STATE_KNOWN }
    {
    }

    bool known() const
    {
        return state_ == STATE_KNOWN;
    }

    uint32_t raw() const
    {
        assert(known());
        return raw_;
    }

    void reset()
    {
        raw_ = 0;
        state_ = STATE_UNKNOWN;
    }

    friend std::ostream& operator<<(std::ostream& os, const simval& v)
    {
        switch (v.state_) {
        case STATE_KNOWN:
            return os << ("$" + hexstring(v.raw_));
        case STATE_UNKNOWN:
            return os << "UNKNOWN";
        default:
            assert(0);
            return os << "INVALID STATE";
        }
    }

    simval& operator+=(const simval& rhs)
    {
        if (known() && rhs.known())
            raw_ += rhs.raw_;
        else
            reset();
        return *this;
    }

    simval& operator-=(const simval& rhs)
    {
        if (known() && rhs.known())
            raw_ -= rhs.raw_;
        else
            reset();
        return *this;
    }

private:
    uint32_t raw_;
    enum { STATE_UNKNOWN, STATE_KNOWN } state_;
};

simval operator+(const simval& lhs, const simval& rhs)
{
    auto res = lhs;
    return res += rhs;
}

simval operator-(const simval& lhs, const simval& rhs)
{
    auto res = lhs;
    return res += rhs;
}

struct simregs {
    simval d[8];
    simval a[8];
};

enum class base_data_type {
    unknown_,
    char_,
    byte_,
    word_,
    long_,
    code_,
    copper_code_,
    jumptab_word_,
    ptr_,
    bptr_,
    bstr_,
    struct_,
    bcpl_seglist_,
    rptrw_,
    patchitem_, // whdload patch item
};

class structure_definition;

class type {
public:
    explicit type(base_data_type t)
        : t_ { t }
        , ptr_ { nullptr }
        , len_ { 0 }
    {
    }
    explicit type(const type& base, uint32_t len, base_data_type t)
        : t_ { t }
        , ptr_ { &base }
        , len_ { len }
    {
        assert(t == base_data_type::ptr_ || (t == base_data_type::bptr_ && len == 0));
    }
    explicit type(const structure_definition& s)
        : t_ { base_data_type::struct_ }
        , struct_ { &s }
        , len_ { 0 }
    {
    }
    explicit type(base_data_type t, uint32_t base)
        : t_ { t }
        , base_ { base }
        , len_ { 0 }
    {
        assert(t == base_data_type::rptrw_);
    }
    type(const type&) = delete;
    type& operator=(const type&) = delete;

    base_data_type base() const
    {
        return t_;
    }

    const type* ptr() const
    {
        return t_ == base_data_type::ptr_ ? ptr_ : nullptr;
    }

    const type* bptr() const
    {
        return t_ == base_data_type::bptr_ ? ptr_ : nullptr;
    }

    const structure_definition* struct_def() const
    {
        return t_ == base_data_type::struct_ ? struct_ : nullptr;
    }

    uint32_t rptr_base() const
    {
        assert(t_ == base_data_type::rptrw_);
        return base_;
    }

    uint32_t len() const
    {
        return len_;
    }

private:
    base_data_type t_;
    union {
        const type* ptr_;
        const structure_definition* struct_;
        uint32_t base_;
    };
    uint32_t len_ = 0;
};

const type unknown_type { base_data_type::unknown_ };
const type char_type { base_data_type::char_ };
const type byte_type { base_data_type::byte_ };
const type word_type { base_data_type::word_ };
const type long_type { base_data_type::long_ };
const type code_type { base_data_type::code_ };
const type copper_code_type { base_data_type::copper_code_ };
const type jumptab_word_type { base_data_type::jumptab_word_ };
const type unknown_ptr { unknown_type, 0, base_data_type::ptr_ };
const type char_ptr { char_type, 0, base_data_type::ptr_ };
const type byte_ptr { byte_type, 0, base_data_type::ptr_ };
const type word_ptr { word_type, 0, base_data_type::ptr_ };
const type long_ptr { long_type, 0, base_data_type::ptr_ };
const type code_ptr { code_type, 0, base_data_type::ptr_ };
const type copper_code_ptr { copper_code_type, 0, base_data_type::ptr_ };
const type bptr_type { unknown_type, 0, base_data_type::bptr_ };
const type bstr_type { base_data_type::bstr_ };
const type bcpl_seglist_type { base_data_type::bcpl_seglist_ };
const type patchitem_type { base_data_type::patchitem_ };

const type hack_str_type { base_data_type::unknown_ };

std::unordered_map<std::string, const type*> typenames {
    { "UNKNOWN", &unknown_type },
    { "CHAR", &char_type },
    { "BYTE", &byte_type },
    { "WORD", &word_type },
    { "LONG", &long_type },
    { "CODE", &code_type },
    { "COPPER", &copper_code_type },
    { "JTABW", &jumptab_word_type },
    { "BSEGLIST", &bcpl_seglist_type },
    { "STR", &hack_str_type },
};

uint32_t sizeof_type(const type& t);
std::ostream& operator<<(std::ostream& os, const type& t);

const type& make_array_type(const type& base, uint32_t len)
{
    assert(len);
    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->ptr() == &base && t->len() == len) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(base, len, base_data_type::ptr_));
    return *types.back().get();
}

const type& make_pointer_type(const type& base)
{
    if (&base == &unknown_type)
        return unknown_ptr;
    else if (&base == &char_type)
        return char_ptr;
    else if (&base == &byte_type)
        return byte_ptr;
    else if (&base == &code_type)
        return code_ptr;

    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->ptr() == &base) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(base, 0, base_data_type::ptr_));
    return *types.back().get();
}

const type& make_bpointer_type(const type& base)
{
    if (&base == &unknown_type)
        return bptr_type;
    assert(base.base() == base_data_type::struct_);
    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->ptr() == &base) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(base, 0, base_data_type::bptr_));
    return *types.back().get();
}

const type& make_struct_type(const structure_definition& s)
{
    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->struct_def() == &s) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(s));
    return *types.back().get();
}

const type& make_rptrw(uint32_t base)
{
    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->rptr_base() == base) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(base_data_type::rptrw_, base));
    return *types.back().get();
}

const type& type_from_size(opsize s)
{
    switch (s) {
    case opsize::none:
    default:
        return unknown_type;
    case opsize::b:
        return byte_type;
    case opsize::w:
        return word_type;
    case opsize::l:
        return long_type;
    }
}

class struct_field {
public:
    struct_field(const char* name, const type& t, int32_t offset = auto_offset)
        : name_ { name }
        , offset_ { offset }
        , type_ { &t }
    {
    }

    const char* name() const
    {
        return name_.c_str();
    }

    int32_t offset() const
    {
        assert(offset_ != auto_offset);
        return offset_;
    }

    int32_t end_offset() const
    {
        assert(offset_ != auto_offset);
        return offset_ + sizeof_type(*type_);
    }

    const type& t() const
    {
        return *type_;
    }

private:
    std::string name_;
    int32_t offset_;
    const type* type_;
    static constexpr int32_t auto_offset = INT32_MAX;

    friend class structure_definition;
};

class structure_definition {
public:
    explicit structure_definition(const char* const name, const std::vector<struct_field>& fields)
        : name_ { name }
        , fields_ { fields }
    {
        // handle auto offset
        int32_t offset = 0;
        for (auto& f : fields_) {
            assert(sizeof_type(f.t()) != 0); // Must not use undefined structures etc. (pointers to structs are fine)
            assert(!std::strchr(f.name_.c_str(), ',') && !std::strchr(f.name_.c_str(), '*')); // Check for some errors
            if (f.offset_ == struct_field::auto_offset)
                f.offset_ = offset;
            offset = f.end_offset();
        }

        std::sort(fields_.begin(), fields_.end(), [](const struct_field& l, const struct_field& r) { return l.offset() < r.offset(); });

        if (!typenames.insert({ name_, &make_struct_type(*this) }).second) {
            assert(false);
            throw std::runtime_error { "Redefinition of struct " + std::string { name_ } };
        }
    }

    structure_definition(structure_definition&) = delete;
    const structure_definition& operator=(structure_definition&) = delete;

    const char* name() const
    {
        return name_.c_str();
    }

    const std::vector<struct_field>& fields() const
    {
        return fields_;
    }

    uint32_t size() const
    {
        return static_cast<uint32_t>(fields_.empty() ? 0 : fields_.back().end_offset());
    }

    uint32_t negsize() const
    {
        if (fields_.empty())
            return 0;
        if (fields_.front().offset() >= 0)
            return 0;
        return -fields_.front().offset();
    }

    std::pair<const struct_field*, int32_t> field_at(int32_t offset) const
    {
        for (const auto& f : fields_) {
            if (offset >= f.offset() && offset < f.end_offset()) {
                const auto diff = offset - f.offset();
                if (f.t().struct_def()) {
                    return f.t().struct_def()->field_at(diff);
                }
                return { &f, diff };
            }
        }
        return { nullptr, 0 };
    }

    std::optional<std::string> field_name(int32_t offset, bool address_op) const
    {
        for (const auto& f : fields_) {
            if (offset >= f.offset() && offset < f.end_offset()) {
                std::string name = f.name();
                const auto diff = offset - f.offset();
                if (f.t().struct_def() && (!address_op || diff)) {
                    if (const auto n = f.t().struct_def()->field_name(diff, address_op); n) {
                        return { name + "+" + *n };
                    }
                }
                if (diff)
                    name += "+$" + hexstring(diff, 4);
                return { name };
            }
        }
        return { };
    }

    int32_t field_offset(const std::string& name) const
    {
        for (const auto& f : fields_) {
            if (f.name() == name)
                return f.offset();
        }
        throw std::runtime_error{"Field " + name + " not found in " + name_};
    }

private:
    std::string name_;
    std::vector<struct_field> fields_;
};

enum class regname : uint8_t {
    D0, D1, D2, D3, D4, D5, D6, D7,
    A0, A1, A2, A3, A4, A5, A6, A7,
};

std::ostream& operator<<(std::ostream& os, regname r)
{
    const auto rv = static_cast<uint8_t>(r);
    return os << (rv > 7 ? 'A' : 'D') << (rv & 7);
}

struct argument_description {
    regname reg;
    std::string name;
    const type* t;
};

class function_description {
public:
    explicit function_description(const std::vector<argument_description>& output, const std::vector<argument_description>& input, const std::function<void(void)>& sim = {})
        : output_ { output }
        , input_ { input }
        , sim_ { sim }
    {
    }

    const std::vector<argument_description>& output() const
    {
        return output_;
    }

    const std::vector<argument_description>& input() const
    {
        return input_;
    }

    const std::function<void(void)>& sim() const
    {
        return sim_;
    }

private:
    std::vector<argument_description> output_;
    std::vector<argument_description> input_;
    std::function<void(void)> sim_;
};

std::ostream& operator<<(std::ostream& os, const type& t)
{
    if (t.ptr()) {
        os << *t.ptr();
        if (t.len())
            os << "[" << t.len() << "]";
        else
            os << "*";
        return os;
    } else if (t.bptr()) {
        assert(t.len() == 0);
        if (t.bptr()->base() == base_data_type::unknown_)
            return os << "BPTR";
        return os << "BPTR(" << *t.bptr() << ")";
    }
    switch (t.base()) {
    case base_data_type::unknown_:
        return os << "UNKNOWN";
    case base_data_type::char_:
        return os << "CHAR";
    case base_data_type::byte_:
        return os << "BYTE";
    case base_data_type::word_:
        return os << "WORD";
    case base_data_type::long_:
        return os << "LONG";
    case base_data_type::code_:
        return os << "CODE";
    case base_data_type::copper_code_:
        return os << "COPPER";
    case base_data_type::jumptab_word_:
        return os << "JTABW";
    case base_data_type::bstr_:
        return os << "BSTR";
    case base_data_type::struct_:
        return os << "struct " << t.struct_def()->name();
    case base_data_type::bcpl_seglist_:
        return os << "BSEGLIST";
    case base_data_type::rptrw_:
        return os << "RPTRW";
    case base_data_type::patchitem_:
        return os << "PATCHITEM";
    default:
        assert(false);
        break;
    }
    return os;
}

uint32_t sizeof_type(const type& t)
{
    switch (t.base()) {
    //case base_data_type::unknown_:
    case base_data_type::char_:
    case base_data_type::byte_:
        return 1;
    case base_data_type::word_:
    case base_data_type::copper_code_:
    case base_data_type::jumptab_word_:
    case base_data_type::rptrw_:
    case base_data_type::patchitem_:
        return 2;
    case base_data_type::long_:
        return 4;
    case base_data_type::code_:
        return 2;
    case base_data_type::ptr_:
        return t.len() ? t.len() * sizeof_type(*t.ptr()) : 4;
    case base_data_type::bptr_:
        assert(t.len() == 0);
        return 4;
    case base_data_type::bstr_:
        return 4;
    case base_data_type::struct_:
        return t.struct_def()->size();
    case base_data_type::bcpl_seglist_:
        // ??
    default:
        assert(false);
    }
    std::ostringstream oss;
    oss << "Unhandled type in sizeof_type: " << t;
    throw std::runtime_error { oss.str() };
}

const type* parse_type(const std::string& str)
{
    std::string basetype;
    size_t p = 0;
    while (p < str.length() && str[p] != '*' && str[p] != '[')
        basetype.push_back(str[p++]);

    const type* t = nullptr;
    if (auto it = typenames.find(basetype); it != typenames.end())
        t = it->second;
    if (!t)
        return nullptr;

    for (; p < str.length(); ++p) {
        if (str[p] == '*') {
            t = &make_pointer_type(*t);
        } else if (str[p] == '[' && ++p < str.length()) {
            uint32_t len = 0;
            while (p < str.length() && isdigit(str[p]))
                len = len * 10 + (str[p++] - '0');
            if (p >= str.length() || str[p] != ']')
                return nullptr;
            ++p;
            t = &make_array_type(*t, len);
        } else {
            return nullptr;
        }
    }
    return t;
}

const type& libvec_code = make_array_type(code_type, 3);

// nodes.h
const structure_definition MinNode {
    "MinNode",
    {
        { "mln_Succ", make_pointer_type(make_struct_type(MinNode)) },
        { "mln_Pred", make_pointer_type(make_struct_type(MinNode)) },
    }
};

const structure_definition Node {
    "Node",
    {
        { "ln_Succ", make_pointer_type(make_struct_type(Node)) },
        { "ln_Pred", make_pointer_type(make_struct_type(Node)) },
        { "ln_Type", byte_type },
        { "ln_Pri", byte_type },
        { "ln_Name", char_ptr },
    }
};

constexpr uint8_t NT_UNKNOWN        = 0;
constexpr uint8_t NT_TASK           = 1;   // Exec task
constexpr uint8_t NT_INTERRUPT      = 2;
constexpr uint8_t NT_DEVICE         = 3;
constexpr uint8_t NT_MSGPORT        = 4;
constexpr uint8_t NT_MESSAGE        = 5;   // Indicates message currently pending
constexpr uint8_t NT_FREEMSG        = 6;
constexpr uint8_t NT_REPLYMSG       = 7;   // Message has been replied
constexpr uint8_t NT_RESOURCE       = 8;
constexpr uint8_t NT_LIBRARY        = 9;
constexpr uint8_t NT_MEMORY         = 10;
constexpr uint8_t NT_SOFTINT        = 11;  // Internal flag used by SoftInits
constexpr uint8_t NT_FONT           = 12;
constexpr uint8_t NT_PROCESS        = 13;  // AmigaDOS Process
constexpr uint8_t NT_SEMAPHORE      = 14;
constexpr uint8_t NT_SIGNALSEM      = 15;  // signal semaphores
constexpr uint8_t NT_BOOTNODE       = 16;
constexpr uint8_t NT_KICKMEM        = 17;
constexpr uint8_t NT_GRAPHICS       = 18;
constexpr uint8_t NT_DEATHMESSAGE   = 19;


// lists.h
const structure_definition List {
    "List",
    {
        { "lh_Head", make_pointer_type(make_struct_type(Node)) },
        { "lh_Tail", make_pointer_type(make_struct_type(Node)) },
        { "lh_TailPred", make_pointer_type(make_struct_type(Node)) },
        { "lh_Type", byte_type },
        { "l_pad", byte_type },
    }
};

const structure_definition MinList {
    "MinList",
    {
        { "MLH_HEAD", make_pointer_type(make_struct_type(MinNode)) },
        { "MLH_TAIL", make_pointer_type(make_struct_type(MinNode)) },
        { "MLH_TAILPRED", make_pointer_type(make_struct_type(MinNode)) },
    }
};

const structure_definition Library {
    "Library",
    {
        { "lib_Node", make_struct_type(Node) },
        { "lib_Flags", byte_type },
        { "lib_pad", byte_type },
        { "lib_NegSize", word_type },
        { "lib_PosSize", word_type },
        { "lib_Version", word_type },
        { "lib_Revision", word_type },
        { "lib_IdString", char_ptr },
        { "lib_Sum", long_type },
        { "lib_OpenCnt", word_type },
    }
};

// interrupts.h

const structure_definition Interrupt {
    "Interrupt",
    {
        { "is_Node", make_struct_type(Node) },
        { "is_Data", unknown_ptr },
        { "is_Code", code_ptr },
    }
};

const structure_definition IntVector {
    "IntVector",
    {
        { "iv_Data", unknown_ptr },
        { "iv_Code", code_ptr },
        { "iv_Node", make_pointer_type(make_struct_type(Node)) },
    }
};

const structure_definition SoftIntList {
    "SoftIntList",
    {
        { "sh_List", make_struct_type(List) },
        { "sh_Pad", word_type },
    }
};

// tasks.h
const structure_definition Task {
    "Task",
    {
        { "tc_Node", make_struct_type(Node) },
        { "tc_Flags", byte_type },
        { "tc_State", byte_type },
        { "tc_IDNestCnt", byte_type },
        { "tc_TDNestCnt", byte_type },
        { "tc_SigAlloc", long_type },
        { "tc_SigWait", long_type },
        { "tc_SigRecvd", long_type },
        { "tc_SigExcept", long_type },
        { "tc_TrapAlloc", word_type },
        { "tc_TrapAble", word_type },
        { "tc_ExceptData", unknown_ptr },
        { "tc_ExceptCode", unknown_ptr },
        { "tc_TrapData", unknown_ptr },
        { "tc_TrapCode", unknown_ptr },
        { "tc_SPReg", unknown_ptr },
        { "tc_SPLower", unknown_ptr },
        { "tc_SPUpper", unknown_ptr },
        { "tc_Switch", code_ptr }, // VOID (*tc_Switch)()
        { "tc_Launch", code_ptr }, // VOID (*tc_Launch)()
        { "tc_MemEntry", make_struct_type(List) },
        { "tc_UserData", unknown_ptr },
    }
};

// resident.h
const structure_definition Resident {
    "Resident",
    {
        { "rt_MatchWord", word_type },
        { "rt_MatchTag", make_pointer_type(make_struct_type(Resident)) },
        { "rt_EndSkip", unknown_ptr },
        { "rt_Flags", byte_type },
        { "rt_Version", byte_type },
        { "rt_Type", byte_type },
        { "rt_Pri", byte_type },
        { "rt_Name", char_ptr },
        { "rt_IdString", char_ptr },
        { "rt_Init", unknown_ptr },
    }
};

// ports.h
const structure_definition MsgPort {
    "MsgPort",
    {
        { "mp_Node", make_struct_type(Node) },
        { "mp_Flags", byte_type },
        { "mp_SigBit", byte_type },
        { "mp_SigTask", unknown_ptr },
        { "mp_MsgList", make_struct_type(List) },
    }
};
const structure_definition Message {
    "Message",
    {
        { "mn_Node", make_struct_type(Node) },
        { "mn_ReplyPort", make_pointer_type(make_struct_type(MsgPort)) },
        { "mn_Length", word_type },
    }
};

// devices.h
const structure_definition Device {
    "Device",
    {
        { "dd_Library", make_struct_type(Library) },

        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },
        { "_LVODevBeginIO", libvec_code, -30 },
        { "_LVODevAbortIO", libvec_code, -36 },
    }
};
const structure_definition Unit {
    "Unit",
    {
        { "unit_MsgPort", make_struct_type(MsgPort) },
        { "unit_flags", byte_type },
        { "unit_pad", byte_type },
        { "unit_OpenCnt", word_type },
    }
};

// io.h
const structure_definition IORequest {
    "IORequest",
    {
        { "io_Message", make_struct_type(Message) },
        { "io_Device", make_pointer_type(make_struct_type(Device)) },
        { "io_Unit", make_pointer_type(make_struct_type(Unit)) },
        { "io_Command", word_type },
        { "io_Flags", byte_type },
        { "io_Error", byte_type },
    }
};
const structure_definition IOStdReq {
    "IOStdReq",
    {
        { "io_Message", make_struct_type(Message) },
        { "io_Device", make_pointer_type(make_struct_type(Device)) },
        { "io_Unit", make_pointer_type(make_struct_type(Unit)) },
        { "io_Command", word_type },
        { "io_Flags", byte_type },
        { "io_Error", byte_type },
        { "io_Actual", long_type },
        { "io_Length", long_type },
        { "io_Data", unknown_ptr },
        { "io_Offset", long_type },
    }
};

// semaphores.h
const structure_definition SemaphoreRequest {
    "SemaphoreRequest",
    {
        { "sr_Link", make_struct_type(MinNode) },
        { "sr_Waiter", make_pointer_type(make_struct_type(Task)) },
    }
};
const structure_definition SignalSemaphore {
    "SignalSemaphore",
    {
        { "ss_Link", make_struct_type(Node) },
        { "ss_NestCount", word_type },
        { "ss_WaitQueue", make_struct_type(MinList) },
        { "ss_MultipleLink", make_struct_type(SemaphoreRequest) },
        { "ss_Owner", make_pointer_type(make_struct_type(Task)) },
        { "ss_QueueCount", word_type },
    }
};
const structure_definition SemaphoreMessage {
    "SemaphoreMessage",
    {
        { "ssm_Message", make_struct_type(Message) },
        { "ssm_Semaphore", make_pointer_type(make_struct_type(SignalSemaphore)) },
    }
};
const structure_definition Semaphore {
    "Semaphore",
    {
        { "sm_MsgPort", make_struct_type(MsgPort) },
        { "sm_Bids", word_type },
    }
};

// execbase.h

constexpr int32_t _LVOSupervisor = -30;
constexpr int32_t _LVOMakeLibrary = -84;
constexpr int32_t _LVOMakeFunctions = -90;
constexpr int32_t _LVOFindResident = -96;
constexpr int32_t _LVOSetIntVector = -162;
constexpr int32_t _LVOAddIntServer = -168;
constexpr int32_t _LVOAllocMem = -198;
constexpr int32_t _LVOAddTask = -282;
constexpr int32_t _LVOFindTask = -294;
constexpr int32_t _LVOPutMsg = -366;
constexpr int32_t _LVOGetMsg = -372;
constexpr int32_t _LVOReplyMsg = -378;
constexpr int32_t _LVOAddLibrary = -396;
constexpr int32_t _LVOOldOpenLibrary = -408;
constexpr int32_t _LVOOpenDevice = -444;
constexpr int32_t _LVOOpenResource = -498;
constexpr int32_t _LVORawDoFmt = -522;
constexpr int32_t _LVOOpenLibrary = -552;

const structure_definition ExecBase {
    "ExecBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "SoftVer", word_type },
        { "LowMemChkSum", word_type },
        { "ChkBase", long_type },
        { "ColdCapture", code_ptr },
        { "CoolCapture", code_ptr },
        { "WarmCapture", code_ptr },
        { "SysStkUpper", unknown_ptr },
        { "SysStkLower", unknown_ptr },
        { "MaxLocMem", long_type },
        { "DebugEntry", unknown_ptr },
        { "DebugData", unknown_ptr },
        { "AlertData", unknown_ptr },
        { "MaxExtMem", unknown_ptr },
        { "ChkSum", word_type },
        { "IntVects", make_array_type(make_struct_type(IntVector), 16) },
        { "ThisTask", make_pointer_type(make_struct_type(Task)) },
        { "IdleCount", long_type },
        { "DispCount", long_type },
        { "Quantum", word_type },
        { "Elapsed", word_type },
        { "SysFlags", word_type },
        { "IDNestCnt", byte_type },
        { "TDNestCnt", byte_type },
        { "AttnFlags", word_type },
        { "AttnResched", word_type },
        { "ResModules", unknown_ptr },
        { "TaskTrapCode", code_ptr },
        { "TaskExceptCode", code_ptr },
        { "TaskExitCode", code_ptr },
        { "TaskSigAlloc", long_type },
        { "TaskTrapAlloc", word_type },
        { "MemList", make_struct_type(List) },
        { "ResourceList", make_struct_type(List) },
        { "DeviceList", make_struct_type(List) },
        { "IntrList", make_struct_type(List) },
        { "LibList", make_struct_type(List) },
        { "PortList", make_struct_type(List) },
        { "TaskReady", make_struct_type(List) },
        { "TaskWait", make_struct_type(List) },
        { "SoftInts", make_array_type(make_struct_type(SoftIntList), 5) },
        { "LastAlert", make_array_type(long_type, 4) },
        { "VBlankFrequency", byte_type },
        { "PowerSupplyFrequency", byte_type },
        { "SemaphoreList", make_struct_type(List) },
        { "KickMemPtr", unknown_ptr },
        { "KickTagPtr", unknown_ptr },
        { "KickCheckSum", unknown_ptr },
#if 0 // V36 onwards
        { "ex_Pad0", word_type },
        { "ex_LaunchPoint", long_type },
        { "ex_RamLibPrivate", unknown_ptr },
        { "ex_EClockFrequency", long_type },
        { "ex_CacheControl", long_type },
        { "ex_TaskID", long_type },
        { "ex_Reserved1", make_array_type(long_type, 5) },
        { "ex_MMULock", unknown_ptr },
        { "ex_Reserved2", make_array_type(long_type, 3) },
        // V39
        { "ex_MemHandlers", make_struct_type(MinList) },
        { "ex_MemHandler", unknown_ptr },
#endif
        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },

        { "_LVOSupervisor", libvec_code, _LVOSupervisor },
        { "_LVOExitIntr", libvec_code, -36 },
        { "_LVOSchedule", libvec_code, -42 },
        { "_LVOReschedule", libvec_code, -48 },
        { "_LVOSwitch", libvec_code, -54 },
        { "_LVODispatch", libvec_code, -60 },
        { "_LVOException", libvec_code, -66 },
        { "_LVOInitCode", libvec_code, -72 },
        { "_LVOInitStruct", libvec_code, -78 },
        { "_LVOMakeLibrary", libvec_code, _LVOMakeLibrary },
        { "_LVOMakeFunctions", libvec_code, _LVOMakeFunctions },
        { "_LVOFindResident", libvec_code, _LVOFindResident },
        { "_LVOInitResident", libvec_code, -102 },
        { "_LVOAlert", libvec_code, -108 },
        { "_LVODebug", libvec_code, -114 },
        { "_LVODisable", libvec_code, -120 },
        { "_LVOEnable", libvec_code, -126 },
        { "_LVOForbid", libvec_code, -132 },
        { "_LVOPermit", libvec_code, -138 },
        { "_LVOSetSR", libvec_code, -144 },
        { "_LVOSuperState", libvec_code, -150 },
        { "_LVOUserState", libvec_code, -156 },
        { "_LVOSetIntVector", libvec_code, _LVOSetIntVector },
        { "_LVOAddIntServer", libvec_code, _LVOAddIntServer },
        { "_LVORemIntServer", libvec_code, -174 },
        { "_LVOCause", libvec_code, -180 },
        { "_LVOAllocate", libvec_code, -186 },
        { "_LVODeallocate", libvec_code, -192 },
        { "_LVOAllocMem", libvec_code, _LVOAllocMem },
        { "_LVOAllocAbs", libvec_code, -204 },
        { "_LVOFreeMem", libvec_code, -210 },
        { "_LVOAvailMem", libvec_code, -216 },
        { "_LVOAllocEntry", libvec_code, -222 },
        { "_LVOFreeEntry", libvec_code, -228 },
        { "_LVOInsert", libvec_code, -234 },
        { "_LVOAddHead", libvec_code, -240 },
        { "_LVOAddTail", libvec_code, -246 },
        { "_LVORemove", libvec_code, -252 },
        { "_LVORemHead", libvec_code, -258 },
        { "_LVORemTail", libvec_code, -264 },
        { "_LVOEnqueue", libvec_code, -270 },
        { "_LVOFindName", libvec_code, -276 },
        { "_LVOAddTask", libvec_code, _LVOAddTask },
        { "_LVORemTask", libvec_code, -288 },
        { "_LVOFindTask", libvec_code, _LVOFindTask },
        { "_LVOSetTaskPri", libvec_code, -300 },
        { "_LVOSetSignal", libvec_code, -306 },
        { "_LVOSetExcept", libvec_code, -312 },
        { "_LVOWait", libvec_code, -318 },
        { "_LVOSignal", libvec_code, -324 },
        { "_LVOAllocSignal", libvec_code, -330 },
        { "_LVOFreeSignal", libvec_code, -336 },
        { "_LVOAllocTrap", libvec_code, -342 },
        { "_LVOFreeTrap", libvec_code, -348 },
        { "_LVOAddPort", libvec_code, -354 },
        { "_LVORemPort", libvec_code, -360 },
        { "_LVOPutMsg", libvec_code, _LVOPutMsg },
        { "_LVOGetMsg", libvec_code, _LVOGetMsg },
        { "_LVOReplyMsg", libvec_code, _LVOReplyMsg },
        { "_LVOWaitPort", libvec_code, -384 },
        { "_LVOFindPort", libvec_code, -390 },
        { "_LVOAddLibrary", libvec_code, _LVOAddLibrary },
        { "_LVORemLibrary", libvec_code, -402 },
        { "_LVOOldOpenLibrary", libvec_code, _LVOOldOpenLibrary },
        { "_LVOCloseLibrary", libvec_code, -414 },
        { "_LVOSetFunction", libvec_code, -420 },
        { "_LVOSumLibrary", libvec_code, -426 },
        { "_LVOAddDevice", libvec_code, -432 },
        { "_LVORemDevice", libvec_code, -438 },
        { "_LVOOpenDevice", libvec_code, _LVOOpenDevice },
        { "_LVOCloseDevice", libvec_code, -450 },
        { "_LVODoIO", libvec_code, -456 },
        { "_LVOSendIO", libvec_code, -462 },
        { "_LVOCheckIO", libvec_code, -468 },
        { "_LVOWaitIO", libvec_code, -474 },
        { "_LVOAbortIO", libvec_code, -480 },
        { "_LVOAddResource", libvec_code, -486 },
        { "_LVORemResource", libvec_code, -492 },
        { "_LVOOpenResource", libvec_code, _LVOOpenResource },
        { "_LVORawIOInit", libvec_code, -504 },
        { "_LVORawMayGetChar", libvec_code, -510 },
        { "_LVORawPutChar", libvec_code, -516 },
        { "_LVORawDoFmt", libvec_code, _LVORawDoFmt },
        { "_LVOGetCC", libvec_code, -528 },
        { "_LVOTypeOfMem", libvec_code, -534 },
        { "_LVOProcure", libvec_code, -540 },
        { "_LVOVacate", libvec_code, -546 },
        { "_LVOOpenLibrary", libvec_code, _LVOOpenLibrary },
        { "_LVOInitSemaphore", libvec_code, -558 },
        { "_LVOObtainSemaphore", libvec_code, -564 },
        { "_LVOReleaseSemaphore", libvec_code, -570 },
        { "_LVOAttemptSemaphore", libvec_code, -576 },
        { "_LVOObtainSemaphoreList", libvec_code, -582 },
        { "_LVOReleaseSemaphoreList", libvec_code, -588 },
        { "_LVOFindSemaphore", libvec_code, -594 },
        { "_LVOAddSemaphore", libvec_code, -600 },
        { "_LVORemSemaphore", libvec_code, -606 },
        { "_LVOSumKickData", libvec_code, -612 },
        { "_LVOAddMemList", libvec_code, -618 },
        { "_LVOCopyMem", libvec_code, -624 },
        { "_LVOCopyMemQuick", libvec_code, -630 },
        { "_LVOCacheClearU", libvec_code, -636 },
        { "_LVOCacheClearE", libvec_code, -642 },
        { "_LVOCacheControl", libvec_code, -648 },
        { "_LVOCreateIORequest", libvec_code, -654 },
        { "_LVODeleteIORequest", libvec_code, -660 },
        { "_LVOCreateMsgPort", libvec_code, -666 },
        { "_LVODeleteMsgPort", libvec_code, -672 },
        { "_LVOObtainSemaphoreShared", libvec_code, -678 },
        { "_LVOAllocVec", libvec_code, -684 },
        { "_LVOFreeVec", libvec_code, -690 },
        { "_LVOCreatePrivatePool", libvec_code, -696 },
        { "_LVODeletePrivatePool", libvec_code, -702 },
        { "_LVOAllocPooled", libvec_code, -708 },
        { "_LVOFreePooled", libvec_code, -714 },
        { "_LVOAttemptSemaphoreShared", libvec_code, -720 },
        { "_LVOColdReboot", libvec_code, -726 },
        { "_LVOStackSwap", libvec_code, -732 },
        { "_LVOChildFree", libvec_code, -738 },
        { "_LVOChildOrphan", libvec_code, -744 },
        { "_LVOChildStatus", libvec_code, -750 },
        { "_LVOChildWait", libvec_code, -756 },
        { "_LVOCachePreDMA", libvec_code, -762 },
        { "_LVOCachePostDMA", libvec_code, -768 },
        { "_LVOAddMemHandler", libvec_code, -774 },
        { "_LVORemMemHandler", libvec_code, -780 },
    }
};

// gfxbase.h

// TODO:
#define TEMP_STRUCT(n) const structure_definition n { #n, {} }
TEMP_STRUCT(bltnode);
TEMP_STRUCT(TextFont);
TEMP_STRUCT(SimpleSprite);
TEMP_STRUCT(MonitorSpec);
TEMP_STRUCT(copinit);
TEMP_STRUCT(cprlist);
TEMP_STRUCT(ColorMap);
TEMP_STRUCT(CopList);
TEMP_STRUCT(UCopList);
TEMP_STRUCT(VSprite);
TEMP_STRUCT(Layer);
TEMP_STRUCT(collTable);

const structure_definition BitMap
{
    "BitMap",
    {
        { "BytesPerRow", word_type },
        { "Rows", word_type },
        { "Flags", byte_type },
        { "Depth", byte_type },
        { "pad", word_type },
        { "Planes", make_array_type(byte_ptr, 8) },
    }
};

const structure_definition RasInfo
{
    "RasInfo",
    {
        { "Next", make_pointer_type(make_struct_type(RasInfo)) },
        { "BitMap", make_pointer_type(make_struct_type(BitMap)) },
        { "RxOffset", word_type },
        { "RyOffset", word_type },
}
};

const structure_definition ViewPort {
    "ViewPort",
    {
        { "Next", make_pointer_type(make_struct_type(ViewPort)) },
        { "ColorMap", make_pointer_type(make_struct_type(ColorMap)) },
        { "DspIns", make_pointer_type(make_struct_type(CopList)) },
        { "SprIns", make_pointer_type(make_struct_type(CopList)) },
        { "ClrIns", make_pointer_type(make_struct_type(CopList)) },
        { "UCopIns", make_pointer_type(make_struct_type(UCopList)) },
        { "DWidth", word_type },
        { "DHeight", word_type },
        { "DxOffset", word_type },
        { "DyOffset", word_type },
        { "Modes", word_type },
        { "SpritePriorities", byte_type },
        { "ExtendedModes", byte_type },
        { "RasInfo", make_pointer_type(make_struct_type(RasInfo)) },
    }
};

const structure_definition AnalogSignalInterval {
    "AnalogSignalInterval",
    {
        { "asi_Start", word_type },
        { "asi_Stop", word_type },
    }
};

const structure_definition View {
    "View",
    {
        { "ViewPort", make_pointer_type(make_struct_type(ViewPort)) },
        { "LOFCprList", make_pointer_type(make_struct_type(cprlist)) },
        { "SHFCprList", make_pointer_type(make_struct_type(cprlist)) },
        { "DyOffset", word_type },
        { "DxOffset", word_type },
        { "Modes", word_type },
    }
};

const structure_definition AreaInfo {
    "AreaInfo",
    {
        { "VctrTbl", word_ptr },
        { "VctrPtr", word_ptr },
        { "FlagTbl", byte_ptr },
        { "FlagPtr", byte_ptr },
        { "Count", word_type },
        { "MaxCount", word_type },
        { "FirstX", word_type },
        { "FirstY", word_type },
    }
};
const structure_definition TmpRas {
    "TmpRas",
    {
        { "RasPtr", byte_ptr },
        { "Size", long_type },
    }
};

const structure_definition GelsInfo {
    "GelsInfo",
    {
        { "sprRsrvd", byte_type },
        { "Flags", byte_type },
        { "gelHead", make_pointer_type(make_struct_type(VSprite)) },
        { "gelTail", make_pointer_type(make_struct_type(VSprite)) },
        { "nextLine", word_ptr },
        { "lastColor", make_pointer_type(word_ptr) },
        { "collHandler", make_pointer_type(make_struct_type(collTable)) },
        { "leftmost", word_type },
        { "rightmost", word_type },
        { "topmost", word_type },
        { "bottommost", word_type },
        { "firstBlissObj", unknown_ptr },
        { "lastBlissObj", unknown_ptr },
    }
};
const structure_definition RastPort {
    "RastPort",
    {
        { "Layer", make_pointer_type(make_struct_type(Layer)) },
        { "BitMap", make_pointer_type(make_struct_type(BitMap)) },
        { "AreaPtrn", word_ptr },
        { "TmpRas", make_pointer_type(make_struct_type(TmpRas)) },
        { "AreaInfo", make_pointer_type(make_struct_type(AreaInfo)) },
        { "GelsInfo", make_pointer_type(make_struct_type(GelsInfo)) },
        { "Mask", byte_type },
        { "FgPen", byte_type },
        { "BgPen", byte_type },
        { "AOlPen", byte_type },
        { "DrawMode", byte_type },
        { "AreaPtSz", byte_type },
        { "linpatcnt", byte_type },
        { "dummy", byte_type },
        { "Flags", word_type },
        { "LinePtrn", word_type },
        { "cp_x", word_type },
        { "cp_y", word_type },
        { "minterms", make_array_type(byte_type, 8) },
        { "PenWidth", word_type },
        { "PenHeight", word_type },
        { "Font", make_pointer_type(make_struct_type(TextFont)) },
        { "AlgoStyle", byte_type },
        { "TxFlags", byte_type },
        { "TxHeight", word_type },
        { "TxWidth", word_type },
        { "TxBaseline", word_type },
        { "TxSpacing", word_type },
        { "RP_User", make_pointer_type(unknown_ptr) },
        { "longreserved", make_array_type(long_type, 2) },
        { "wordreserved", make_array_type(word_type, 7) },
        { "reserved", make_array_type(byte_type, 8) },
    }
};

constexpr int32_t _LVOWaitBlit = -228;

const structure_definition GfxBase {
    "GfxBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "ActiView", make_pointer_type(make_struct_type(View)) },
        { "copinit", make_pointer_type(make_struct_type(copinit)) },
        { "cia", long_ptr },
        { "blitter", long_ptr },
        { "LOFlist", word_ptr },
        { "SHFlist", word_ptr },
        { "blthd", make_pointer_type(make_struct_type(bltnode)) },
        { "blttl", make_pointer_type(make_struct_type(bltnode)) },
        { "bsblthd", make_pointer_type(make_struct_type(bltnode)) },
        { "bsblttl", make_pointer_type(make_struct_type(bltnode)) },
        { "vbsrv", make_struct_type(Interrupt) },
        { "timsrv", make_struct_type(Interrupt) },
        { "bltsrv", make_struct_type(Interrupt) },
        { "TextFonts", make_struct_type(List) },
        { "DefaultFont", make_pointer_type(make_struct_type(TextFont)) },
        { "Modes", word_type },
        { "VBlank", byte_type },
        { "Debug", byte_type },
        { "BeamSync", word_type },
        { "system_bplcon0", word_type },
        { "SpriteReserved", byte_type },
        { "bytereserved", byte_type },
        { "Flags", word_type },
        { "BlitLock", word_type },
        { "BlitNest", word_type },
        { "BlitWaitQ", make_struct_type(List) },
        { "BlitOwner", make_pointer_type(make_struct_type(Task)) },
        { "TOF_WaitQ", make_struct_type(List) },
        { "DisplayFlags", word_type },
        { "SimpleSprites", make_pointer_type(make_pointer_type(make_struct_type(SimpleSprite))) },
        { "MaxDisplayRow", word_type },
        { "MaxDisplayColumn", word_type },
        { "NormalDisplayRows", word_type },
        { "NormalDisplayColumns", word_type },
        { "NormalDPMX", word_type },
        { "NormalDPMY", word_type },
        { "LastChanceMemory", make_pointer_type(make_struct_type(SignalSemaphore)) },
        { "LCMptr", word_ptr },
        { "MicrosPerLine", word_type },
        { "MinDisplayColumn", word_type },
        { "ChipRevBits0", byte_type },
        { "MemType", byte_type },
        { "crb_reserved", make_array_type(byte_type, 4) },
        { "monitor_id", word_type },
        { "hedley", make_array_type(long_type, 8) },
        { "hedley_sprites", make_array_type(long_type, 8) },
        { "hedley_sprites1", make_array_type(long_type, 8) },
        { "hedley_count", word_type },
        { "hedley_flags", word_type },
        { "hedley_tmp", word_type },
        { "hash_table", long_ptr },
        { "current_tot_rows", word_type },
        { "current_tot_cclks", word_type },
        { "hedley_hint", byte_type },
        { "hedley_hint2", byte_type },
        { "nreserved", make_array_type(long_type, 4) },
        { "a2024_sync_raster", long_ptr },
        { "control_delta_pal", word_type },
        { "control_delta_ntsc", word_type },
        { "current_monitor", make_pointer_type(make_struct_type(MonitorSpec)) },
        { "MonitorList", make_struct_type(List) },
        { "default_monitor", make_pointer_type(make_struct_type(MonitorSpec)) },
        { "MonitorListSemaphore", make_pointer_type(make_struct_type(SignalSemaphore)) },
        { "DisplayInfoDataBase", unknown_ptr },
        { "TopLine", word_type },
        { "ActiViewCprSemaphore", make_pointer_type(make_struct_type(SignalSemaphore)) },
        { "UtilBase", long_ptr },
        { "ExecBase", long_ptr },
        { "bwshifts", byte_ptr },
        { "StrtFetchMasks", word_ptr },
        { "StopFetchMasks", word_ptr },
        { "Overrun", word_ptr },
        { "RealStops", word_ptr },
        { "SpriteWidth", word_type },
        { "SpriteFMode", word_type },
        { "SoftSprites", byte_type },
        { "arraywidth", byte_type },
        { "DefaultSpriteWidth", word_type },
        { "SprMoveDisable", byte_type },
        { "WantChips", byte_type },
        { "BoardMemType", byte_type },
        { "Bugs", byte_type },
        { "gb_LayersBase", long_ptr },
        { "ColorMask", long_type },
        { "IVector", unknown_ptr },
        { "IData", unknown_ptr },
        { "SpecialCounter", long_type },
        { "DBList", unknown_ptr },
        { "MonitorFlags", word_type },
        { "ScanDoubledSprites", byte_type },
        { "BP3Bits", byte_type },
        { "MonitorVBlank", make_struct_type(AnalogSignalInterval) },
        { "natural_monitor", make_pointer_type(make_struct_type(MonitorSpec)) },
        { "ProgData", unknown_ptr },
        { "ExtSprites", byte_type },
        { "pad3", byte_type },
        { "GfxFlags", word_type },
        { "VBCounter", long_type },
        { "HashTableSemaphore", make_pointer_type(make_struct_type(SignalSemaphore)) },
        { "HWEmul", make_array_type(long_ptr, 9) },

        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },

        { "_LVOBltBitMap", libvec_code, -30 },
        { "_LVOBltTemplate", libvec_code, -36 },
        { "_LVOClearEOL", libvec_code, -42 },
        { "_LVOClearScreen", libvec_code, -48 },
        { "_LVOTextLength", libvec_code, -54 },
        { "_LVOText", libvec_code, -60 },
        { "_LVOSetFont", libvec_code, -66 },
        { "_LVOOpenFont", libvec_code, -72 },
        { "_LVOCloseFont", libvec_code, -78 },
        { "_LVOAskSoftStyle", libvec_code, -84 },
        { "_LVOSetSoftStyle", libvec_code, -90 },
        { "_LVOAddBob", libvec_code, -96 },
        { "_LVOAddVSprite", libvec_code, -102 },
        { "_LVODoCollision", libvec_code, -108 },
        { "_LVODrawGList", libvec_code, -114 },
        { "_LVOInitGels", libvec_code, -120 },
        { "_LVOInitMasks", libvec_code, -126 },
        { "_LVORemIBob", libvec_code, -132 },
        { "_LVORemVSprite", libvec_code, -138 },
        { "_LVOSetCollision", libvec_code, -144 },
        { "_LVOSortGList", libvec_code, -150 },
        { "_LVOAddAnimOb", libvec_code, -156 },
        { "_LVOAnimate", libvec_code, -162 },
        { "_LVOGetGBuffers", libvec_code, -168 },
        { "_LVOInitGMasks", libvec_code, -174 },
        { "_LVODrawEllipse", libvec_code, -180 },
        { "_LVOAreaEllipse", libvec_code, -186 },
        { "_LVOLoadRGB4", libvec_code, -192 },
        { "_LVOInitRastPort", libvec_code, -198 },
        { "_LVOInitVPort", libvec_code, -204 },
        { "_LVOMrgCop", libvec_code, -210 },
        { "_LVOMakeVPort", libvec_code, -216 },
        { "_LVOLoadView", libvec_code, -222 },
        { "_LVOWaitBlit", libvec_code, _LVOWaitBlit },
        { "_LVOSetRast", libvec_code, -234 },
        { "_LVOMove", libvec_code, -240 },
        { "_LVODraw", libvec_code, -246 },
        { "_LVOAreaMove", libvec_code, -252 },
        { "_LVOAreaDraw", libvec_code, -258 },
        { "_LVOAreaEnd", libvec_code, -264 },
        { "_LVOWaitTOF", libvec_code, -270 },
        { "_LVOQBlit", libvec_code, -276 },
        { "_LVOInitArea", libvec_code, -282 },
        { "_LVOSetRGB4", libvec_code, -288 },
        { "_LVOQBSBlit", libvec_code, -294 },
        { "_LVOBltClear", libvec_code, -300 },
        { "_LVORectFill", libvec_code, -306 },
        { "_LVOBltPattern", libvec_code, -312 },
        { "_LVOReadPixel", libvec_code, -318 },
        { "_LVOWritePixel", libvec_code, -324 },
        { "_LVOFlood", libvec_code, -330 },
        { "_LVOPolyDraw", libvec_code, -336 },
        { "_LVOSetAPen", libvec_code, -342 },
        { "_LVOSetBPen", libvec_code, -348 },
        { "_LVOSetDrMd", libvec_code, -354 },
        { "_LVOInitView", libvec_code, -360 },
        { "_LVOCBump", libvec_code, -366 },
        { "_LVOCMove", libvec_code, -372 },
        { "_LVOCWait", libvec_code, -378 },
        { "_LVOVBeamPos", libvec_code, -384 },
        { "_LVOInitBitMap", libvec_code, -390 },
        { "_LVOScrollRaster", libvec_code, -396 },
        { "_LVOWaitBOVP", libvec_code, -402 },
        { "_LVOGetSprite", libvec_code, -408 },
        { "_LVOFreeSprite", libvec_code, -414 },
        { "_LVOChangeSprite", libvec_code, -420 },
        { "_LVOMoveSprite", libvec_code, -426 },
        { "_LVOLockLayerRom", libvec_code, -432 },
        { "_LVOUnlockLayerRom", libvec_code, -438 },
        { "_LVOSyncSBitMap", libvec_code, -444 },
        { "_LVOCopySBitMap", libvec_code, -450 },
        { "_LVOOwnBlitter", libvec_code, -456 },
        { "_LVODisownBlitter", libvec_code, -462 },
        { "_LVOInitTmpRas", libvec_code, -468 },
        { "_LVOAskFont", libvec_code, -474 },
        { "_LVOAddFont", libvec_code, -480 },
        { "_LVORemFont", libvec_code, -486 },
        { "_LVOAllocRaster", libvec_code, -492 },
        { "_LVOFreeRaster", libvec_code, -498 },
        { "_LVOAndRectRegion", libvec_code, -504 },
        { "_LVOOrRectRegion", libvec_code, -510 },
        { "_LVONewRegion", libvec_code, -516 },
        { "_LVOClearRectRegion", libvec_code, -522 },
        { "_LVOClearRegion", libvec_code, -528 },
        { "_LVODisposeRegion", libvec_code, -534 },
        { "_LVOFreeVPortCopLists", libvec_code, -540 },
        { "_LVOFreeCopList", libvec_code, -546 },
        { "_LVOClipBlit", libvec_code, -552 },
        { "_LVOXorRectRegion", libvec_code, -558 },
        { "_LVOFreeCprList", libvec_code, -564 },
        { "_LVOGetColorMap", libvec_code, -570 },
        { "_LVOFreeColorMap", libvec_code, -576 },
        { "_LVOGetRGB4", libvec_code, -582 },
        { "_LVOScrollVPort", libvec_code, -588 },
        { "_LVOUCopperListInit", libvec_code, -594 },
        { "_LVOFreeGBuffers", libvec_code, -600 },
        { "_LVOBltBitMapRastPort", libvec_code, -606 },
        { "_LVOOrRegionRegion", libvec_code, -612 },
        { "_LVOXorRegionRegion", libvec_code, -618 },
        { "_LVOAndRegionRegion", libvec_code, -624 },
        { "_LVOSetRGB4CM", libvec_code, -630 },
        { "_LVOBltMaskBitMapRastPort", libvec_code, -636 },
        { "_LVOGraphicsReserved1", libvec_code, -642 },
        { "_LVOGraphicsReserved2", libvec_code, -648 },
        { "_LVOAttemptLockLayerRom", libvec_code, -654 },
        { "_LVOGfxNew", libvec_code, -660 },
        { "_LVOGfxFree", libvec_code, -666 },
        { "_LVOGfxAssociate", libvec_code, -672 },
        { "_LVOBitMapScale", libvec_code, -678 },
        { "_LVOScaleDiv", libvec_code, -684 },
        { "_LVOTextExtent", libvec_code, -690 },
        { "_LVOTextFit", libvec_code, -696 },
        { "_LVOGfxLookUp", libvec_code, -702 },
        { "_LVOVideoControl", libvec_code, -708 },
        { "_LVOOpenMonitor", libvec_code, -714 },
        { "_LVOCloseMonitor", libvec_code, -720 },
        { "_LVOFindDisplayInfo", libvec_code, -726 },
        { "_LVONextDisplayInfo", libvec_code, -732 },
        { "_LVOAddDisplayInfo", libvec_code, -738 },
        { "_LVOAddDisplayInfoData", libvec_code, -744 },
        { "_LVOSetDisplayInfoData", libvec_code, -750 },
        { "_LVOGetDisplayInfoData", libvec_code, -756 },
        { "_LVOFontExtent", libvec_code, -762 },
        { "_LVOReadPixelLine8", libvec_code, -768 },
        { "_LVOWritePixelLine8", libvec_code, -774 },
        { "_LVOReadPixelArray8", libvec_code, -780 },
        { "_LVOWritePixelArray8", libvec_code, -786 },
        { "_LVOGetVPModeID", libvec_code, -792 },
        { "_LVOModeNotAvailable", libvec_code, -798 },
        { "_LVOWeighTAMatch", libvec_code, -804 },
        { "_LVOEraseRect", libvec_code, -810 },
        { "_LVOExtendFont", libvec_code, -816 },
        { "_LVOStripFont", libvec_code, -822 },
        { "_LVOCalcIVG", libvec_code, -828 },
        { "_LVOAttachPalExtra", libvec_code, -834 },
        { "_LVOObtainBestPenA", libvec_code, -840 },
        { "_LVOGfxInternal3", libvec_code, -846 },
        { "_LVOSetRGB32", libvec_code, -852 },
        { "_LVOGetAPen", libvec_code, -858 },
        { "_LVOGetBPen", libvec_code, -864 },
        { "_LVOGetDrMd", libvec_code, -870 },
        { "_LVOGetOutlinePen", libvec_code, -876 },
        { "_LVOLoadRGB32", libvec_code, -882 },
        { "_LVOSetChipRev", libvec_code, -888 },
        { "_LVOSetABPenDrMd", libvec_code, -894 },
        { "_LVOGetRGB32", libvec_code, -900 },
        { "_LVOGfxSpare1", libvec_code, -906 },
        { "_LVOAllocBitMap", libvec_code, -918 },
        { "_LVOFreeBitMap", libvec_code, -924 },
        { "_LVOGetExtSpriteA", libvec_code, -930 },
        { "_LVOCoerceMode", libvec_code, -936 },
        { "_LVOChangeVPBitMap", libvec_code, -942 },
        { "_LVOReleasePen", libvec_code, -948 },
        { "_LVOObtainPen", libvec_code, -954 },
        { "_LVOGetBitMapAttr", libvec_code, -960 },
        { "_LVOAllocDBufInfo", libvec_code, -966 },
        { "_LVOFreeDBufInfo", libvec_code, -972 },
        { "_LVOSetOutlinePen", libvec_code, -978 },
        { "_LVOSetWriteMask", libvec_code, -984 },
        { "_LVOSetMaxPen", libvec_code, -990 },
        { "_LVOSetRGB32CM", libvec_code, -996 },
        { "_LVOScrollRasterBF", libvec_code, -1002 },
        { "_LVOFindColor", libvec_code, -1008 },
        { "_LVOGfxSpare2", libvec_code, -1014 },
        { "_LVOAllocSpriteDataA", libvec_code, -1020 },
        { "_LVOChangeExtSpriteA", libvec_code, -1026 },
        { "_LVOFreeSpriteData", libvec_code, -1032 },
        { "_LVOSetRPAttrsA", libvec_code, -1038 },
        { "_LVOGetRPAttrsA", libvec_code, -1044 },
        { "_LVOBestModeIDA", libvec_code, -1050 },
    }
};

// Dos
TEMP_STRUCT(RootNode);
TEMP_STRUCT(ErrorString);


const structure_definition timeval {
    "timeval",
    {
        { "tv_secs", long_type },
        { "tv_micro", long_type },
    }
};
const structure_definition timerequest {
    "timerequest",
    { 
        { "tr_node", make_struct_type(IORequest) },
        { "tr_time", make_struct_type(timeval) },
    }
};

constexpr int32_t _LVOOpen = -30;
constexpr int32_t _LVOWrite = -48;

const structure_definition DosBase {
    "DosBase",
    {
        { "dl_lib", make_struct_type(Library) },
        { "dl_Root", make_pointer_type(make_struct_type(RootNode)) },
        { "dl_GV", unknown_ptr },
        { "dl_A2", long_type },
        { "dl_A5", long_type },
        { "dl_A6", long_type },
        { "dl_Errors", make_pointer_type(make_struct_type(ErrorString)) },
        { "dl_TimeReq", make_pointer_type(make_struct_type(timerequest)) },
        { "dl_UtilityBase", make_pointer_type(make_struct_type(Library)) },
        { "dl_IntuitionBase", make_pointer_type(make_struct_type(Library)) },

        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },

        { "_LVOOpen", libvec_code, _LVOOpen },
        { "_LVOClose", libvec_code, -36 },
        { "_LVORead", libvec_code, -42 },
        { "_LVOWrite", libvec_code, _LVOWrite },
        { "_LVOInput", libvec_code, -54 },
        { "_LVOOutput", libvec_code, -60 },
        { "_LVOSeek", libvec_code, -66 },
        { "_LVODeleteFile", libvec_code, -72 },
        { "_LVORename", libvec_code, -78 },
        { "_LVOLock", libvec_code, -84 },
        { "_LVOUnLock", libvec_code, -90 },
        { "_LVODupLock", libvec_code, -96 },
        { "_LVOExamine", libvec_code, -102 },
        { "_LVOExNext", libvec_code, -108 },
        { "_LVOInfo", libvec_code, -114 },
        { "_LVOCreateDir", libvec_code, -120 },
        { "_LVOCurrentDir", libvec_code, -126 },
        { "_LVOIoErr", libvec_code, -132 },
        { "_LVOCreateProc", libvec_code, -138 },
        { "_LVOExit", libvec_code, -144 },
        { "_LVOLoadSeg", libvec_code, -150 },
        { "_LVOUnLoadSeg", libvec_code, -156 },
        { "_LVOGetPacket", libvec_code, -162 },
        { "_LVOQueuePacket", libvec_code, -168 },
        { "_LVODeviceProc", libvec_code, -174 },
        { "_LVOSetComment", libvec_code, -180 },
        { "_LVOSetProtection", libvec_code, -186 },
        { "_LVODateStamp", libvec_code, -192 },
        { "_LVODelay", libvec_code, -198 },
        { "_LVOWaitForChar", libvec_code, -204 },
        { "_LVOParentDir", libvec_code, -210 },
        { "_LVOIsInteractive", libvec_code, -216 },
        { "_LVOExecute", libvec_code, -222 },
        { "_LVOAllocDosObject", libvec_code, -228 },
        { "_LVOFreeDosObject", libvec_code, -234 },
        { "_LVODoPkt", libvec_code, -240 },
        { "_LVOSendPkt", libvec_code, -246 },
        { "_LVOWaitPkt", libvec_code, -252 },
        { "_LVOReplyPkt", libvec_code, -258 },
        { "_LVOAbortPkt", libvec_code, -264 },
        { "_LVOLockRecord", libvec_code, -270 },
        { "_LVOLockRecords", libvec_code, -276 },
        { "_LVOUnLockRecord", libvec_code, -282 },
        { "_LVOUnLockRecords", libvec_code, -288 },
        { "_LVOSelectInput", libvec_code, -294 },
        { "_LVOSelectOutput", libvec_code, -300 },
        { "_LVOFGetC", libvec_code, -306 },
        { "_LVOFPutC", libvec_code, -312 },
        { "_LVOUnGetC", libvec_code, -318 },
        { "_LVOFRead", libvec_code, -324 },
        { "_LVOFWrite", libvec_code, -330 },
        { "_LVOFGets", libvec_code, -336 },
        { "_LVOFPuts", libvec_code, -342 },
        { "_LVOVFWritef", libvec_code, -348 },
        { "_LVOVFPrintf", libvec_code, -354 },
        { "_LVOFlush", libvec_code, -360 },
        { "_LVOSetVBuf", libvec_code, -366 },
        { "_LVODupLockFromFH", libvec_code, -372 },
        { "_LVOOpenFromLock", libvec_code, -378 },
        { "_LVOParentOffH", libvec_code, -384 },
        { "_LVOExamineFH", libvec_code, -390 },
        { "_LVOSetFileDate", libvec_code, -396 },
        { "_LVONameFromLock", libvec_code, -402 },
        { "_LVONameFromFH", libvec_code, -408 },
        { "_LVOSplitName", libvec_code, -414 },
        { "_LVOSameLock", libvec_code, -420 },
        { "_LVOSetMode", libvec_code, -426 },
        { "_LVOExAll", libvec_code, -432 },
        { "_LVOReadLink", libvec_code, -438 },
        { "_LVOMakeLink", libvec_code, -444 },
        { "_LVOChangeMode", libvec_code, -450 },
        { "_LVOSetFileSize", libvec_code, -456 },
        { "_LVOSetIoErr", libvec_code, -462 },
        { "_LVOFault", libvec_code, -468 },
        { "_LVOPrintFault", libvec_code, -474 },
        { "_LVOErrorReport", libvec_code, -480 },
        { "_LVOCli", libvec_code, -492 },
        { "_LVOCreateNewProc", libvec_code, -498 },
        { "_LVORunCommand", libvec_code, -504 },
        { "_LVOGetConsoleTask", libvec_code, -510 },
        { "_LVOSetConsoleTask", libvec_code, -516 },
        { "_LVOGetFileSysTask", libvec_code, -522 },
        { "_LVOSetFileSysTask", libvec_code, -528 },
        { "_LVOGetArgStr", libvec_code, -534 },
        { "_LVOSetArgStr", libvec_code, -540 },
        { "_LVOFindCliProc", libvec_code, -546 },
        { "_LVOMaxCli", libvec_code, -552 },
        { "_LVOSetCurrentDirName", libvec_code, -558 },
        { "_LVOGetCurrentDirName", libvec_code, -564 },
        { "_LVOSetProgramName", libvec_code, -570 },
        { "_LVOGetProgramName", libvec_code, -576 },
        { "_LVOSetPrompt", libvec_code, -582 },
        { "_LVOGetPrompt", libvec_code, -588 },
        { "_LVOSetProgramDir", libvec_code, -594 },
        { "_LVOGetProgramDir", libvec_code, -600 },
        { "_LVOSystemTagList", libvec_code, -606 },
        { "_LVOAssignLock", libvec_code, -612 },
        { "_LVOAssignLate", libvec_code, -618 },
        { "_LVOAssignPath", libvec_code, -624 },
        { "_LVOAssignAdd", libvec_code, -630 },
        { "_LVORemAssignList", libvec_code, -636 },
        { "_LVOGetDeviceProc", libvec_code, -642 },
        { "_LVOFreeDeviceProc", libvec_code, -648 },
        { "_LVOLockDosList", libvec_code, -654 },
        { "_LVOUnLockDosList", libvec_code, -660 },
        { "_LVOAttemptLockDosList", libvec_code, -666 },
        { "_LVORemDosEntry", libvec_code, -672 },
        { "_LVOAddDosEntry", libvec_code, -678 },
        { "_LVOFindDosEntry", libvec_code, -684 },
        { "_LVONextDosEntry", libvec_code, -690 },
        { "_LVOMakeDosEntry", libvec_code, -696 },
        { "_LVOFreeDosEntry", libvec_code, -702 },
        { "_LVOIsFileSystem", libvec_code, -708 },
        { "_LVOFormat", libvec_code, -714 },
        { "_LVORelabel", libvec_code, -720 },
        { "_LVOInhibit", libvec_code, -726 },
        { "_LVOAddBuffers", libvec_code, -732 },
        { "_LVOCompareDates", libvec_code, -738 },
        { "_LVODateToStr", libvec_code, -744 },
        { "_LVOStrToDate", libvec_code, -750 },
        { "_LVOInternalLoadSeg", libvec_code, -756 },
        { "_LVOInternalUnLoadSeg", libvec_code, -762 },
        { "_LVONewLoadSeg", libvec_code, -768 },
        { "_LVOAddSegment", libvec_code, -774 },
        { "_LVOFindSegment", libvec_code, -780 },
        { "_LVORemSegment", libvec_code, -786 },
        { "_LVOCheckSignal", libvec_code, -792 },
        { "_LVOReadArgs", libvec_code, -798 },
        { "_LVOFindArg", libvec_code, -804 },
        { "_LVOReadItem", libvec_code, -810 },
        { "_LVOStrToLong", libvec_code, -816 },
        { "_LVOMatchFirst", libvec_code, -822 },
        { "_LVOMatchNext", libvec_code, -828 },
        { "_LVOMatchEnd", libvec_code, -834 },
        { "_LVOParsePattern", libvec_code, -840 },
        { "_LVOMatchPattern", libvec_code, -846 },
        { "_LVODosNameFromAnchor", libvec_code, -852 },
        { "_LVOFreeArgs", libvec_code, -858 },
        { "_LVOFilePart", libvec_code, -870 },
        { "_LVOPathPart", libvec_code, -876 },
        { "_LVOAddPart", libvec_code, -882 },
        { "_LVOStartNotify", libvec_code, -888 },
        { "_LVOEndNotify", libvec_code, -894 },
        { "_LVOSetVar", libvec_code, -900 },
        { "_LVOGetVar", libvec_code, -906 },
        { "_LVODeleteVar", libvec_code, -912 },
        { "_LVOFindVar", libvec_code, -918 },
        { "_LVOCliInit", libvec_code, -924 },
        { "_LVOCliInitNewCli", libvec_code, -930 },
        { "_LVOCliInitRun", libvec_code, -936 },
        { "_LVOWriteChars", libvec_code, -942 },
        { "_LVOPutStr", libvec_code, -948 },
        { "_LVOVPrintf", libvec_code, -954 },
        { "_LVOParsePatternNoCase", libvec_code, -966 },
        { "_LVOMatchPatternNoCase", libvec_code, -972 },
        { "_LVODosGetString", libvec_code, -978 },
        { "_LVOSameDevice", libvec_code, -984 },
        { "_LVOExAllEnd", libvec_code, -990 },
        { "_LVOSetOwner", libvec_code, -996 },
    }
};

const structure_definition CommandLineInterface {
    "CommandLineInterface",
    {
        { "cli_Result2", long_type },
        { "cli_SetName", bstr_type },
        { "cli_CommandDir", bptr_type },
        { "cli_ReturnCode", long_type },
        { "cli_CommandName", bstr_type },
        { "cli_FailLevel", long_type },
        { "cli_Prompt", bstr_type },
        { "cli_StandardInput", bptr_type },
        { "cli_CurrentInput", bptr_type },
        { "cli_CommandFile", bstr_type },
        { "cli_Interactive", long_type },
        { "cli_Background", long_type },
        { "cli_CurrentOutput", bptr_type },
        { "cli_DefaultStack", long_type },
        { "cli_StandardOutput", bptr_type },
        { "cli_Module", bptr_type },
    }
};

const structure_definition Process {
    "Process",
    {
        { "pr_Task", make_struct_type(Task) },
        { "pr_MsgPort", make_struct_type(MsgPort) },
        { "pr_Pad", word_type },
        { "pr_SegList", bptr_type },
        { "pr_StackSize", long_type },
        { "pr_GlobVec", unknown_ptr },
        { "pr_TaskNum", long_type },
        { "pr_StackBase", bptr_type },
        { "pr_Result2", long_type },
        { "pr_CurrentDir", bptr_type },
        { "pr_CIS", bptr_type },
        { "pr_COS", bptr_type },
        { "pr_ConsoleTask", unknown_ptr },
        { "pr_FileSystemTask", unknown_ptr },
        { "pr_CLI", make_bpointer_type(make_struct_type(CommandLineInterface)) },
        { "pr_ReturnAddr", unknown_ptr },
        { "pr_PktWait", unknown_ptr },
        { "pr_WindowPtr", unknown_ptr },
        #if 0 // New in 2.0
        { "pr_HomeDir", bptr_type },
        { "pr_Flags", long_type },
        { "pr_ExitCode", code_ptr }, // void (*pr_ExitCode)()
        { "pr_ExitData", long_type },
        { "pr_Arguments", byte_ptr },
        { "pr_LocalVars", make_struct_type(MinList) },
        { "pr_ShellPrivate", long_type },
        { "pr_CES", bptr_type },
        #endif
    }
};

// Global vector (info from http://aminet.net/package/dev/misc/BCPL)
const structure_definition DosGlobalVec
{
    "DosGlobalVec",
    {
        { "SYSREQUEST", long_type, -0x84 },
        { "TOCSTR", long_type, -0x80 },
        { "TOBSTR", long_type, -0x7c },
        { "EXECUTEC", long_type, -0x6c },
        { "ISINTERACT", long_type, -0x68 },
        { "DATESTAMPC", long_type, -0x64 },
        { "SETPROTECT", long_type, -0x60 },
        { "SETCOMMENT", long_type, -0x5c },
        { "DEVICEPROCC", long_type, -0x58 },
        { "QUEUEPKT", long_type, -0x54 },
        { "CLEARMEM", long_type, -0x50 },
        { "LOADSEG", long_type, -0x4c },
        { "CREATEPROCC", long_type, -0x48 },
        { "IOERR", long_type, -0x44 },
        { "CURRENTDIR", long_type, -0x40 },
        { "CREATEDIRC", long_type, -0x3c },
        { "INFO", long_type, -0x38 },
        { "EXNEXT", long_type, -0x34 },
        { "EXAMINE", long_type, -0x30 },
        { "LOCKC", long_type, -0x2c },
        { "RENAMEC", long_type, -0x28 },
        { "DELETEFILE", long_type, -0x24 },
        { "SEEK", long_type, -0x20 },
        { "WRITE", long_type, -0x18 },
        { "READ", long_type, -0xc },
        { "STRNCPY", long_type, -0x8 },
        { "OPEN", long_type, -0x4 },
        { "VECSIZE", long_type, 0x0 },
        { "START", long_type, 0x4 },
        { "EXIT", long_type, 0x8 },
        { "MULTIPLY", long_type, 0xc },
        { "DIVIDE", long_type, 0x10 },
        { "MOD", long_type, 0x14 },
        { "SETIO", long_type, 0x18 },
        { "PACKLW", long_type, 0x20 },
        { "UNPACKLW", long_type, 0x24 },
        { "SETRES2", long_type, 0x28 },
        { "MAKEGV", long_type, 0x34 },
        { "FINDTASK", long_type, 0x38 },
        { "GETBYTE", long_type, 0x3c },
        { "PUTBYTE", long_type, 0x40 },
        { "LEVEL", long_type, 0x44 },
        { "LONGJUMP1", long_type, 0x48 },
        { "ALLOCMEM", long_type, 0x4c },
        { "LONGJUMP2", long_type, 0x50 },
        { "DOIO", long_type, 0x54 },
        { "SENDIO", long_type, 0x58 },
        { "CREATECO", long_type, 0x5c },
        { "DELETECO", long_type, 0x60 },
        { "CALLCO", long_type, 0x64 },
        { "COWAIT", long_type, 0x68 },
        { "RESUMECO", long_type, 0x6c },
        { "INSTALLSEG", long_type, 0x70 },
        { "GETVEC1", long_type, 0x74 },
        { "FREEVEC", long_type, 0x78 },
        { "OPENDEV", long_type, 0x7c },
        { "CLOSEDEV", long_type, 0x80 },
        { "CREATEPROCB", long_type, 0x84 },
        { "REMPROCESS", long_type, 0x88 },
        { "PARENTDIR", long_type, 0x8c },
        { "SETSIGS", long_type, 0x90 },
        { "CLEARSIGS", long_type, 0x94 },
        { "DOSALERT", long_type, 0x98 },
        { "FINDROOT", long_type, 0x9c },
        { "READINC", long_type, 0xa0 },
        { "TASKWAIT1", long_type, 0xa4 },
        { "PUTPKT", long_type, 0xa8 },
        { "WRITEOUTC", long_type, 0xac },
        { "PACKSTRING", long_type, 0xb0 },
        { "UNPACKSTRING", long_type, 0xb4 },
        { "HOLDTASK", long_type, 0xb8 },
        { "DELAY", long_type, 0xbc },
        { "SENDPKT", long_type, 0xc0 },
        { "RETURNPKT", long_type, 0xc4 },
        { "OPENWINDOW", long_type, 0xc8 },
        { "SETCURDIR", long_type, 0xcc },
        { "BUILDSYSREQ", long_type, 0xd0 },
        { "WRITET", long_type, 0xd4 },
        { "RDCH", long_type, 0xd8 },
        { "UNRDCH", long_type, 0xdc },
        { "WRCH", long_type, 0xe0 },
        { "READINB", long_type, 0xe4 },
        { "WRITEOUTB", long_type, 0xe8 },
        { "FINDINPUT", long_type, 0xec },
        { "FINDOUTPUT", long_type, 0xf0 },
        { "SELECTINPUT", long_type, 0xf4 },
        { "SELECTOUTPUT", long_type, 0xf8 },
        { "ENDREAD", long_type, 0xfc },
        { "ENDWRITE", long_type, 0x100 },
        { "INPUT", long_type, 0x104 },
        { "OUTPUT", long_type, 0x108 },
        { "READN", long_type, 0x10c },
        { "NEWLINE", long_type, 0x110 },
        { "WRITEI", long_type, 0x114 },
        { "WRITEN", long_type, 0x118 },
        { "WRITEHEX", long_type, 0x11c },
        { "WRITEOCT", long_type, 0x120 },
        { "WRITES", long_type, 0x124 },
        { "WRITEF", long_type, 0x128 },
        { "TOUPPER", long_type, 0x12c },
        { "CHARCMP", long_type, 0x130 },
        { "STRCMP", long_type, 0x134 },
        { "RDARGS", long_type, 0x138 },
        { "RDLINE", long_type, 0x13c },
        { "PARSESTRING", long_type, 0x140 },
        { "LOAD", long_type, 0x144 },
        { "UNLOAD", long_type, 0x148 },
        { "TIDYUP", long_type, 0x150 },
        { "ADDDEVICE", long_type, 0x154 },
        { "DATESTAMPB", long_type, 0x158 },
        { "WAITFORCHAR", long_type, 0x15c },
        { "EXECLIB", long_type, 0x160 },
        { "FINDSEGARRAY", long_type, 0x164 },
        { "DELETEOBJ", long_type, 0x168 },
        { "RENAMEB", long_type, 0x16c },
        { "CLOSE", long_type, 0x174 },
        { "GETWORD", long_type, 0x178 },
        { "PUTWORD", long_type, 0x17c },
        { "TASKWAIT2", long_type, 0x190 },
        { "EXECUTEB", long_type, 0x194 },
        { "DEVICEPROCB", long_type, 0x198 },
        { "LIBCALL", long_type, 0x19c },
        { "WRITEERR", long_type, 0x1a0 },
        { "FINDCONHAND", long_type, 0x1a4 },
        { "FINDFILEHAND", long_type, 0x1a8 },
        { "EXTDEVNAME", long_type, 0x1ac },
        { "LOCKB1", long_type, 0x1b0 },
        { "UNLOCK", long_type, 0x1b4 },
        { "GETLONG", long_type, 0x1b8 },
        { "PUTLONG", long_type, 0x1bc },
        { "OPENFILE", long_type, 0x1c0 },
        { "DUPLOCK", long_type, 0x1c4 },
        { "MAKESYSREQ", long_type, 0x1c8 },
        { "STRCPY", long_type, 0x1cc },
        { "RUNLOADED", long_type, 0x1e4 },
        { "LOCKB2", long_type, 0x1ec },
        { "FINDDEVINFO", long_type, 0x1f0 },
        { "CREATEDIRB", long_type, 0x1f4 },
        { "CMPTIME", long_type, 0x1f8 },
        { "TIMERIO", long_type, 0x1fc },
        { "SETTIME", long_type, 0x200 },
        { "FINDCLI", long_type, 0x218 },
        { "globvec_Private", make_array_type(long_type, 0x100), 0x21c },
    }
};

const structure_definition BcplSupport {
    "BcplSupport",
    {
        { "CALL", code_type, 0x00 },
        { "MUL", code_type, 0x10 },
        { "DIV", code_type, 0x12 },
        { "DIV2", code_type, 0x14 },
    }
};

// intution
TEMP_STRUCT(Window);
TEMP_STRUCT(Screen);
const structure_definition IntuitionBase {
    "IntuitionBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "ViewLord", make_struct_type(View) },
        { "ActiveWindow", make_pointer_type(make_struct_type(Window)) },
        { "ActiveScreen", make_pointer_type(make_struct_type(Screen)) },
        { "FirstScreen", make_pointer_type(make_struct_type(Screen)) },
        { "Flags", long_type },
        { "MouseY", word_type },
        { "MouseX", word_type },
        { "Seconds", long_type },
        { "Micros", long_type },
        { "ib_Private", make_array_type(long_type, 0x564/4) }, // 0x5d4 is total size in KS1.3 (including the above stuff, but leave a little extra)

        { "_LVOOpenIntuition", libvec_code, -30 },
        { "_LVOIntuition", libvec_code, -36 },
        { "_LVOAddGadget", libvec_code, -42 },
        { "_LVOClearDMRequest", libvec_code, -48 },
        { "_LVOClearMenuStrip", libvec_code, -54 },
        { "_LVOClearPointer", libvec_code, -60 },
        { "_LVOCloseScreen", libvec_code, -66 },
        { "_LVOCloseWindow", libvec_code, -72 },
        { "_LVOCloseWorkBench", libvec_code, -78 },
        { "_LVOCurrentTime", libvec_code, -84 },
        { "_LVODisplayAlert", libvec_code, -90 },
        { "_LVODisplayBeep", libvec_code, -96 },
        { "_LVODoubleClick", libvec_code, -102 },
        { "_LVODrawBorder", libvec_code, -108 },
        { "_LVODrawImage", libvec_code, -114 },
        { "_LVOEndRequest", libvec_code, -120 },
        { "_LVOGetDefPrefs", libvec_code, -126 },
        { "_LVOGetPrefs", libvec_code, -132 },
        { "_LVOInitRequester", libvec_code, -138 },
        { "_LVOItemAddress", libvec_code, -144 },
        { "_LVOModifyIDCMP", libvec_code, -150 },
        { "_LVOModifyProp", libvec_code, -156 },
        { "_LVOMoveScreen", libvec_code, -162 },
        { "_LVOMoveWindow", libvec_code, -168 },
        { "_LVOOffGadget", libvec_code, -174 },
        { "_LVOOffMenu", libvec_code, -180 },
        { "_LVOOnGadget", libvec_code, -186 },
        { "_LVOOnMenu", libvec_code, -192 },
        { "_LVOOpenScreen", libvec_code, -198 },
        { "_LVOOpenWindow", libvec_code, -204 },
        { "_LVOOpenWorkBench", libvec_code, -210 },
        { "_LVOPrintIText", libvec_code, -216 },
        { "_LVORefreshGadgets", libvec_code, -222 },
        { "_LVORemoveGadget", libvec_code, -228 },
        { "_LVOReportMouse", libvec_code, -234 },
        { "_LVORequest", libvec_code, -240 },
        { "_LVOScreenToBack", libvec_code, -246 },
        { "_LVOScreenToFront", libvec_code, -252 },
        { "_LVOSetDMRequest", libvec_code, -258 },
        { "_LVOSetMenuStrip", libvec_code, -264 },
        { "_LVOSetPointer", libvec_code, -270 },
        { "_LVOSetWindowTitles", libvec_code, -276 },
        { "_LVOShowTitle", libvec_code, -282 },
        { "_LVOSizeWindow", libvec_code, -288 },
        { "_LVOViewAddress", libvec_code, -294 },
        { "_LVOViewPortAddress", libvec_code, -300 },
        { "_LVOWindowToBack", libvec_code, -306 },
        { "_LVOWindowToFront", libvec_code, -312 },
        { "_LVOWindowLimits", libvec_code, -318 },
        { "_LVOSetPrefs", libvec_code, -324 },
        { "_LVOIntuiTextLength", libvec_code, -330 },
        { "_LVOWBenchToBack", libvec_code, -336 },
        { "_LVOWBenchToFront", libvec_code, -342 },
        { "_LVOAutoRequest", libvec_code, -348 },
        { "_LVOBeginRefresh", libvec_code, -354 },
        { "_LVOBuildSysRequest", libvec_code, -360 },
        { "_LVOEndRefresh", libvec_code, -366 },
        { "_LVOFreeSysRequest", libvec_code, -372 },
        { "_LVOMakeScreen", libvec_code, -378 },
        { "_LVORemakeDisplay", libvec_code, -384 },
        { "_LVORethinkDisplay", libvec_code, -390 },
        { "_LVOAllocRemember", libvec_code, -396 },
        { "_LVOAlohaWorkbench", libvec_code, -402 },
        { "_LVOFreeRemember", libvec_code, -408 },
        { "_LVOLockIBase", libvec_code, -414 },
        { "_LVOUnlockIBase", libvec_code, -420 },
        { "_LVOGetScreenData", libvec_code, -426 },
        { "_LVORefreshGList", libvec_code, -432 },
        { "_LVOAddGList", libvec_code, -438 },
        { "_LVORemoveGList", libvec_code, -444 },
        { "_LVOActivateWindow", libvec_code, -450 },
        { "_LVORefreshWindowFrame", libvec_code, -456 },
        { "_LVOActivateGadget", libvec_code, -462 },
        { "_LVONewModifyProp", libvec_code, -468 },
        { "_LVOQueryOverscan", libvec_code, -474 },
        { "_LVOMoveWindowInFrontOf", libvec_code, -480 },
        { "_LVOChangeWindowBox", libvec_code, -486 },
        { "_LVOSetEditHook", libvec_code, -492 },
        { "_LVOSetMouseQueue", libvec_code, -498 },
        { "_LVOZipWindow", libvec_code, -504 },
        { "_LVOLockPubScreen", libvec_code, -510 },
        { "_LVOUnlockPubScreen", libvec_code, -516 },
        { "_LVOLockPubScreenList", libvec_code, -522 },
        { "_LVOUnlockPubScreenList", libvec_code, -528 },
        { "_LVONextPubScreen", libvec_code, -534 },
        { "_LVOSetDefaultPubScreen", libvec_code, -540 },
        { "_LVOSetPubScreenModes", libvec_code, -546 },
        { "_LVOPubScreenStatus", libvec_code, -552 },
        { "_LVOObtainGIRPort", libvec_code, -558 },
        { "_LVOReleaseGIRPort", libvec_code, -564 },
        { "_LVOGadgetMouse", libvec_code, -570 },
        { "_LVOSetIPrefs", libvec_code, -576 },
        { "_LVOGetDefaultPubScreen", libvec_code, -582 },
        { "_LVOEasyRequestArgs", libvec_code, -588 },
        { "_LVOBuildEasyRequestArgs", libvec_code, -594 },
        { "_LVOSysReqHandler", libvec_code, -600 },
        { "_LVOOpenWindowTagList", libvec_code, -606 },
        { "_LVOOpenScreenTagList", libvec_code, -612 },
        { "_LVODrawImageState", libvec_code, -618 },
        { "_LVOPointInImage", libvec_code, -624 },
        { "_LVOEraseImage", libvec_code, -630 },
        { "_LVONewObjectA", libvec_code, -636 },
        { "_LVODisposeObject", libvec_code, -642 },
        { "_LVOSetAttrsA", libvec_code, -648 },
        { "_LVOGetAttr", libvec_code, -654 },
        { "_LVOSetGadgetAttrsA", libvec_code, -660 },
        { "_LVONextObject", libvec_code, -666 },
        { "_LVOFindClass", libvec_code, -672 },
        { "_LVOMakeClass", libvec_code, -678 },
        { "_LVOAddClass", libvec_code, -684 },
        { "_LVOGetScreenDrawInfo", libvec_code, -690 },
        { "_LVOFreeScreenDrawInfo", libvec_code, -696 },
        { "_LVOResetMenuStrip", libvec_code, -702 },
        { "_LVORemoveClass", libvec_code, -708 },
        { "_LVOFreeClass", libvec_code, -714 },
        { "_LVOlockPubClass", libvec_code, -720 },
        { "_LVOunlockPubClass", libvec_code, -726 },
        { "_LVOAllocScreenBuffer", libvec_code, -768 },
        { "_LVOFreeScreenBuffer", libvec_code, -774 },
        { "_LVOChangeScreenBuffer", libvec_code, -780 },
        { "_LVOScreenDepth", libvec_code, -786 },
        { "_LVOScreenPosition", libvec_code, -792 },
        { "_LVOScrollWindowRaster", libvec_code, -798 },
        { "_LVOLendMenus", libvec_code, -804 },
        { "_LVODoGadgetMethodA", libvec_code, -810 },
        { "_LVOSetWindowPointerA", libvec_code, -816 },
        { "_LVOTimedDisplayAlert", libvec_code, -822 },
        { "_LVOHelpControl", libvec_code, -828 },
    }
};

// expansion
const structure_definition ExpansionRom {
    "ExpansionRom",
    {
        { "er_Type", byte_type },
        { "er_Product", byte_type },
        { "er_Flags", byte_type },
        { "er_Reserved03", byte_type },
        { "er_Manufacturer", word_type },
        { "er_SerialNumber", long_type },
        { "er_InitDiagVec", word_type },
        { "er_DiagAreaCopy", long_type },
        //{ "er_Reserved0c", byte_type },
        //{ "er_Reserved0d", byte_type },
        //{ "er_Reserved0e", byte_type },
        //{ "er_Reserved0f", byte_type },
    }
};

const structure_definition ConfigDev {
    "ConfigDev",
    {
        { "cd_Node", make_struct_type(Node) },
        { "cd_Flags", byte_type },
        { "cd_Pad", byte_type },
        { "cd_Rom", make_struct_type(ExpansionRom) },
        { "cd_BoardAddr", unknown_ptr },
        { "cd_BoardSize", long_type },
        { "cd_SlotAddr", word_type },
        { "cd_SlotSize", word_type },
        { "cd_Driver", unknown_ptr },
        { "", make_pointer_type(make_struct_type(ConfigDev)) },
        { "cd_Unused", make_array_type(long_type, 4) },
    }
};
const structure_definition CurrentBinding
{
    "CurrentBinding",
    {
        { "cb_ConfigDev", make_pointer_type(make_struct_type(ConfigDev)) },
        { "cb_FileName", char_ptr },
        { "cb_ProductString", char_ptr },
        { "cb_ToolTypes", make_pointer_type(char_ptr) },
    }
};

const structure_definition BootNode {
    "BootNode",
    {
        { "bn_Node", make_struct_type(Node) },
        { "bn_Flags", word_type },
        { "bn_DeviceNode", unknown_ptr },
    }
};
const structure_definition ExpansionBase {
    "ExpansionBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "Flags", byte_type },
        { "eb_Private01", byte_type },
        { "eb_SysBase", long_type }, // eb_Private02
        { "eb_SegList", long_type }, // eb_Private03
        { "eb_CurrentBinding", make_struct_type(CurrentBinding) }, // eb_Private04
        { "eb_ConfigDevList", make_struct_type(List) }, // eb_Private05
        { "MountList", make_struct_type(List) },
        { "eb_UnknownData", make_array_type(byte_type, 0x1c8 - 0x58) },

        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },
        { "_LVOAddConfigDev", libvec_code, -30 },

        { "_LVOAddBootNode", libvec_code, -36 },
        { "_LVOAllocBoardMem", libvec_code, -42 },
        { "_LVOAllocConfigDev", libvec_code, -48 },
        { "_LVOAllocExpansionMem", libvec_code, -54 },
        { "_LVOConfigBoard", libvec_code, -60 },
        { "_LVOConfigChain", libvec_code, -66 },
        { "_LVOFindConfigDev", libvec_code, -72 },
        { "_LVOFreeBoardMem", libvec_code, -78 },
        { "_LVOFreeConfigDev", libvec_code, -84 },
        { "_LVOFreeExpansionMem", libvec_code, -90 },
        { "_LVOReadExpansionByte", libvec_code, -96 },
        { "_LVOReadExpansionRom", libvec_code, -102 },
        { "_LVORemConfigDev", libvec_code, -108 },
        { "_LVOWriteExpansionByte", libvec_code, -114 },
        { "_LVOObtainConfigBinding", libvec_code, -120 },
        { "_LVOReleaseConfigBinding", libvec_code, -126 },
        { "_LVOSetCurrentBinding", libvec_code, -132 },
        { "_LVOGetCurrentBinding", libvec_code, -138 },
        { "_LVOMakeDosNode", libvec_code, -144 },
        { "_LVOAddDosNode", libvec_code, -150 },
    }
};

const structure_definition RomBootBase {
    "RomBootBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "rb_SysBase", long_type },

        { "_LVOOpenLib", libvec_code, -6 },
        { "_LVOCloseLib", libvec_code, -12 },
        { "_LVOExpungeLib", libvec_code, -18 },
        { "_LVOReservedFuncLib", libvec_code, -24 },

    }
};

const structure_definition DiskResource {
    "DiskResource",
    {
        { "LibNode", make_struct_type(Library) },
        { "_LVOAllocUnit", libvec_code, -6 },
        { "_LVOFreeUnit", libvec_code, -12 },
        { "_LVOGetUnit", libvec_code, -18 },
        { "_LVOGiveUnit", libvec_code, -24 },
        { "_LVOGetUnitID", libvec_code, -30 },
        { "_LVOReadUnitID", libvec_code, -36 }, // Functions in V37 or higher (2.0)
    }
};


struct rom_tag {
    uint32_t matchtag; // RT_MATCHTAG     (pointer RTC_MATCHWORD)
    uint32_t endskip; // RT_ENDSKIP      (pointer to end of code)
    uint8_t flags; // RT_FLAGS        (no flags)
    uint8_t version; // RT_VERSION      (version number)
    uint8_t type; // RT_TYPE         (NT_LIBRARY)
    uint8_t priority; // RT_PRI          (priority = 126)
    uint32_t name_addr; // RT_NAME         (pointer to name)
    uint32_t id_addr; // RT_IDSTRING     (pointer to ID string)
    uint32_t init_addr; // RT_INIT         (execution address)*

    std::string name;
};

std::vector<rom_tag> scan_for_rom_tags(const std::vector<uint8_t>& data, uint32_t base = 0)
{
    if (data.size() <= 24)
        return {};
    std::vector<rom_tag> rom_tags;
    for (uint32_t offset = 0; offset < data.size() - 24;) {
        if (get_u16(&data[offset]) != 0x4afc || get_u32(&data[offset + 2]) != offset + base) {
            offset += 2;
            continue;
        }

        // std::cout << "Found tag at $" << hexfmt(rom_base + offset) << "\n";
        rom_tag tag {
            .matchtag = get_u32(&data[offset + 2]),
            .endskip = get_u32(&data[offset + 6]),
            .flags = data[offset + 10],
            .version = data[offset + 11],
            .type = data[offset + 12],
            .priority = data[offset + 13],
            .name_addr = get_u32(&data[offset + 14]),
            .id_addr = get_u32(&data[offset + 18]),
            .init_addr = get_u32(&data[offset + 22]),
            .name = "",
        };

        uint32_t end_offset = tag.endskip - base;
        if (end_offset <= offset || end_offset > data.size()) {
            std::cout << "Invalid end offset\n";
            end_offset = offset + 26;
        }

        auto get_string = [&](uint32_t addr) {
            if (addr <= base || addr - base >= data.size())
                throw std::runtime_error { "Invalid string in ROM tag: addr=$" + hexstring(addr) };
            addr -= base;
            std::string s;
            for (; addr < data.size() && data[addr]; ++addr) {
                if (data[addr] >= ' ' && data[addr] < 128)
                    s.push_back(data[addr]);
                else
                    s += "\\x" + hexstring(data[addr]);
            }
            return s;
        };

        tag.name = get_string(tag.name_addr);

        // std::cout << "  End=$" << hexfmt(tag.endskip) << "\n";
        // std::cout << "  Flags=$" << hexfmt(tag.flags) << " version=$" << hexfmt(tag.version) << " type=$" << hexfmt(tag.type) << " priority=$" << hexfmt(tag.priority) << "\n";
        // std::cout << "  Name='" << tag.name << "'\n";
        // std::cout << "  ID='" << get_string(tag.id_addr) << "'\n";
        // std::cout << "  Init=$" << hexfmt(tag.init_addr) << "\n";

        rom_tags.push_back(tag);
        offset = end_offset;
    }

    return rom_tags;
}

const char* const* cia_bit_names(uint32_t addr)
{
    const auto reg = (addr >> 8) & 0xf;
    const auto cia = (addr >> 12) & 3;
    if (cia == 0 || cia == 3) // Both or neither
        return nullptr;

    switch (reg) {
    case 0x0: // Port A
    case 0x2: // Direction for port A (0=input, 1=output)
        return cia == 1 ? ciabpra_bitnames : ciaapra_bitnames;

    case 0x1: // Port B
    case 0x3: // Direction for port B (input -> read as 0xff if not driven due to pull-ups)
        if (cia == 2)
            return nullptr; // Parallel port
        return ciabprb_bitnames;
    case 0x4: // Timer A low byte (.715909 Mhz NTSC; .709379 Mhz PAL)
    case 0x5: // Timer A high byte
    case 0x6: // Timer B low byte
    case 0x7: // Timer B high byte
    case 0x8: // Event counter bits 7-0 (CIAA: vsync, CIAB: hsync)
    case 0x9: // Event counter bits 15-8
    case 0xA: // Event counter bits 23-16
    case 0xB: // not connected
    case 0xC: // Serial data register (CIAA: connected to keyboard, CIAB: not used)
        return nullptr;
    case 0xD: // Interrupt control register
        return ciaicr_bitnames;
    case 0xE: // Control register A
        return ciacra_bitnames;
    case 0xF: // Control register B
        return ciacrb_bitnames;
    }
    return nullptr;
}

void maybe_add_bitnum_info(std::ostringstream& extra, uint8_t bitnum, const simval& aval)
{
    if (!aval.known())
        return;
    const auto addr = aval.raw();
    if (addr >= 0xA00000 && addr < 0xC00000) {
        assert(bitnum < 8);
        auto names = cia_bit_names(addr);
        if (names)
            extra << " bit " << static_cast<int>(bitnum) << " = " << names[bitnum];
        return;
    } else if (addr >= 0xDE0000 && addr < 0xE00000) {
        const auto reg = (addr & 0x1fe);
        if (!(addr & 1))
            bitnum += 8;
        if (reg == 0x02 || reg == 0x96) {
            // DMACON(R)
            extra << " bit " << static_cast<int>(bitnum) << " = " << dmacon_bitnames[bitnum];
            return;
        } else if (reg == 0x1c || reg == 0x1e || reg == 0x9a || reg == 0x9c) {
            // Interrupt bits
            extra << " bit " << static_cast<int>(bitnum) << " = " << int_bitnames[bitnum];
            return;
        } else if (reg == 0x10 || reg == 0x9e) {
            // ADKCON(R)
            extra << " bit " << static_cast<int>(bitnum) << " = " << adkcon_bitnames[bitnum];
            return;
        } else if (reg == 0x16 && bitnum == 10) {
            // POTGOR (special case for RMB)
            extra << " /RMB";
            return;
        }
        std::cerr << "TODO: Add bitnum info for custom register " << custom_regname[reg >> 1] << " ($" << hexfmt(reg, 3) << ") bit " << static_cast<int>(bitnum) << " \n";
    }    
}

void maybe_add_cia_bit_info(std::ostringstream& extra, uint32_t addr, uint8_t val, bool mask)
{
    auto names = cia_bit_names(addr);
    if (!names)
        return;

    if (mask) {
        if (val == 0xff)
            return;
        val = ~val;
        extra << " clear";
    } else {
        if (val == 0)
            return;
        extra << " set";
    }

    bool first = true;
    for (int bit = 7; bit >= 0; --bit) {
        if (val & (1 << bit)) {
            extra << (first ? " " : "+") << names[bit];
            first = false;
        }
    }
}

void add_bit_names(std::ostream& extra, const char* const *names, uint16_t val)
{
    bool first = true;
    for (int bit = 15; bit >= 0; --bit) {
        if (val & (1 << bit)) {
            if (!*names[bit])
                continue;
            extra << (first ? " " : "+") << names[bit];
            first = false;
        }
    }
}

void maybe_add_custom_bit_info(std::ostream& extra, uint32_t addr, uint16_t val)
{
    if (addr & 1)
        return;
    addr &= 0x1fe;
    if (addr == 0x96 || addr == 0x9e) {
        // DMACON / ADKCON
        if (val == 0x7fff) {
            extra << " disable all";
            return;
        } else if (val == 0) {
            return;
        }
        add_bit_names(extra, addr == 0x96 ? dmacon_bitnames : adkcon_bitnames, val);
    } else if (addr == 0x9a || addr == 0x9c) {
        // INTENA/INTREQ
        if (val == 0x7fff) {
            extra << " " << (addr == 0x9a ? "disable" : "acknowledge") <<  " all";
            return;
        } else if (val == 0) {
            return;
        }
        add_bit_names(extra, int_bitnames, val);
    } else if (addr == 0x100) {
        int bpu = (val & 0x10) >> 1 | (val & 0x7000) >> 12;
        val &= ~0x7010;
        extra << " BPU=" << bpu;
        if (!val)
            return;
        const char* bplcon0_bitnames[] = {
            "ECSENA", "ERSY", "LACE", "LPEN", "",
            "BYPOSS", "SHRES", "UHRES", "GUAD", "COLOR", "DPF",
            "HAM","","","","HIRES"
        };
        add_bit_names(extra, bplcon0_bitnames, val);
    }
}

constexpr uint16_t JMP_ABS_L_instruction = 0x4EF9;

class analyzer {
public:
    explicit analyzer()
        : written_(max_mem)
        , data_handled_at_(max_mem)
        , data_(max_mem)
        , regs_ {}
    {
    }

    struct label_info {
        std::string name;
        const type* t;
    };

    void do_system_scan();

    uint32_t fake_exec_base() const
    {
        assert(exec_base_);
        return exec_base_;
    }

    uint32_t alloc_fake_mem(uint32_t size)
    {
        size = (size + 3) & -4; // align
        if (size < alloc_top_ && !written_[alloc_top_ - size]) {
            alloc_top_ -= size;
            std::fill(written_.begin() + alloc_top_, written_.begin() + alloc_top_ + size, true); // So assignments will be tracked and pointers are deemed OK
            return alloc_top_;
        }
        return 0;
    }

    void read_info_file(const std::string& filename)
    {
        std::ifstream in { filename };
        if (!in || !in.is_open())
            throw std::runtime_error { "Could not open " + filename };

        std::vector<struct_field> struct_fields;
        bool in_struct = false;
        std::string struct_name;

        uint32_t linenum = 1;
        for (std::string line; std::getline(in, line); ++linenum) {
            size_t start = 0, end = line.length();
            while (start < line.length() && isspace(line[start]))
                ++start;
            for (size_t p = start; p < end; ++p) {
                if (line[p] == '#') {
                    end = p;
                    break;
                }
            }
            while (end > start && isspace(line[end - 1]))
                --end;
            if (start >= end)
                continue;

            auto split = [](const std::string& s, char delim) {
                std::vector<std::string> parts;
                std::istringstream iss { s };
                for (std::string part; std::getline(iss, part, delim);) {
                    if (!part.empty())
                        parts.push_back(part);
                }
                return parts;
            };

            auto get_reg_and_equal = [](const std::string& s) -> std::optional<regname> {
                if (s.length() < 4 || s[2] != '=')
                    return {};
                if ((s[0] != 'A' && s[0] != 'D') || s[1] < '0' || s[1] > '7')
                    return {};
                return static_cast<regname>(s[1] - '0' + (s[0] == 'A' ? 8 : 0));
            };

            std::vector<std::string> parts = split(line.substr(start, end - start), ' ');

            if (parts.size() == 1 && parts[0] == "ESTRUCT") {
                if (!in_struct)
                    throw std::runtime_error { "ESTRUCT outside structure in " + filename + " line " + std::to_string(linenum) + ": " + line };
                in_struct = false;
                struct_defs_.push_back(std::make_unique<structure_definition>(struct_name.c_str(), struct_fields));
                struct_fields.clear();
                continue;
            } else if (parts.size() == 2 && parts[0] == "STRUCT") {
                if (in_struct)
                    throw std::runtime_error { "Already in structure in " + filename + " line " + std::to_string(linenum) + ": " + line };
                struct_name = parts[1];
                in_struct = true;
                continue;
            }

            if (parts.size() == 1) {
                parts = split(parts[0], '=');
                if (parts.size() == 2) {
                    const auto [ok, addr] = from_hex(parts[0]);
                    if (ok) {
                        alias_or_forced_mem_.push_back(std::make_pair(addr, parts[1]));
                        continue;
                    }
                }
                throw std::runtime_error { "Invalid function alias/forced memory in " + filename + " line " + std::to_string(linenum) + ": " + line };
            }

            if (parts.size() == 2) {
                const auto [ok, addr] = from_hex(parts[0]);
                const auto rn = get_reg_and_equal(parts[1]);
                if (ok && rn && parts[1].length() >= 4) {
                    auto lab = parts[1].substr(3);
                    if (lab[0] >= '0' && lab[0] <= '9') {
                        if (const auto [ok2, val] = from_hex(lab); ok2) {
                            forced_values_.insert({ addr, { *rn, val } });
                            continue;
                        }
                    } else {
                        delayed_forced_values_.push_back({ addr, *rn, lab });
                        continue;
                    }
                }
            }

            if (parts.size() != 3)
                throw std::runtime_error { "Error in " + filename + " line " + std::to_string(linenum) + ": " + line };

            const auto [ok, addr] = from_hex(parts[0]);
            if (!ok)
                throw std::runtime_error { "Invalid address in " + filename + " line " + std::to_string(linenum) + ": " + line };

            if (parts[1] == "IGNORE") {
                const auto [ok2, iend] = from_hex(parts[2]);
                if (!ok2)
                    throw std::runtime_error { "Invalid address in " + filename + " line " + std::to_string(linenum) + ": " + line };
                insert_area(ignored_areas_, addr, iend);
                continue;
            }

            // quick hack for BCPL code
            if (parts[1] == "BCPLCODE") {
                parts[1] = "CODE";
                forced_values_.insert({ addr, { regname::A0, 0 } });
                delayed_forced_values_.push_back({ addr, regname::A2, DosGlobalVec.name() });
                forced_values_.insert({ addr, { regname::A4, addr } }); // Only true if used for function entry!
                delayed_forced_values_.push_back({ addr, regname::A5, BcplSupport.name() });
                // A6 is return function
            }

            const auto t = parse_type(parts[1]);
            if (!t)
                throw std::runtime_error { "Invalid type in " + filename + " line " + std::to_string(linenum) + ": " + line };
            auto lab = parts[2];
            if (lab == "?") {
                lab = (t == &code_type ? "func_" : t == &hack_str_type ? "text_" :  "dat_") + hexstring(addr);
            } else if (t == &code_type) {
                auto pos = lab.find_first_of('(');
                if (pos != std::string::npos) {
                    if (lab.back() != ')')
                        throw std::runtime_error { "Invalid function declaration in " + filename + " line " + std::to_string(linenum) + ": " + line };

                    std::vector<argument_description> input;
                    for (const auto& arg : split(lab.substr(pos + 1, lab.length() - 1 - (pos + 1)), ',')) {
                        auto name_and_type = split(arg, ':');
                        if (name_and_type.size() == 1 || name_and_type.size() == 2) {
                            if (const auto rn = get_reg_and_equal(name_and_type.back()); rn) {
                                if (const auto at = parse_type(name_and_type.back().substr(3)); at) {
                                    input.push_back(argument_description { *rn, name_and_type.size() == 2 ? name_and_type[0] : "arg_" + std::to_string(input.size()), at });
                                    continue;
                                }
                            }
                        }
                        throw std::runtime_error { "Invalid function argument " + arg + " in " + filename + " line " + std::to_string(linenum) + ": " + line };
                    }
                    // TODO outputs..
                    functions_.insert({ addr, function_description { {}, input } });
                    lab.erase(pos);
                }
            }

            if (in_struct)
                struct_fields.push_back({lab.c_str(), *t, static_cast<int32_t>(addr)});
            else
                predef_info_.push_back({ addr, { lab, t } });
        }

        if (in_struct)
            throw std::runtime_error { "Unterminated structure " + struct_name };
    }

    const label_info* find_predef_into(uint32_t query_addr)
    {
        for (const auto& [addr, info] : predef_info_) {
            if (addr == query_addr)
                return &info;
        }
        return nullptr;
    }

    void write_data(uint32_t addr, const uint8_t* data, uint32_t length)
    {
        if (static_cast<size_t>(addr) + length > data_.size())
            throw std::runtime_error { "Out of range" };
        memcpy(&data_[addr], data, length);
        for (uint32_t i = 0; i < length; ++i)
            written_[i + addr] = true;
        insert_area(areas_, addr, addr + length);
    }

    void add_start_root(uint32_t start)
    {
        // Only add if not supplied in info file
        auto it = std::find_if(predef_info_.begin(), predef_info_.end(), [start](const auto& pi) { return pi.first == start; });
        if (it == predef_info_.end()) {
            add_label(start, "Start", code_type);
            add_root(start, simregs {}, true);
        } else if (it->second.t != &code_type) {
            std::cerr << "Warning: " << it->second.name << " should be CODE type since it's supplied as start root\n";
        }
    }

    void add_int_vectors()
    {
        // Add roots for all defined interrupt vectors up to and including traps
        for (uint8_t i = 2; i < 48; ++i) {
            if (!written_[i * 4])
                continue;
            auto ptr = get_u32(&data_[i * 4]);
            if (!pointer_ok(ptr))
                continue;
            // Only add if not supplied in info file
            auto it = std::find_if(predef_info_.begin(), predef_info_.end(), [ptr](const auto& pi) { return pi.first == ptr; });
            if (it == predef_info_.end()) {
                add_label(ptr, interrupt_name(i) + "Handler", code_type);
                add_root(ptr, simregs {}, true);
            } else if (it->second.t != &code_type) {
                std::cerr << "Warning: " << it->second.name << " should be CODE type since it's an interrupt handler for " << interrupt_name(i) << "\n";
            }
            add_auto_label(i * 4, code_ptr);
        }
    }

    void add_root(uint32_t addr, const simregs& regs, bool force = false, bool is_func = true)
    {
        if (addr >= max_mem || (addr & 1))
            return;

        // Don't visit non-written areas
        if (!force && (!written_[addr] || addr < 32*4))
            return;

        if (addr >= alloc_top_ && addr <= alloc_start)
            return;

        if (visited_.find(addr) == visited_.end()) {
            // Don't add if already predefined as non-code
            if (auto info = find_predef_into(addr); info && info->t != &code_type) {
                // Special case for CODE at offset 0 in struct..
                if (info->t->struct_def()) {
                    auto [field, offset] = info->t->struct_def()->field_at(0);
                    if (!field || &field->t() != &code_type)
                        return;
                } else {
                    return;
                }
            }

            auto root_regs = regs;
            auto forced = forced_values_.equal_range(addr);
            for (auto fit = forced.first; fit != forced.second; ++fit) {
                reg_from_name(root_regs, fit->second.first) = simval { fit->second.second };
            }

            roots_.push_back({ addr, root_regs });
            visited_[addr] = root_regs; // Mark visitied now to avoid re-insertion
        }
        add_auto_label(addr, code_type, is_func ? "func" : "lab");
    }

    void add_label(uint32_t addr, const std::string& name, const type& t, bool is_auto_label = true)
    {
        auto [li, offset] = find_label(addr);
        if (!li || (offset < 0 && &t == &code_type)) {
            labels_.insert({ addr, { name, &t } });
            return;
        }

        // Don't override predefined info
        if (find_predef_into(addr))
            return;

        // Exact match and type is same or we already know a better one
        if (li->t == &t || &t == &unknown_type) {
            // But update in case this isn't an automatic label
            if (&t != &unknown_type && !is_auto_label)
                li->name = name;
            return;
        }

        //std::cerr << "Warning: Label " << name << " $" << hexfmt(addr) << " " << t << " overlaps " << li->name << " " << *li->t << " offset $" << hexfmt(offset) << "\n";

        // Part or a structure/array, ignore
        if (li->t->struct_def() || (li->t->ptr() && li->t->len()))
            return;

        // Always update unkown type if we know a better one (and prefer code)
        if (li->t == &unknown_type || &t == &code_type) {
            // Don't override symbol names
            if (!is_auto_label || li-> name.starts_with("dat"))
                li->name = name;
            li->t = &t;
            return;
        }

        // Update for larger size, e.g. WORD->LONG
        if (li->t != &code_type && sizeof_type(t) > sizeof_type(*li->t)) {
            li->t = &t;
        }
    }

    std::pair<label_info*, int32_t> find_label(uint32_t addr)
    {
        if (addr >= max_mem || labels_.empty())
            return { nullptr, 0 };
        auto it = labels_.lower_bound(addr);
        if (it != labels_.end()) {
            const int32_t offset = addr - it->first;
            assert(offset <= 0);
            if (!offset)
                return { &it->second, 0 }; // Exact match
            if (it->second.t->struct_def() && it->second.t->struct_def()->negsize() >= static_cast<uint32_t>(-offset))
                return { &it->second, offset };
        }
        // At this point it can't be an exact match, and can't be part of a structures negative area
        if (it == labels_.begin())
            return { nullptr, 0 }; // Before any labels
        --it;
        const int32_t offset = addr - it->first;
        assert(offset > 0);
        if (it->second.t == &code_type || it->second.t == &unknown_type)
            return { nullptr, 0 };
        if (static_cast<uint32_t>(offset) < sizeof_type(*it->second.t))
            return { &it->second, offset };
        return { nullptr, 0 };
    }

    void add_auto_label(uint32_t addr, const type& t, const std::string& prefix = "dat")
    {
        if (addr < interrupts_end && (addr & 3) == 0 && &t != &code_type) {
            // Interrupt vector
            const auto vec = static_cast<uint8_t>(addr >> 2);
            switch (vec) {
            case 0: // Reset ssp
            case 1: // Reset PC
                break;
            default:
                add_label(addr, interrupt_name(vec) + "Vec", code_ptr);
                break;
            }
            return;
        }

        add_label(addr, prefix + "_" + hexstring(addr), t);
    }

    uint32_t alloc_library(const std::string& id, const structure_definition& def)
    {
        uint32_t addr = alloc_fake_mem(def.negsize() + def.size());
        if (!addr)
            throw std::runtime_error { id + " could not allocate memory" };
        addr += def.negsize();

        add_label(addr, id, make_struct_type(def));
        return addr;
    }

    uint32_t add_library(const std::string& name, const std::string& id, const structure_definition& def)
    {
        if (library_bases_.find(name) != library_bases_.end())
            throw std::runtime_error { name + " already added" };
        const auto addr = alloc_library(id, def);
        library_bases_.insert({ name, addr });
        return addr;
    }

    void handle_rtf_autoinit(uint32_t addr)
    {
        assert(addr + 16 <= max_mem);
        assert(!regs_.a[0].known());
        assert(!regs_.a[1].known());
        assert(!regs_.a[2].known());
        assert(!regs_.d[0].known());
        assert(!regs_.d[1].known());


        const uint32_t dSize = get_u32(&data_[addr]);
        const uint32_t vectors = get_u32(&data_[addr + 4]);
        const uint32_t structure = get_u32(&data_[addr + 8]);
        const uint32_t initFunc = get_u32(&data_[addr + 12]);

        regs_.a[0] = simval {vectors};
        regs_.a[1] = simval {structure};
        regs_.a[2] = simval {initFunc};
        regs_.d[0] = simval {dSize};
        regs_.d[1] = simval {}; // segList

        do_make_library();

        regs_.a[0] = simval{};
        regs_.a[1] = simval{};
        regs_.a[2] = simval{};
        regs_.d[0] = simval{};
        regs_.d[1] = simval{};
    }


    void add_fakes()
    {
        if (!exec_base_) {
            exec_base_ = add_library("exec.library", "SysBase", ExecBase);
            saved_pointers_.insert({ 4, exec_base_ });
            fake_process_ = alloc_fake_mem(Process.size());
            add_label(fake_process_, "FakeProcess", make_struct_type(Process));
            update_mem(opsize::l, exec_base_ + ExecBase.field_offset("ThisTask"), simval { fake_process_ });
        } else {
            library_bases_.insert({ "exec.library", exec_base_ });
        }
        const auto gfx_base = add_library("graphics.library", "GfxBase", GfxBase);
        if (const auto a = exec_base_ + ExecBase.field_offset("IntVects") + 0x48; get_u32(&data_[a]) == 0) {
            // Stuff gfxbase into IntVects[6].iv_Data ...
            saved_pointers_.insert({ a, gfx_base });
        }
        const auto dos_base = add_library("dos.library", "DosBase", DosBase);
        add_library("intuition.library", "IntuitionBase", IntuitionBase);
        add_library("expansion.library", "ExpansionBase", ExpansionBase);
        add_library("romboot.library", "RomBootBase", RomBootBase);

        add_label(alloc_fake_mem(ConfigDev.size()), "FakeConfigDev", make_struct_type(ConfigDev), false);
        const auto fake_dev = alloc_library("FakeDevice", Device);
        const auto fake_ioreq = alloc_fake_mem(IOStdReq.size());
        add_label(fake_ioreq, "FakeIoRequest", make_struct_type(IOStdReq), false);
        update_mem(opsize::l, fake_ioreq + IOStdReq.field_offset("io_Device"), simval { fake_dev });

        add_library("disk.resource", "DiskResourceBase", DiskResource);

        // exec.library
        auto add_int = [this]() { do_add_int(); };
        auto open_library = [this]() { do_open_library(); };
        functions_.insert({ exec_base_ + _LVOSupervisor, function_description {
                                                             {},
                                                             { { regname::A5, "userFunc", &code_ptr } },
                                                         } });
        functions_.insert({ exec_base_ + _LVOMakeLibrary, function_description { {}, {
                                                                                         { regname::A0, "vectors", &unknown_ptr },
                                                                                         { regname::A1, "structure", &unknown_ptr },
                                                                                         { regname::A2, "init", /*&code_ptr*/ &unknown_ptr }, // Handle adding root in do_make_library
                                                                                         { regname::D0, "dSize", &long_type },
                                                                                         { regname::D1, "segList", &bptr_type },
                                                                                     },
                                                              [this]() {
                                                                  do_make_library();
                                                              } } });
        functions_.insert({ exec_base_ + _LVOMakeFunctions, function_description { { { regname::D0, "tableSize", &long_type },
                                                                                   },
                                                                {
                                                                    { regname::A0, "target", &unknown_ptr },
                                                                    { regname::A1, "functionArray", &unknown_ptr },
                                                                    { regname::A2, "funcDispBase", &unknown_ptr },
            }, [this]() { do_make_functions(); } } });
        functions_.insert({ exec_base_ + _LVOFindResident, function_description { {}, { { regname::A1, "name", &char_ptr } }, [this]() { do_find_resident();  } } });

        functions_.insert({ exec_base_ + _LVOSetIntVector, function_description {
                                                               {},
                                                               { { regname::D0, "intNum", &byte_type }, { regname::A1, "interrupt", &make_pointer_type(make_struct_type(Interrupt)) } },
                                                               add_int,
                                                           } });
        functions_.insert({ exec_base_ + _LVOAddIntServer, function_description {
                                                               {},
                                                               { { regname::D0, "intNum", &byte_type }, { regname::A1, "interrupt", &make_pointer_type(make_struct_type(Interrupt)) } },
                                                               add_int,
                                                           } });
        functions_.insert({ exec_base_ + _LVOAllocMem, function_description { {}, { { regname::D0, "byteSize", &long_type }, { regname::D1, "attributes", &long_type } }, [this]() {
                                                                                 do_alloc_mem();
                                                                             } } });
        functions_.insert({exec_base_ + _LVOAddTask, function_description
            {
                {}, {
                    { regname::A1, "task", &make_pointer_type(make_struct_type(Task)) },
                    { regname::A2, "initialPC", &code_ptr },
                    { regname::A3, "finalPC", &code_ptr }
                },
                [this]() {
                    regs_.d[0] = simval { fake_process_ };
                }
            }});
        functions_.insert({ exec_base_ + _LVOFindTask, function_description { {}, { { regname::A1, "name", &char_ptr } }, [this]() {
                                                                                 regs_.d[0] = simval { fake_process_ };
                                                                             } } });
        functions_.insert({ exec_base_ + _LVOPutMsg, function_description { {}, { { regname::A0, "port", &make_pointer_type(make_struct_type(MsgPort)) }, { regname::A1, "message", &make_pointer_type(make_struct_type(Message)) } } } });
        functions_.insert({ exec_base_ + _LVOGetMsg, function_description { {}, { { regname::A0, "port", &make_pointer_type(make_struct_type(MsgPort)) }, } } });
        functions_.insert({ exec_base_ + _LVOReplyMsg, function_description { {}, { { regname::A1, "message", &make_pointer_type(make_struct_type(Message)) }, } } });
        functions_.insert({ exec_base_ + _LVOAddLibrary, function_description { {}, { { regname::A1, "library", &make_pointer_type(make_struct_type(Library)) } } } });
        functions_.insert({ exec_base_ + _LVOOldOpenLibrary, function_description { {}, { { regname::A1, "libName", &char_ptr } }, open_library } });
        functions_.insert({ exec_base_ + _LVOOpenDevice, function_description { {}, {
                                                                                        { regname::A0, "devName", &char_ptr },
                                                                                        { regname::D0, "unitNumber", &byte_type },
                                                                                        { regname::A1, "iORequest", &make_pointer_type(make_struct_type(IOStdReq)) },
                                                                                        { regname::D1, "flags", &byte_type },
                                                                                    },
                                                             /*open_device*/ } });
        functions_.insert({ exec_base_ + _LVOOpenResource, function_description { {}, {
                                                                                          { regname::A1, "resName", &char_ptr },
                                                                                      },
                                                               /*open_resource*/ open_library } });
        functions_.insert({ exec_base_ + _LVOOpenLibrary, function_description { {}, {
                                                                                         { regname::A1, "libName", &char_ptr },
                                                                                         { regname::D0, "version", &long_type },
                                                                                     },
                                                              open_library } });

        functions_.insert({ exec_base_ + _LVORawDoFmt, function_description { {}, {
                                                                                         { regname::A0, "FormatString", &char_ptr },
                                                                                         { regname::A1, "DataStream", &unknown_ptr },
                                                                                         { regname::A2, "PutChProc", &code_ptr },
                                                                                         { regname::A3, "PutChData", &unknown_ptr },
                                                                                     },
                                                              open_library } });

        // dos.library
        functions_.insert({ dos_base + _LVOOpen, function_description {
                                                        {},
                                                        { { regname::D1, "name", &char_ptr }, { regname::D2, "accessMode ", &long_type } },
                                                    } });
        functions_.insert({ dos_base + _LVOWrite, function_description {
                                                        {},
                                                        { { regname::D1, "file", &bptr_type }, { regname::D2, "buffer", &unknown_ptr }, { regname::D3, "length", &long_type } },
                                                    } });

        // BCPL stuff
        alloc_library("DosGlobalVec", DosGlobalVec);
        const auto fake_bcpl_entry = alloc_library("BcplSupport", BcplSupport);
        functions_.insert({ fake_bcpl_entry, function_description {
                                                 {},
                                                 { { regname::A4, "function", &code_ptr } },
                                             } });

        // graphics.library
        functions_.insert({ gfx_base + _LVOWaitBlit, function_description{{}, { } }}); // registers are preseved
        const auto fake_view = alloc_fake_mem(View.size());
        add_label(fake_view, "FakeView", make_struct_type(View));
        update_mem(opsize::l, gfx_base + GfxBase.field_offset("ActiView"), simval { fake_view });

        //std::cerr << "Fake execbase: $" << hexfmt(exec_base_) << "\n";
    }

    std::optional<uint32_t> label_adddress(const std::string& lab)
    {
        // TODO: This lookup is slow
        auto it = std::find_if(labels_.begin(), labels_.end(), [&lab = lab](const auto& l) { return l.second.name == lab; });
        if (it == labels_.end())
            return {};
        return it->first;
    }

    void run()
    {
        handle_predef_info();
        if (!exec_base_)
            add_fakes();

        for (const auto& [addr, reg, lab] : delayed_forced_values_) {
            auto la = label_adddress(lab);
            if (!la)
                throw std::runtime_error { "Invalid label \"" + lab + "\" used for forced value at address $" + hexstring(addr) };
            forced_values_.insert({ addr, { reg, *la } });
        }
        for (const auto& [addr, expr] : alias_or_forced_mem_) {
            if (addr + 3 >= max_mem)
                throw std::runtime_error { "Invalid function alias/forced address $" + hexstring(addr) + " \"" + expr + "\"" };
            auto base = expr;
            int32_t ofs = 0;
            auto pos = base.find_first_of("-");
            if (pos != std::string::npos) {
                auto [ok, val] = from_hex(base.substr(pos + 1));
                if (!ok)
                    throw std::runtime_error { "Invalid function alias/forced mem for expression address $" + hexstring(addr) + " \"" + expr + "\"" };
                base = base.substr(0, pos);
                ofs = -static_cast<int32_t>(val);
            }
            auto la = label_adddress(base);
            if (!la)
                throw std::runtime_error { "Invalid function alias/forced mem for expression address (label not found) $" + hexstring(addr) + " \"" + expr + "\"" };
            const auto actual_address = *la + ofs;

            auto fit = functions_.find(actual_address);
            if (fit == functions_.end()) {
                // Must be a forced memory value
                //put_u32(&data_[addr], actual_address);
                saved_pointers_.insert({ addr, actual_address});
                std::fill(written_.begin() + actual_address, written_.begin() + actual_address + 4, true);
                continue;
            }

            if (!functions_.insert({ addr, fit->second }).second)
                throw std::runtime_error { "Invalid function alias/forced mem for expression address (function already defined there) $" + hexstring(addr) + " \"" + expr + "\"" };
            add_root(addr, simregs{});
        }

        process_roots();
        // Now add any predefined roots that were not automatically inferred
        for (const auto& pi : predef_info_) {
            if (pi.second.t == &code_type && visited_.find(pi.first) == visited_.end()) {
                add_root(pi.first, simregs {}, true);
                process_roots();
            } else if (pi.second.t->base() == base_data_type::ptr_ && pi.second.t->ptr()->base() == base_data_type::jumptab_word_ && pi.second.t->len()) {
                for (uint32_t i = 0; i < pi.second.t->len(); ++i) {
                    int16_t offset = get_u16(&data_[pi.first + 2 * i]);
                    if (!offset)
                        continue;
                    add_root(pi.first + offset, simregs {}, true);
                }
                process_roots();
            }
        }

        // Prune overlapping labels
        for (auto it = labels_.begin(); it != labels_.end();) {
            if (it->second.t == &unknown_type || it->second.t == &code_type) {
                ++it;
                continue;
            }
            const auto next = it->first + sizeof_type(*it->second.t);
            ++it;
            while (it != labels_.end() && it->first < next) {
                //std::cerr << "Erasing " << it->second.name << " " << *it->second.t << "\n";
                it = labels_.erase(it);
            }
        }

        for (const auto& area : areas_) {
            uint32_t pos = area.beg;

            while (pos < area.end) {
                if (auto ignored = find_area(ignored_areas_, pos)) {
                    pos = ignored->end;
                    continue;
                }

                auto it = visited_.find(pos);
                if (it == visited_.end()) {
                    const auto next_visited = visited_.upper_bound(pos);
                    const auto next_visited_pos = std::min(area.end, next_visited == visited_.end() ? ~0U : next_visited->first);
                    handle_data_area(pos, next_visited_pos);
                    pos = next_visited_pos;
                    continue;
                }

                maybe_print_label(pos);
                regs_ = it->second;
                uint16_t iwords[max_instruction_words];
                read_instruction(iwords, pos);
                const auto& inst = instructions[iwords[0]];

                // Check for SMC variables
                for (auto lit = labels_.lower_bound(pos + 1); lit != labels_.end() && lit->first < pos + 2 * inst.ilen; ++lit) {
                    //std::cerr << "Possible SMC: " << lit->second.name << " pos=$" << hexfmt(pos) << " addr=$" << hexfmt(lit->first) << "\n";
                    std::cout << lit->second.name << "=*+" << lit->first - pos << "\t; SMC!\n";
                }

                if (inst.type == inst_type::ILLEGAL && iwords[0] != illegal_instruction_num) {
                    if (iwords[0] == movec_instruction_dr0_num || iwords[0] == movec_instruction_dr1_num) {
                        // MOVEC special case
                        const auto op = get_u16(&data_[pos + 2]);
                        const auto regname = (op & 0x8000 ? "A" : "D") + std::to_string((op >> 12) & 7);
                        std::string crname;                        
                        static const std::map<uint32_t, std::string> crnames = {
                            { 0x002, "CACR" }, // Cache Control Register
                            { 0x801, "VBR" }, // Vector Base Register
                        };
                        if (auto crit = crnames.find(op & 0xfff); crit != crnames.end())
                            crname = crit->second;
                        else
                            crname = "CR" + hexstring(op & 0xfff, 3);
                        std::cout << "\tMOVEC\t";
                        if (iwords[0] & 1)
                            std::cout << regname << ", " << crname;
                        else
                            std::cout << crname << ", " << regname;
                        std::cout << "\t ; " << hexfmt(iwords[0]) << " " << hexfmt(op) << "\n";
                        pos += 4;
                        continue;
                    }
                    std::cout << "\tDC.W\t$" << hexfmt(iwords[0]) << " ; ILLEGAL\n";
                    pos += 2;
                    continue;
                }

                std::ostringstream extra;
                fixed_width_line fwl { std::cout };

                if (show_bytes) {
                    // TODO: Support logical address
                    // TODO: Also output for DC.W stuff
                    extra << hexfmt(pos, 6) << ": ";
                    for (int i = 0; i < inst.ilen; ++i)
                        extra << hexfmt(iwords[i], 4);
                }

                std::cout << "\t" << inst.name;
                for (int i = 0; i < inst.nea; ++i) {
                    const auto ea = inst.ea[i];
                    std::cout << (i ? "," : "\t");

                    bool is_dest = false;
                    // Supress printing known register value?
                    if (i == 1 && (inst.type == inst_type::MOVE || inst.type == inst_type::MOVEA || inst.type == inst_type::MOVEQ || inst.type == inst_type::LEA))
                        is_dest = true;

                    auto do_known = [&](const simval& val) {
                        if (is_dest || !verbose_disasm)
                            return;
                        if (val.known()) {
                            extra << " " << ea_string(ea) << " = $" << hexfmt(val.raw());
                        }
                    };
                    auto maybe_add_reg_info = [&](uint32_t addr) {
                        if (i != 1 || !ea_val_[0].known())
                            return;
                        if (addr >= 0xDE0000 && addr < 0xE00000) { // Custom register
                            if (inst.size == opsize::w && inst.type == inst_type::MOVE) {
                                maybe_add_custom_bit_info(extra, addr, static_cast<uint16_t>(ea_val_[0].raw()));
                            }
                        } else if (addr >= 0xA00000 && addr < 0xC00000 && (addr & 0xBFC0FE) == 0xBFC000) { // CIA
                            if (inst.size != opsize::b)
                                return;
                            maybe_add_cia_bit_info(extra, addr, static_cast<uint8_t>(ea_val_[0].raw()), inst.type == inst_type::AND || inst.type == inst_type::ANDI);
                        }
                    };
                    auto check_aind = [&]() {
                        const auto& areg = regs_.a[ea & ea_xn_mask];
                        if (!areg.known())
                            return;
                        const auto a = areg.raw();
                        maybe_add_reg_info(a);
                        if (is_dest || !verbose_disasm)
                            return;
                        if (auto lit = labels_.find(a); lit != labels_.end()) {
                            extra << " A" << (ea & ea_xn_mask) << " = " << lit->second.name << " (" << *lit->second.t << ")";
                        } else {
                            extra << " A" << (ea & ea_xn_mask) << " = $" << hexfmt(a);
                        }
                    };

                    switch (ea >> ea_m_shift) {
                    case ea_m_Dn:
                        std::cout << "D" << (ea & ea_xn_mask);
                        do_known(ea_val_[i]);
                        break;
                    case ea_m_An:
                        std::cout << "A" << (ea & ea_xn_mask);
                        do_known(ea_val_[i]);
                        break;
                    case ea_m_A_ind:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")";
                        check_aind();
                        break;
                    case ea_m_A_ind_post:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")+";
                        check_aind();
                        break;
                    case ea_m_A_ind_pre:
                        std::cout << "-(A" << (ea & ea_xn_mask) << ")";
                        check_aind();
                        break;
                    case ea_m_A_ind_disp16: {
                        auto n = static_cast<int16_t>(ea_data_[i]);
                        const auto& aval = regs_.a[ea & 7];
                        std::ostringstream desc;
                        if (n < 0) {
                            desc << "-";
                            n = -n;
                        }
                        desc << "$";
                        desc << hexfmt(static_cast<uint16_t>(n));
                        desc << "(A" << (ea & 7) << ")";
                        if (aval.known()) {
                            const int32_t offset = static_cast<int16_t>(ea_data_[i]);
                            const auto addr = aval.raw() + offset;
                            if (addr >= 0xA00000 && addr < 0xC00000 && (addr & 0xBFC0FE) == 0xBFC000) { // CIA
                                std::cout << cia_regname[(addr >> 8) & 0xf];
                                if (int ofs = (addr & 1) - (aval.raw() & 0x1ff); ofs != 0)
                                    std::cout << (ofs > 0 ? "+" : "-") << (ofs > 0 ? ofs : -ofs);
                                std::cout << "(A" << (ea & 7) << ")";
                                maybe_add_reg_info(addr);
                                if (verbose_disasm) extra << " " << desc.str() << " = $" << hexfmt(addr);
                                break;
                                // XXX
                            } else if (addr >= 0xDE0000 && addr < 0xE00000) { // Custom reg
                                std::cout << custom_regname[(addr >> 1) & 0xff];
                                if (int ofs = (addr & 1) - (aval.raw() & 0x1ff); ofs != 0)
                                    std::cout << (ofs > 0 ? "+" : "-") << (ofs > 0 ? ofs : -ofs);
                                std::cout << "(A" << (ea & 7) << ")";
                                maybe_add_reg_info(addr);
                                if (verbose_disasm) extra << " " << desc.str() << " = $" << hexfmt(addr);
                                break;
                            } else if (auto [li, lofs] = find_label(addr); li) {
                                if (auto lit = labels_.find(aval.raw()); lit != labels_.end()) {
                                    const auto& t = *lit->second.t;
                                    if (verbose_disasm)
                                        extra << " A" << (ea & 7) << " = " << lit->second.name << " (" << t << ")";
                                    if (t.struct_def()) {
                                        if (auto name = t.struct_def()->field_name(offset, inst.type == inst_type::LEA); name) {
                                            std::cout << *name << "(A" << (ea & 7) << ")";
                                            break;
                                        }
                                    }
                                    if (verbose_disasm) {
                                        extra << " " << desc.str() << " -> " << li->name;
                                        if (lofs)
                                            extra << (lofs > 0 ? '+' : '-') << "$" << hexfmt(lofs > 0 ? lofs : -lofs, 4);
                                        extra << " (" << t << ")";
                                    }
                                    // std::cout << li->name;
                                    // if (lofs)
                                    //     std::cout << (lofs > 0 ? '+' : '-') << "$" << hexfmt(lofs > 0 ? lofs : -lofs, 4);
                                    // std::cout << "-" << lit->second.name << "(A" << (ea & 7) << ")";
                                    // break;
                                } else {
                                    //std::cout << li->name;
                                    //lofs -= aval.raw();
                                    //if (lofs)
                                    //    std::cout << (lofs > 0 ? '+' : '-') << "$" << hexfmt(lofs > 0 ? lofs : -lofs);
                                    //std::cout << "(A" << (ea & 7) << ")";
                                    if (verbose_disasm)
                                        extra << " A" << (ea & 7) << " = $" << hexfmt(aval.raw());
                                    //break;
                                }
                            } else {
                                if (verbose_disasm)
                                    extra << " " << desc.str() << " = $" << hexfmt(addr);
                            }
                        }

                        std::cout << desc.str();
                        break;
                    }
                    case ea_m_A_ind_index: {
                        const auto extw = ea_data_[i];
                        // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                        auto disp = static_cast<int8_t>(extw & 255);
                        std::ostringstream desc;
                        if (disp < 0) {
                            desc << "-";
                            disp = -disp;
                        }
                        desc << "$";
                        desc << hexfmt(static_cast<uint8_t>(disp)) << "(A" << (ea & 7) << ",";
                        desc << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                        desc << ")";
                        // TODO: Handle know values..
                        std::cout << desc.str();
                        check_aind();
                        break;
                    }
                    case ea_m_Other:
                        switch (ea & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l: {
                            const auto suffix = (ea & ea_xn_mask) == ea_other_abs_w ? ".W" : "";
                            if (inst.type != inst_type::PEA || ea_addr_[i].raw() > 0x2000) { // arbitrary limit
                                const auto addr = ea_addr_[i].raw();
                                print_addr(addr);
                                std::cout << suffix;
                                maybe_add_reg_info(addr);
                            } else
                                std::cout << "$" << hexfmt(ea_addr_[i].raw()) << suffix;
                            break;
                        }
                        case ea_other_pc_disp16:
                            print_addr(ea_addr_[i].raw());
                            std::cout << "(PC)";
                            break;
                        case ea_other_pc_index: {
                            const auto extw = ea_data_[i];
                            // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                            auto disp = static_cast<int8_t>(extw & 255);
                            #if 0
                            std::cout << "$";
                            if (disp < 0) {
                                std::cout << "-";
                                disp = -disp;
                            }
                            std::cout << hexfmt(static_cast<uint8_t>(disp));
                            #else
                            print_addr(pos + 2 + disp);
                            #endif
                            std::cout << "(PC,";
                            std::cout << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                            std::cout << ")";
                            // TODO: Handle know values..
                            break;
                        }
                        case ea_other_imm:
                            std::cout << "#";
                            if (inst.size != opsize::l || ea_data_[i] < interrupts_end || !print_addr_maybe(ea_data_[i]))
                                std::cout << "$" << hexfmt(ea_data_[i], opsize_bytes(inst.size) * 2);
                            break;
                        default:
                            throw std::runtime_error { "TODO: " + ea_string(ea) };
                        }
                        break;
                    default:
                        if (ea == ea_sr) {
                            std::cout << "SR";
                        } else if (ea == ea_ccr) {
                            std::cout << "CCR";
                        } else if (ea == ea_usp) {
                            std::cout << "USP";
                        } else if (ea == ea_reglist) {
                            assert(inst.nea == 2);
                            std::cout << reg_list_string(static_cast<uint16_t>(ea_data_[i]), i == 0 && (inst.ea[1] >> 3) == ea_m_A_ind_pre);
                        } else if (ea == ea_bitnum) {
                            std::cout << "#" << ea_data_[i];
                            assert(i == 0 && inst.nea == 2);
                            maybe_add_bitnum_info(extra, static_cast<uint8_t>(ea_data_[i]), ea_addr_[1]);
                        } else if (ea == ea_disp) {
                            print_addr(ea_addr_[i].raw());
                        } else {
                            std::cout << "#" << static_cast<int>(static_cast<int8_t>(inst.data));
                        }
                        break;
                    }
                }
                if (!extra.str().empty()) {
                    fwl.flush(true);
                    std::cout << ";" << extra.str();
                }
                std::cout << "\n";
                pos += inst.ilen * 2;
            }
        }

#if 0
        std::cerr << "exec_base = $" << hexfmt(exec_base_) << "\n";
        for (const auto& l : library_bases_)
            std::cerr << l.first << ": $" << hexfmt(l.second) << "\n";
#endif
    }

    void add_rom_tag(const rom_tag& tag);
    void handle_whdload(uint32_t header_address);
    uint32_t handle_whd_patchlist(const uint32_t pl_start, bool analyze);

private:
    static constexpr uint32_t max_mem = 1 << 24;
    struct area {
        uint32_t beg;
        uint32_t end;
    };
    struct delayed_forced_value {
        uint32_t addr;
        regname reg;
        std::string label;
    };
    std::vector<bool> written_;
    std::vector<bool> data_handled_at_;
    std::vector<uint8_t> data_;
    std::vector<std::pair<uint32_t, simregs>> roots_;
    std::map<uint32_t, simregs> visited_;
    std::map<uint32_t, label_info> labels_;
    std::map<uint32_t, function_description> functions_;
    simregs regs_;
    std::vector<area> areas_;
    std::vector<std::pair<uint32_t, label_info>> predef_info_;
    std::vector<area> ignored_areas_;
    std::multimap<uint32_t, std::pair<regname, uint32_t>> forced_values_; 
    uint32_t exec_base_ = 0;
    std::map<std::string, uint32_t> library_bases_;
    std::vector<delayed_forced_value> delayed_forced_values_;
    std::vector<std::pair<uint32_t, std::string>> alias_or_forced_mem_;
    std::vector<std::unique_ptr<structure_definition>> struct_defs_;

    uint32_t fake_process_ = 0;

    uint32_t ea_data_[2];
    simval ea_addr_[2];
    simval ea_val_[2];
    std::map<uint32_t, uint32_t> saved_pointers_;
    static constexpr uint32_t alloc_start = 0xa00000;
    uint32_t alloc_top_ = alloc_start; // serve memory (from AllocMem) from here..

    static const area* find_area(const std::vector<area>& areas, uint32_t addr)
    {
        for (const auto& a : areas)
            if (addr >= a.beg && addr < a.end)
                return &a;
        return nullptr;
    }

    static void insert_area(std::vector<area>& areas, uint32_t beg, uint32_t end)
    {
        auto it = areas.begin();
        for (; it != areas.end(); ++it) {
            if (!(it->end <= beg || end <= it->beg))
                throw std::runtime_error { "Area overlap" };
            if (beg < it->beg)
                break;
        }
        areas.insert(it, { beg, end });
    }

    void process_roots()
    {
        while (!roots_.empty()) {
            const auto r = roots_.back();
            roots_.pop_back();
            regs_ = r.second;
            trace(r.first);
        }
    }

    std::map<uint32_t, label_info>::const_iterator maybe_print_label(uint32_t pos) const
    {
        auto it = labels_.find(pos);
        if (it != labels_.end()) {
            const bool extra = true; // verbose_disasm || !it->second.name.ends_with(hexstring(pos));
            if (extra)
                std::cout << std::setw(line_width) << std::left; 
            std::cout << it->second.name;
            if (extra)
                std::cout << ";" << hexfmt(it->first, 6);
            if (verbose_disasm)
                std::cout << " " << *it->second.t;
            std::cout << "\n";
        }
        return it;
    }

    void handle_array_range(uint32_t& pos, uint32_t end, uint32_t elemsize, bool is_ptr = false)
    {
        assert(!is_ptr || elemsize == 4);
        const uint32_t startpos = pos;
        assert(elemsize == 1 || elemsize == 2 || elemsize == 4);
        assert((end - startpos) % elemsize == 0);
        const char suffix = elemsize == 1 ? 'B' : elemsize == 2 ? 'W' : 'L';
        const uint32_t elem_per_line = elemsize == 1 ? 16 : elemsize == 2 ? 8 : 4;
        while (pos < end) {
            const auto here = std::min(elem_per_line, (end - pos) / elemsize);
            // Check for a run of similar elements
            uint32_t runlen = 1;
            uint32_t runend = pos + elemsize;
            while (runend < end) {
                if (memcmp(&data_[pos], &data_[runend], elemsize))
                    break;
                ++runlen;
                runend += elemsize;
            }

            if (runlen > elem_per_line || (runlen > 1 && pos == startpos && runend == end)) {
                const uint32_t val = elemsize == 1 ? data_[pos] : elemsize == 2 ? get_u16(&data_[pos]) : get_u32(&data_[pos]);
                std::cout << "\tDCB." << suffix << "\t$" << hexfmt(runlen, runlen < 256 ? 2 : runlen < 65536 ? 4 : 8) << ",";
                if (is_ptr)
                    print_addr(val);
                else
                    std::cout << "$" << hexfmt(val, 2 * elemsize);
                std::cout << "\n";
                pos += runlen * elemsize;
                continue;
            }

            std::cout << "\tDC." << suffix << "\t";
            for (uint32_t i = 0; i < here && pos < end; ++i) {
                if (i)
                    std::cout << ",";
                if (elemsize == 1)
                    std::cout << "$" << hexfmt(data_[pos]);
                else if (elemsize == 2)
                    std::cout << "$" << hexfmt(get_u16(&data_[pos]));
                else if (is_ptr)
                    print_addr(get_u32(&data_[pos]));
                else
                    std::cout << "$" << hexfmt(get_u32(&data_[pos]));
                pos += elemsize;
            }
            std::cout << "\n";
        }
    }

    void handle_char_array(uint32_t& pos, uint32_t next_pos, uint32_t len)
    {
        assert(len);
        bool in = false;
        uint32_t linepos = 0;
        auto end_quote = [&]() {
            if (in) {
                std::cout << '\'';
                in = false;
                linepos++;
            }
        };
        auto maybe_sep = [&]() {
            if (linepos > 16) {
                std::cout << ", ";
                linepos += 3;
            }
        };
        for (uint32_t i = 0; i < len && pos < next_pos; ++i, ++pos) {
            if (linepos == 0) {
                std::cout << "\tDC.B\t";
                linepos = 16;
                in = false;
            }
            const uint8_t c = data_[pos];
            if (c >= ' ' && c < 128 && c != '\'') {
                if (!in) {
                    maybe_sep();
                    std::cout << '\'';
                    in = true;
                    ++linepos;
                }
                std::cout << static_cast<char>(c);
                ++linepos;
            } else {
                end_quote();
                maybe_sep();
                std::cout << "$" << hexfmt(c);
                linepos += 3;
            }
            if (linepos + in >= 80) {
                end_quote();
                std::cout << "\n";
                linepos = 0;
            }
        }
        end_quote();
        if (linepos)
            std::cout << "\n";
    }

    void handle_jumptab_word(uint32_t& pos, uint32_t next_pos, uint32_t len)
    {
        const auto base = pos;
        for (; pos < next_pos && len; --len, pos += 2) {
            int16_t offset = get_u16(&data_[pos]);
            std::cout << "\tDC.W\t";
            if (offset == 0) {
                std::cout << "0";
            } else {
                print_addr(base + offset);
                std::cout << "-";
                print_addr(base);
            }
            std::cout << "\n";
        }
    }

    void handle_copper_code(uint32_t& pos, uint32_t next_pos, uint32_t len)
    {
        assert(len % 2 == 0);
        assert(pos + len * 2 <= next_pos);
        while (pos < next_pos && len) {
            const auto ir1 = get_u16(&data_[pos]);
            const auto ir2 = get_u16(&data_[pos + 2]);
            pos += 4;
            len -= 2;
            if (ir1 & 1) {
                // Wait/skip
                std::cout << "\tDC.W\t$" << hexfmt(ir1) << ",$" << hexfmt(ir2) << "\t; ";
                if (ir1 == 0xffff && ir2 == 0xfffe) {
                    std::cout << "End of copperlist\n";
                } else {
                    const auto vp = (ir1 >> 8) & 0xff;
                    const auto hp = ir1 & 0xfe;
                    const auto ve = 0x80 | ((ir2 >> 8) & 0x7f);
                    const auto he = ir2 & 0xfe;
                    std::cout << (ir2 & 1 ? "Skip if" : "Wait for") << " vpos >= $" << hexfmt(vp & ve, 2) << " and hpos >= $" << hexfmt(hp & he, 2) << " BFD " << !!(ir2 & 0x8000) << "\n";
                }
            } else if (ir1 >= 0x200) {
                // Not a valid register
                std::cout << "\tDC.W\t$" << hexfmt(ir1) << ",$" << hexfmt(ir2) << "\n";
            } else {
                std::ostringstream extra;
                maybe_add_custom_bit_info(extra, ir1, ir2);
                std::cout << "\tDC.W\t" << custom_regname[(ir1 >> 1) & 0xff] << ",$" << hexfmt(ir2);
                if (!extra.str().empty())
                    std::cout << "\t;" << extra.str();
                std::cout << "\n"; 
            }
        }
    }

    bool handle_typed_data(uint32_t& pos, uint32_t next_pos, const type& t, std::string name="")
    {
        switch (t.base()) {
        case base_data_type::unknown_:
        case base_data_type::code_:
            return false;
        case base_data_type::char_:
        case base_data_type::byte_:
            std::cout << "\tDC.B\t$" << hexfmt(data_[pos]) << "\n";
            ++pos;
            return true;
        case base_data_type::word_:
            std::cout << "\tDC.W\t$" << hexfmt(get_u16(&data_[pos])) << "\n";
            pos += 2;
            return true;
        case base_data_type::long_:
            std::cout << "\tDC.L\t$" << hexfmt(get_u32(&data_[pos])) << "\n";
            pos += 4;
            return true;
        case base_data_type::ptr_:
            if (t.len()) {
                if (t.ptr()->base() == base_data_type::char_) {
                    handle_char_array(pos, next_pos, t.len());
                } else if (t.ptr()->base() == base_data_type::copper_code_) {
                    handle_copper_code(pos, next_pos, t.len());
                } else if (t.ptr()->base() == base_data_type::jumptab_word_) {
                    handle_jumptab_word(pos, next_pos, t.len());
                } else if (t.ptr()->base() == base_data_type::patchitem_) {
                    const uint32_t end = handle_whd_patchlist(pos, false);
                    if (end > next_pos) {
                        std::cerr << "\tPATCHLIST OVERFLOWED end = $"  << hexfmt(end) << " next_pos = $" << hexfmt(next_pos) << "\n"; 
                    }
                    pos = end;
                    return true;
                } else {
                    const auto elem_size = sizeof_type(*t.ptr());
                    if (elem_size <= 4) {
                        handle_array_range(pos, std::min(pos + elem_size * t.len(), next_pos), elem_size, t.ptr()->ptr() != nullptr);
                    } else {
                        assert(!name.empty());
                        for (uint32_t i = 0; i < t.len() && pos + elem_size <= next_pos; ++i) {
                            handle_typed_data(pos, next_pos, *t.ptr(), name + "[" + std::to_string(i) + "]" );
                        }
                        assert(pos <= next_pos);
                    }
                }
            } else {
                std::cout << "\tDC.L\t";
                if (!print_addr_maybe(get_u32(&data_[pos])))
                    std::cout << "$" << hexfmt(get_u32(&data_[pos]));
                std::cout << "\n";
                pos += 4;
            }
            return true;
        case base_data_type::bptr_:
        case base_data_type::bstr_: {
            assert(!t.len());
            const auto addr = get_u32(&data_[pos]) * 4;
            pos += 4;
            std::cout << "\tDC.L\t$" << hexfmt(addr/4);
            if (addr) {
                std::cout << " ; points to ";
                print_addr(addr);
            }
            std::cout << "\n";
            return true;
        }
        case base_data_type::struct_: {
            auto end_pos = pos + t.struct_def()->size();
            if (pos == end_pos)
                return false;
            if (end_pos > next_pos) {
                //std::cerr << "WARNING: Structure size goes beyond next_pos!\n";
                //assert(!"TODO");
                end_pos = next_pos;
            }
            for (const auto& f : t.struct_def()->fields()) {
                if (f.offset() < 0)
                    continue;
                auto p = pos + f.offset();
                if (p >= end_pos)
                    break;
                const auto n = name + "." + f.name();
                if (verbose_disasm)
                    std::cout << "; " << std::left << std::setw(line_width) << n << " $" << hexfmt(p) << " " << f.t() << "\n";
                handle_typed_data(p, next_pos, f.t(), n);
            }
            pos = end_pos;
            return true;
        }
        case base_data_type::rptrw_: {
            int16_t offset = get_u16(&data_[pos]);
            std::cout << "\tDC.W\t";
            if (offset == 0) {
                std::cout << "0";
            } else {
                const auto base = t.rptr_base();
                print_addr(base + offset);
                std::cout << "-";
                print_addr(base);
            }
            std::cout << "\n";
            pos += 2;
            return true;
        }
        default:
            std::cerr << "TODO: Handle typed data with type=" << t << "\n";
            assert(!"TODO");
            break;
        }
        return false;
    }

    void handle_data_area(uint32_t pos, uint32_t end)
    {
        assert(pos < end && end <= max_mem);

        while (pos < end) {
            auto actual_end = end;
            auto next_pos = end;
            for (const auto& a : ignored_areas_) {
                if (pos <= a.end && a.beg <= end) {
                    actual_end = a.beg;
                    next_pos = a.end + 2;
                    break;
                }
            }
            handle_data_area_checked(pos, actual_end);
            if (next_pos != end)
                std::cout << "; Ignored area: $" << hexfmt(actual_end) << "-$" << hexfmt(next_pos-2) << "\n";
            pos = next_pos;
        }
    }

    void handle_data_area_checked(uint32_t pos, uint32_t end)
    {
        while (pos < end) {
            const auto next_label = labels_.upper_bound(pos);
            const auto next_pos = std::min(end, next_label == labels_.end() ? ~0U : next_label->first);

            if (auto it = maybe_print_label(pos); it != labels_.end()) {
                if (handle_typed_data(pos, next_pos, *it->second.t, it->second.name))
                    continue;
            } else {
                std::cout << "; $" << hexfmt(pos) << "\n";
            }

            // ALIGN
            if ((pos & 1) || next_pos == pos + 1) {
                std::cout << "\tDC.B\t$" << hexfmt(data_[pos]) << "\n";
                if (++pos == next_pos)
                    continue;
            }                

            handle_array_range(pos, next_pos & ~1, 2);
        }
    }

    bool print_addr_maybe(uint32_t addr)
    {
        if (auto [lp, offset] = find_label(addr); lp) {
            std::cout << lp->name;
            if (offset) {
                if (lp->t->base() == base_data_type::struct_) {
                    if (auto fn = lp->t->struct_def()->field_name(offset, false); fn) {
                        std::cout << "+" << *fn;
                        return true;
                    }
                }
                if (offset < 0) {
                    std::cout << "-";
                    offset = -offset;
                    assert(!"TODO: Check this");
                } else {
                    std::cout << "+";
                }
                std::cout << "$" << hexfmt(offset, offset < 0x10000 ? offset < 0x100 ? 2 :  4 : 8);                
            }
            return true;
        }

        //if (auto it = labels_.find(addr); it != labels_.end()) {
        //    std::cout << it->second.name;
        //    return true;
        //}

        if (addr == 4) {
            std::cout << "AbsExecBase";
            return true;
        }

        if (addr >= 0xA00000 && addr < 0xC00000) {
            // CIA-A is selected when A12=0, CIA-B is selcted when A13=0
            switch ((addr >> 12) & 3) {
            case 0: // Both!
                std::cout << "ciaboth";
                break;
            case 1: // CIAB
                std::cout << "ciab";
                break;
            case 2: // CIAA
                std::cout << "ciaa";
                break;
            case 3: // Niether!
                return false;
            }
            std::cout << "+" << cia_regname[(addr >> 8) & 0xf];
            return true;
        }

        if (addr == 0xdff000) {
            std::cout << "custom";
            return true;
        }
        if (addr >= 0xDE0000 && addr < 0xE00000) {
            std::string regname = custom_regname[(addr >> 1) & 0xff];
            std::cout << "custom+"<<regname;
            if (addr & 1)
                std::cout << "+1";
            return true;
        }

        return false;
    }

    void print_addr(uint32_t addr)
    {
        if (!print_addr_maybe(addr))
            std::cout << "$" << hexfmt(addr);
    }

    uint16_t read_iword(uint32_t addr)
    {
        if ((addr & 1) || addr < 0x80 || addr > max_mem - 2)
            throw std::runtime_error { "Reading instruction word from invalid address $" + hexstring(addr) };
        return get_u16(&data_[addr]);
    }

    void read_instruction(uint16_t* iwords, const uint32_t addr)
    {
        iwords[0] = read_iword(addr);
        const auto& inst = instructions[iwords[0]];
        for (uint8_t i = 1; i < inst.ilen; ++i) {
            iwords[i] = read_iword(addr + i * 2);
        }

        unsigned eaw = 1;

        // Reglist is always first
        uint16_t reglist = 0;
        if (inst.nea == 2 && (inst.ea[0] == ea_reglist || inst.ea[1] == ea_reglist))
            reglist = iwords[eaw++];

        ea_val_[0] = simval {};
        ea_addr_[0] = simval {};
        ea_data_[0] = 0;
        ea_val_[1] = simval {};
        ea_addr_[1] = simval {};
        ea_data_[1] = 0;

        for (int i = 0; i < inst.nea; ++i) {
            const auto ea = inst.ea[i];
            switch (ea >> ea_m_shift) {
            case ea_m_Dn:
                ea_val_[i] = regs_.d[ea & ea_xn_mask];
                break;
            case ea_m_An:
                ea_val_[i] = regs_.a[ea & ea_xn_mask];
                break;
            case ea_m_A_ind:
                ea_addr_[i] = regs_.a[ea & ea_xn_mask];
                ea_val_[i] = read_mem(ea_addr_[i]);
                break;
            case ea_m_A_ind_post:
                ea_addr_[i] = regs_.a[ea & ea_xn_mask];
                regs_.a[ea & ea_xn_mask] += simval { opsize_bytes(inst.size) };
                ea_val_[i] = read_mem(ea_addr_[i]);
                break;
            case ea_m_A_ind_pre:
                regs_.a[ea & ea_xn_mask] -= simval { opsize_bytes(inst.size) };
                ea_addr_[i] = regs_.a[ea & ea_xn_mask];
                ea_val_[i] = read_mem(ea_addr_[i]);
                break;
            case ea_m_A_ind_disp16: {
                assert(eaw < inst.ilen);
                int16_t n = iwords[eaw++];
                ea_data_[i] = static_cast<uint32_t>(n);
                auto areg = regs_.a[ea & ea_xn_mask];               
                if (areg.known()) {
                    ea_addr_[i] = simval { areg.raw() + n };
                    ea_val_[i] = read_mem(ea_addr_[i]);
                }
                break;
            }
            case ea_m_A_ind_index: {
                assert(eaw < inst.ilen);
                const auto extw = iwords[eaw++];
                ea_data_[i] = static_cast<uint32_t>(extw);
                // TODO: ea_val_
                break;
            }
            case ea_m_Other:
                switch (ea & ea_xn_mask) {
                case ea_other_abs_w:
                    ea_data_[i] = static_cast<uint32_t>(static_cast<int16_t>(iwords[eaw++]));
                    ea_addr_[i] = simval { ea_data_[i] };
                    ea_val_[i] = read_mem(ea_addr_[i]);
                    break;
                case ea_other_abs_l:
                    ea_data_[i] = iwords[eaw] << 16 | iwords[eaw + 1];
                    ea_addr_[i] = simval { ea_data_[i] };
                    ea_val_[i] = read_mem(ea_addr_[i]);
                    eaw += 2;
                    break;
                case ea_other_pc_disp16: {
                    assert(eaw < inst.ilen);
                    int16_t n = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(n);
                    ea_addr_[i] = simval { addr + (eaw - 1) * 2 + n };
                    ea_val_[i] = read_mem(ea_addr_[i]);
                    break;
                }
                case ea_other_pc_index: {
                    assert(eaw < inst.ilen);
                    assert(eaw == 1); // Will be printed wrong since we don't know the address
                    const auto extw = iwords[eaw++];
                    const auto disp = static_cast<int8_t>(extw & 0xff);
                    ea_data_[i] = static_cast<uint32_t>(extw);
                    add_auto_label(addr + (eaw - 1) * 2 + disp, unknown_type);
                    // TODO: ea_val_
                    // TODO: ea_addr_
                    break;
                }
                case ea_other_imm:
                    if (inst.size == opsize::l) {
                        assert(eaw + 1 < inst.ilen);
                        ea_data_[i] = iwords[eaw] << 16 | iwords[eaw + 1];
                        eaw += 2;
                    } else {
                        assert(eaw < inst.ilen);
                        if (inst.size == opsize::b)
                            ea_data_[i] = static_cast<uint8_t>(iwords[eaw++]);
                        else
                            ea_data_[i] = iwords[eaw++];
                    }
                    ea_val_[i] = simval { ea_data_[i] };
                    break;
                default:
                    throw std::runtime_error { "TODO: " + ea_string(ea) };
                }
                break;
            default:
                if (ea == ea_sr || ea == ea_ccr || ea == ea_usp) {
                    // TODO
                    break;
                } else if (ea == ea_reglist) {
                    assert(inst.nea == 2);
                    ea_data_[i] = reglist;
                    break;
                } else if (ea == ea_bitnum) {
                    assert(eaw < inst.ilen);
                    uint16_t b = iwords[eaw++];
                    if (inst.size == opsize::b)
                        b &= 7;
                    else
                        b &= 31;
                    ea_data_[i] = b;
                    break;
                }

                if (inst.extra & extra_disp_flag) {
                    assert(ea == ea_disp);
                    assert(eaw < inst.ilen);
                    const auto disp = static_cast<int16_t>(iwords[eaw++]);
                    ea_data_[i] = static_cast<uint32_t>(disp);
                    ea_addr_[i] = simval { addr + 2 + disp };
                } else if (ea == ea_disp) {
                    ea_data_[i] = static_cast<int8_t>(inst.data);
                    ea_addr_[i] = simval { addr + 2 + static_cast<int8_t>(inst.data) };
                } else {
                }
                break;
            }
        }
        assert(eaw == inst.ilen);
    }

    void print_sim_regs()
    {
        for (uint32_t i = 0; i < 16; ++i) {
            if (i == 8)
                std::cout << "\n";
            else if (i)
                std::cout << " ";
            std::cout << (i & 8 ? "A" : "D") << (i & 7) << "=" << std::setw(9) << std::left << (i < 8 ? regs_.d[i & 7] : regs_.a[i & 7]);
        }
        std::cout << "\n";
    }
    
    std::optional<std::string> try_read_string_lower(const simval& addr)
    {
        if (!addr.known())
            return {};
        auto ptr = addr.raw();
        if (ptr >= max_mem || !written_[ptr])
            return {};
        std::string libname;
        while (ptr < max_mem && written_[ptr] && data_[ptr]) {
            auto c = data_[ptr++];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            libname.push_back(c);
        }
        return { libname };
    }

    void do_open_library()
    {
        auto libname = try_read_string_lower(regs_.a[1]);
        regs_.d[0] = simval {};
        if (!libname)
            return;
        if (auto it = library_bases_.find(*libname); it != library_bases_.end()) {
            regs_.d[0] = simval { it->second };
            return;
        }
        std::cerr << "Library not found: \"" << *libname << "\"\n";
    }

    void do_find_resident()
    {
        auto resname = try_read_string_lower(regs_.a[1]);
        regs_.d[0] = simval {};
        if (!resname)
            return;
        if (auto it = library_bases_.find(*resname); it != library_bases_.end()) {
            regs_.d[0] = simval { it->second };
            return;
        }
        std::cerr << "Resident not found: \"" << *resname << "\"\n";
    }

    void do_alloc_mem()
    {
        if (regs_.d[0].known()) {
            const auto size = regs_.d[0].raw();
            if (const uint32_t ptr = alloc_fake_mem(size)) {
                regs_.d[0] = simval {ptr};
                return;
            }
            else
                std::cerr << "AllocMem failed for size: $" << hexfmt(size) << " alloc_top_=$" << hexfmt(alloc_top_) << "\n";
        }
        regs_.d[0] = simval {};
    }

    void do_init_struct(uint32_t init_table, uint32_t mem, uint32_t /*size*/)
    {
        const auto table_start = init_table;
        // Note: seems to always be kept aligned despite description...
        for (;;) {
            if (init_table + 3 >= max_mem || !written_[init_table])
                return;
            const auto cmd = data_[init_table++];
            if (!cmd) {
                if (!data_[init_table])
                    ++init_table;
                break;
            }

            /*
    ddssnnnn
        dd  the destination type (and size):
            00  no offset, use next destination, nnnn is count
            01  no offset, use next destination, nnnn is repeat
            10  destination offset is in the next byte, nnnn is count
            11  destination offset is in the next 24-bits, nnnn is count
        ss  the size and location of the source:
            00  long, from the next two aligned words
            01  word, from the next aligned word
            10  byte, from the next byte
            11  ERROR - will cause an ALERT (see below)
      nnnn  the count or repeat:
         count  the (number+1) of source items to copy
        repeat  the source is copied (number+1) times.
*/

            const auto dd  = cmd >> 6;
            const auto ss  = (cmd >> 4) & 3;
            const auto rep = (cmd & 15) + 1;

            uint32_t offset = 0, val = 0;
            opsize sz {};
            switch (dd) {
            case 0b00:
            case 0b01:
                goto unsupported;
            case 0b10:
                offset = data_[init_table++];
                break;
            case 0b11:
                offset = data_[init_table++] << 16;
                offset |= data_[init_table++] << 8;
                offset |= data_[init_table++];
                break;
            }
            switch (ss) {
            case 0b00:
                if (init_table & 1)
                    ++init_table;
                val = get_u32(&data_[init_table]);
                init_table += 4;
                sz = opsize::l;
                break;
            case 0b01:
                if (init_table & 1)
                    ++init_table;
                val = get_u16(&data_[init_table]);
                init_table += 2;
                sz = opsize::w;
                break;
            case 0b10:
                val = data_[init_table++];
                ++init_table; // Seems to always be kept aligned?
                sz = opsize::b;
                break;
            case 0b11:
                goto unsupported;
            }

            if (0) {
            unsupported:
                std::cerr << "TODO: Init struct cmd=$" << hexfmt(cmd) << " dd=" << (int)dd << " ss=" << (int)dd << " rep=" << (int)rep << "\n";
                throw std::runtime_error { "TODO" };
            }

            for (int i = 0; i < rep; ++i) {
                update_mem(sz, mem + offset + i * opsize_bytes(sz), simval { val });
            }
        }
        const auto sz = init_table - table_start;
        const bool w = !(table_start & 1) && !(sz & 1);
        add_auto_label(table_start, make_array_type(w ? word_type : byte_type, w ? sz >> 1 : sz), "inittab");
    }

    void do_make_library()
    {
        //std::cerr << "TODO: Handle MakeLibrary\n";
        //std::cerr << "\tvectors = " << regs_.a[0] << "\n";
        //std::cerr << "\tstructure = " << regs_.a[1] << "\n";
        //std::cerr << "\tinit = " << regs_.a[2] << "\n";
        //std::cerr << "\tdSize = " << regs_.d[0] << "\n";
        //std::cerr << "\tsegList = " << regs_.d[1] << "\n";

        std::vector<uint32_t> vectors;
        if (regs_.a[0].known() && pointer_ok(regs_.a[0].raw())) {
            const auto vecbase = regs_.a[0].raw();
            if (get_u16(&data_[vecbase]) == 0xffff) {
                for (uint32_t a = vecbase + 2;; a += 2) {
                    if (!pointer_ok(a))
                        break;
                    const auto ofs = get_u16(&data_[a]);
                    if (ofs == 0xffff)
                        break;
                    vectors.push_back(static_cast<int16_t>(ofs) + vecbase);
                }
                add_auto_label(vecbase, make_array_type(word_type, 2 + static_cast<uint32_t>(vectors.size())), "libvecofs");
            } else {
                for (uint32_t a = vecbase; pointer_ok(a); a += 4) {
                    const auto v = get_u32(&data_[a]);
                    if (v == 0xffffffff)
                        break;
                    vectors.push_back(v);
                }
                add_auto_label(vecbase, make_array_type(code_ptr, 1 + static_cast<uint32_t>(vectors.size())), "libvecs");
            }
        } else {
            vectors.resize(256); // Fake number
        }

        const auto data_size = regs_.d[0].known() ? regs_.d[0].raw() : 256; // Fake number if unknown
        const auto neg_size = static_cast<uint32_t>(vectors.size()) * 6;
        
        auto lib_ptr = alloc_fake_mem(data_size + neg_size);
        if (lib_ptr)
            lib_ptr += neg_size;
        // TODO: points to library base
        simregs lib_init_regs {};
        if (lib_ptr)
            lib_init_regs.a[6] = simval { lib_ptr };
        auto fptr = lib_ptr;
        //int i = 0;
        for (const auto v : vectors) {
            if (pointer_ok(v)) {
                add_root(v, lib_init_regs);
            }
            //std::cerr << "Vector " << (-6 * (i + 1)) << " $" << hexfmt(v) << "\n";
            //++i;
            if (fptr) {
                fptr -= 6;
                update_mem(opsize::w, fptr, simval { JMP_ABS_L_instruction }); // JMP abs.l (not currently written)
                update_mem(opsize::l, fptr + 2, simval { v });
            }
        }

        if (lib_ptr) {
            if (regs_.a[1].known() && pointer_ok(regs_.a[1].raw())) {
                do_init_struct(regs_.a[1].raw(), lib_ptr, data_size);
            }

            regs_.d[0] = simval { lib_ptr };
            add_auto_label(lib_ptr, make_struct_type(Library), "lib");
        } else {
            regs_.d[0] = simval {};
        }

        if (regs_.a[2].known() && pointer_ok(regs_.a[2].raw())) {
            //init -  If non-NULL, an entry point that will be called before adding
            //the library to the system.  Registers are as follows:
            //        d0 = libAddr    ;Your Library Address
            //        a0 = segList    ;Your AmigaDOS segment list
            //        a6 = ExecBase  ;Address of exec.library
            simregs init_regs = regs_; // d0 handled above
            init_regs.a[0] = regs_.d[1];
            init_regs.a[6] = simval { exec_base_ };
            add_root(regs_.a[2].raw(), init_regs);
        }
    }

    void do_make_functions()
    {
        // A0 = target, A1 = functionArray, A2 = funcDispBase

        if (!regs_.a[0].known() || !regs_.a[1].known() || !regs_.a[2].known())
            return;

        const auto target = regs_.a[0].raw();
        const auto functionArray = regs_.a[1].raw();
        const auto funcDispBase = regs_.a[2].raw();

        uint32_t dest = target - 6;
        uint32_t array = functionArray;

        // Assume target is library base
        simregs regs{};
        regs.a[6] = simval { target };

        if (funcDispBase == 0) {
            throw std::runtime_error {"TODO: MakeFunctions with funcDispBase == 0"};
        } else {
            // Word displacement
            for (;pointer_ok(array); array += 2, dest -= 6) { 
                const int16_t disp = get_u16(&data_[array]);
                if (disp == -1)
                    break;
                const uint32_t addr = funcDispBase + disp;
                add_root(addr, regs);
                update_mem(opsize::w, target, simval { JMP_ABS_L_instruction }); // JMP abs.l (not currently written)
                update_mem(opsize::l, target + 2, simval { addr });
            }
            add_auto_label(functionArray, make_array_type(word_type, 1 + (array - functionArray) / 2), "funcarray");
        }
    }

    void do_add_int()
    {
        if (!regs_.a[1].known())
            return;
        const auto data = read_mem(simval{regs_.a[1].raw() + 0x0e });
        const auto code = read_mem(simval{regs_.a[1].raw() + 0x12 });
        if (code.known()) {
            simregs r {};
            /*
            D0 - scratch
            D1 - scratch (on entry: active interrupts -> equals INTENA & INTREQ)
            A0 - scratch (on entry: pointer to base of custom chips for fast indexing)
            A1 - scratch (on entry: Interrupt's IS_DATA pointer)
            A5 - jump vector register (scratch on call)
            A6 - Exec library base pointer (scratch on call)
            */
            r.a[0] = simval { 0xdff000 };
            r.a[1] = data; 
            r.a[6] = simval { exec_base_ };
            add_root(code.raw(), r);
        }
    }

    simval& reg_from_name(simregs& regs, regname reg)
    {
        assert(static_cast<uint32_t>(reg) < 16);
        return (reg >= regname::A0 ? regs.a : regs.d)[static_cast<uint8_t>(reg) & 7];
    }

    void do_branch(uint32_t addr, bool is_func)
    {
        auto it = functions_.find(addr);
        if (it == functions_.end()) {
            add_root(addr, regs_, false, is_func);
            if (is_func) {
                regs_.a[0] = simval {};
                regs_.a[1] = simval {};
                regs_.d[0] = simval {};
                regs_.d[1] = simval {};
            }
            return;
        }
        //std::cerr << "Found function at $" << hexfmt(addr) << "\n";
        const auto& fd = it->second;
        for (const auto& i : fd.input()) {
            const auto& reg = reg_from_name(regs_, i.reg);
            //std::cerr << i.reg << " = " << reg << " -> type " << *i.t << "\n";
            if (auto pt = i.t->ptr()) {
                if (reg.known()) {
                    if (pt == &code_type) {
                        add_root(reg.raw(), regs_);
                    } else if (pt == &char_type) {
                        maybe_find_string_at(reg.raw());
                    } else if (pt->struct_def()) {
                        //handle_struct_at(reg.raw(), *pt->struct_def());
                        if (auto lit = labels_.find(reg.raw()); lit != labels_.end()) {
                            if (!lit->second.t->ptr() && !lit->second.t->struct_def()) {
                                std::cerr << "Warning: Overriding type of " << lit->second.name << " previous type " << *lit->second.t << " new type " << *pt << "\n";
                                lit->second.t = pt;
                            } else if (lit->second.t != pt) {
                                std::cerr << "Warning: Type conflict for " << lit->second.name << " previous type " << *lit->second.t << " new type " << *pt << "\n";
                            }
                        } else {
                            add_auto_label(reg.raw(), *pt, pt->struct_def()->name());
                        }
                    }
                }
            }
        }
        if (fd.sim()) {
            fd.sim()();
        } else if (!fd.output().empty()) {
            throw std::runtime_error { "TODO: update regs not implemented in do_branch" };
        } else {
            // Kill standard registers
            regs_.a[0] = simval {};
            regs_.a[1] = simval {};
            regs_.d[0] = simval {};
            regs_.d[1] = simval {};
        }
    }

    void do_jump_table(uint32_t addr, uint32_t len)
    {
        for (uint32_t i = 0; i < len; ++i) {
            const uint32_t word_addr = addr + 2 * i;
            if (word_addr + 1 < max_mem && written_[addr]) {
                int16_t offset = get_u16(&data_[word_addr]);
                if (!offset)
                    continue;
                do_branch(addr + offset, true);
            }
        }
    }

    void check_jump_table(uint32_t addr)
    {
        if (addr + 2 >= max_mem || !written_[addr])
            return;

        auto it = labels_.find(addr);
        if (it == labels_.end()) {
            return;
        }
        auto& t = *it->second.t;
        if (t.base() == base_data_type::ptr_ && t.ptr()->base() == base_data_type::jumptab_word_ && t.len()) {
            do_jump_table(addr, t.len());
            return;
        }
        if (!it->second.name.starts_with("dat"))
            return;

        // TODO: Consider that it doesn't have to be an indirect table
        static std::map<uint32_t, bool> informed;
        if (!informed[addr]) {
            uint32_t next_addr = max_mem;
            if (auto next = it; ++next != labels_.end())
                next_addr = next->first;            

            uint32_t a = addr;
            for (; a < next_addr && written_[a]; a += 2) {
                int16_t ofs = get_u16(&data_[a]);
                if (ofs & 1)
                    break;
                if (ofs == 0)
                    continue; // Allow null entries
                uint32_t dest = addr + ofs;
                if (dest >= max_mem || !written_[dest])
                    break;
                if (instructions[get_u16(&data_[dest])].type == inst_type::ILLEGAL)
                    break;
                // Heuristic end
                if (abs(ofs) > 1040)
                    break;
            }
            const uint32_t size = (a - addr) / 2;
            if (size) {
                add_auto_label(addr, make_array_type(jumptab_word_type, size), "jumptab");
                do_jump_table(addr, size);
                std::cerr << "Added jump table at $" << hexfmt(addr) << " (JTABW[" << size << "])\n";
            }
            informed[addr] = true;
        }
    }

    void trace(uint32_t addr)
    {
        for (;;) {
            const auto start = addr;

            if (start >= max_mem || !written_[start])
                return;

            uint16_t iwords[max_instruction_words];
            read_instruction(iwords, addr);
            const auto& inst = instructions[iwords[0]];
            addr += 2 * inst.ilen;

            auto forced = forced_values_.equal_range(start);
            for (auto fit = forced.first; fit != forced.second; ++fit) {
                reg_from_name(regs_, fit->second.first) = simval { fit->second.second };
            }

            #if 0
            if (1 /*start >= 0x00fe9174 && start <= 0x00fe9272*/) {
                print_sim_regs();
                disasm(std::cout, start, iwords, inst.ilen);
                std::cout << "\n";
            }
            #endif

            if (visited_.find(start) == visited_.end()) {
                // Want registers before update
                // TODO: Combine register values (or something)
                visited_[start] = regs_;
            }

            switch (inst.type) {
            case inst_type::Bcc:
            case inst_type::BSR:
            case inst_type::BRA: {
                assert(inst.nea == 1 && inst.ea[0] == ea_disp);
                do_branch(ea_addr_[0].raw(), inst.type == inst_type::BSR);
                if (inst.type == inst_type::BRA)
                    return;
                break;
            }
            case inst_type::DBcc:
                assert(inst.nea == 2 && inst.ea[1] == ea_disp && inst.ilen == 2);
                // don't use do_branch here
                add_root(ea_addr_[1].raw(), regs_, false, false);
                break;

            case inst_type::JSR:
            case inst_type::JMP:
                assert(inst.nea == 1);
                if (ea_addr_[0].known())
                    do_branch(ea_addr_[0].raw(), true);
                else if((inst.ea[0] >> ea_m_shift) == ea_m_A_ind_index && regs_.a[inst.ea[0] & 7].known())
                    check_jump_table(regs_.a[inst.ea[0] & 7].raw());
                else if (inst.ea[0] == ea_pc_index)
                    check_jump_table(start + 2 + static_cast<int8_t>(ea_data_[0] & 255));
                if (inst.type == inst_type::JMP)
                    return;
                break;

            case inst_type::ILLEGAL:
                // Support skipping MOVEC
                if (iwords[0] == movec_instruction_dr0_num || iwords[0] == movec_instruction_dr1_num) {
                    addr += 2;
                    continue;
                }
                [[fallthrough]];
            case inst_type::RTS:
            case inst_type::RTE:
            case inst_type::RTR:
                return;

            case inst_type::EXG: {
                auto get_reg = [&](uint8_t ea) -> simval& {
                    assert(ea < 16);
                    return (ea < 8 ? regs_.d : regs_.a)[ea & 7];
                };
                std::swap(get_reg(inst.ea[0]), get_reg(inst.ea[1]));
            }
            break;

            default:
                for (int i = 0; i < inst.nea; ++i) {
                    if (!ea_addr_[i].known())
                        continue;
                    const auto ea_addr = ea_addr_[i].raw() & 0xffffff;
                    if (ea_addr >= written_.size() || !written_[ea_addr])
                        continue;

                    const auto& t = inst.type == inst_type::LEA ? unknown_type : type_from_size(inst.size);

                    if (ea_addr < 0x400) {
                        if (inst.type == inst_type::PEA)
                            continue;
                        if (inst.size != opsize::l)
                            continue;
                        if (ea_addr & 3)
                            continue;
                    }

                    switch (inst.ea[i] >> ea_m_shift) {
                    case ea_m_Dn:
                        break;
                    case ea_m_An:
                    case ea_m_A_ind:
                    case ea_m_A_ind_post:
                    case ea_m_A_ind_pre:
                    case ea_m_A_ind_disp16:
                    case ea_m_A_ind_index:
                        add_auto_label(ea_addr, t);
                        break;
                    case ea_m_Other:
                        switch (inst.ea[i] & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l:
                        case ea_other_pc_disp16:
                        case ea_other_pc_index:
                            add_auto_label(ea_addr, t);
                            break;
                        case ea_other_imm:
                            break;
                        default:
                            throw std::runtime_error { "Unsupported EA " + ea_string(inst.ea[i]) };
                        }
                        [[fallthrough]];
                    default:
                        if (inst.ea[i] == ea_disp)
                            throw std::runtime_error { "Unexpected EA " + ea_string(inst.ea[i]) + " for " + inst.name };
                    }
                }
                sim_inst(inst);
            }
        }
    }

    simval read_mem(const simval& addr)
    {
        if (!addr.known())
            return simval {};
        const auto a = addr.raw();
        if (a < 0xa00000) {
            //std::cerr << "Tring to restore from $" << hexfmt(a) << "\n";
            if (auto it = saved_pointers_.find(a); it != saved_pointers_.end())
                return simval { it->second };
            return written_[a] ? simval { get_u32(&data_[a]) } : simval {};
        }
        return simval {};
    }

    void update_mem(opsize size, uint32_t addr, const simval& val)
    {
        if (!val.known())
            return;

        const auto rv = val.raw();
        
        //std::cerr << "Update $" << hexfmt(addr) << "." << (size == opsize::l ? "L" : size == opsize::w ? "W" : "B") << " to $" << hexfmt(val.raw(), 2*opsize_bytes(size)) << "\n";
        if (size == opsize::l && !(addr & 3) && addr < /*0x400*/interrupts_end) {
            add_auto_label(addr, code_ptr, "interrupthandler");
            add_root(rv, simregs {});
            return;
        }

        // COP1LCH/COP2LCH (only handle normal addresses for now, not mirrored ones)
        if (size == opsize::l && (addr == 0xDFF080 || addr == 0xDFF084)) {
            if (rv && !find_label(rv).first) // Don't override other labels
                maybe_find_copper_list_at(rv);
            return;
        }

        if (size == opsize::l && !(addr & 1) && addr < 0xa00000) {
            //std::cerr << "Saving at $" << hexfmt(addr) << ": $" << hexfmt(rv) << "\n";
            saved_pointers_.insert({ addr, rv });
            if (auto lit = labels_.find(addr); lit != labels_.end() && lit->second.t == &code_ptr) {
                //throw std::runtime_error { "TODO: CodePtr at " + hexstring(addr) + " updated to " + hexstring(val.raw()) };
                add_root(rv, simregs {});
            }
        }

        //throw std::runtime_error { "TODO" };
    }

    void update_ea(opsize size, uint8_t idx, uint8_t ea, const simval& val)
    {
        // TODO
        if (size != opsize::l)
            return;

        switch (ea >> ea_m_shift) {
        case ea_m_Dn:
            regs_.d[ea & ea_xn_mask] = val;
            return;
        case ea_m_An:
            regs_.a[ea & ea_xn_mask] = val;
            return;
        case ea_m_A_ind:
        case ea_m_A_ind_post:
        case ea_m_A_ind_disp16:
        case ea_m_A_ind_index:
            if (ea_addr_[idx].known()) {
                update_mem(size, ea_addr_[idx].raw(), val);
                if (size == opsize::l && val.known()) {
                    const auto a = regs_.a[ea & ea_xn_mask].raw();
                    if (auto lit = labels_.find(a); lit != labels_.end() && lit->second.t->struct_def()) {
                        const int32_t struct_offset = ea_addr_[idx].raw() - a;
                        if (const auto [field, field_offset] = lit->second.t->struct_def()->field_at(struct_offset); field) {
                            if (field_offset == 0 && &field->t() == &code_ptr) {
                                //std::cerr << "code_ptr updated for " << lit->second.name << " " << *lit->second.t->struct_def()->field_name(struct_offset, false) << " to $" << hexfmt(val.raw()) << "\n";
                                add_root(val.raw(), simregs {});
                            } else if (struct_offset < 0 && field_offset == 2) {
                                // Patching library vector
                                //std::cerr << "code_ptr updated for library vector " << lit->second.name << " " << *lit->second.t->struct_def()->field_name(struct_offset, false) << " to $" << hexfmt(val.raw()) << "\n";
                                add_root(val.raw(), simregs {});
                            }
                        }
                    }
                }
            }
            break;
        //case ea_m_A_ind_pre:
            // TODO
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                if (ea_addr_[idx].known())
                    update_mem(size, ea_addr_[idx].raw(), val);
                break;
            }
        }
    }

    simval* reg_from_ea(uint8_t ea)
    {
        if (ea >> ea_m_shift == ea_m_Dn)
            return &regs_.d[ea & ea_xn_mask];
        else if (ea >> ea_m_shift == ea_m_An)
            return &regs_.a[ea & ea_xn_mask];
        return nullptr;
    }

    void sim_inst(const instruction& inst)
    {
        switch (inst.type) {
        case inst_type::ADD:
        case inst_type::ADDA:
            if (inst.size == opsize::l) {
                if (auto reg = reg_from_ea(inst.ea[1])) {
                    *reg += ea_val_[0];
                    break;
                }
            }
            break;
        case inst_type::LEA:
            update_ea(opsize::l, 1, inst.ea[1], ea_addr_[0]);
            break;
        case inst_type::LINK: {
            // Allocate a stack frame and make it unqiue
            auto& areg = regs_.a[inst.ea[0] & ea_xn_mask];
            areg = simval {};
            if (const auto amm = -static_cast<int16_t>(ea_data_[1]); amm > 0) {
                if (const auto ptr = alloc_fake_mem(amm)) {
                    areg = simval { ptr };
                }
            }
            break;
        }
        case inst_type::MOVE:
            update_ea(inst.size, 1, inst.ea[1], ea_val_[0]);
            break;
        case inst_type::MOVEA: {
            auto val = ea_val_[0].known() ? simval { static_cast<uint32_t>(sext(ea_val_[0].raw(), inst.size)) } : simval {};
            if (inst.ea[0] == ea_immediate && val.raw() < max_mem && written_[val.raw()])
                add_auto_label(ea_val_[0].raw(), unknown_type);
            update_ea(opsize::l, 1, inst.ea[1], val);
            break;
        }
        case inst_type::MOVEQ: {
            assert(inst.ea[0] == ea_data8);
            assert(inst.ea[1] < 8);
            regs_.d[inst.ea[1]] = simval { static_cast<uint32_t>(sext(inst.data, opsize::b)) };
            break;
        }
        case inst_type::SUB:
        case inst_type::SUBA:
            if (inst.size == opsize::l) {
                if (auto reg = reg_from_ea(inst.ea[1])) {
                    // SUB r, r => r = 0 idiom
                    if (auto reg2 = reg_from_ea(inst.ea[0]); reg2 == reg)
                        *reg = simval{0};
                    else
                        *reg -= ea_val_[0];
                    break;
                }
            }
            break;
        default:
            break;
        }
    }

    bool pointer_ok(uint32_t addr)
    {
        return addr && !(addr & 1) && addr < max_mem - 3 && written_[addr];
    }

    uint32_t try_read_pointer(uint32_t addr)
    {
        if (!pointer_ok(addr))
            return 0;
        const auto p = get_u32(&data_[addr]);
        return pointer_ok(p) ? p : 0;
    }

    void handle_struct_at(uint32_t addr, const structure_definition& s);
    void maybe_find_string_at(uint32_t addr, const std::string& label="");
    void maybe_find_copper_list_at(uint32_t addr);
    void handle_data_at(uint32_t addr, const type& t);
    void handle_pointer_to(uint32_t addr, const type& t);
    void handle_bseglist_at(uint32_t addr, const std::string& name);
    void handle_predef_info();
    uint32_t check_exec_base();
};

void analyzer::add_rom_tag(const rom_tag& tag)
{
    std::string name;
    for (char c : tag.name) {
        if (isalpha(c) || (!name.empty() && isdigit(c)))
            name.push_back(c);
        else
            name.push_back('_');
    }

    auto slen = [&](uint32_t addr) {
        uint32_t l = 0;
        while (addr + 1 < data_.size() && data_[addr++])
            ++l;
        return l + 1; // Include nul terminator
    };

    add_label(tag.matchtag, name + "_matchword", word_type);
    add_label(tag.matchtag + 2, name + "_matchtag", unknown_ptr); // Pointer to struct Resident
    add_label(tag.matchtag + 6, name + "_endskip", unknown_ptr);
    add_label(tag.matchtag + 10, name + "_flags", byte_type);
    add_label(tag.matchtag + 11, name + "_version", byte_type);
    add_label(tag.matchtag + 12, name + "_type", byte_type);
    add_label(tag.matchtag + 13, name + "_priority", byte_type);
    add_label(tag.matchtag + 14, name + "_name_addr", char_ptr);
    add_label(tag.matchtag + 18, name + "_id_addr", char_ptr);
    add_label(tag.matchtag + 22, name + "_init_addr", code_ptr);

    add_label(tag.name_addr, name + "_name", make_array_type(char_type, slen(tag.name_addr)));
    add_label(tag.id_addr, name + "_id", make_array_type(char_type, slen(tag.id_addr)));
    if (tag.init_addr) {
        if (!(tag.flags & 0x80)) { // Not RTF_AUTOINIT
            simregs rom_tag_init_regs = {};
            // InitResident calls the RT_INIT function with these arguments:
            rom_tag_init_regs.d[0] = simval { 0 }; // D0 = 0
            rom_tag_init_regs.a[0] = simval { 0 }; // A0 = segList (NULL for ROM modules)
            rom_tag_init_regs.a[6] = simval { fake_exec_base() }; // A6 = ExecBase

            add_label(tag.init_addr, name + "_init", code_type);
            add_root(tag.init_addr, rom_tag_init_regs);
        } else {
            add_label(tag.init_addr, name + "_init_dsize", long_type);
            add_label(tag.init_addr + 4, name + "_init_functable", unknown_ptr);
            add_label(tag.init_addr + 8, name + "_init_datatable", unknown_ptr);
            add_label(tag.init_addr + 12, name + "_init_routine", code_ptr);
            handle_rtf_autoinit(tag.init_addr);
        }
    }
}

void analyzer::handle_struct_at(uint32_t addr, const structure_definition& s)
{
    if (!pointer_ok(addr))
        return;
    
    if (&s == &Node && !data_handled_at_[addr] && !find_label(addr).first) {
        switch (data_[addr + 0x08]) { // ln_Type
        case NT_UNKNOWN:
            break;
        case NT_TASK:
            handle_struct_at(addr, Task);
            return;
        case NT_INTERRUPT:
            handle_struct_at(addr, Interrupt);
            return;
        case NT_DEVICE:
        case NT_RESOURCE:
        case NT_LIBRARY: {
            handle_struct_at(addr, Library);
            const auto neg_size = get_u16(&data_[addr + 0x10]); // lib_NegSize
            if (neg_size % 6)
                return;

            simregs r {};
            r.a[6] = simval { addr };
            auto n = "lib" + hexstring(addr) + "_Func";
            for (int i = 0; i < neg_size / 6; ++i) {
                const auto faddr = addr - (i + 1) * 6;

                if (get_u16(&data_[faddr]) == JMP_ABS_L_instruction) {
                    const auto impl_addr = get_u32(&data_[faddr + 2]);
                    add_label(impl_addr, n + std::to_string(i), code_type);
                    add_root(impl_addr, r);
                }

                add_label(faddr, n + std::to_string(i) + "Vec", code_type);
                add_root(faddr, r);
            }
            return;
        }
        case NT_MEMORY: // MemList
            break;
        case NT_PROCESS:
            handle_struct_at(addr, Process);
            return;
        case NT_SIGNALSEM:
            handle_struct_at(addr, SignalSemaphore);
            return;
        default:
            std::cerr << "Unhandled node type " << static_cast<int>(data_[addr + 0x08]) << " at $" << hexfmt(addr) << "\n";
        }
    }

    add_auto_label(addr, make_struct_type(s), s.name());
    for (const auto& f : s.fields()) {
        const auto faddr = addr + f.offset();
        if (f.offset() < 0) {
            if (&f.t() != &libvec_code)
                continue;
            if (faddr < 256 * 4 || faddr + 6 > max_mem)
                continue;
            simregs r{};
            r.a[6] = simval{addr};
            auto fn = std::string { f.name() };
            if (fn.compare(0, 4, "_LVO") == 0)
                fn.erase(1, 3);

            if (get_u16(&data_[faddr]) == JMP_ABS_L_instruction) {
                const auto impl_addr = get_u32(&data_[faddr + 2]);
                add_label(impl_addr, std::string { s.name() } + fn, code_type);
                add_root(impl_addr, r);
            }

            add_label(faddr, std::string { s.name() } + fn + "Vec", code_type);
            add_root(faddr, r);
            continue;
        }

        handle_data_at(faddr, f.t());
    }
}

void analyzer::maybe_find_string_at(uint32_t addr, const std::string& label)
{
    if (addr >= max_mem || !written_[addr])
        return;

    const auto start_addr = addr;

     // Arbitrary limits
    constexpr uint32_t max_len = 4096;
    uint32_t len = 0, nprint = 0;
    while (addr < max_mem && written_[addr]) {
        if (len > max_len)
            return;
        const auto c = data_[addr];
        if (!c) {
            ++addr;
            ++len;
            // Fold extra NUL into string if present (even after dc.b)
            if (addr + 1 < max_mem && written_[addr + 1] && !data_[addr]) {
                ++addr;
                ++len;            
            }
            break;
        }
        if (!isprint(c))
            ++nprint;
        ++len;
        ++addr;
    }

    // Too many non-printable characters?
    if (nprint * 10 > len)
        return;

    std::fill(data_handled_at_.begin() + start_addr, data_handled_at_.begin() + start_addr + len, true);
    add_label(start_addr, !label.empty() ? label : "text_" + hexstring(start_addr), make_array_type(char_type, len));
}

void analyzer::maybe_find_copper_list_at(uint32_t addr)
{
    addr &= ~1;
    if (addr >= max_mem || addr + 3 >= max_mem || !written_[addr])
        return;

    const auto start_addr = addr;
    // Arbitrary limits
    constexpr uint32_t max_len = 4096;
    uint32_t len = 0;
    while (addr + 3 < max_mem && written_[addr] && written_[addr+2] && len < max_len) {
        const auto ir1 = get_u16(&data_[addr]);
        const auto ir2 = get_u16(&data_[addr + 2]);

        // Delete any automatic word labels here
        if (auto it = labels_.find(addr); it != labels_.end() && it->second.name.find("dat_", 0) != std::string::npos) {
            labels_.erase(it);
        }
        if (auto it = labels_.find(addr + 2); it != labels_.end() && it->second.name.find("dat_", 0) != std::string::npos) {
            labels_.erase(it);
        }

        addr += 4;
        ++len;
        if (ir1 == 0xffff && ir2 == 0xfffe)
            break;
        // Copper jump?
        if (ir1 == 0x88 || ir1 == 0x8a)
            break;
        // Write to invalid register
        if (!(ir1 & 1) && ir1 >= 0x200)
            break;
    }
    std::fill(data_handled_at_.begin() + start_addr, data_handled_at_.begin() + start_addr + len * 4, true);
    add_label(start_addr, "copperlist_" + hexstring(start_addr), make_array_type(copper_code_type, len * 2));
}

void analyzer::handle_bseglist_at(uint32_t addr, const std::string& name)
{
    if (name.empty())
        throw std::runtime_error{"TODO: Handle nameless BCPL seglist"};

    auto globvec = label_adddress(DosGlobalVec.name());
    auto supp = label_adddress(BcplSupport.name());
    if (!globvec || !supp)
        throw std::runtime_error { "TODO in handle_bseglist_at use delayed lookup for globalvec/bcplsupport?" };

    simregs sr {};
    sr.a[0] = simval { 0 };
    sr.a[2] = simval { *globvec };
    sr.a[5] = simval { *supp };
    for (uint32_t cnt = 0; addr + 3 <= max_mem && written_[addr] && !data_handled_at_[addr]; ++cnt) {
        data_handled_at_[addr] = true;

        add_label(addr, name + "_Link" + std::to_string(cnt), bptr_type, false);
        add_label(addr + 4, name + "_Size" + std::to_string(cnt), long_type, false);
        const auto code_addr = addr + 8;
        sr.a[4] = simval { code_addr };
        add_root(code_addr, sr);
        addr = get_u32(&data_[addr]) << 2;
    }
}

void analyzer::handle_data_at(uint32_t addr, const type& t)
{
    if (!addr || addr >= max_mem || !written_[addr])
        return;

    if (data_handled_at_[addr]) {
        //std::cerr << "ignoring $" << hexfmt(addr) << " of type " << t << " - already handled\n";
        return;
    }
    //std::cerr << "handling $" << hexfmt(addr) << " of type " << t << "\n";

    switch (t.base()) {
    default:
        assert(!"TODO");
        [[fallthrough]];
    case base_data_type::unknown_:
    case base_data_type::char_:
    case base_data_type::byte_:
    case base_data_type::word_:
    case base_data_type::long_:
    case base_data_type::jumptab_word_:
        break;
    case base_data_type::code_:
        //assert(false);
        add_root(addr, simregs {}, true);
        break;
    case base_data_type::ptr_:
        if (const auto len = t.len(); len != 0) {
            //assert(t.ptr() != &code_type); // Convinient enough to allow e.g. CODE[10] 
            if (t.ptr() == &copper_code_type) {
                break;
            }

            const auto size = sizeof_type(*t.ptr());
            for (uint32_t i = 0; i < len; ++i)
                handle_data_at(addr + i * size, *t.ptr());
            break;
        }
        data_handled_at_[addr] = true; // Prevent infinite recursion
        if (t.ptr() != &unknown_type)
            handle_pointer_to(get_u32(&data_[addr]), *t.ptr());
        break;
    case base_data_type::bptr_:
        assert(t.len() == 0);
        data_handled_at_[addr] = true; // Prevent infinite recursion
        if (t.bptr() != &unknown_type)
            handle_pointer_to(get_u32(&data_[addr]) * 4, *t.bptr());
        break;
    case base_data_type::bstr_: {
        const auto p = get_u32(&data_[addr]) * 4;
        if (p && p + 256 < max_mem && written_[p]) {
            add_auto_label(p, byte_type, "bstr");
            if (data_[p])
                add_auto_label(p + 1, make_array_type(char_type, data_[p]), "bstr_text");
        }
        break;
    }
    case base_data_type::struct_:
        handle_struct_at(addr, *t.struct_def());
        break;
    //case base_data_type::bcpl_seglist_:
    //    handle_bseglist_at(addr);
    //    break;
    }

    data_handled_at_[addr] = true;
}

void analyzer::handle_pointer_to(uint32_t addr, const type& t)
{
    if (!addr || addr == 0xffffffff || addr + 1 >= max_mem || !written_[addr] || &t == &unknown_type)
        return;

    switch (t.base()) {
    default:
        std::cerr << "TODO: $" << hexfmt(addr) << " pointer to " << t << "\n";
        assert(!"TODO");
        [[fallthrough]];
    case base_data_type::char_:
        maybe_find_string_at(addr);
        return;
    case base_data_type::byte_:
    case base_data_type::word_:
    case base_data_type::long_:
        return;
    case base_data_type::code_:
        add_root(addr, simregs {}, true);
        return;
    case base_data_type::copper_code_:
        maybe_find_copper_list_at(addr);
        return;
    //case base_data_type::ptr_:
    //case base_data_type::bptr_:
    case base_data_type::struct_:
        handle_struct_at(addr, *t.struct_def());
        return;
    }
}

uint32_t analyzer::check_exec_base()
{
    const uint32_t base = try_read_pointer(4);
    if (!base || !written_[base] || !written_[base + 608] || ~base != get_u32(&data_[base + 0x26]))
        return 0;
    uint16_t csum = 0;
    for (uint32_t offset = 0x22; offset < 0x54; offset += 2)
        csum += get_u16(&data_[base + offset]);
    if (csum != 0xffff)
        return 0;

    // Check if Chip/fast mem matches loaded areas
    uint32_t MaxLocMem = get_u32(&data_[base + 0x3e]);
    uint32_t MaxExtMem = get_u32(&data_[base + 0x4e]);
    for (const auto& a : areas_) {
        if (MaxLocMem == a.end)
            MaxLocMem = 0;
        else if (MaxExtMem == a.end)
            MaxExtMem = 0;
    }
    if (MaxLocMem || MaxExtMem)
        return 0;
    return base;
}

void analyzer::handle_predef_info()
{
    for (const auto& i : predef_info_) {
        const auto& t = *i.second.t;

        if (&t == &hack_str_type) {
            maybe_find_string_at(i.first, i.second.name);
            continue;
        }

        if (&t == &bcpl_seglist_type) {
            handle_bseglist_at(i.first, i.second.name);
            continue;
        }

        add_label(i.first, i.second.name, t, false);
        // Don't add root here in case better register values can be inferred
        if (&t != &code_type && &t != &copper_code_ptr) {
            const auto sz = &t == &unknown_type ? 1 : sizeof_type(t);
            if (i.first >= max_mem || i.first + sz > max_mem) {
                throw std::runtime_error { "Definition of " + i.second.name + " is outside valid memory" };
            }
            // Allow making fake types outside written area
            std::fill(written_.begin() + i.first, written_.begin() + i.first + sz, true);

            handle_data_at(i.first, t);
        }
    }
}

void analyzer::do_system_scan()
{
    exec_base_ = check_exec_base();
    if (!exec_base_) {
        std::cerr << "ExecBase not valid\n";
        return;
    }

    const uint32_t this_task_ptr = get_u32(&data_[exec_base_ + 0x0114]);

    auto process_seg_list = [this](uint32_t seg_list_ptr) {
        if (!pointer_ok(seg_list_ptr))
            return;
        std::cerr << "SegList: $" << hexfmt(seg_list_ptr) << "\n";
        hexdump16(std::cerr, seg_list_ptr, &data_[seg_list_ptr], 32);
        for (uint32_t cnt = 0; pointer_ok(seg_list_ptr); seg_list_ptr = get_u32(&data_[seg_list_ptr]) * 4, ++cnt) {
            std::cerr << "Segment at: $" << hexfmt(seg_list_ptr+4) << " size: $" << hexfmt(get_u32(&data_[seg_list_ptr-4])) << "\n";
            if (cnt == 0)
                add_start_root(seg_list_ptr + 4);
        }
    };

    if (!pointer_ok(this_task_ptr)) {
        std::cerr << "ThisTask not valid\n";
        goto finish;
    }

    if (data_[this_task_ptr + 0x08] != NT_PROCESS) {
        std::cerr << "ThisTask is not a process (type=" << static_cast<int>(data_[this_task_ptr + 0x08]) << ")\n";
        goto finish;
    }

    if (const auto pr_cli = get_u32(&data_[this_task_ptr + 0xac]); pointer_ok(pr_cli * 4)) {
        if (const uint32_t cli_name_addr = get_u32(&data_[pr_cli * 4 + 0x10]) * 4; cli_name_addr + 256 < max_mem && written_[cli_name_addr]) {
            const auto len = data_[cli_name_addr];
            std::cerr << "Current task: \"" << std::string{ &data_[cli_name_addr + 1], &data_[cli_name_addr + 1 + len] } << "\"\n";
        }
        add_label(this_task_ptr, "ThisProcess", make_struct_type(Process));
        add_label(pr_cli * 4, "ThisCli", make_struct_type(CommandLineInterface));
        handle_data_at(this_task_ptr, make_struct_type(Process));
        process_seg_list(get_u32(&data_[pr_cli * 4 + 0x3c]) * 4); // BADDR(BADDR(proc->pr_CLI)->cli_Module)
    } else {
        if (const uint32_t task_name_addr = get_u32(&data_[this_task_ptr + 0x0a]); task_name_addr < max_mem && written_[task_name_addr])
            std::cerr << "Current task: \"" << reinterpret_cast<const char*>(&data_[task_name_addr]) << "\"\n";

        add_label(this_task_ptr, "ThisProcess", make_struct_type(Process));

        const auto seg_list_array = get_u32(&data_[this_task_ptr + 0x80]) * 4; // BADDR(proc->pr_SegList)
        // Array of seg lists used by this process
        if (!pointer_ok(seg_list_array) || get_u32(&data_[seg_list_array]) > 4)
            return;

        hexdump16(std::cerr, seg_list_array, &data_[seg_list_array], 32);
        // Ignore array size (first element)
        process_seg_list(get_u32(&data_[seg_list_array + 3 * 4]) * 4);
        process_seg_list(get_u32(&data_[seg_list_array + 4 * 4]) * 4);
    }

finish:
    add_label(exec_base_, "SysBase", make_struct_type(ExecBase));
    handle_data_at(exec_base_, make_struct_type(ExecBase));
}

const structure_definition ResidentLoader {
    "ResidentLoader",
    {
        { "resload_Install", long_type },
        { "resload_Abort", long_type },
        { "resload_LoadFile", long_type },
        { "resload_SaveFile", long_type },
        { "resload_SetCACR", long_type },
        { "resload_ListFiles", long_type },
        { "resload_Decrunch", long_type },
        { "resload_LoadFileDecrunch", long_type },
        { "resload_FlushCache", long_type },
        { "resload_GetFileSize", long_type },
        { "resload_DiskLoad", long_type },
        { "resload_DiskLoadDev", long_type },
        { "resload_CRC16", long_type },
        { "resload_Control", long_type },
        { "resload_SaveFileOffset", long_type },
        { "resload_ProtectRead", long_type },
        { "resload_ProtectReadWrite", long_type },
        { "resload_ProtectWrite", long_type },
        { "resload_ProtectRemove", long_type },
        { "resload_LoadFileOffset", long_type },
        { "resload_Relocate", long_type },
        { "resload_Delay", long_type },
        { "resload_DeleteFile", long_type },
        { "resload_ProtectSMC", long_type },
        { "resload_SetCPU", long_type },
        { "resload_Patch", long_type },
        { "resload_LoadKick", long_type },
        { "resload_Delta", long_type },
        { "resload_GetFileSizeDec", long_type },
        { "resload_PatchSeg", long_type },
        { "resload_Examine", long_type },
        { "resload_ExNext", long_type },
        { "resload_GetCustom", long_type },
        { "resload_VSNPrintF", long_type },
        { "resload_Log", long_type },
    }
};

struct patch_list_item {
    const char* name;
    std::vector<base_data_type> args;
} patch_list[] = {
    { "END", {} },
    { "R", { base_data_type::code_ } },
    { "P", { base_data_type::code_, base_data_type::code_ } },
    { "PS", { base_data_type::code_, base_data_type::code_ } },
    { "S", { base_data_type::code_, (base_data_type)-(int)base_data_type::word_ } },
    { "I", { base_data_type::code_ } },
    { "B", { base_data_type::code_, base_data_type::byte_ } },
    { "W", { base_data_type::code_, base_data_type::word_ } },
    { "L", { base_data_type::code_, base_data_type::long_ } },
    { "A", { base_data_type::code_, base_data_type::long_ } },
    { "PA", { base_data_type::code_, base_data_type::rptrw_ } },
    { "NOP", { base_data_type::code_, base_data_type::word_ } },
    { "C", {} },
    { "CB", {} },
    { "CW", {} },
    { "CL", {} },
    { "PSS", { base_data_type::code_, base_data_type::code_, (base_data_type) - (int)base_data_type::word_ } },
    { "NEXT", {} }, // 17 Handled specially
    { "AB", {} },
    { "AW", {} },
    { "AL", {} },
    { "DATA", {} },
    { "ORB", { base_data_type::code_, base_data_type::byte_ } },
    { "ORW", { base_data_type::code_, base_data_type::word_ } },
    { "ORL", { base_data_type::code_, base_data_type::long_ } },
    { "GA", {} },
    { "BKPT", {} },
    { "BELL", {} },
    { "IFBW", {} }, // 28
    { "IFC1", {} }, // 29
    { "IFC2", {} }, // 30
    { "IFC3", {} }, // 31
    { "IFC4", {} }, // 32
    { "IFC5", {} }, // 33
    { "IFC1X", {} }, // 34
    { "IFC2X", {} }, // 35
    { "IFC3X", {} }, // 36
    { "IFC4X", {} }, // 37
    { "IFC5X", {} }, // 38
    { "ELSE", {} }, // 39
    { "ENDIF", {} }, //40
};

uint32_t analyzer::handle_whd_patchlist(const uint32_t pl_start, bool analyze)
{
    if (analyze) {
        if (data_handled_at_[pl_start])
            return 0;
        data_handled_at_[pl_start] = true;
    } else {
        std::cout << "\tPL_START\n";
    }
    int nest = 0;
    uint32_t addr = pl_start;
    for (;;) {
        uint16_t type = get_u16(&data_[addr]);
        addr += 2;
        if (type == 0) {
            if (!analyze)
                std::cout << "\tPL_END\n";
            break;
        }

        uint32_t cmd_addr = 0;
        bool has_cmd_arg = !(type & (1 << 14));
        // Bit14: No args, Bit15: word arg
        if (has_cmd_arg) {
            if (!(type & (1 << 15))) {
                cmd_addr = get_u32(&data_[addr]);
                addr += 4;
            } else {
                cmd_addr = get_u16(&data_[addr]);
                addr += 2;
            }
        }
        type &= 0x3fff;
        if (type >= sizeof(patch_list) / sizeof(*patch_list))
            throw std::runtime_error { "Unknown patch list item " + std::to_string(type) + " cmd_addr = $" + hexstring(cmd_addr) };

        if (type == 17) {
            // PL_NEXT
            const auto taddr = pl_start + static_cast<int16_t>(get_u16(&data_[addr]));
            addr += 2;
            if (!analyze) {
                std::cout << "\tPL_NEXT\t";
                print_addr(taddr);
                std::cout << "\n";
            } else {
                handle_whd_patchlist(taddr, true);
            }
            if (nest)
                continue;
            else
                break;
        } else if (type >= 28 && type <= 38) {
            ++nest;
        } else if (type == 40) {
            --nest;
        }

        const auto& d = patch_list[type];

        if (d.args.empty() && has_cmd_arg)
            throw std::runtime_error { "TODO: Handle PL_" + std::string { d.name } };

        if (!analyze)
            std::cout << "\tPL_" << d.name;
        for (size_t argcnt = 0; argcnt < d.args.size(); ++argcnt) {
            if (!analyze)
                std::cout << (argcnt == 0 ? "\t" : ",");
            if (argcnt == 0 && has_cmd_arg) {
                if (!analyze)
                    std::cout << "$" << hexstring(cmd_addr);
                continue;
            }
            auto t = d.args[argcnt];
            bool is_signed = false;
            if ((int)t < 0) {
                t = (base_data_type) - (int)t;
                is_signed = true;
            }
            switch (t) {
            case base_data_type::byte_:
                if (!analyze)
                    std::cout << "$" << hexstring((uint8_t)get_u16(&data_[addr]));
                addr += 2;
                break;
            case base_data_type::word_:
                if (!analyze) {
                    uint16_t v = get_u16(&data_[addr]);
                    if (is_signed)
                        std::cout << static_cast<int>(static_cast<int16_t>(v));
                    else
                        std::cout << "$" << hexstring(v);
                }
                addr += 2;
                break;
            case base_data_type::long_:
                if (!analyze)
                    std::cout << "$" << hexstring(get_u32(&data_[addr]));
                addr += 4;
                break;
            case base_data_type::code_: {
                const auto code_addr = pl_start + static_cast<int16_t>(get_u16(&data_[addr]));
                if (!analyze)
                    print_addr(code_addr);
                else
                    add_root(code_addr, simregs {});
                addr += 2;
            } break;
            case base_data_type::rptrw_:
                if (analyze) {
                    const auto taddr = pl_start + static_cast<int16_t>(get_u16(&data_[addr]));
                    print_addr(taddr);
                }
                addr += 2;
                break;
            default:
                std::cerr << "TODO: Handle patch list arg type " << int(d.args[argcnt]) << "\n";
                exit(1);
            }
        }
        if (!analyze)
            std::cout << "\n";
    }
    if (analyze)
        add_auto_label(pl_start, make_array_type(patchitem_type, (addr - pl_start) / 2), "pl");
    return addr;
}

void analyzer::handle_whdload(uint32_t header_address)
{
    const auto& hdr_rptr = make_rptrw(header_address);
    auto resolve_rptr = [&](int16_t offset) -> uint32_t {
        int16_t offset2 = get_u16(&data_[header_address + offset]);
        if (offset2 == 0)
            return 0;
        else
            return header_address + offset2;
    };
    auto add_string_rptr = [&](int16_t offset, const std::string& name) {
        add_label(header_address + offset, name, hdr_rptr);
        uint32_t addr = resolve_rptr(offset);
        if (!addr)
            return;
        maybe_find_string_at(addr, "text" + name);
    };

    const auto resload = alloc_fake_mem(ResidentLoader.size());
    add_label(resload, "_resload", make_struct_type(ResidentLoader));

    // resload_Patch
    
    functions_.insert({ resload + 0x64, function_description {
                                                         {}, { { regname::A0, "patchlist", &word_ptr }, { regname::A1, "destination", &code_ptr } }, [this]() {
                                                                  if (!regs_.a[0].known())
                                                                      return;
                                                                  handle_whd_patchlist(regs_.a[0].raw(), true);
                                                              } } });


    /*
    STRUCTURE	WHDLoadSlave,0
	STRUCT	ws_Security,4	;moveq #-1,d0 rts
	STRUCT	ws_ID,8		;"WHDLOADS"
	UWORD	ws_Version	;required WHDLoad version
	UWORD	ws_Flags	;see below
	ULONG	ws_BaseMemSize	;size of required memory (multiple of $1000)
	ULONG	ws_ExecInstall	;must be 0
	RPTR	ws_GameLoader	;Slave code, called by WHDLoad
	RPTR	ws_CurrentDir	;
	RPTR	ws_DontCache	;
    */
    add_label(header_address + 0, "_base", long_type);
    add_label(header_address + 4, "_ID", make_array_type(char_type, 8));
    add_label(header_address + 12, "_Version", word_type);
    add_label(header_address + 14, "_Flags", word_type);
    add_label(header_address + 16, "_BaseMemSize", long_type);
    add_label(header_address + 20, "_ExecInstall", long_type);
    add_label(header_address + 24, "_GameLoader", hdr_rptr);
    const auto start_address = resolve_rptr(24);
    add_label(start_address, "_start", code_type);
    simregs initregs{};
    initregs.a[0] = simval { resload };
    add_root(start_address, initregs); 
    add_string_rptr(26, "_CurrentDir");
    add_string_rptr(28, "_DontCache");

    const uint16_t version = get_u16(&data_[header_address + 12]);
    if (version >= 4) {
        add_label(header_address + 30, "_keydebug", byte_type);
        add_label(header_address + 31, "_keyexit", byte_type);
    }
    if (version >= 8) {
        add_label(header_address + 32, "_ExpMem", long_type);
    }
    if (version >= 10) {
        add_string_rptr(36, "_name");
        add_string_rptr(38, "_copy");
        add_string_rptr(40, "_info");
    }
    if (version >= 16) {
        add_string_rptr(42, "_kickname");
        add_label(header_address + 44, "_kicksize", long_type);
        add_label(header_address + 48, "_kickcrc", word_type);
    }
    if (version >= 17) {
        add_string_rptr(50, "_config");
    }
}

constexpr uint32_t HUNK_UNIT         = 999;
constexpr uint32_t HUNK_NAME         = 1000;
constexpr uint32_t HUNK_CODE         = 1001;
constexpr uint32_t HUNK_DATA         = 1002;
constexpr uint32_t HUNK_BSS          = 1003;
constexpr uint32_t HUNK_RELOC32      = 1004;
constexpr uint32_t HUNK_RELOC16      = 1005;
constexpr uint32_t HUNK_RELOC8       = 1006;
constexpr uint32_t HUNK_EXT          = 1007;
constexpr uint32_t HUNK_SYMBOL       = 1008;
constexpr uint32_t HUNK_DEBUG        = 1009;
constexpr uint32_t HUNK_END          = 1010;
constexpr uint32_t HUNK_HEADER       = 1011;
constexpr uint32_t HUNK_OVERLAY      = 1013;
constexpr uint32_t HUNK_BREAK        = 1014;
constexpr uint32_t HUNK_DREL32       = 1015;
constexpr uint32_t HUNK_DREL16       = 1016;
constexpr uint32_t HUNK_DREL8        = 1017;
constexpr uint32_t HUNK_LIB          = 1018;
constexpr uint32_t HUNK_INDEX        = 1019;
constexpr uint32_t HUNK_RELOC32SHORT = 1020;
constexpr uint32_t HUNK_RELRELOC32   = 1021;
constexpr uint32_t HUNK_ABSRELOC16   = 1022;

std::string hunk_type_string(uint32_t hunk_type)
{
    switch (hunk_type) {
#define HTS(x) case x: return #x
        HTS(HUNK_UNIT);
        HTS(HUNK_NAME);
        HTS(HUNK_CODE);
        HTS(HUNK_DATA);
        HTS(HUNK_BSS);
        HTS(HUNK_RELOC32);
        HTS(HUNK_RELOC16);
        HTS(HUNK_RELOC8);
        HTS(HUNK_EXT);
        HTS(HUNK_SYMBOL);
        HTS(HUNK_DEBUG);
        HTS(HUNK_END);
        HTS(HUNK_HEADER);
        HTS(HUNK_OVERLAY);
        HTS(HUNK_BREAK);
        HTS(HUNK_DREL32);
        HTS(HUNK_DREL16);
        HTS(HUNK_DREL8);
        HTS(HUNK_LIB);
        HTS(HUNK_INDEX);
        HTS(HUNK_RELOC32SHORT);
        HTS(HUNK_RELRELOC32);
        HTS(HUNK_ABSRELOC16);
#undef HTS
    }
    return "Unknown hunk type " + std::to_string(hunk_type) + " $" + hexstring(hunk_type);
}

constexpr uint8_t EXT_SYMB	    = 0;    // symbol table
constexpr uint8_t EXT_DEF		= 1;    // relocatable definition
constexpr uint8_t EXT_ABS		= 2;    // Absolute definition
constexpr uint8_t EXT_RES		= 3;    // no longer supported
constexpr uint8_t EXT_REF32	    = 129;  // 32 bit absolute reference to symbol
constexpr uint8_t EXT_COMMON	= 130;  // 32 bit absolute reference to COMMON block
constexpr uint8_t EXT_REF16	    = 131;  // 16 bit PC-relative reference to symbol
constexpr uint8_t EXT_REF8	    = 132;  //  8 bit PC-relative reference to symbol
constexpr uint8_t EXT_DEXT32	= 133;  // 32 bit data relative reference
constexpr uint8_t EXT_DEXT16	= 134;  // 16 bit data relative reference
constexpr uint8_t EXT_DEXT8	    = 135;  //  8 bit data relative reference
constexpr uint8_t EXT_RELREF32	= 136;  // 32 bit PC-relative reference to symbol
constexpr uint8_t EXT_RELCOMMON	= 137;  // 32 bit PC-relative reference to COMMON block
constexpr uint8_t EXT_ABSREF16	= 138;  // 16 bit absolute reference to symbol
constexpr uint8_t EXT_ABSREF8	= 139;  // 8 bit absolute reference to symbol

constexpr uint32_t block_id_mask = (1 << 29U) - 1;

std::string ext_type_string(uint8_t ext_type)
{
    switch (ext_type) {
#define ETS(x) case x: return #x
        ETS(EXT_SYMB);
        ETS(EXT_DEF);
        ETS(EXT_ABS);
        ETS(EXT_RES);
        ETS(EXT_REF32);
        ETS(EXT_COMMON);
        ETS(EXT_REF16);
        ETS(EXT_REF8);
        ETS(EXT_DEXT32);
        ETS(EXT_DEXT16);
        ETS(EXT_DEXT8);
        ETS(EXT_RELREF32);
        ETS(EXT_RELCOMMON);
        ETS(EXT_ABSREF16);
        ETS(EXT_ABSREF8);
#undef ETS
    }
    return "Unknown external reference type " + std::to_string(ext_type);
}


constexpr bool is_initial_hunk(uint32_t hunk_type)
{
    return hunk_type == HUNK_CODE || hunk_type == HUNK_DATA || hunk_type == HUNK_BSS;
}

class hunk_file {
public:
    explicit hunk_file(const std::vector<uint8_t>& data, analyzer* a)
        : data_ { data }
        , a_ { a }
    {
        if (data.size() < 8)
            throw std::runtime_error { "File is too small to be Amiga HUNK file" };
        const uint32_t header_type = read_u32();
        if (header_type == HUNK_UNIT)
            read_hunk_unit();
        else if (header_type == HUNK_HEADER)
            read_hunk_exe();
        else
            throw std::runtime_error { "Invalid hunk type " + std::to_string(header_type) };
    }

private:
    const std::vector<uint8_t>& data_;
    analyzer* a_;
    uint32_t pos_ = 0;

    struct reloc_info {
        uint32_t hunk_ref;
        std::vector<uint32_t> relocs;
    };

    struct symbol_info {
        std::string name;
        uint32_t addr;
    };

    void check_pos(size_t size)
    {
        if (pos_ + size > data_.size())
            throw std::runtime_error { "Unexpected end of hunk file" };
    }

    uint16_t read_u16()
    {
        check_pos(2);
        auto l = get_u16(&data_[pos_]);
        pos_ += 2;
        return l;
    }

    uint32_t read_u32()
    {
        check_pos(4);
        auto l = get_u32(&data_[pos_]);
        pos_ += 4;
        return l;
    }

    std::string read_string_size(uint32_t num_longs)
    {
        const auto size = num_longs << 2;
        if (!size)
            return "";
        check_pos(size);
        const auto start = pos_;
        auto end = pos_ + size - 1;
        while (end > pos_ && !data_[end])
            --end;
        pos_ += size;
        return std::string { &data_[start], &data_[end + 1] };
    }

    std::string read_string()
    {
        return read_string_size(read_u32());
    }

    std::vector<symbol_info> read_hunk_symbol()
    {
        std::vector<symbol_info> syms;
        while (const auto name_longs = read_u32()) {
            auto name = read_string_size(name_longs);
            auto ofs = read_u32();
            syms.push_back({ std::move(name), ofs });
        }
        return syms;
    }

    void read_hunk_debug()
    {
        const auto size = read_u32() << 2;
        if (!size)
            return;
        check_pos(size);

        if (size < 8)
            throw std::runtime_error { "Invalid HUNK_DEBUG size " + std::to_string(size) };

        std::cerr << "HUNK_DEBUG ";
        hexdump(std::cerr, &data_[pos_], 8);

        pos_ += size;
    }

    std::vector<reloc_info> read_hunk_reloc32()
    {
        std::vector<reloc_info> relocs;
        for (;;) {
            const auto num_offsets = read_u32();
            if (!num_offsets)
                break;
            reloc_info r;
            r.hunk_ref = read_u32();
            r.relocs.resize(num_offsets);
            for (uint32_t i = 0; i < num_offsets; ++i)
                r.relocs[i] = read_u32();
            relocs.emplace_back(std::move(r));
        }
        return relocs;
    }

    std::vector<reloc_info> read_hunk_reloc32short()
    {
        std::vector<reloc_info> relocs;
        for (;;) {
            const auto num_offsets = read_u16();
            if (!num_offsets)
                break;
            reloc_info r;
            r.hunk_ref = read_u16();
            r.relocs.resize(num_offsets);
            for (uint32_t i = 0; i < num_offsets; ++i)
                r.relocs[i] = read_u16();
            relocs.emplace_back(std::move(r));
        }
        pos_ = (pos_+3) & -4; // Ensure long word alignment
        return relocs;
    }

    void read_hunk_exe();
    void read_hunk_unit();
};

void hunk_file::read_hunk_exe()
{
    // Only valid for HUNK_HEADER
    if (read_u32() != 0)
        throw std::runtime_error { "Hunk file contains resident libraries" };

    const uint32_t table_size = read_u32();
    const uint32_t first_hunk = read_u32();
    const uint32_t last_hunk = read_u32();

    if (table_size != (last_hunk - first_hunk) + 1)
        throw std::runtime_error { "Invalid hunk file. Table size does not match first/last hunk" };

    uint32_t chip_ptr = 0x400;
    uint32_t fast_ptr = 0x200000;

    struct hunk_info {
        uint32_t type;
        uint32_t addr;
        uint32_t flags;
        std::vector<uint8_t> data;
        uint32_t loaded_size;
    };
    std::vector<symbol_info> symbols;

    std::vector<hunk_info> hunks(table_size);
    for (uint32_t i = 0; i < table_size; ++i) {
        const auto v = read_u32();
        auto flags = (v >> 29) & 0b110;
        const auto size = (v & 0x3fffffff) << 2;
        if (flags == 0b110) {
            flags = read_u32() & ~(1<<30);
        }
        // +8 is for hunk size + bptr to segment list
        const auto aligned_size = (size + 8 + 4095) & -4096;
        if (flags & 2) {
            hunks[i].addr = chip_ptr + 8;
            chip_ptr += aligned_size;
            if (chip_ptr > 0x200000)
                throw std::runtime_error { "Out of chip mem" };
        } else {
            hunks[i].addr = fast_ptr + 8;
            fast_ptr += aligned_size;
            if (fast_ptr > 0xa00000)
                throw std::runtime_error { "Out of fast mem" };
        }
        hunks[i].data.resize(size);
        hunks[i].flags = flags;
        std::cerr << "Hunk " << i << " $" << hexfmt(size) << " " << (flags & 2 ? "CHIP" : flags & 4 ? "FAST" : "    ") << " @ " << hexfmt(hunks[i].addr) << "\n";
    }

    uint32_t table_index = 0;
    while (table_index < table_size && pos_ < data_.size()) {
        const uint32_t hunk_num = first_hunk + table_index;

        const uint32_t hunk_type = read_u32() & block_id_mask;

        if (hunk_type == HUNK_DEBUG) {
            read_hunk_debug();
            continue;
        }

        if (!is_initial_hunk(hunk_type))
            throw std::runtime_error { "Expected libvec_code, DATA or BSS hunk for hunk " + std::to_string(hunk_num) + " got " + hunk_type_string(hunk_type) };
        const uint32_t hunk_longs = read_u32();
        const uint32_t hunk_bytes = hunk_longs << 2;
        std::cerr << "\tHUNK_" << (hunk_type == HUNK_CODE ? "CODE" : hunk_type == HUNK_DATA ? "DATA" : "BSS ") << " size = $" << hexfmt(hunk_bytes) << "\n";

        hunks[hunk_num].type = hunk_type;
        hunks[hunk_num].loaded_size = hunk_bytes;

        if (hunk_bytes > hunks[hunk_num].data.size()) {
            throw std::runtime_error { "Too much data for hunk " + std::to_string(hunk_num) };
        }

        if (hunk_type != HUNK_BSS && hunk_bytes) {
            check_pos(hunk_bytes);
            memcpy(&hunks[hunk_num].data[0], &data_[pos_], hunk_bytes);
            pos_ += hunk_bytes;
        }

        while (pos_ <= data_.size() - 4 && !is_initial_hunk(get_u32(&data_[pos_]) & block_id_mask)) {
            const auto ht = read_u32() & block_id_mask;
            std::cerr << "\t\t" << hunk_type_string(ht) << "\n";
            switch (ht) {
            case HUNK_SYMBOL: {
                auto syms = read_hunk_symbol();
                for (auto& s : syms) {
                    symbols.push_back({ std::move(s.name), hunks[hunk_num].addr + s.addr });
                    std::cerr << "\t\t\t$" << hexfmt(symbols.back().addr) << "\t" << symbols.back().name << "\n"; 
                }
                break;
            }
            case HUNK_RELOC32:
            case HUNK_RELRELOC32: {
                const uint32_t base = ht == HUNK_RELRELOC32 ? hunks[hunk_num].addr : 0;
                for (const auto& r : read_hunk_reloc32()) {
                    if (r.hunk_ref > last_hunk)
                        throw std::runtime_error { "Invalid " + hunk_type_string(ht) + " refers to unknown hunk " + std::to_string(r.hunk_ref) };
                    auto& hd = hunks[hunk_num].data;
                    for (const auto ofs : r.relocs) {
                        if (ofs > hd.size() - 4)
                            throw std::runtime_error { "Invalid relocation in " + hunk_type_string(ht) };
                        auto c = &hd[ofs];
                        put_u32(c, get_u32(c) + hunks[r.hunk_ref].addr - base);
                    }
                }
                break;
            }
            case HUNK_RELOC32SHORT:
            case HUNK_DREL32:
                for (const auto& r : read_hunk_reloc32short()) {
                    if (r.hunk_ref > last_hunk)
                        throw std::runtime_error { "Invalid " + hunk_type_string(ht) + " refers to unknown hunk " + std::to_string(r.hunk_ref) };
                    auto& hd = hunks[hunk_num].data;
                    for (const auto ofs : r.relocs) {
                        if (ofs > hd.size() - 4)
                            throw std::runtime_error { "Invalid relocation in " + hunk_type_string(ht) };
                        auto c = &hd[ofs];
                        put_u32(c, get_u32(c) + hunks[r.hunk_ref].addr);
                    }
                }
                break;
            case HUNK_END:
                goto next;
            case HUNK_OVERLAY: {
                // HACK: Just skip everything after HUNK_OVERLAY for now...
                // See http://aminet.net/package/docs/misc/Overlay for more info
                const auto overlay_table_size = read_u32() + 1;
                std::cerr << "\t\t\tTable size " << overlay_table_size << "\n";
                check_pos(overlay_table_size << 2);
                hexdump(std::cerr, &data_[pos_], overlay_table_size << 2);
                table_index = table_size - 1;
                pos_ = static_cast<uint32_t>(data_.size()); // Skip to end
                break;
            }
            default:
                throw std::runtime_error { "Unsupported HUNK type " + hunk_type_string(ht) + " in executable" };
            }
        }
next:
        ++table_index;
    }

    if (table_index != table_size) {
        // Just make this a warning (Phenomena - Animotion)
        std::cerr << "Warning: Only " + std::to_string(table_index) + " out of " + std::to_string(table_size) + " hunks read\n";
    }

    // E.g. ZeeWolf contains extra junk bytes at end
    //if (pos_ != data_.size())
    //    throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data_.size(), 8) };

    if (!a_) {
        for (uint32_t i = 0; i < table_size; ++i) {
            if (hunks[i].type != HUNK_CODE)
                continue;
            disasm_stmts(hunks[i].data, 0, hunks[i].loaded_size, hunks[i].addr);
        }
    } else {
        a_->add_fakes();
        for (const auto& s : symbols)
            a_->add_label(s.addr, s.name, unknown_type);
        bool has_code = false;
        uint32_t whdload_start = 0;
        std::vector<rom_tag> tags;
        for (size_t i = 0, sz = hunks.size(); i < sz; ++i) {
            const auto& h = hunks[i];
            const auto hunk_size = static_cast<uint32_t>(h.data.size());
            const auto hunk_name = "hunk" + std::to_string(i);
            uint8_t header[8];
            put_u32(&header[0], hunk_size);
            put_u32(&header[4], (i == sz-1 ? 0 : hunks[i+1].addr - 4) >> 2); // BPTR to next hunk
            a_->add_label(h.addr - 8, hunk_name + "_size", long_type);
            a_->add_label(h.addr - 4, hunk_name + "_link", long_type);
            a_->write_data(h.addr - 8, header, sizeof(header));
            a_->write_data(h.addr, h.data.data(), hunk_size);
            if (h.type == HUNK_CODE && !h.data.empty()) {
                if (!a_->find_predef_into(h.addr)) {
                    // MOVEQ #-1,D0 / RTS + DC.B "WHDLAODS" => Whdload
                    if (h.data.size() > 32 && memcmp(h.data.data(), "\x70\xFF\x4E\x75WHDLOADS", 12) == 0) {
                        whdload_start = h.addr;
                    } else {
                        if (!has_code)
                            a_->add_label(h.addr, "$$entry", code_type); // Add label if not already present
                        a_->add_root(h.addr, simregs {});
                    }
                }
                for (const auto& t : scan_for_rom_tags(h.data, h.addr))
                    a_->add_rom_tag(t);
                has_code = true;
            } else {
                if (!a_->find_predef_into(h.addr)) {
                    const std::string t = h.flags == 1 ? "chip" : "";
                    a_->add_auto_label(h.addr, unknown_type, t + (h.type == HUNK_CODE ? "code" : h.type == HUNK_BSS ? "bss" : "data"));
                }
            }
        }
        assert(has_code);

        if (whdload_start)
            a_->handle_whdload(whdload_start);

        a_->run();
    }
}

void hunk_file::read_hunk_unit()
{
    if (a_)
        throw std::runtime_error { "Analyze not supported for HUNK_UNIT" };

    const auto unit_name = read_string();
    std::cerr << "HUNK_UNIT '" << unit_name << "'\n";

    std::vector<std::vector<uint8_t>> code_hunks;

    while (pos_ < data_.size()) {
        auto hunk_type = read_u32();
        const auto flags = hunk_type >> 29;
        hunk_type &= block_id_mask;
        switch (hunk_type) {
        case HUNK_NAME: {
            const auto name = read_string();
            std::cerr << "\tHUNK_NAME " << name << "\n";
            break;
        }
        case HUNK_CODE:
        case HUNK_DATA: {
            const auto size = read_u32() << 2;
            check_pos(size);
            std::cerr << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags>>1) << "\n";
            if (hunk_type == HUNK_CODE)
                code_hunks.push_back(std::vector<uint8_t>(&data_[pos_], &data_[pos_ + size]));
            pos_ += size;
            break;
        }
        case HUNK_BSS: {
            const auto size = read_u32() << 2;
            std::cerr << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags >> 1) << "\n";
            break;
        }
        case HUNK_RELOC32:
            std::cerr << "\t" << hunk_type_string(hunk_type) << "\n";
            read_hunk_reloc32();
            break;
        case HUNK_EXT: {
            std::cerr << "\t" << hunk_type_string(hunk_type) << "\n";
            while (const auto ext = read_u32()) {
                const auto sym_type = static_cast<uint8_t>(ext >> 24);
                const auto name     = read_string_size(ext & 0xffffff);
                std::cerr << "\t\t" << ext_type_string(sym_type) << "\t" << name << "\n";
                switch (sym_type) {
                case EXT_DEF: {
                    const auto offset = read_u32();
                    std::cerr << "\t\t\t$" << hexfmt(offset) << "\n";
                    break;
                }
                case EXT_REF8:
                case EXT_REF16:
                case EXT_REF32: {
                    const auto nref = read_u32();
                    for (uint32_t i = 0; i < nref; ++i) {
                        const auto offset = read_u32();
                        std::cerr << "\t\t\t$" << hexfmt(offset) << "\n";
                    }
                    break;
                }
                default:
                    throw std::runtime_error { "Unsupported external reference of type " + ext_type_string(sym_type) };
                }
            }
            break;
        }
        case HUNK_SYMBOL: {
            std::cerr << "\t" << hunk_type_string(hunk_type) << "\n";
            const auto syms = read_hunk_symbol();
            for (const auto& s : syms) {
                std::cerr << "\t\t$" << hexfmt(s.addr) << "\t" << s.name << "\n";
            }
            break;
        }
        case HUNK_END:
            std::cerr << "\t" << hunk_type_string(hunk_type) << "\n";
            break;
        default:
            throw std::runtime_error { "Unsupported HUNK type " + hunk_type_string(hunk_type) + " in unit" };
        }
    }

    if (pos_ != data_.size())
        throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data_.size(), 8) };

    for (const auto& c : code_hunks) {
        std::cout << "\n";
        disasm_stmts(c, 0, static_cast<uint32_t>(c.size()));
    }
}

void read_hunk(const std::vector<uint8_t>& data, analyzer* a)
{
    hunk_file hf { data, a };
}

bool is_rom(const std::vector<uint8_t>& data)
{
    if (data.size() < 8 * 1024)
        return false;
    if (get_u16(&data[2]) != 0x4ef9) // jump
        return false;
    switch (get_u16(&data[0])) {
    case 0x1111: // 256K (or boot rom 8K)
        return data.size() == 8 * 1024 || data.size() == 256 * 1024;
    case 0x1114: // 512K
        return data.size() == 512 * 1024;
    case 0x1116: // rekick
        return true;
    }
    return false;
}

void handle_rom(const std::vector<uint8_t>& data, analyzer* a)
{
    const auto start = get_u32(&data[4]);
    //std::cout << "ROM detected\n";
    //std::cout << "Entry point: $" << hexfmt(start) << "\n";
    const uint32_t rom_base = data.size() == 1024 * 1024 ? 0xf00000 : start & 0xffff0000;
    if (rom_base != 0xf00000 && rom_base != 0xf80000 && rom_base != 0xfc0000 && rom_base != 0x200000)
        throw std::runtime_error { "Unsupported ROM base: $" + hexstring(rom_base) };
    //std::cout << "Base address: $" << hexfmt(rom_base) << "\n";
    if (start - rom_base >= data.size())
        throw std::runtime_error { "Invalid ROM entry point: $" + hexstring(start) };

    // Scan for ROM tags
    auto rom_tags = scan_for_rom_tags(data, rom_base);

    if (!a) {
        if (rom_tags.empty()) {
            disasm_stmts(data, start - rom_base, static_cast<uint32_t>(data.size()), rom_base);
        } else {
            for (const auto& tag : rom_tags) {
                disasm_stmts(data, tag.init_addr - rom_base, tag.endskip - rom_base, rom_base);
            }
        }
        return;
    }

    a->write_data(rom_base, data.data(), static_cast<uint32_t>(data.size()));
    a->add_fakes();

    #if 0
    if (1) {
        std::sort(rom_tags.begin(), rom_tags.end(), [](const rom_tag& l, const rom_tag& r) {
            return l.priority >= r.priority;
        });
        for (const auto& tag : rom_tags)
            //if (tag.flags & 0x01)
                std::cout << tag.name << " priority=$" << hexfmt(tag.priority) << " flags=$" << hexfmt(tag.flags) << "\n";
        return;
    }
    #endif

    bool has_entry_name = false;
    for (const auto& tag : rom_tags) {
        a->add_rom_tag(tag);
        if (tag.init_addr == start)
            has_entry_name = true;
    }

    if (!has_entry_name) {
        a->add_label(start, "$$entry", code_type);
        a->add_root(start, simregs {});
    }

    a->add_label(rom_base, "RomStart", word_type);
    a->run();
}

bool validate_bootchecksum(const std::vector<uint8_t>& data)
{
    assert(data.size() >= 1024);
    uint32_t csum = 0;
    for (uint32_t i = 0; i < 1024; i += 4) {
        const auto before = csum;
        csum += get_u32(&data[i]);
        if (csum < before) // carry?
            ++csum;
    }
    return csum == 0xffffffff;
}

void usage()
{
    std::cerr << "Usage: m68kdisasm [options] [-a] [-i info] [-cut start len] file [options...]\n";
    std::cerr << "   file    source file\n";
    std::cerr << "   -a      analyze\n";
    std::cerr << "   -i      infofile (implies -a)\n";
    std::cerr << "   -cut    cut out part of file\n";
    std::cerr << "   -rom    force rom mode\n";
    std::cerr << "   -bytes  show bytes and address for all instructions\n";
    std::cerr << "\n";
    std::cerr << "Options for non-hunk files:";
    std::cerr << "   Normal (non-analysis mode) options: [base]\n";
    std::cerr << "   Analayze options: [start pc] [base]\n";
}

int main(int argc, char* argv[])
{
    try {
        bool force_rom = false;
        std::unique_ptr<analyzer> a;
        int cut_offset = -1, cut_length = -1;
        while (argc >= 2) {
            if (!strcmp(argv[1], "-a")) {
                if (!a)
                    a = std::make_unique<analyzer>();
                ++argv;
                --argc;
            } else if (!strcmp(argv[1], "-rom")) {
                force_rom = true;
                ++argv;
                --argc;
            } else if (!strcmp(argv[1], "-bytes")) {
                show_bytes = true;
                ++argv;
                --argc;
            } else if (!strcmp(argv[1], "-i")) {
                ++argv;
                --argc;
                if (argc < 2) {
                    usage();
                    return 1;
                }
                if (!a)
                    a = std::make_unique<analyzer>();
                a->read_info_file(argv[1]);
                ++argv;
                --argc;
            } else if (!strcmp(argv[1], "-cut")) {
                ++argv;
                --argc;
                if (argc < 3) {
                    usage();
                    return 1;
                }
                cut_offset = hex_or_die(argv[1]);
                cut_length = hex_or_die(argv[2]);
                argv += 2;
                argc -= 2;
            } else {
                break;
            }
        }

        if (argc < 2) {
            usage();
            return 1;
        }
        
        auto data = read_file(argv[1]);
        if (cut_offset >= 0) {
            if ((size_t)cut_offset >= data.size() || (size_t)cut_offset + (size_t)cut_length > data.size()) {
                std::cerr << "Invalid cut\n";
                usage();
                return 1;
            }
            data.erase(data.begin(), data.begin() + cut_offset);
            data.erase(data.begin() + cut_length, data.end());
        }

        if (force_rom || is_rom(data)) {
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            handle_rom(data, a.get());
        } else if (cut_offset < 0 && data.size() > 4 && (get_u32(&data[0]) == HUNK_HEADER || get_u32(&data[0]) == HUNK_UNIT)) {
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            read_hunk(data, a.get());
        } else if (argc == 2 && a && data.size() >= 1024 && data[0] == 'D' && data[1] == 'O' && data[2] == 'S' && validate_bootchecksum(data)) {
            // Bootblock (analyzed)
            const auto base = 0x200000;
            a->write_data(base, data.data(), 1024);
            a->add_fakes();
            simregs regs {};
            regs.a[6] = simval { a->fake_exec_base() };

            // A1 points to an IORequest ready
            const auto ireq_ptr = a->alloc_fake_mem(IOStdReq.size());
            if (ireq_ptr) {
                a->add_label(ireq_ptr, "BootIORequest", make_struct_type(IOStdReq));
                regs.a[1] = simval { ireq_ptr };
            }
            a->add_label(base, "bootsig", make_array_type(char_type, 4));
            a->add_label(base + 0x4, "checksum", long_type);
            a->add_label(base + 0x8, "rootblock", long_type);
            a->add_label(base + 0xc, "$$entry", code_type);

            a->add_root(base + 0x0c, regs);
            a->run();
        } else if (data.size() > 128*1024+20 && !memcmp(data.data(), "MEMDUMP!", 8)) {
            if (!a)
                throw std::runtime_error { "Currently memdumps are only supported for analysis" };
            if (argc > 2)
                throw std::runtime_error { "Arguments for memdump analysis not supported" };
            for (size_t pos = 8; pos < data.size();) {
                if (pos + 12 >= data.size())
                    throw std::runtime_error { "Invalid memory dump" };
                if (!memcmp(data.data() + pos, "PART", 4)) {
                    const uint32_t base = get_u32(&data[pos + 4]);
                    const uint32_t size = get_u32(&data[pos + 8]);
                    pos += 12;
                    if (size > (1<<24) || base >= (1<<24) || base + size >= (1<<24) || pos + size > data.size())
                        throw std::runtime_error { "Invalid memory dump part" };
                    a->write_data(base, data.data() + pos, size);
                    pos += size;
                } else if (!memcmp(data.data() + pos, "REGS", 4)) {
                    pos += 4;
                    if (pos + 76 != data.size())
                        throw std::runtime_error { "Invalid memory dump (expected registers last)" };

                    auto get_val = [&]() {
                        const auto val = get_u32(data.data() + pos);
                        pos += 4;
                        return val;
                    };
                    simregs r { };
                    const auto pc = get_val();
                    get_val(); // SR
                    for (int i = 0; i < 8; ++i)
                        r.d[i] = simval { get_val() };
                    for (int i = 0; i < 8; ++i)
                        r.a[i] = simval { get_val() };
                    get_val(); // USP/SSP (that isn't A7)
                    assert(pos == data.size());
                    a->add_label(pc, "CurrentPosition", code_type);
                    a->add_root(pc, r);
                    std::cerr << "Current PC: $" << hexfmt(pc) << "\n";
                    break;
                } else {
                    throw std::runtime_error { "Invalid memory dump" };
                }
            }
            a->do_system_scan();
            a->add_int_vectors();
            a->add_fakes();
            a->run();
        } else {
            if (a) {
                uint32_t start = 0;
                uint32_t load_base = 0;
                if (argc > 2)
                    start = hex_or_die(argv[2]);
                if (argc > 3)
                    load_base = hex_or_die(argv[3]);
                a->write_data(load_base, data.data(), static_cast<uint32_t>(data.size()));
                if (start)
                    a->add_start_root(start);
                else
                    a->do_system_scan();
                if (load_base)
                    std::cout << "\tORG $" << hexfmt(load_base) << "\n";
                else
                    a->add_int_vectors();
                a->add_fakes();
                a->run();
            } else {
                if (argc > 3)
                    throw std::runtime_error { "Too many arguments" };
                const auto base = argc > 2 ? hex_or_die(argv[2]) : 0;
                disasm_stmts(data, 0, static_cast<uint32_t>(data.size()), base);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
