#include <cassert>
#include <iostream>
#include <fstream>

#include "disasm.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"

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

constexpr const char* const cia_regname[16] = {
    "pra",
    "prb",
    "ddra",
    "ddrb",
    "talo",
    "tahi",
    "tblo",
    "tbhi",
    "todlo",
    "todmid",
    "todhi ",
    "unused",
    "sdr",
    "icr",
    "cra",
    "crb"
};


constexpr const char* const custom_regname[0x100] = {
    "BLTDDAT",
    "DMACONR",
    "VPOSR",
    "VHPOSR",
    "DSKDATR",
    "JOY0DAT",
    "JOY1DAT",
    "CLXDAT",
    "ADKCONR",
    "POT0DAT",
    "POT1DAT",
    "POTGOR",
    "SERDATR",
    "DSKBYTR",
    "INTENAR",
    "INTREQR",
    "DSKPTH",
    "DSKPTL",
    "DSKLEN",
    "DSKDAT",
    "REFPTR",
    "VPOSW",
    "VHPOSW",
    "COPCON",
    "SERDAT",
    "SERPER",
    "POTGO",
    "JOYTEST",
    "STREQU",
    "STRVBL",
    "STRHOR",
    "STRLONG",
    "BLTCON0",
    "BLTCON1",
    "BLTAFWM",
    "BLTALWM",
    "BLTCPTH",
    "BLTCPTL",
    "BLTBPTH",
    "BLTBPTL",
    "BLTAPTH",
    "BLTAPTL",
    "BLTDPTH",
    "BLTDPTL",
    "BLTSIZE",
    "BLTCON0L",
    "BLTSIZV",
    "BLTSIZH",
    "BLTCMOD",
    "BLTBMOD",
    "BLTAMOD",
    "BLTDMOD",
    "RESERVED_068",
    "RESERVED_06a",
    "RESERVED_06c",
    "RESERVED_06e",
    "BLTCDAT",
    "BLTBDAT",
    "BLTADAT",
    "RESERVED_076",
    "SPRHDAT",
    "BPLHDAT",
    "LISAID",
    "DSKSYNC",
    "COP1LCH",
    "COP1LCL",
    "COP2LCH",
    "COP2LCL",
    "COPJMP1",
    "COPJMP2",
    "COPINS",
    "DIWSTRT",
    "DIWSTOP",
    "DDFSTRT",
    "DDFSTOP",
    "DMACON",
    "CLXCON",
    "INTENA",
    "INTREQ",
    "ADKCON",
    "AUD0LCH",
    "AUD0LCL",
    "AUD0LEN",
    "AUD0PER",
    "AUD0VOL",
    "AUD0DAT",
    "RESERVED_0ac",
    "RESERVED_0ae",
    "AUD1LCH",
    "AUD1LCL",
    "AUD1LEN",
    "AUD1PER",
    "AUD1VOL",
    "AUD1DAT",
    "RESERVED_0bc",
    "RESERVED_0be",
    "AUD2LCH",
    "AUD2LCL",
    "AUD2LEN",
    "AUD2PER",
    "AUD2VOL",
    "AUD2DAT",
    "RESERVED_0cc",
    "RESERVED_0ce",
    "AUD3LCH",
    "AUD3LCL",
    "AUD3LEN",
    "AUD3PER",
    "AUD3VOL",
    "AUD3DAT",
    "RESERVED_0dc",
    "RESERVED_0de",
    "BPL1PTH",
    "BPL1PTL",
    "BPL2PTH",
    "BPL2PTL",
    "BPL3PTH",
    "BPL3PTL",
    "BPL4PTH",
    "BPL4PTL",
    "BPL5PTH",
    "BPL5PTL",
    "BPL6PTH",
    "BPL6PTL",
    "BPL7PTH",
    "BPL7PTL",
    "BPL8PTH",
    "BPL8PTL",
    "BPLCON0",
    "BPLCON1",
    "BPLCON2",
    "BPLCON3",
    "BPL1MOD",
    "BPL2MOD",
    "BPLCON4",
    "CLXCON2",
    "BPL1DAT",
    "BPL2DAT",
    "BPL3DAT",
    "BPL4DAT",
    "BPL5DAT",
    "BPL6DAT",
    "BPL7DAT",
    "BPL8DAT",
    "SPR0PTH",
    "SPR0PTL",
    "SPR1PTH",
    "SPR1PTL",
    "SPR2PTH",
    "SPR2PTL",
    "SPR3PTH",
    "SPR3PTL",
    "SPR4PTH",
    "SPR4PTL",
    "SPR5PTH",
    "SPR5PTL",
    "SPR6PTH",
    "SPR6PTL",
    "SPR7PTH",
    "SPR7PTL",
    "SPR0POS",
    "SPR0CTL",
    "SPR0DATA",
    "SPR0DATB",
    "SPR1POS",
    "SPR1CTL",
    "SPR1DATA",
    "SPR1DATB",
    "SPR2POS",
    "SPR2CTL",
    "SPR2DATA",
    "SPR2DATB",
    "SPR3POS",
    "SPR3CTL",
    "SPR3DATA",
    "SPR3DATB",
    "SPR4POS",
    "SPR4CTL",
    "SPR4DATA",
    "SPR4DATB",
    "SPR5POS",
    "SPR5CTL",
    "SPR5DATA",
    "SPR5DATB",
    "SPR6POS",
    "SPR6CTL",
    "SPR6DATA",
    "SPR6DATB",
    "SPR7POS",
    "SPR7CTL",
    "SPR7DATA",
    "SPR7DATB",
    "COLOR00",
    "COLOR01",
    "COLOR02",
    "COLOR03",
    "COLOR04",
    "COLOR05",
    "COLOR06",
    "COLOR07",
    "COLOR08",
    "COLOR09",
    "COLOR10",
    "COLOR11",
    "COLOR12",
    "COLOR13",
    "COLOR14",
    "COLOR15",
    "COLOR16",
    "COLOR17",
    "COLOR18",
    "COLOR19",
    "COLOR20",
    "COLOR21",
    "COLOR22",
    "COLOR23",
    "COLOR24",
    "COLOR25",
    "COLOR26",
    "COLOR27",
    "COLOR28",
    "COLOR29",
    "COLOR30",
    "COLOR31",
    "HTOTAL",
    "HSSTOP",
    "HBSTRT",
    "HBSTOP",
    "VTOTAL",
    "VSSTOP",
    "VBSTRT",
    "VBSTOP",
    "SPRHSTRT",
    "SPRHSTOP",
    "BPLHSTRT",
    "BPLHSTOP",
    "HHPOSW",
    "HHPOSR",
    "BEAMCON0",
    "HSSTRT",
    "VSSTRT",
    "HCENTER",
    "DIWHIGH",
    "BPLHMOD",
    "SPRHPTH",
    "SPRHPTL",
    "BPLHPTH",
    "BPLHPTL",
    "RESERVED_1f0",
    "RESERVED_1f2",
    "RESERVED_1f4",
    "RESERVED_1f6",
    "RESERVED_1f8",
    "RESERVED_1fa",
    "FMODE",
    "CUSTOM_NOOP",
};


#include <queue>
#include <sstream>
#include <map>
#include <iomanip>

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

private:
    uint32_t raw_;
    enum { STATE_UNKNOWN, STATE_KNOWN } state_;
};

simval operator+(const simval& lhs, const simval& rhs)
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
    ptr_,
    struct_,
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
    explicit type(const type& base, uint32_t len)
        : t_ { base_data_type::ptr_ }
        , ptr_ { &base }
        , len_ { len }
    {
    }
    explicit type(const structure_definition& s)
        : t_ { base_data_type::struct_ }
        , struct_ { &s }
        , len_ { 0 }
    {
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

    const structure_definition* struct_def() const
    {
        return t_ == base_data_type::struct_ ? struct_ : nullptr;
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
    };
    uint32_t len_ = 0;
};

const type unknown_type { base_data_type::unknown_ };
const type char_type { base_data_type::char_ };
const type byte_type { base_data_type::byte_ };
const type word_type { base_data_type::word_ };
const type long_type { base_data_type::long_ };
const type code_type { base_data_type::code_ };
const type unknown_ptr { unknown_type, 0 };
const type char_ptr { char_type, 0 };
const type byte_ptr { byte_type, 0 };
const type word_ptr { word_type, 0 };
const type long_ptr { long_type, 0 };
const type code_ptr { code_type, 0 };
// For now just alias these to long
const type& bptr_type = long_type;
const type& bstr_type = long_type;

std::unordered_map<std::string, const type*> typenames {
    { "UNKNOWN", &unknown_type },
    { "CHAR", &char_type },
    { "BYTE", &byte_type },
    { "WORD", &word_type },
    { "LONG", &long_type },
    { "CODE", &code_type },
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
    types.push_back(std::make_unique<type>(base, len));
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
    types.push_back(std::make_unique<type>(base, 0));
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
        return name_;
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
    const char* name_;
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
            if (f.offset_ == struct_field::auto_offset)
                f.offset_ = offset;
            offset = f.end_offset();
        }

        std::sort(fields_.begin(), fields_.end(), [](const struct_field& l, const struct_field& r) { return l.offset() < r.offset(); });

        if (!typenames.insert({ name_, &make_struct_type(*this) }).second) {
            throw std::runtime_error { "Redefinition of struct " + std::string { name_ } };
        }
    }

    structure_definition(structure_definition&) = delete;
    const structure_definition& operator=(structure_definition&) = delete;

    const char* name() const
    {
        return name_;
    }

    const std::vector<struct_field>& fields() const
    {
        return fields_;
    }

    uint32_t size() const
    {
        return static_cast<uint32_t>(fields_.empty() ? 0 : fields_.back().end_offset());
    }

    #if 0
    const struct_field* field_at(int32_t offset) const
    {
        for (const auto& f : fields_) {
            if (offset >= f.offset() && offset < f.end_offset()) {
                if (f.t().struct_def())
                    return f.t().struct_def()->field_at(offset - f.offset());
                return &f;
            }
        }
        return nullptr;
    }
    #endif

    std::pair<bool, std::string> field_name(int32_t offset, bool address_op) const
    {
        for (const auto& f : fields_) {
            if (offset >= f.offset() && offset < f.end_offset()) {
                std::string name = f.name();
                const auto diff = offset - f.offset();
                if (f.t().struct_def() && (!address_op || diff)) {
                    if (const auto [ok, n] = f.t().struct_def()->field_name(diff, address_op); ok) {
                        return { true, name + "_" + n };
                    }
                }
                if (diff)
                    name += "+$" + hexstring(diff, 4);
                return { true, name };
            }
        }
        return { false, "" };
    }

private:
    const char* const name_;
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
    const char* name;
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

    void simulate() const
    {
        if (sim_)
            sim_();
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
    case base_data_type::struct_:
        return os << "struct " << t.struct_def()->name();
    }
    assert(false);
    return os;
}

uint32_t sizeof_type(const type& t)
{
    if (t.ptr())
        return t.len() ? t.len() * sizeof_type(*t.ptr()) : 4;
    switch (t.base()) {
    //case base_data_type::unknown_:
    case base_data_type::char_:
    case base_data_type::byte_:
        return 1;
    case base_data_type::word_:
        return 2;
    case base_data_type::long_:
        return 4;
        //case base_data_type::code_:
        //case base_data_type::ptr_:
    case base_data_type::struct_:
        return t.struct_def()->size();
    }
    std::ostringstream oss;
    oss << "Unhandled type in sizeof_type: " << t;
    throw std::runtime_error { oss.str() };
}

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

// lists.h
const structure_definition List {
    "List",
    {
        { "lh_Head", make_pointer_type(make_struct_type(List)) },
        { "lh_Tail", make_pointer_type(make_struct_type(List)) },
        { "lh_TailPred", make_pointer_type(make_struct_type(List)) },
        { "lh_Type", byte_type },
        { "l_pad", byte_type },
    }
};

const structure_definition MinList {
    "MinList",
    {
        { "mlh_Head", make_pointer_type(make_struct_type(MinList)) },
        { "mlh_Tail", make_pointer_type(make_struct_type(MinList)) },
        { "mlh_TailPred", make_pointer_type(make_struct_type(MinList)) },
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
        { "iv_Node", make_pointer_type(make_struct_type(IntVector)) },
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

// execbase.h

constexpr int32_t _LVOSupervisor = -30;
constexpr int32_t _LVOFindResident = -96;
constexpr int32_t _LVOAllocMem = -198;
constexpr int32_t _LVOFindTask = -294;
constexpr int32_t _LVOAddLibrary = -396;
constexpr int32_t _LVOOldOpenLibrary = -408;
constexpr int32_t _LVOOpenDevice = -444;
constexpr int32_t _LVOOpenLibrary = -552;

const structure_definition ExecBase {
    "ExecBase",
    {
        { "LibNode", make_struct_type(Library) },
        { "SoftVer", word_type },
        { "LowMemChkSum", word_type },
        { "ChkBase", long_type },
        { "ColdCapture", unknown_ptr },
        { "CoolCapture", unknown_ptr },
        { "WarmCapture", unknown_ptr },
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
        { "TaskTrapCode", unknown_ptr },
        { "TaskExceptCode", unknown_ptr },
        { "TaskExitCode", unknown_ptr },
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
        { "ex_Pad0", word_type },
        { "ex_LaunchPoint", long_type },
        { "ex_RamLibPrivate", unknown_ptr },
        { "ex_EClockFrequency", long_type },
        { "ex_CacheControl", long_type },
        { "ex_TaskID", long_type },
        { "ex_Reserved1", make_array_type(long_type, 5) },
        { "ex_MMULock", unknown_ptr },
        { "ex_Reserved2", make_array_type(long_type, 3) },
        { "ex_MemHandlers", make_struct_type(MinList) },
        { "ex_MemHandler", unknown_ptr },

        { "_LVOSupervisor", code_ptr, _LVOSupervisor },
        { "_LVOExitIntr", code_ptr, -36 },
        { "_LVOSchedule", code_ptr, -42 },
        { "_LVOReschedule", code_ptr, -48 },
        { "_LVOSwitch", code_ptr, -54 },
        { "_LVODispatch", code_ptr, -60 },
        { "_LVOException", code_ptr, -66 },
        { "_LVOInitCode", code_ptr, -72 },
        { "_LVOInitStruct", code_ptr, -78 },
        { "_LVOMakeLibrary", code_ptr, -84 },
        { "_LVOMakeFunctions", code_ptr, -90 },
        { "_LVOFindResident", code_ptr, _LVOFindResident },
        { "_LVOInitResident", code_ptr, -102 },
        { "_LVOAlert", code_ptr, -108 },
        { "_LVODebug", code_ptr, -114 },
        { "_LVODisable", code_ptr, -120 },
        { "_LVOEnable", code_ptr, -126 },
        { "_LVOForbid", code_ptr, -132 },
        { "_LVOPermit", code_ptr, -138 },
        { "_LVOSetSR", code_ptr, -144 },
        { "_LVOSuperState", code_ptr, -150 },
        { "_LVOUserState", code_ptr, -156 },
        { "_LVOSetIntVector", code_ptr, -162 },
        { "_LVOAddIntServer", code_ptr, -168 },
        { "_LVORemIntServer", code_ptr, -174 },
        { "_LVOCause", code_ptr, -180 },
        { "_LVOAllocate", code_ptr, -186 },
        { "_LVODeallocate", code_ptr, -192 },
        { "_LVOAllocMem", code_ptr, _LVOAllocMem },
        { "_LVOAllocAbs", code_ptr, -204 },
        { "_LVOFreeMem", code_ptr, -210 },
        { "_LVOAvailMem", code_ptr, -216 },
        { "_LVOAllocEntry", code_ptr, -222 },
        { "_LVOFreeEntry", code_ptr, -228 },
        { "_LVOInsert", code_ptr, -234 },
        { "_LVOAddHead", code_ptr, -240 },
        { "_LVOAddTail", code_ptr, -246 },
        { "_LVORemove", code_ptr, -252 },
        { "_LVORemHead", code_ptr, -258 },
        { "_LVORemTail", code_ptr, -264 },
        { "_LVOEnqueue", code_ptr, -270 },
        { "_LVOFindName", code_ptr, -276 },
        { "_LVOAddTask", code_ptr, -282 },
        { "_LVORemTask", code_ptr, -288 },
        { "_LVOFindTask", code_ptr, _LVOFindTask },
        { "_LVOSetTaskPri", code_ptr, -300 },
        { "_LVOSetSignal", code_ptr, -306 },
        { "_LVOSetExcept", code_ptr, -312 },
        { "_LVOWait", code_ptr, -318 },
        { "_LVOSignal", code_ptr, -324 },
        { "_LVOAllocSignal", code_ptr, -330 },
        { "_LVOFreeSignal", code_ptr, -336 },
        { "_LVOAllocTrap", code_ptr, -342 },
        { "_LVOFreeTrap", code_ptr, -348 },
        { "_LVOAddPort", code_ptr, -354 },
        { "_LVORemPort", code_ptr, -360 },
        { "_LVOPutMsg", code_ptr, -366 },
        { "_LVOGetMsg", code_ptr, -372 },
        { "_LVOReplyMsg", code_ptr, -378 },
        { "_LVOWaitPort", code_ptr, -384 },
        { "_LVOFindPort", code_ptr, -390 },
        { "_LVOAddLibrary", code_ptr, _LVOAddLibrary },
        { "_LVORemLibrary", code_ptr, -402 },
        { "_LVOOldOpenLibrary", code_ptr, _LVOOldOpenLibrary },
        { "_LVOCloseLibrary", code_ptr, -414 },
        { "_LVOSetFunction", code_ptr, -420 },
        { "_LVOSumLibrary", code_ptr, -426 },
        { "_LVOAddDevice", code_ptr, -432 },
        { "_LVORemDevice", code_ptr, -438 },
        { "_LVOOpenDevice", code_ptr, _LVOOpenDevice },
        { "_LVOCloseDevice", code_ptr, -450 },
        { "_LVODoIO", code_ptr, -456 },
        { "_LVOSendIO", code_ptr, -462 },
        { "_LVOCheckIO", code_ptr, -468 },
        { "_LVOWaitIO", code_ptr, -474 },
        { "_LVOAbortIO", code_ptr, -480 },
        { "_LVOAddResource", code_ptr, -486 },
        { "_LVORemResource", code_ptr, -492 },
        { "_LVOOpenResource", code_ptr, -498 },
        { "_LVORawIOInit", code_ptr, -504 },
        { "_LVORawMayGetChar", code_ptr, -510 },
        { "_LVORawPutChar", code_ptr, -516 },
        { "_LVORawDoFmt", code_ptr, -522 },
        { "_LVOGetCC", code_ptr, -528 },
        { "_LVOTypeOfMem", code_ptr, -534 },
        { "_LVOProcure", code_ptr, -540 },
        { "_LVOVacate", code_ptr, -546 },
        { "_LVOOpenLibrary", code_ptr, _LVOOpenLibrary },
        { "_LVOInitSemaphore", code_ptr, -558 },
        { "_LVOObtainSemaphore", code_ptr, -564 },
        { "_LVOReleaseSemaphore", code_ptr, -570 },
        { "_LVOAttemptSemaphore", code_ptr, -576 },
        { "_LVOObtainSemaphoreList", code_ptr, -582 },
        { "_LVOReleaseSemaphoreList", code_ptr, -588 },
        { "_LVOFindSemaphore", code_ptr, -594 },
        { "_LVOAddSemaphore", code_ptr, -600 },
        { "_LVORemSemaphore", code_ptr, -606 },
        { "_LVOSumKickData", code_ptr, -612 },
        { "_LVOAddMemList", code_ptr, -618 },
        { "_LVOCopyMem", code_ptr, -624 },
        { "_LVOCopyMemQuick", code_ptr, -630 },
        { "_LVOCacheClearU", code_ptr, -636 },
        { "_LVOCacheClearE", code_ptr, -642 },
        { "_LVOCacheControl", code_ptr, -648 },
        { "_LVOCreateIORequest", code_ptr, -654 },
        { "_LVODeleteIORequest", code_ptr, -660 },
        { "_LVOCreateMsgPort", code_ptr, -666 },
        { "_LVODeleteMsgPort", code_ptr, -672 },
        { "_LVOObtainSemaphoreShared", code_ptr, -678 },
        { "_LVOAllocVec", code_ptr, -684 },
        { "_LVOFreeVec", code_ptr, -690 },
        { "_LVOCreatePrivatePool", code_ptr, -696 },
        { "_LVODeletePrivatePool", code_ptr, -702 },
        { "_LVOAllocPooled", code_ptr, -708 },
        { "_LVOFreePooled", code_ptr, -714 },
        { "_LVOAttemptSemaphoreShared", code_ptr, -720 },
        { "_LVOColdReboot", code_ptr, -726 },
        { "_LVOStackSwap", code_ptr, -732 },
        { "_LVOChildFree", code_ptr, -738 },
        { "_LVOChildOrphan", code_ptr, -744 },
        { "_LVOChildStatus", code_ptr, -750 },
        { "_LVOChildWait", code_ptr, -756 },
        { "_LVOCachePreDMA", code_ptr, -762 },
        { "_LVOCachePostDMA", code_ptr, -768 },
        { "_LVOAddMemHandler", code_ptr, -774 },
        { "_LVORemMemHandler", code_ptr, -780 },
    }
};

// gfxbase.h

// TODO:
#define TEMP_STRUCT(n) const structure_definition n { #n, {} }
TEMP_STRUCT(View);
TEMP_STRUCT(copinit);
TEMP_STRUCT(bltnode);
TEMP_STRUCT(TextFont);
TEMP_STRUCT(SimpleSprite);
TEMP_STRUCT(SignalSemaphore);
TEMP_STRUCT(MonitorSpec);
TEMP_STRUCT(AnalogSignalInterval);

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
        { "blthd,*blttl", make_pointer_type(make_struct_type(bltnode)) },
        { "bsblthd,*bsblttl", make_pointer_type(make_struct_type(bltnode)) },
        { "vbsrv,timsrv,bltsrv", make_struct_type(Interrupt) },
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
        { "*SimpleSprites", make_pointer_type(make_struct_type(SimpleSprite)) },
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

        { "_LVOBltBitMap", code_ptr, -30 },
        { "_LVOBltTemplate", code_ptr, -36 },
        { "_LVOClearEOL", code_ptr, -42 },
        { "_LVOClearScreen", code_ptr, -48 },
        { "_LVOTextLength", code_ptr, -54 },
        { "_LVOText", code_ptr, -60 },
        { "_LVOSetFont", code_ptr, -66 },
        { "_LVOOpenFont", code_ptr, -72 },
        { "_LVOCloseFont", code_ptr, -78 },
        { "_LVOAskSoftStyle", code_ptr, -84 },
        { "_LVOSetSoftStyle", code_ptr, -90 },
        { "_LVOAddBob", code_ptr, -96 },
        { "_LVOAddVSprite", code_ptr, -102 },
        { "_LVODoCollision", code_ptr, -108 },
        { "_LVODrawGList", code_ptr, -114 },
        { "_LVOInitGels", code_ptr, -120 },
        { "_LVOInitMasks", code_ptr, -126 },
        { "_LVORemIBob", code_ptr, -132 },
        { "_LVORemVSprite", code_ptr, -138 },
        { "_LVOSetCollision", code_ptr, -144 },
        { "_LVOSortGList", code_ptr, -150 },
        { "_LVOAddAnimOb", code_ptr, -156 },
        { "_LVOAnimate", code_ptr, -162 },
        { "_LVOGetGBuffers", code_ptr, -168 },
        { "_LVOInitGMasks", code_ptr, -174 },
        { "_LVODrawEllipse", code_ptr, -180 },
        { "_LVOAreaEllipse", code_ptr, -186 },
        { "_LVOLoadRGB4", code_ptr, -192 },
        { "_LVOInitRastPort", code_ptr, -198 },
        { "_LVOInitVPort", code_ptr, -204 },
        { "_LVOMrgCop", code_ptr, -210 },
        { "_LVOMakeVPort", code_ptr, -216 },
        { "_LVOLoadView", code_ptr, -222 },
        { "_LVOWaitBlit", code_ptr, -228 },
        { "_LVOSetRast", code_ptr, -234 },
        { "_LVOMove", code_ptr, -240 },
        { "_LVODraw", code_ptr, -246 },
        { "_LVOAreaMove", code_ptr, -252 },
        { "_LVOAreaDraw", code_ptr, -258 },
        { "_LVOAreaEnd", code_ptr, -264 },
        { "_LVOWaitTOF", code_ptr, -270 },
        { "_LVOQBlit", code_ptr, -276 },
        { "_LVOInitArea", code_ptr, -282 },
        { "_LVOSetRGB4", code_ptr, -288 },
        { "_LVOQBSBlit", code_ptr, -294 },
        { "_LVOBltClear", code_ptr, -300 },
        { "_LVORectFill", code_ptr, -306 },
        { "_LVOBltPattern", code_ptr, -312 },
        { "_LVOReadPixel", code_ptr, -318 },
        { "_LVOWritePixel", code_ptr, -324 },
        { "_LVOFlood", code_ptr, -330 },
        { "_LVOPolyDraw", code_ptr, -336 },
        { "_LVOSetAPen", code_ptr, -342 },
        { "_LVOSetBPen", code_ptr, -348 },
        { "_LVOSetDrMd", code_ptr, -354 },
        { "_LVOInitView", code_ptr, -360 },
        { "_LVOCBump", code_ptr, -366 },
        { "_LVOCMove", code_ptr, -372 },
        { "_LVOCWait", code_ptr, -378 },
        { "_LVOVBeamPos", code_ptr, -384 },
        { "_LVOInitBitMap", code_ptr, -390 },
        { "_LVOScrollRaster", code_ptr, -396 },
        { "_LVOWaitBOVP", code_ptr, -402 },
        { "_LVOGetSprite", code_ptr, -408 },
        { "_LVOFreeSprite", code_ptr, -414 },
        { "_LVOChangeSprite", code_ptr, -420 },
        { "_LVOMoveSprite", code_ptr, -426 },
        { "_LVOLockLayerRom", code_ptr, -432 },
        { "_LVOUnlockLayerRom", code_ptr, -438 },
        { "_LVOSyncSBitMap", code_ptr, -444 },
        { "_LVOCopySBitMap", code_ptr, -450 },
        { "_LVOOwnBlitter", code_ptr, -456 },
        { "_LVODisownBlitter", code_ptr, -462 },
        { "_LVOInitTmpRas", code_ptr, -468 },
        { "_LVOAskFont", code_ptr, -474 },
        { "_LVOAddFont", code_ptr, -480 },
        { "_LVORemFont", code_ptr, -486 },
        { "_LVOAllocRaster", code_ptr, -492 },
        { "_LVOFreeRaster", code_ptr, -498 },
        { "_LVOAndRectRegion", code_ptr, -504 },
        { "_LVOOrRectRegion", code_ptr, -510 },
        { "_LVONewRegion", code_ptr, -516 },
        { "_LVOClearRectRegion", code_ptr, -522 },
        { "_LVOClearRegion", code_ptr, -528 },
        { "_LVODisposeRegion", code_ptr, -534 },
        { "_LVOFreeVPortCopLists", code_ptr, -540 },
        { "_LVOFreeCopList", code_ptr, -546 },
        { "_LVOClipBlit", code_ptr, -552 },
        { "_LVOXorRectRegion", code_ptr, -558 },
        { "_LVOFreeCprList", code_ptr, -564 },
        { "_LVOGetColorMap", code_ptr, -570 },
        { "_LVOFreeColorMap", code_ptr, -576 },
        { "_LVOGetRGB4", code_ptr, -582 },
        { "_LVOScrollVPort", code_ptr, -588 },
        { "_LVOUCopperListInit", code_ptr, -594 },
        { "_LVOFreeGBuffers", code_ptr, -600 },
        { "_LVOBltBitMapRastPort", code_ptr, -606 },
        { "_LVOOrRegionRegion", code_ptr, -612 },
        { "_LVOXorRegionRegion", code_ptr, -618 },
        { "_LVOAndRegionRegion", code_ptr, -624 },
        { "_LVOSetRGB4CM", code_ptr, -630 },
        { "_LVOBltMaskBitMapRastPort", code_ptr, -636 },
        { "_LVOGraphicsReserved1", code_ptr, -642 },
        { "_LVOGraphicsReserved2", code_ptr, -648 },
        { "_LVOAttemptLockLayerRom", code_ptr, -654 },
        { "_LVOGfxNew", code_ptr, -660 },
        { "_LVOGfxFree", code_ptr, -666 },
        { "_LVOGfxAssociate", code_ptr, -672 },
        { "_LVOBitMapScale", code_ptr, -678 },
        { "_LVOScaleDiv", code_ptr, -684 },
        { "_LVOTextExtent", code_ptr, -690 },
        { "_LVOTextFit", code_ptr, -696 },
        { "_LVOGfxLookUp", code_ptr, -702 },
        { "_LVOVideoControl", code_ptr, -708 },
        { "_LVOOpenMonitor", code_ptr, -714 },
        { "_LVOCloseMonitor", code_ptr, -720 },
        { "_LVOFindDisplayInfo", code_ptr, -726 },
        { "_LVONextDisplayInfo", code_ptr, -732 },
        { "_LVOAddDisplayInfo", code_ptr, -738 },
        { "_LVOAddDisplayInfoData", code_ptr, -744 },
        { "_LVOSetDisplayInfoData", code_ptr, -750 },
        { "_LVOGetDisplayInfoData", code_ptr, -756 },
        { "_LVOFontExtent", code_ptr, -762 },
        { "_LVOReadPixelLine8", code_ptr, -768 },
        { "_LVOWritePixelLine8", code_ptr, -774 },
        { "_LVOReadPixelArray8", code_ptr, -780 },
        { "_LVOWritePixelArray8", code_ptr, -786 },
        { "_LVOGetVPModeID", code_ptr, -792 },
        { "_LVOModeNotAvailable", code_ptr, -798 },
        { "_LVOWeighTAMatch", code_ptr, -804 },
        { "_LVOEraseRect", code_ptr, -810 },
        { "_LVOExtendFont", code_ptr, -816 },
        { "_LVOStripFont", code_ptr, -822 },
        { "_LVOCalcIVG", code_ptr, -828 },
        { "_LVOAttachPalExtra", code_ptr, -834 },
        { "_LVOObtainBestPenA", code_ptr, -840 },
        { "_LVOGfxInternal3", code_ptr, -846 },
        { "_LVOSetRGB32", code_ptr, -852 },
        { "_LVOGetAPen", code_ptr, -858 },
        { "_LVOGetBPen", code_ptr, -864 },
        { "_LVOGetDrMd", code_ptr, -870 },
        { "_LVOGetOutlinePen", code_ptr, -876 },
        { "_LVOLoadRGB32", code_ptr, -882 },
        { "_LVOSetChipRev", code_ptr, -888 },
        { "_LVOSetABPenDrMd", code_ptr, -894 },
        { "_LVOGetRGB32", code_ptr, -900 },
        { "_LVOGfxSpare1", code_ptr, -906 },
        { "_LVOAllocBitMap", code_ptr, -918 },
        { "_LVOFreeBitMap", code_ptr, -924 },
        { "_LVOGetExtSpriteA", code_ptr, -930 },
        { "_LVOCoerceMode", code_ptr, -936 },
        { "_LVOChangeVPBitMap", code_ptr, -942 },
        { "_LVOReleasePen", code_ptr, -948 },
        { "_LVOObtainPen", code_ptr, -954 },
        { "_LVOGetBitMapAttr", code_ptr, -960 },
        { "_LVOAllocDBufInfo", code_ptr, -966 },
        { "_LVOFreeDBufInfo", code_ptr, -972 },
        { "_LVOSetOutlinePen", code_ptr, -978 },
        { "_LVOSetWriteMask", code_ptr, -984 },
        { "_LVOSetMaxPen", code_ptr, -990 },
        { "_LVOSetRGB32CM", code_ptr, -996 },
        { "_LVOScrollRasterBF", code_ptr, -1002 },
        { "_LVOFindColor", code_ptr, -1008 },
        { "_LVOGfxSpare2", code_ptr, -1014 },
        { "_LVOAllocSpriteDataA", code_ptr, -1020 },
        { "_LVOChangeExtSpriteA", code_ptr, -1026 },
        { "_LVOFreeSpriteData", code_ptr, -1032 },
        { "_LVOSetRPAttrsA", code_ptr, -1038 },
        { "_LVOGetRPAttrsA", code_ptr, -1044 },
        { "_LVOBestModeIDA", code_ptr, -1050 },
    }
};

// Dos
TEMP_STRUCT(RootNode);
TEMP_STRUCT(ErrorString);
TEMP_STRUCT(timerequest);
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

        { "_LVOOpen", code_ptr, -30 },
        { "_LVOClose", code_ptr, -36 },
        { "_LVORead", code_ptr, -42 },
        { "_LVOWrite", code_ptr, -48 },
        { "_LVOInput", code_ptr, -54 },
        { "_LVOOutput", code_ptr, -60 },
        { "_LVOSeek", code_ptr, -66 },
        { "_LVODeleteFile", code_ptr, -72 },
        { "_LVORename", code_ptr, -78 },
        { "_LVOLock", code_ptr, -84 },
        { "_LVOUnLock", code_ptr, -90 },
        { "_LVODupLock", code_ptr, -96 },
        { "_LVOExamine", code_ptr, -102 },
        { "_LVOExNext", code_ptr, -108 },
        { "_LVOInfo", code_ptr, -114 },
        { "_LVOCreateDir", code_ptr, -120 },
        { "_LVOCurrentDir", code_ptr, -126 },
        { "_LVOIoErr", code_ptr, -132 },
        { "_LVOCreateProc", code_ptr, -138 },
        { "_LVOExit", code_ptr, -144 },
        { "_LVOLoadSeg", code_ptr, -150 },
        { "_LVOUnLoadSeg", code_ptr, -156 },
        { "_LVOGetPacket", code_ptr, -162 },
        { "_LVOQueuePacket", code_ptr, -168 },
        { "_LVODeviceProc", code_ptr, -174 },
        { "_LVOSetComment", code_ptr, -180 },
        { "_LVOSetProtection", code_ptr, -186 },
        { "_LVODateStamp", code_ptr, -192 },
        { "_LVODelay", code_ptr, -198 },
        { "_LVOWaitForChar", code_ptr, -204 },
        { "_LVOParentDir", code_ptr, -210 },
        { "_LVOIsInteractive", code_ptr, -216 },
        { "_LVOExecute", code_ptr, -222 },
        { "_LVOAllocDosObject", code_ptr, -228 },
        { "_LVOFreeDosObject", code_ptr, -234 },
        { "_LVODoPkt", code_ptr, -240 },
        { "_LVOSendPkt", code_ptr, -246 },
        { "_LVOWaitPkt", code_ptr, -252 },
        { "_LVOReplyPkt", code_ptr, -258 },
        { "_LVOAbortPkt", code_ptr, -264 },
        { "_LVOLockRecord", code_ptr, -270 },
        { "_LVOLockRecords", code_ptr, -276 },
        { "_LVOUnLockRecord", code_ptr, -282 },
        { "_LVOUnLockRecords", code_ptr, -288 },
        { "_LVOSelectInput", code_ptr, -294 },
        { "_LVOSelectOutput", code_ptr, -300 },
        { "_LVOFGetC", code_ptr, -306 },
        { "_LVOFPutC", code_ptr, -312 },
        { "_LVOUnGetC", code_ptr, -318 },
        { "_LVOFRead", code_ptr, -324 },
        { "_LVOFWrite", code_ptr, -330 },
        { "_LVOFGets", code_ptr, -336 },
        { "_LVOFPuts", code_ptr, -342 },
        { "_LVOVFWritef", code_ptr, -348 },
        { "_LVOVFPrintf", code_ptr, -354 },
        { "_LVOFlush", code_ptr, -360 },
        { "_LVOSetVBuf", code_ptr, -366 },
        { "_LVODupLockFromFH", code_ptr, -372 },
        { "_LVOOpenFromLock", code_ptr, -378 },
        { "_LVOParentOffH", code_ptr, -384 },
        { "_LVOExamineFH", code_ptr, -390 },
        { "_LVOSetFileDate", code_ptr, -396 },
        { "_LVONameFromLock", code_ptr, -402 },
        { "_LVONameFromFH", code_ptr, -408 },
        { "_LVOSplitName", code_ptr, -414 },
        { "_LVOSameLock", code_ptr, -420 },
        { "_LVOSetMode", code_ptr, -426 },
        { "_LVOExAll", code_ptr, -432 },
        { "_LVOReadLink", code_ptr, -438 },
        { "_LVOMakeLink", code_ptr, -444 },
        { "_LVOChangeMode", code_ptr, -450 },
        { "_LVOSetFileSize", code_ptr, -456 },
        { "_LVOSetIoErr", code_ptr, -462 },
        { "_LVOFault", code_ptr, -468 },
        { "_LVOPrintFault", code_ptr, -474 },
        { "_LVOErrorReport", code_ptr, -480 },
        { "_LVOCli", code_ptr, -492 },
        { "_LVOCreateNewProc", code_ptr, -498 },
        { "_LVORunCommand", code_ptr, -504 },
        { "_LVOGetConsoleTask", code_ptr, -510 },
        { "_LVOSetConsoleTask", code_ptr, -516 },
        { "_LVOGetFileSysTask", code_ptr, -522 },
        { "_LVOSetFileSysTask", code_ptr, -528 },
        { "_LVOGetArgStr", code_ptr, -534 },
        { "_LVOSetArgStr", code_ptr, -540 },
        { "_LVOFindCliProc", code_ptr, -546 },
        { "_LVOMaxCli", code_ptr, -552 },
        { "_LVOSetCurrentDirName", code_ptr, -558 },
        { "_LVOGetCurrentDirName", code_ptr, -564 },
        { "_LVOSetProgramName", code_ptr, -570 },
        { "_LVOGetProgramName", code_ptr, -576 },
        { "_LVOSetPrompt", code_ptr, -582 },
        { "_LVOGetPrompt", code_ptr, -588 },
        { "_LVOSetProgramDir", code_ptr, -594 },
        { "_LVOGetProgramDir", code_ptr, -600 },
        { "_LVOSystemTagList", code_ptr, -606 },
        { "_LVOAssignLock", code_ptr, -612 },
        { "_LVOAssignLate", code_ptr, -618 },
        { "_LVOAssignPath", code_ptr, -624 },
        { "_LVOAssignAdd", code_ptr, -630 },
        { "_LVORemAssignList", code_ptr, -636 },
        { "_LVOGetDeviceProc", code_ptr, -642 },
        { "_LVOFreeDeviceProc", code_ptr, -648 },
        { "_LVOLockDosList", code_ptr, -654 },
        { "_LVOUnLockDosList", code_ptr, -660 },
        { "_LVOAttemptLockDosList", code_ptr, -666 },
        { "_LVORemDosEntry", code_ptr, -672 },
        { "_LVOAddDosEntry", code_ptr, -678 },
        { "_LVOFindDosEntry", code_ptr, -684 },
        { "_LVONextDosEntry", code_ptr, -690 },
        { "_LVOMakeDosEntry", code_ptr, -696 },
        { "_LVOFreeDosEntry", code_ptr, -702 },
        { "_LVOIsFileSystem", code_ptr, -708 },
        { "_LVOFormat", code_ptr, -714 },
        { "_LVORelabel", code_ptr, -720 },
        { "_LVOInhibit", code_ptr, -726 },
        { "_LVOAddBuffers", code_ptr, -732 },
        { "_LVOCompareDates", code_ptr, -738 },
        { "_LVODateToStr", code_ptr, -744 },
        { "_LVOStrToDate", code_ptr, -750 },
        { "_LVOInternalLoadSeg", code_ptr, -756 },
        { "_LVOInternalUnLoadSeg", code_ptr, -762 },
        { "_LVONewLoadSeg", code_ptr, -768 },
        { "_LVOAddSegment", code_ptr, -774 },
        { "_LVOFindSegment", code_ptr, -780 },
        { "_LVORemSegment", code_ptr, -786 },
        { "_LVOCheckSignal", code_ptr, -792 },
        { "_LVOReadArgs", code_ptr, -798 },
        { "_LVOFindArg", code_ptr, -804 },
        { "_LVOReadItem", code_ptr, -810 },
        { "_LVOStrToLong", code_ptr, -816 },
        { "_LVOMatchFirst", code_ptr, -822 },
        { "_LVOMatchNext", code_ptr, -828 },
        { "_LVOMatchEnd", code_ptr, -834 },
        { "_LVOParsePattern", code_ptr, -840 },
        { "_LVOMatchPattern", code_ptr, -846 },
        { "_LVODosNameFromAnchor", code_ptr, -852 },
        { "_LVOFreeArgs", code_ptr, -858 },
        { "_LVOFilePart", code_ptr, -870 },
        { "_LVOPathPart", code_ptr, -876 },
        { "_LVOAddPart", code_ptr, -882 },
        { "_LVOStartNotify", code_ptr, -888 },
        { "_LVOEndNotify", code_ptr, -894 },
        { "_LVOSetVar", code_ptr, -900 },
        { "_LVOGetVar", code_ptr, -906 },
        { "_LVODeleteVar", code_ptr, -912 },
        { "_LVOFindVar", code_ptr, -918 },
        { "_LVOCliInit", code_ptr, -924 },
        { "_LVOCliInitNewCli", code_ptr, -930 },
        { "_LVOCliInitRun", code_ptr, -936 },
        { "_LVOWriteChars", code_ptr, -942 },
        { "_LVOPutStr", code_ptr, -948 },
        { "_LVOVPrintf", code_ptr, -954 },
        { "_LVOParsePatternNoCase", code_ptr, -966 },
        { "_LVOMatchPatternNoCase", code_ptr, -972 },
        { "_LVODosGetString", code_ptr, -978 },
        { "_LVOSameDevice", code_ptr, -984 },
        { "_LVOExAllEnd", code_ptr, -990 },
        { "_LVOSetOwner", code_ptr, -996 },
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
        { "pr_CLI", bptr_type },
        { "pr_ReturnAddr", unknown_ptr },
        { "pr_PktWait", unknown_ptr },
        { "pr_WindowPtr", unknown_ptr },
        { "pr_HomeDir", bptr_type },
        { "pr_Flags", long_type },
        { "pr_ExitCode", code_ptr }, // void (*pr_ExitCode)()
        { "pr_ExitData", long_type },
        { "pr_Arguments", byte_ptr },
        { "pr_LocalVars", make_struct_type(MinList) },
        { "pr_ShellPrivate", long_type },
        { "pr_CES", bptr_type },
    }
};

class analyzer {
public:
    explicit analyzer()
        : written_(max_mem)
        , data_(max_mem)
        , regs_ {}
    {
    }

    static constexpr uint32_t fake_exec_base = 0xcc000000;

    void read_info_file(const std::string& filename)
    {
        std::ifstream in { filename };
        if (!in || !in.is_open())
            throw std::runtime_error { "Could not open " + filename };

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
            std::vector<std::string> parts;
            std::istringstream iss { line.substr(start, end - start) };
            for (std::string part; std::getline(iss, part, ' ');) {
                if (!part.empty())
                    parts.push_back(part);
            }
            if (parts.size() != 3)
                throw std::runtime_error { "Error in " + filename + " line " + std::to_string(linenum) + ": " + line };
            const auto [ok, addr] = from_hex(parts[0]);
            if (!ok)
                throw std::runtime_error { "Invalid address in " + filename + " line " + std::to_string(linenum) + ": " + line };

            std::string basetype;
            size_t p = 0;
            while (p < parts[1].length() && parts[1][p] != '*' && parts[1][p] != '[')
                basetype.push_back(parts[1][p++]);

            const type* t = nullptr;
            if (auto it = typenames.find(basetype); it != typenames.end())
                t = it->second;
            if (!t)
                throw std::runtime_error { "Invalid base type in " + filename + " line " + std::to_string(linenum) + ": " + line + " \"" + basetype + "\"" };

            for (; p < parts[1].length(); ++p) {
                if (parts[1][p] == '*')
                    t = &make_pointer_type(*t);
                else if (parts[1][p] == '[' && ++p < parts[1].length()) {
                    uint32_t len = 0;
                    while (p < parts[1].length() && isdigit(parts[1][p]))
                        len = len * 10 + (parts[1][p++] - '0');
                    if (p >= parts[1].length() || parts[1][p] != ']')
                        goto typeerr;
                    ++p;
                    t = &make_array_type(*t, len);
                } else {
                typeerr:
                    throw std::runtime_error { "Unsupported type in " + filename + " line " + std::to_string(linenum) + ": " + line + " \"" + parts[1] + "\"" };
                }
            }

            predef_info_.push_back({ addr, { parts[2], t } });
        }
    }

    void write_data(uint32_t addr, const uint8_t* data, uint32_t length)
    {
        if (static_cast<size_t>(addr) + length > data_.size())
            throw std::runtime_error { "Out of range" };
        memcpy(&data_[addr], data, length);
        for (uint32_t i = 0; i < length; ++i)
            written_[i + addr] = true;
        insert_area(addr, addr + length);
    }

    void add_root(uint32_t addr, const simregs& regs, bool force = false)
    {
        if (addr >= max_mem || (addr & 1))
            return;

        // Don't visit non-written areas
        if (!force && (!written_[addr] || addr < 0x400))
            return;

        if (visited_.find(addr) == visited_.end()) {
            roots_.push({ addr, regs });
            visited_[addr] = regs; // Mark visitied now to avoid re-insertion
        }
        add_auto_label(addr, code_type);
    }

    void add_label(uint32_t addr, const std::string& name, const type& t)
    {
        auto it = labels_.find(addr);
        if (it == labels_.end())
            labels_[addr] = { name, &t };
        else if (it->second.t != &t) {
            if (it->second.t == &unknown_type)
                it->second.t = &t;
            else
                std::cerr << "Type conflict for " << name << " was " << *it->second.t << " now " << t << "\n";
        }
    }

    void add_auto_label(uint32_t addr, const type& t)
    {
        if (addr < 0x400 && (addr & 3) == 0) {
            // Interrupt vector
            const auto vec = addr >> 2;
            std::string lab = "Interrupt" + hexstring(vec, 2) + "Vec";
            switch (vec) {
            case 0: // Reset ssp
            case 1: // Reset PC
                return;
            case 2:
                lab = "BusErrorVec";
                break;
            case 3:
                lab = "BusErrorVec";
                break;
            case 4:
                lab = "IllegalInstructionVec";
                break;
            case 5:
                lab = "ZeroDivideVec";
                break;
            case 6:
                lab = "ChkExceptionVec";
                break;
            case 7:
                lab = "TrapVExceptionVec";
                break;
            case 8:
                lab = "PrivililegeViolationVec";
                break;
            case 9:
                lab = "TraceExceptionVec";
                break;
            case 10:
                lab = "Line1010ExceptionVec";
                break;
            case 11:
                lab = "Line1111ExceptionVec";
                break;
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
                lab = "Level" + std::to_string(vec - 24) + "Vec";
                break;
            default:
                if (vec >= 32 && vec < 48)
                    lab = "Trap" + hexstring(vec - 32, 1) + "Vec";
            }
            add_label(addr, lab, code_ptr);
            return;
        }

        add_label(addr, "lab_" + hexstring(addr), t);
    }

    void add_library(const std::string& name, const std::string& id, uint32_t addr, const type& t)
    {
        if (library_bases_.find(name) != library_bases_.end())
            throw std::runtime_error { name + " already added" };
        add_label(addr, id, t);
        library_bases_.insert({ name, addr });
    }

    void run()
    {
        handle_predef_info();

        if (!exec_base_) {
            // add fake library bases
            exec_base_ = fake_exec_base;
            add_library("graphics.library", "GfxBase", exec_base_ + 1 * 0x10000, make_struct_type(GfxBase));
            add_library("dos.library", "DosBase", exec_base_ + 2 * 0x10000, make_struct_type(DosBase));
        } else {
            throw std::runtime_error { "TODO: Support exec_base being present already" };
        }


        saved_pointers_.insert({ 4, exec_base_ });
        add_library("exec.library", "SysBase", exec_base_, make_struct_type(ExecBase));
        fake_process_ = alloc_fake_mem(Process.size());
        add_label(fake_process_, "FakeProcess", make_struct_type(Process));
        // exec.library
        functions_.insert({ exec_base_ + _LVOSupervisor, function_description {
                                                             {},
                                                             { { regname::A5, "userFunc", &code_ptr } },
                                                         } });
        functions_.insert({ exec_base_ + _LVOFindResident, function_description {
                                                             {},
                                                             { { regname::A1, "name", &char_ptr } },
                                                         } });

        functions_.insert({ exec_base_ + _LVOAllocMem, function_description {
                                                             {},
                                                           { { regname::D0, "byteSize", &long_type }, { regname::D1, "attributes", &long_type } }, [this]() {
                                                                                 do_alloc_mem();
                                                                             }
                                                         } });
        functions_.insert({ exec_base_ + _LVOFindTask, function_description { {}, { { regname::A1, "name", &char_ptr } }, [this]() {
                                                                                 regs_.d[0] = simval { fake_process_ };
                                                                             } } });
        functions_.insert({ exec_base_ + _LVOAddLibrary, function_description { {}, { { regname::A1, "library", &make_pointer_type(make_struct_type(Library)) } } } });
        auto open_library = [this]() { do_open_library(); };
        functions_.insert({ exec_base_ + _LVOOldOpenLibrary, function_description {
                                                             {},
                                                             { { regname::A1, "libName", &char_ptr } },
                open_library } });
        //_LVOOpenDevice
        functions_.insert({ exec_base_ + _LVOOpenDevice, function_description { {}, {
                                                                                        { regname::A0, "devName", &char_ptr },
                                                                                        { regname::D0, "unitNumber", &byte_type },
                                                                                        { regname::A1, "iORequest", &make_pointer_type(make_struct_type(IOStdReq)) },
                                                                                        { regname::D1, "flags", &byte_type }, 
                                                                                    },
                                                             /*open_device*/ } });
        functions_.insert({ exec_base_ + _LVOOpenLibrary, function_description { {}, {
                                                                                         { regname::A1, "libName", &char_ptr },
                                                                                         { regname::D0, "version", &long_type },
                                                                                     },
                                                              open_library} });        

        while (!roots_.empty()) {
            const auto r = roots_.front();
            roots_.pop();
            regs_ = r.second;
            trace(r.first);
        }

        for (const auto& area : areas_) {
            uint32_t pos = area.beg;

            while (pos < area.end) {
                auto it = visited_.find(pos);
                if (it == visited_.end()) {
                    const auto next_visited = visited_.upper_bound(pos);
                    const auto next_visited_pos = std::min(area.end, next_visited == visited_.end() ? ~0U : next_visited->first);
                    handle_data_area(pos, next_visited_pos);
                    pos = next_visited_pos;
                    continue;
                }

                maybe_print_label(pos);
                uint16_t iwords[max_instruction_words];
                read_instruction(iwords, pos);
                const auto& inst = instructions[iwords[0]];

                regs_ = it->second;

                std::ostringstream extra;

                std::cout << "\t" << inst.name;
                for (int i = 0; i < inst.nea; ++i) {
                    const auto ea = inst.ea[i];
                    std::cout << (i ? ", " : "\t");

                    auto do_known = [&](std::string lab = "") {
                        if (!ea_val_[i].known())
                            return;
                        if (lab.empty())
                            lab = ea_string(ea);
                        if (ea_val_[i].known()) {
                            extra << "\t" << lab << "=$" << hexfmt(ea_val_[i].raw());
                        }
                    };

                    switch (ea >> ea_m_shift) {
                    case ea_m_Dn:
                        std::cout << "D" << (ea & ea_xn_mask);
                        do_known();
                        break;
                    case ea_m_An:
                        std::cout << "A" << (ea & ea_xn_mask);
                        do_known();
                        break;
                    case ea_m_A_ind:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_post:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")+";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_pre:
                        std::cout << "-(A" << (ea & ea_xn_mask) << ")";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_disp16: {
                        auto n = static_cast<int16_t>(ea_data_[i]);
                        const auto& aval = regs_.a[ea & 7];
                        std::ostringstream desc;
                        desc << "$";
                        if (n < 0) {
                            desc << "-";
                            n = -n;
                        }
                        desc << hexfmt(static_cast<uint16_t>(n));
                        desc << "(A" << (ea & 7) << ")";
                        if (aval.known()) {
                            const int32_t offset = static_cast<int16_t>(ea_data_[i]);
                            const auto addr = aval.raw() + offset;
                            // Custom reg
                            if (addr >= 0xDE0000 && addr < 0xE00000) {
                                const auto ra = addr & ~0x1ff;
                                std::cout << custom_regname[(addr >> 1) & 0xff];
                                if (int ofs = (addr & 1) - (aval.raw() & 0x1ff); ofs != 0)
                                    std::cout << (ofs > 0 ? "+" : "-") << (ofs > 0 ? ofs : -ofs);
                                std::cout << "(A" << (ea & 7) << ")";
                                break;
                            } else if (auto lit = labels_.find(aval.raw()); lit != labels_.end()) {
                                const auto& t = *lit->second.t;
                                extra << "\t" << "A" << (ea & 7) << " = " << lit->second.name << " (" << t << ")";
                                if (t.struct_def()) {
                                    #if 0
                                    if (auto f = t.struct_def()->field_at(offset)) {
                                        std::cout << f->name() <<  "(A" << (ea & 7) << ")";
                                        break;
                                    }
                                    #endif
                                    if (const auto [ok, name] = t.struct_def()->field_name(offset, inst.type == inst_type::LEA); ok) {
                                        std::cout << name << "(A" << (ea & 7) << ")";
                                        break;
                                    }
                                }
                            } else {
                                extra << "\t" << desc.str() << " = $" << hexfmt(addr);
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
                        desc << "$";
                        if (disp < 0) {
                            desc << "-";
                            disp = -disp;
                        }
                        desc << hexfmt(static_cast<uint8_t>(disp)) << "(A" << (ea & 7) << ",";
                        desc << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                        desc << ")";
                        // TODO: Handle know values..
                        std::cout << desc.str();
                        break;
                    }
                    case ea_m_Other:
                        switch (ea & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l:
                            print_addr(ea_addr_[i].raw());
                            break;
                        case ea_other_pc_disp16:
                            print_addr(ea_addr_[i].raw());
                            std::cout << "(PC)";
                            break;
                        case ea_other_pc_index: {
                            const auto extw = ea_data_[i];
                            // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                            auto disp = static_cast<int8_t>(extw & 255);
                            std::cout << "$";
                            if (disp < 0) {
                                std::cout << "-";
                                disp = -disp;
                            }
                            std::cout << hexfmt(static_cast<uint8_t>(disp)) << "(PC,";
                            std::cout << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                            std::cout << ")";
                            // TODO: Handle know values..
                            break;
                        }
                        case ea_other_imm:
                            std::cout << "#";
                            if (inst.size != opsize::l || ea_data_[i] < 0x400 || !print_addr_maybe(ea_data_[i]))
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
                        } else if (ea == ea_disp) {
                            print_addr(ea_addr_[i].raw());
                        } else {
                            if (inst.size == opsize::l && inst.data > 0x400 && labels_.find(inst.data) != labels_.end()) {
                                std::cout << "#";
                                print_addr(inst.data);
                            } else {
                                std::cout << "#$" << hexfmt(inst.data);
                            }
                        }
                        break;
                    }
                }
                if (!extra.str().empty()) {
                    std::cout << "\t;" << extra.str();
                }
                std::cout << "\n";
                pos += inst.ilen * 2;
            }
        }
    }

private:
    static constexpr uint32_t max_mem = 1 << 24;
    struct area {
        uint32_t beg;
        uint32_t end;
    };
    struct label_info {
        std::string name;
        const type* t;
    };
    std::vector<bool> written_;
    std::vector<uint8_t> data_;
    std::queue<std::pair<uint32_t, simregs>> roots_;
    std::map<uint32_t, simregs> visited_;
    std::map<uint32_t, label_info> labels_;
    std::map<uint32_t, function_description> functions_;
    simregs regs_;
    std::vector<area> areas_;
    std::vector<std::pair<uint32_t, label_info>> predef_info_;
    uint32_t exec_base_ = 0;
    std::map<std::string, uint32_t> library_bases_;
    uint32_t fake_process_ = 0;

    uint32_t ea_data_[2];
    simval ea_addr_[2];
    simval ea_val_[2];
    std::map<uint32_t, uint32_t> saved_pointers_;
    uint32_t alloc_top_ = 0xa00000; // serve memory (from AllocMem) from here..

    void insert_area(uint32_t beg, uint32_t end)
    {
        auto it = areas_.begin();
        for (; it != areas_.end(); ++it) {
            //return a0 <= b1 && b0 <= a1;
            if (beg <= it->end && it->beg <= end)
                throw std::runtime_error { "Area overlap" };
            if (beg < it->beg)
                break;
        }
        areas_.insert(it, { beg, end });
    }

    std::map<uint32_t, label_info>::const_iterator maybe_print_label(uint32_t pos) const
    {
        auto it = labels_.find(pos);
        if (it != labels_.end()) {
            std::cout << std::setw(32) << std::left << it->second.name << "\t; $" << hexfmt(it->first) << " " << *it->second.t << "\n";
        }
        return it;
    }

    void handle_array_range(uint32_t& pos, uint32_t end, uint32_t elemsize)
    {
        const uint32_t startpos = pos;
        assert(elemsize == 1 || elemsize == 2 || elemsize == 4);
        assert((end - startpos) % elemsize == 0);
        const char suffix = elemsize == 1 ? 'b' : elemsize == 2 ? 'w' : 'l';
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
                std::cout << "\tds." << suffix << "\t$" << hexfmt(runlen, runlen < 256 ? 2 : runlen < 65536 ? 4 : 8)
                          << ", $" << hexfmt(val, 2 * elemsize) << "\n";
                pos += runlen * elemsize;
                continue;
            }

            std::cout << "\tdc." << suffix << "\t";
            for (uint32_t i = 0; i < here && pos < end; ++i) {
                if (i)
                    std::cout << ", ";
                std::cout << "$";
                if (elemsize == 1)
                    std::cout << hexfmt(data_[pos]);
                else if (elemsize == 2)
                    std::cout << hexfmt(get_u16(&data_[pos]));
                else
                    std::cout << hexfmt(get_u32(&data_[pos]));
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
                std::cout << "\tdc.b\t";
                linepos = 16;
                in = false;
            }
            const uint8_t c = data_[pos];
            if (c >= ' ' && c < 128) {
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

    bool handle_typed_data(uint32_t& pos, uint32_t next_pos, const type& t, std::string name="")
    {
        switch (t.base()) {
        case base_data_type::unknown_:
        case base_data_type::code_:
            return false;
        case base_data_type::char_:
        case base_data_type::byte_:
            std::cout << "\tdc.b\t$" << hexfmt(data_[pos]) << "\n";
            ++pos;
            return true;
        case base_data_type::word_:
            std::cout << "\tdc.w\t$" << hexfmt(get_u16(&data_[pos])) << "\n";
            pos += 2;
            return true;
        case base_data_type::long_:
            std::cout << "\tdc.l\t$" << hexfmt(get_u32(&data_[pos])) << "\n";
            pos += 4;
            return true;
        case base_data_type::ptr_:
            if (t.len()) {
                if (t.ptr()->base() == base_data_type::char_) {
                    handle_char_array(pos, next_pos, t.len());
                } else {
                    const auto elem_size = sizeof_type(*t.ptr());
                    if (elem_size > 4) {
                        assert(false);
                        throw std::runtime_error { "TODO: Handle array of something other than elementary types" };
                    }
                    handle_array_range(pos, std::min(pos + elem_size * t.len(), next_pos), elem_size);
                }
            } else {
                std::cout << "\tdc.l\t";
                if (!print_addr_maybe(get_u32(&data_[pos])))
                    std::cout << "$" << hexfmt(get_u32(&data_[pos]));
                std::cout << "\n";
                pos += 4;
            }
            return true;
        case base_data_type::struct_: {
            const auto end_pos = pos + t.struct_def()->size();
            if (end_pos > next_pos) {
                std::cerr << "WARNING: Structure size goes beyond next_pos!\n";
                assert(!"TODO");
                return false;
            }
            for (const auto& f : t.struct_def()->fields()) {
                if (f.offset() < 0)
                    continue;
                const auto n = name + "." + f.name();
                auto p = pos + f.offset();
                std::cout << "; " << std::left << std::setw(32) << n << " $" << hexfmt(p) << " " << f.t() << "\n";
                handle_typed_data(p, next_pos, f.t(), n);
            }
            pos = end_pos;
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
        if (labels_.find(pos) == labels_.end())
            std::cout << "; $" << hexfmt(pos) << "\n";
        while (pos < end) {
            const auto next_label = labels_.upper_bound(pos);
            const auto next_pos = std::min(end, next_label == labels_.end() ? ~0U : next_label->first);

            if (auto it = maybe_print_label(pos); it != labels_.end() && handle_typed_data(pos, next_pos, *it->second.t, it->second.name))
                continue;

            // ALIGN
            if ((pos & 1) || next_pos == pos + 1) {
                std::cout << "\tdc.b\t$" << hexfmt(data_[pos]) << "\n";
                if (++pos == next_pos)
                    continue;
            }

            handle_array_range(pos, next_pos & ~1, 2);
        }
    }

    bool print_addr_maybe(uint32_t addr)
    {
        if (auto it = labels_.find(addr); it != labels_.end()) {
            std::cout << it->second.name;
            return true;
        }

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
            std::cout << cia_regname[(addr >> 8) & 0xf];
            return true;
        }

        if (addr == 0xdff000) {
            std::cout << "CustomBase";
            return true;
        }
        if (addr >= 0xDE0000 && addr < 0xE00000) {
            std::string regname = custom_regname[(addr >> 1) & 0xff];
            for (auto& r : regname) {
                // tolower
                if (r >= 'A' && r <= 'Z')
                    r += 32;
            }
            std::cout << regname;
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
        if ((addr & 1) || addr < 0x400 || addr > max_mem - 2)
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
                ea_val_[i] = read_mem(ea_addr_[i]);
                // TODO: increment
                break;
            case ea_m_A_ind_pre:
                // TODO: decrement
                // TODO: ea_val_, ea_addr_
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
                    const auto extw = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(extw);
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

    void do_open_library()
    {
        if (!regs_.a[1].known())
            return;
        auto ptr = regs_.a[1].raw();
        if (!pointer_ok(ptr))
            return;
        std::string libname;
        while (ptr < max_mem && written_[ptr] && data_[ptr]) {
            auto c = data_[ptr++];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            libname.push_back(c);
        }
        if (auto it = library_bases_.find(libname); it != library_bases_.end()) {
            regs_.d[0] = simval { it->second };
            return;
        }
        throw std::runtime_error { "Library not found: \"" + libname + "\"" };
    }

    uint32_t alloc_fake_mem(uint32_t size)
    {
        if (size < alloc_top_ && !written_[alloc_top_ - size]) {
            alloc_top_ -= size;
            std::fill(written_.begin() + alloc_top_, written_.begin() + alloc_top_ + size, true); // So assignments will be tracked and pointers are deemed OK
            return alloc_top_;
        }
        return 0;
    }

    void do_alloc_mem()
    {
        if (regs_.d[0].known()) {
            const auto size = regs_.d[0].raw();
            if (uint32_t ptr = alloc_fake_mem(size))
                return;
            else
                std::cerr << "AllocMem failed for size: $" << hexfmt(size) << " alloc_top_=$" << hexfmt(alloc_top_) << "\n";
        }
        regs_.d[0] = simval {};
    }

    void do_branch(uint32_t addr, bool kill_std_regs)
    {
        auto it = functions_.find(addr);
        if (it == functions_.end()) {
            add_root(addr, regs_);
            if (kill_std_regs) {
                regs_.a[0] = simval {};
                regs_.a[1] = simval {};
                regs_.d[0] = simval {};
                regs_.d[1] = simval {};
            }
            return;
        }
        const auto& fd = it->second;
        for (const auto& i : fd.input()) {
            //std::cerr << "TODO: " << i.reg << " contains " << i.name << " (" << *i.t << ")\n";
            if (i.reg >= regname::A0 && i.reg < regname::A7) {
                if (auto pt = i.t->ptr()) {
                    const auto& areg = regs_.a[static_cast<uint8_t>(i.reg) - static_cast<uint8_t>(regname::A0)];
                    if (areg.known()) {                        
                        if (pt == &code_type) {
                            add_root(areg.raw(), regs_);
                        } else if (pt == &char_type) {
                            maybe_find_string_at(areg.raw());
                        } else if (pt->struct_def()) {
                            //handle_struct_at(areg.raw(), *pt->struct_def());
                            add_auto_label(areg.raw(), *pt);
                        }
                    }
                }
            }
        }
        if (!fd.output().empty())
            throw std::runtime_error { "TODO: update regs not implemented in do_branch" };
        fd.simulate();
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

            #if 0
            print_sim_regs();
            disasm(std::cout, start, iwords, inst.ilen);
            std::cout << "\n";
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
                add_root(ea_addr_[1].raw(), regs_);
                break;

            case inst_type::JSR:
            case inst_type::JMP:
                assert(inst.nea == 1);
                if (ea_addr_[0].known())
                    do_branch(ea_addr_[0].raw(), true);
                if (inst.type == inst_type::JMP)
                    return;
                break;

            case inst_type::ILLEGAL:
            case inst_type::RTS:
            case inst_type::RTE:
            case inst_type::RTR:
                return;

            default:
                for (int i = 0; i < inst.nea; ++i) {
                    if (!ea_addr_[i].known())
                        continue;
                    const auto ea_addr = ea_addr_[i].raw() & 0xffffff;
                    if (ea_addr >= written_.size() || !written_[ea_addr])
                        continue;

                    const auto& t = inst.type == inst_type::LEA ? unknown_type : type_from_size(inst.size);

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
        if (size == opsize::l && !(addr & 3) && addr < 0x400) {
            add_auto_label(addr, code_ptr);
            add_root(rv, simregs {});
            return;
        }

        if (size == opsize::l && !(addr & 1) && addr < 0xa00000) {
            //std::cerr << "Saving at $" << hexfmt(addr) << ": $" << hexfmt(rv) << "\n";
            saved_pointers_.insert({ addr, rv });
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
            if (ea_addr_[idx].known())
                update_mem(size, ea_addr_[idx].raw(), val);
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
        case inst_type::MOVE:
            update_ea(inst.size, 1, inst.ea[1], ea_val_[0]);
            break;
        case inst_type::MOVEA:
            auto val = ea_val_[0].known() ? simval { static_cast<uint32_t>(sext(ea_val_[0].raw(), inst.size)) } : simval {};
            if (inst.ea[0] == ea_immediate && val.raw() < max_mem && written_[val.raw()])
                add_auto_label(ea_val_[0].raw(), unknown_type);
            update_ea(opsize::l, 1, inst.ea[1], val);
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
    void maybe_find_string_at(uint32_t addr);
    void handle_data_at(uint32_t addr, const type& t);
    void handle_predef_info();
};


void analyzer::handle_struct_at(uint32_t addr, const structure_definition& s)
{
    if (!pointer_ok(addr))
        return;
    for (const auto& f : s.fields()) {
        handle_data_at(addr + f.offset(), f.t());
    }
}

void analyzer::maybe_find_string_at(uint32_t addr)
{
    if (!pointer_ok(addr))
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

    add_label(start_addr, "text_" + hexstring(start_addr), make_array_type(char_type, len));
}

void analyzer::handle_data_at(uint32_t addr, const type& t)
{
    if (!pointer_ok(addr) || !written_[addr])
        return;

    switch (t.base()) {
    default:
        assert(!"TODO");
        [[fallthrough]];
    case base_data_type::unknown_:
    case base_data_type::char_:
    case base_data_type::byte_:
    case base_data_type::word_:
    case base_data_type::long_:
        return;
    case base_data_type::code_:
        add_root(addr, simregs {}, true);
        return;
    case base_data_type::ptr_:
        if (auto p = try_read_pointer(addr); p) {
            auto it = labels_.find(p);
            if (it != labels_.end()) {
                if (it->second.t != t.ptr())
                    std::cerr << "TODO: Maybe handle " << *t.ptr() << " at $" << hexfmt(p) << " - already present as " << it->second.name << " (" << *it->second.t << ")\n";
                return;
            }

            if (t.ptr() == &code_type) {
                std::cerr << "TODO: Pointer to code at $" << hexfmt(p) << "!\n";
                assert(0);
                return;
            } else if (t.ptr()->base() == base_data_type::struct_) {
                std::cerr << "TODO: Pointer to struct at $" << hexfmt(p) << "!\n";
                assert(0);
                return;
            } else if (t.ptr() == &char_type) {
                maybe_find_string_at(p);
                return;
            }
        }
        return;
    case base_data_type::struct_:
        handle_struct_at(addr, *t.struct_def());
        return;
    }
}

void analyzer::handle_predef_info()
{
    for (const auto& i : predef_info_) {
        const auto& t = *i.second.t;
        add_label(i.first, i.second.name, t);
        handle_data_at(i.first, t);
    }
    predef_info_.clear();
}


constexpr uint32_t HUNK_UNIT    = 999;
constexpr uint32_t HUNK_NAME    = 1000;
constexpr uint32_t HUNK_CODE    = 1001;
constexpr uint32_t HUNK_DATA    = 1002;
constexpr uint32_t HUNK_BSS     = 1003;
constexpr uint32_t HUNK_RELOC32 = 1004;
constexpr uint32_t HUNK_RELOC16 = 1005;
constexpr uint32_t HUNK_RELOC8  = 1006;
constexpr uint32_t HUNK_EXT     = 1007;
constexpr uint32_t HUNK_SYMBOL  = 1008;
constexpr uint32_t HUNK_DEBUG   = 1009;
constexpr uint32_t HUNK_END     = 1010;
constexpr uint32_t HUNK_HEADER  = 1011;
constexpr uint32_t HUNK_OVERLAY = 1013;
constexpr uint32_t HUNK_BREAK   = 1014;
constexpr uint32_t HUNK_DREL32  = 1015;
constexpr uint32_t HUNK_DREL16  = 1016;
constexpr uint32_t HUNK_DREL8   = 1017;
constexpr uint32_t HUNK_LIB     = 1018;
constexpr uint32_t HUNK_INDEX   = 1019;

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
#undef HTS
    }
    return "Unknown hunk type " + std::to_string(hunk_type);
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

        std::cout << "HUNK_DEBUG ";
        hexdump(std::cout, &data_[pos_], 8);

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
        std::vector<uint8_t> data;
        uint32_t loaded_size;
    };
    std::vector<symbol_info> symbols;

    std::vector<hunk_info> hunks(table_size);
    for (uint32_t i = 0; i < table_size; ++i) {
        const auto v = read_u32();
        const auto flags = v >> 30;
        const auto size = (v & 0x3fffffff) << 2;
        if (flags == 3) {
            throw std::runtime_error { "Unsupported hunk size: $" + hexstring(v) };
        }
        const auto aligned_size = (size + 4095) & -4096;
        if (flags == 1) {
            hunks[i].addr = chip_ptr;
            chip_ptr += aligned_size;
            if (chip_ptr > 0x200000)
                throw std::runtime_error { "Out of chip mem" };
        } else {
            hunks[i].addr = fast_ptr;
            fast_ptr += aligned_size;
            if (fast_ptr > 0xa00000)
                throw std::runtime_error { "Out of fast mem" };
        }
        hunks[i].data.resize(size);
        std::cout << "Hunk " << i << " $" << hexfmt(size) << " " << (flags == 1 ? "CHIP" : flags == 2 ? "FAST" : "    ") << " @ " << hexfmt(hunks[i].addr) << "\n";
    }

    uint32_t table_index = 0;
    while (table_index < table_size) {
        const uint32_t hunk_num = first_hunk + table_index;

        const uint32_t hunk_type = read_u32();

        if (hunk_type == HUNK_DEBUG) {
            read_hunk_debug();
            continue;
        }

        if (!is_initial_hunk(hunk_type))
            throw std::runtime_error { "Expected CODE, DATA or BSS hunk for hunk " + std::to_string(hunk_num) + " got " + hunk_type_string(hunk_type) };
        const uint32_t hunk_longs = read_u32();
        const uint32_t hunk_bytes = hunk_longs << 2;
        std::cout << "\tHUNK_" << (hunk_type == HUNK_CODE ? "CODE" : hunk_type == HUNK_DATA ? "DATA" : "BSS ") << " size = $" << hexfmt(hunk_bytes) << "\n";

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

        while (pos_ <= data_.size() - 4 && !is_initial_hunk(get_u32(&data_[pos_]))) {
            const auto ht = read_u32();
            std::cout << "\t\t" << hunk_type_string(ht) << "\n";
            switch (ht) {
            case HUNK_SYMBOL: {
                auto syms = read_hunk_symbol();
                for (auto& s : syms) {
                    symbols.push_back({ std::move(s.name), hunks[hunk_num].addr + s.addr });
                    std::cout << "\t\t\t$" << hexfmt(symbols.back().addr) << "\t" << symbols.back().name << "\n"; 
                }
                break;
            }
            case HUNK_RELOC32:
                for (const auto& r : read_hunk_reloc32()) {
                    if (r.hunk_ref > last_hunk)
                        throw std::runtime_error { "Invalid RELOC32 refers to unknown hunk " + std::to_string(r.hunk_ref) };
                    auto& hd = hunks[hunk_num].data;
                    for (const auto ofs: r.relocs) {
                        if (ofs > hd.size() - 4)
                            throw std::runtime_error { "Invalid relocation" };
                        auto c = &hd[ofs];
                        put_u32(c, get_u32(c) + hunks[r.hunk_ref].addr);
                    }
                }
                break;
            case HUNK_END:
                break;
            case HUNK_OVERLAY: {
                // HACK: Just skip everything after HUNK_OVERLAY for now...
                // See http://aminet.net/package/docs/misc/Overlay for more info
                const auto overlay_table_size = read_u32() + 1;
                std::cout << "\t\t\tTable size " << overlay_table_size << "\n";
                check_pos(overlay_table_size << 2);
                hexdump(std::cout, &data_[pos_], overlay_table_size << 2);
                table_index = table_size - 1;
                pos_ = static_cast<uint32_t>(data_.size()); // Skip to end
                break;
            }
            default:
                throw std::runtime_error { "Unsupported HUNK type " + hunk_type_string(ht) + " in executable" };
            }
        }

        ++table_index;
    }

    if (table_index != table_size)
        throw std::runtime_error { "Only " + std::to_string(table_index) + " out of " + std::to_string(table_size) + " hunks read" };

    if (pos_ != data_.size())
        throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data_.size(), 8) };

    if (!a_) {
        for (uint32_t i = 0; i < table_size; ++i) {
            if (hunks[i].type != HUNK_CODE)
                continue;
            disasm_stmts(hunks[i].data, 0, hunks[i].loaded_size, hunks[i].addr);
        }
    } else {
        for (const auto& s : symbols)
            a_->add_label(s.addr, s.name, unknown_type);
        bool has_code = false;
        for (const auto& h : hunks) {
            a_->write_data(h.addr, h.data.data(), static_cast<uint32_t>(h.data.size()));
            if (!has_code && h.type == HUNK_CODE) {
                a_->add_label(h.addr, "$$entry", code_type); // Add label if not already present
                a_->add_root(h.addr, simregs {});
                has_code = true;
            }
        }
        assert(has_code);

        a_->run();
    }
}

void hunk_file::read_hunk_unit()
{
    if (a_)
        throw std::runtime_error { "Analyze not supported for HUNK_UNIT" };

    const auto unit_name = read_string();
    std::cout << "HUNK_UNIT '" << unit_name << "'\n";

    std::vector<std::vector<uint8_t>> code_hunks;

    while (pos_ < data_.size()) {
        auto hunk_type = read_u32();
        const auto flags = hunk_type >> 29;
        hunk_type &= 0x1fffffff;
        switch (hunk_type) {
        case HUNK_NAME: {
            const auto name = read_string();
            std::cout << "\tHUNK_NAME " << name << "\n";
            break;
        }
        case HUNK_CODE:
        case HUNK_DATA: {
            const auto size = read_u32() << 2;
            check_pos(size);
            std::cout << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags>>1) << "\n";
            if (hunk_type == HUNK_CODE)
                code_hunks.push_back(std::vector<uint8_t>(&data_[pos_], &data_[pos_ + size]));
            pos_ += size;
            break;
        }
        case HUNK_BSS: {
            const auto size = read_u32() << 2;
            std::cout << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags >> 1) << "\n";
            break;
        }
        case HUNK_RELOC32:
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            read_hunk_reloc32();
            break;
        case HUNK_EXT: {
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            while (const auto ext = read_u32()) {
                const auto sym_type = static_cast<uint8_t>(ext >> 24);
                const auto name     = read_string_size(ext & 0xffffff);
                std::cout << "\t\t" << ext_type_string(sym_type) << "\t" << name << "\n";
                switch (sym_type) {
                case EXT_DEF: {
                    const auto offset = read_u32();
                    std::cout << "\t\t\t$" << hexfmt(offset) << "\n";
                    break;
                }
                case EXT_REF8:
                case EXT_REF16:
                case EXT_REF32: {
                    const auto nref = read_u32();
                    for (uint32_t i = 0; i < nref; ++i) {
                        const auto offset = read_u32();
                        std::cout << "\t\t\t$" << hexfmt(offset) << "\n";
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
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            const auto syms = read_hunk_symbol();
            for (const auto& s : syms) {
                std::cout << "\t\t$" << hexfmt(s.addr) << "\t" << s.name << "\n";
            }
            break;
        }
        case HUNK_END:
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
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

struct rom_tag {
    uint32_t matchtag;  // RT_MATCHTAG     (pointer RTC_MATCHWORD)
    uint32_t endskip;   // RT_ENDSKIP      (pointer to end of code)
    uint8_t  flags;     // RT_FLAGS        (no flags)
    uint8_t  version;   // RT_VERSION      (version number)
    uint8_t  type;      // RT_TYPE         (NT_LIBRARY)
    uint8_t  priority;  // RT_PRI          (priority = 126)
    uint32_t name_addr; // RT_NAME         (pointer to name)
    uint32_t id_addr;   // RT_IDSTRING     (pointer to ID string)
    uint32_t init_addr; // RT_INIT         (execution address)*

    std::string name;
};

void handle_rom(const std::vector<uint8_t>& data, analyzer* a)
{
    const auto start = get_u32(&data[4]);
    //std::cout << "ROM detected\n";
    //std::cout << "Entry point: $" << hexfmt(start) << "\n";
    const uint32_t rom_base = start & 0xffff0000;
    if (rom_base != 0xf80000 && rom_base != 0xfc0000)
        throw std::runtime_error { "Unsupported ROM base: $" + hexstring(rom_base) };
    //std::cout << "Base address: $" << hexfmt(rom_base) << "\n";
    if (start - rom_base >= data.size())
        throw std::runtime_error { "Invalid ROM entry point: $" + hexstring(start) };

    // Scan for ROM tags
    std::vector<rom_tag> rom_tags;
    for (uint32_t offset = 0; offset < data.size() - 24;) {
        if (get_u16(&data[offset]) != 0x4afc || get_u32(&data[offset + 2]) != offset + rom_base) {
            offset += 2;
            continue;
        }

        //std::cout << "Found tag at $" << hexfmt(rom_base + offset) << "\n";
        rom_tag tag
        {
            .matchtag = get_u32(&data[offset + 2]),
            .endskip = get_u32(&data[offset + 6]),
            .flags = data[offset + 10],
            .version = data[offset + 11],
            .type = data[offset + 12],
            .priority = data[offset + 13],
            .name_addr = get_u32(&data[offset + 14]),
            .id_addr = get_u32(&data[offset + 18]),
            .init_addr = get_u32(&data[offset + 22]),
        };

        uint32_t end_offset = tag.endskip - rom_base;
        if (end_offset <= offset || end_offset > data.size()) {
            std::cout << "Invalid end offset\n";
            end_offset = offset + 26;
        }

        auto get_string = [&](uint32_t addr) {
            if (addr <= rom_base || addr - rom_base >= data.size())
                throw std::runtime_error { "Invalid string in ROM tag: addr=$" + hexstring(addr) };
            addr -= rom_base;
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

        //std::cout << "  End=$" << hexfmt(tag.endskip) << "\n";
        //std::cout << "  Flags=$" << hexfmt(tag.flags) << " version=$" << hexfmt(tag.version) << " type=$" << hexfmt(tag.type) << " priority=$" << hexfmt(tag.priority) << "\n";
        //std::cout << "  Name='" << tag.name << "'\n";
        //std::cout << "  ID='" << get_string(tag.id_addr) << "'\n";
        //std::cout << "  Init=$" << hexfmt(tag.init_addr) << "\n";

        rom_tags.push_back(tag);
        offset = end_offset;
    }

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

    bool has_entry_name = false;
    simregs rom_tag_init_regs = {};
    // InitResident calls the RT_INIT function with these arguments:      
    rom_tag_init_regs.d[0] = simval { 0 }; // D0 = 0
    rom_tag_init_regs.a[0] = simval { 0 }; // A0 = segList (NULL for ROM modules)
    rom_tag_init_regs.a[6] = simval { analyzer::fake_exec_base }; // A6 = ExecBase
    for (const auto& tag : rom_tags) {
        std::string name;
        for (char c : tag.name) {
            if (isalpha(c) || (!name.empty() && isdigit(c)))
                name.push_back(c);
            else
                name.push_back('_');
        }

        auto slen = [&](uint32_t addr) {
            addr -= rom_base;
            uint32_t l = 0;
            while (addr + 1 < data.size() && data[addr++])
                ++l;
            return l + 1; // Include nul terminator
        };

        a->add_label(tag.matchtag, name + "_matchword", word_type);
        a->add_label(tag.matchtag + 2, name + "_matchtag", unknown_ptr); // Pointer to struct Resident
        a->add_label(tag.matchtag + 6, name + "_endskip", unknown_ptr);
        a->add_label(tag.matchtag + 10, name + "_flags", byte_type);
        a->add_label(tag.matchtag + 11, name + "_version", byte_type);
        a->add_label(tag.matchtag + 12, name + "_type", byte_type);
        a->add_label(tag.matchtag + 13, name + "_priority", byte_type);
        a->add_label(tag.matchtag + 14, name + "_name_addr", char_ptr);
        a->add_label(tag.matchtag + 18, name + "_id_addr", char_ptr);
        a->add_label(tag.matchtag + 22, name + "_init_addr", code_ptr);

        a->add_label(tag.name_addr, name + "_name", make_array_type(char_type, slen(tag.name_addr)));
        a->add_label(tag.id_addr, name + "_id", make_array_type(char_type, slen(tag.id_addr)));
        if (tag.init_addr) {
            a->add_label(tag.init_addr, name + "_init", code_type);
            if (!(tag.flags & 0x80)) // Not RTF_AUTOINIT
                a->add_root(tag.init_addr, rom_tag_init_regs);
        }
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

void usage()
{
    std::cerr << "Usage: m68kdisasm [-a] [-i info] file [options...]\n";
    std::cerr << "   file    source file\n";
    std::cerr << "   -a      analyze\n";
    std::cerr << "   -i      infofile (implies -a)\n";
    std::cerr << "\n";
    std::cerr << "Options for non-hunk files:";
    std::cerr << "   Normal (non-analysis mode) options: [offset] [end] - Normal dissasembly of starting from offset..end\n";
    std::cerr << "   Analayze options: PC starting program counter of memory dump\n";
}

int main(int argc, char* argv[])
{
    try {
        std::unique_ptr<analyzer> a;
        while (argc >= 2) {
            if (!strcmp(argv[1], "-a")) {
                if (!a)
                    a = std::make_unique<analyzer>();
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
            } else {
                break;
            }
        }

        if (argc < 2) {
            usage();
            return 1;
        }
        
        const auto data = read_file(argv[1]);
        if (data.size() > 4 && (get_u32(&data[0]) == HUNK_HEADER || get_u32(&data[0]) == HUNK_UNIT)) {
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            read_hunk(data, a.get());
        } else if (data.size() >= 8 * 1024 && data[0] == 0x11 && (data[1] == 0x11 || data[1] == 0x14) && get_u16(&data[2]) == 0x4ef9) {
            // ROM
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            handle_rom(data, a.get());
        } else {
            if (a) {
                if (argc <= 2)
                    throw std::runtime_error { "Missing start PC" };
                a->write_data(0, data.data(), static_cast<uint32_t>(data.size()));
                a->add_root(hex_or_die(argv[2]), simregs {});
                a->run();
            } else {
                if (argc > 4)
                    throw std::runtime_error { "Too many arguments" };
                const auto offset = argc > 2 ? hex_or_die(argv[2]) : 0;
                const auto end = argc > 3 ? hex_or_die(argv[3]) : static_cast<uint32_t>(data.size());
                disasm_stmts(data, offset, end);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
