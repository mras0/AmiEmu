#include "asm.h"
#include "instruction.h"
#include "ioutil.h"
#include "disasm.h"
#include "memory.h"
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
#include <algorithm>

constexpr uint8_t opsize_mask_none = 0;
constexpr uint8_t opsize_mask_b    = 1 << 0;
constexpr uint8_t opsize_mask_w    = 1 << 1;
constexpr uint8_t opsize_mask_l    = 1 << 2;
constexpr uint8_t opsize_mask_bw   = opsize_mask_b | opsize_mask_w;
constexpr uint8_t opsize_mask_bl   = opsize_mask_b | opsize_mask_l;
constexpr uint8_t opsize_mask_wl   = opsize_mask_w | opsize_mask_l;
constexpr uint8_t opsize_mask_bwl  = opsize_mask_b | opsize_mask_w | opsize_mask_l;

constexpr bool range8(uint32_t val)
{
    return static_cast<int32_t>(val) >= -128 && static_cast<int32_t>(val) <= 127;
}

constexpr bool range16(uint32_t val)
{
    return static_cast<int32_t>(val) >= -32768 && static_cast<int32_t>(val) <= 32767;
}

#define WITH_CC(X, prefix, tname, fname, m, d, no) \
    X(tname      , m, d, no) \
    X(fname      , m, d, no) \
    X(prefix##HI , m, d, no) \
    X(prefix##LS , m, d, no) \
    X(prefix##CC , m, d, no) \
    X(prefix##CS , m, d, no) \
    X(prefix##NE , m, d, no) \
    X(prefix##EQ , m, d, no) \
    X(prefix##VC , m, d, no) \
    X(prefix##VS , m, d, no) \
    X(prefix##PL , m, d, no) \
    X(prefix##MI , m, d, no) \
    X(prefix##GE , m, d, no) \
    X(prefix##LT , m, d, no) \
    X(prefix##GT , m, d, no) \
    X(prefix##LE , m, d, no)

#define INSTRUCTIONS(X)                   \
    X(ABCD      , b    , b    , 2)        \
    X(ADD       , bwl  , w    , 2)        \
    X(ADDQ      , bwl  , w    , 2)        \
    X(ADDX      , bwl  , w    , 2)        \
    X(AND       , bwl  , w    , 2)        \
    X(ASL       , bwl  , w    , 3)        \
    X(ASR       , bwl  , w    , 3)        \
    X(BCHG      , bl   , none , 2)        \
    X(BCLR      , bl   , none , 2)        \
    X(BSET      , bl   , none , 2)        \
    X(BTST      , bl   , none , 2)        \
    WITH_CC(X, B, BRA, BSR, bw, none, 1)  \
    X(CHK       , w    , w    , 2)        \
    X(CLR       , bwl  , w    , 1)        \
    X(CMP       , bwl  , w    , 2)        \
    X(CMPM      , bwl  , w    , 2)        \
    WITH_CC(X, DB, DBT, DBF, w, w, 2)     \
    X(DIVS      , w    , w    , 2)        \
    X(DIVU      , w    , w    , 2)        \
    X(EOR       , bwl  , w    , 2)        \
    X(EXT       , wl   , w    , 1)        \
    X(EXG       , l    , none , 2)        \
    X(ILLEGAL   , none , none , 0)        \
    X(JMP       , none , none , 1)        \
    X(JSR       , none , none , 1)        \
    X(LEA       , l    , l    , 2)        \
    X(LINK      , w    , w    , 2)        \
    X(LSL       , bwl  , w    , 3)        \
    X(LSR       , bwl  , w    , 3)        \
    X(MOVE      , bwl  , w    , 2)        \
    X(MOVEM     , wl   , w    , 2)        \
    X(MOVEP     , wl   , w    , 2)        \
    X(MOVEQ     , l    , l    , 2)        \
    X(MULS      , w    , w    , 2)        \
    X(MULU      , w    , w    , 2)        \
    X(NBCD      , b    , b    , 1)        \
    X(NEG       , bwl  , w    , 1)        \
    X(NEGX      , bwl  , w    , 1)        \
    X(NOP       , none , none , 0)        \
    X(NOT       , bwl  , w    , 1)        \
    X(OR        , bwl  , w    , 2)        \
    X(PEA       , l    , l    , 1)        \
    X(RESET     , none , none , 0)        \
    X(ROL       , bwl  , w    , 3)        \
    X(ROR       , bwl  , w    , 3)        \
    X(ROXL      , bwl  , w    , 3)        \
    X(ROXR      , bwl  , w    , 3)        \
    X(RTE       , none , none , 0)        \
    X(RTR       , none , none , 0)        \
    X(RTS       , none , none , 0)        \
    X(SBCD      , b    , b    , 2)        \
    WITH_CC(X, S, ST, SF, b, b, 1)        \
    X(STOP      , none , none , 1)        \
    X(SUB       , bwl  , w    , 2)        \
    X(SUBX      , bwl  , w    , 2)        \
    X(SUBQ      , bwl  , w    , 2)        \
    X(SWAP      , w    , w    , 1)        \
    X(TAS       , b    , b    , 1)        \
    X(TRAP      , none , none , 1)        \
    X(TRAPV     , none , none , 0)        \
    X(TST       , bwl  , w    , 1)        \
    X(UNLK      , none , none , 1)        \

#define TOKENS(X)       \
    X(size_b , ".B"   ) \
    X(size_w , ".W"   ) \
    X(size_l , ".L"   ) \
    X(d0     , "D0"   ) \
    X(d1     , "D1"   ) \
    X(d2     , "D2"   ) \
    X(d3     , "D3"   ) \
    X(d4     , "D4"   ) \
    X(d5     , "D5"   ) \
    X(d6     , "D6"   ) \
    X(d7     , "D7"   ) \
    X(a0     , "A0"   ) \
    X(a1     , "A1"   ) \
    X(a2     , "A2"   ) \
    X(a3     , "A3"   ) \
    X(a4     , "A4"   ) \
    X(a5     , "A5"   ) \
    X(a6     , "A6"   ) \
    X(a7     , "A7"   ) \
    X(usp    , "USP"  ) \
    X(pc     , "PC"   ) \
    X(sr     , "SR"   ) \
    X(ccr    , "CCR"  ) \
    X(dc     , "DC"   ) \
    X(even   , "EVEN" ) \

enum class token_type {
    eof,
    whitespace,
    newline,
    number,
    // 33 ... 127 reserved for operators
    hash = '#',
    quote = '\'',
    lparen = '(',
    rparen = ')',
    plus = '+',
    comma = ',',
    minus = '-',
    slash = '/',
    colon = ':',
    equal = '=',

    last_operator = 127,

#define DEF_TOKEN(n, t) n,
    TOKENS(DEF_TOKEN)
#undef DEF_TOKEN

    instruction_start_minus_1,
#define TOKEN_INST(n, m, d, no) n,
    INSTRUCTIONS(TOKEN_INST)
#undef TOKEN_INST

    identifier_start,
};

constexpr struct instruction_info_type {
    token_type type;
    const char* const name;
    uint8_t opsize_mask;
    opsize default_size;
    uint8_t num_operands;
} instruction_info[] = {
#define INSTRUCTION_INFO(name, osize_mask, def_size, num_oper) { token_type::name, #name, opsize_mask_##osize_mask, opsize::def_size, num_oper },
    INSTRUCTIONS(INSTRUCTION_INFO)
#undef INSTRUCTION_INFO
};

#define ASSEMBLER_ERROR(...)                                                                                                                   \
    do {                                                                                                                                       \
        std::ostringstream oss;                                                                                                                \
        oss << "Error in line " << line_ << " column " << col_ << ": " << __VA_ARGS__ << " in function " << __func__ << " line " << __LINE__;  \
        throw std::runtime_error { oss.str() };                                                                                                \
    } while (0)

class assembler {
public:
    explicit assembler(uint32_t start_pc, const char* text, const std::vector<std::pair<const char*, uint32_t>>& predefines)
        : pc_ { start_pc }
        , input_ { text }
    {
        for (const auto& [name, val] : predefines) {
            auto tt = make_identifier(name);
            if (tt < token_type::identifier_start)
                ASSEMBLER_ERROR("Invalid predefined identifier " << name);
            auto& ii = identifier_info(tt);
            if (ii.has_value)
                ASSEMBLER_ERROR("Redefinition of " << ii.id);
            ii.has_value = true;
            ii.value = val;
        }
    }

    std::vector<uint8_t> process()
    {
        do {
            const bool start_of_line = col_ == 1;

            get_token();
            if (token_type_ == token_type::whitespace || token_type_ == token_type::newline) {
                continue;
            } else if (token_type_ == token_type::eof) {
                break;
            } else if (is_identifier(token_type_)) {
                const auto l = token_type_;
                get_token();
                if (token_type_ == token_type::colon) {
                    get_token();
                } else if (token_type_ == token_type::equal) {
                    get_token();
                    auto& ii = identifier_info(l);
                    if (ii.has_value)
                        ASSEMBLER_ERROR("Redefinition of " << ii.id);
                    else if (!ii.fixups.empty())
                        ASSEMBLER_ERROR("Invalid definition for " << ii.id << " (previously referenced)");
                    ii.has_value = true;
                    auto val = process_number();
                    if (val.has_fixups())
                        ASSEMBLER_ERROR("Invalid definition for " << ii.id << "(needs fixups)");
                    ii.value = val.val;
                    skip_to_eol();
                    continue;
                } else if (!start_of_line) {
                    ASSEMBLER_ERROR("Expected instruction got " << token_type_string(l));
                }
                auto& ii = identifier_info(l);
                if (ii.has_value) {
                    ASSEMBLER_ERROR("Redefinition of " << ii.id);
                }
                ii.has_value = true;
                ii.value = pc_;
                for (const auto& f : ii.fixups) {
                    auto it = pending_fixups_.find(f.offset);
                    if (it == pending_fixups_.end())
                        ASSEMBLER_ERROR("Invalid fixup for " << ii.id);
                    if (it->second.size != f.size)
                        ASSEMBLER_ERROR("Size mismatch for " << ii.id << " expected size " << opsize_bytes(it->second.size) << " got " << opsize_bytes(f.size));
                    it->second.value += f.negate ? -static_cast<int32_t>(ii.value) : ii.value;
                }
                ii.fixups.clear();
            } else if (is_instruction(token_type_)) {
                const auto inst = token_type_;
                get_token();
                process_instruction(inst);
            } else if (token_type_ == token_type::dc) {
                process_dc();
            } else if (token_type_ == token_type::even) {
                if (pc_ & 1) {
                    result_.push_back(0);
                    ++pc_;
                }
            } else {
                ASSEMBLER_ERROR("Unexpected token: " << token_type_string(token_type_));
            }
        } while (token_type_ != token_type::eof);

        for (const auto& i : identifier_info_) {
            if (!i->has_value || !i->fixups.empty()) {
                ASSEMBLER_ERROR(i->id << " referenced but not defined");
            }
        }

        // Apply fixups. Delayed until here to allow full 32-bit intermediate values.
        for (const auto& [offset, f] : pending_fixups_) {
            //std::cout << "Applying fixup offset=$" << hexfmt(offset) << " " << token_type_string(f.inst) << " line " << f.line << " value $" << hexfmt(f.value) << "\n";
            switch (f.size) {
            case opsize::b:
                assert(offset & 1);
                if (!range8(f.value))
                    ASSEMBLER_ERROR("Value ($" << hexfmt(f.value) << ") of out 8-bit range in line " << f.line << " for " << token_type_string(f.inst) );
                result_[offset] = static_cast<uint8_t>(f.value);
                break;
            case opsize::w:
                assert(!(offset & 1));
                if (!range16(f.value))
                    ASSEMBLER_ERROR("Value ($" << hexfmt(f.value) << ") of out 16-bit range in line " << f.line << " for " << token_type_string(f.inst));
                put_u16(&result_[offset], static_cast<uint16_t>(f.value));
                break;
            case opsize::l:
                assert(!(offset & 1));
                put_u32(&result_[offset], f.value);
                break;
            }
        }

        return std::move(result_);
    }

private:
    std::unordered_map<std::string, token_type> id_map_ = {
        #define INST_ID_INIT(n, m, d, no) { #n, token_type::n },
        INSTRUCTIONS(INST_ID_INIT)
        #undef INST_ID_INIT
        #define TOKEN_ID_INIT(n, t) { t, token_type::n },
        TOKENS(TOKEN_ID_INIT)
        #undef TOKEN_ID_INIT
        // Synonyms
        { "SP", token_type::a7 },
    };

    struct fixup_type {
        opsize size;
        bool negate;
        uint32_t offset;
    };

    struct pending_fixup {
        token_type inst;
        uint32_t line;
        opsize size;
        uint32_t value;
    };

    struct identifier_info_type {
        std::string id;
        bool has_value;
        uint32_t value;
        std::vector<fixup_type> fixups;
    };

    std::vector<std::unique_ptr<identifier_info_type>> identifier_info_;
    std::map<uint32_t, pending_fixup> pending_fixups_;

    std::vector<uint8_t> result_;
    uint32_t pc_;
    const char* input_;
    uint32_t line_ = 1;
    uint32_t col_ = 1;

    token_type token_type_ = token_type::eof;
    std::string token_text_;
    uint32_t token_number_ = 0;

    static constexpr bool is_instruction(token_type t)
    {
        return static_cast<uint32_t>(t) > static_cast<uint32_t>(token_type::instruction_start_minus_1) && static_cast<uint32_t>(t) < static_cast<uint32_t>(token_type::identifier_start);
    }

    static constexpr bool is_identifier(token_type t)
    {
        return static_cast<uint32_t>(t) >= static_cast<uint32_t>(token_type::identifier_start);
    }

    static constexpr bool is_register(token_type t)
    {
        return static_cast<uint32_t>(t) >= static_cast<uint32_t>(token_type::d0) && static_cast<uint32_t>(t) <= static_cast<uint32_t>(token_type::a7);
    }

    static constexpr bool is_data_register(token_type t)
    {
        return static_cast<uint32_t>(t) >= static_cast<uint32_t>(token_type::d0) && static_cast<uint32_t>(t) <= static_cast<uint32_t>(token_type::d7);
    }

    static constexpr bool is_address_register(token_type t)
    {
        return static_cast<uint32_t>(t) >= static_cast<uint32_t>(token_type::a0) && static_cast<uint32_t>(t) <= static_cast<uint32_t>(token_type::a7);
    }

    identifier_info_type& identifier_info(token_type t)
    {
        const auto id_idx = static_cast<uint32_t>(t) - static_cast<uint32_t>(token_type::identifier_start);
        assert(id_idx < identifier_info_.size());
        return *identifier_info_[id_idx];
    }

    std::string token_type_string(token_type tt)
    {
        switch (tt) {
        case token_type::eof:
            return "EOF";
        case token_type::whitespace:
            return "WHITESPACE";
        case token_type::newline:
            return "NEWLINE";
        case token_type::number:
            return "NUMBER";
        #define CASE_INST_TOKEN(n, m, d, no) case token_type::n: return #n;
            INSTRUCTIONS(CASE_INST_TOKEN)
        #undef CASE_INST_TOKEN
        #define CASE_TOKEN(n, t) case token_type::n: return t;
            TOKENS(CASE_TOKEN)
        #undef CASE_TOKEN
        default:
            const auto ti = static_cast<uint32_t>(tt);
            if (ti >= 33 && ti <= 127) {
                std::ostringstream oss;
                oss << "OPERATOR \"" << static_cast<char>(ti) << "\"";
                return oss.str();
            }
            const auto id_idx = ti - static_cast<uint32_t>(token_type::identifier_start);
            if (id_idx < identifier_info_.size()) {
                std::ostringstream oss;
                oss << "INDENTIFIER \"" << identifier_info_[id_idx]->id << "\"";
                return oss.str();
            }
            ASSEMBLER_ERROR("Unknown token type: " << ti);
        }
    }

    token_type make_identifier(const char* text)
    {
        std::string uc = toupper_str(text);

        if (auto it = id_map_.find(uc); it != id_map_.end())
            return it->second;

        const uint32_t id = static_cast<uint32_t>(identifier_info_.size()) + static_cast<uint32_t>(token_type::identifier_start);
        id_map_[uc] = static_cast<token_type>(id);
        auto ii = std::make_unique<identifier_info_type>();
        ii->id = std::move(uc);
        identifier_info_.push_back(std::move(ii));
        return static_cast<token_type>(id);
    }

    void read_number(uint8_t base)
    {
        uint32_t n = 0;
        bool any = false;
        for (;;) {
            const char c = *input_;
            uint32_t v = 0;
            if (c >= '0' && c <= '9') {
                v = c - '0';
            } else if (c >= 'A' && c <= 'F') {
                v = c - 'A' + 10;
            } else if (c >= 'a' && c <= 'f') {
                v = c - 'a' + 10;
            } else {
                break;
            }
            if (n * base < n)
                ASSEMBLER_ERROR("Number is too large");
            if (v >= base)
                ASSEMBLER_ERROR("Digit out of range for base " << static_cast<int>(base));
            ++col_;
            ++input_;
            n = n * base + v;
            any = true;
        }
        if (!any)
            ASSEMBLER_ERROR("No digits in number");
        token_number_ = n;
    }

    void get_token()
    {
        const char c = *input_++;
        token_text_ = c;
        token_number_ = 0;
        ++col_;
        switch (c) {
        case '\0':
            token_type_ = token_type::eof;
            return;
        case '\n':
            token_type_ = token_type::newline;
            ++line_;
            col_ = 1;
            return;
        case ';':
            while (*input_ && *input_ != '\n')
                ++input_;
            token_type_ = token_type::whitespace;
            return;
        case '#':
        case '\'':
        case '(':
        case ')':
        case '+':
        case ',':
        case '-':
        case '/':
        case ':':
        //case '<':
        case '=':
        //case '>':
        //case '?':
            token_type_ = static_cast<token_type>(c);
            return;
        case '$':
            token_type_ = token_type::number;
            read_number(16);
            return;
        }

        if (c >= '0' && c <= '9') {
            --input_;
            --col_;
            token_type_ = token_type::number;
            read_number(10);
            return;
        }

        if (isalpha(c) || c == '_' || c == '.') {
            while (isalnum(*input_) || *input_ == '_') {
                token_text_ += *input_++;
                ++col_;
            }

            token_type_ = make_identifier(token_text_.c_str());
            return;
        }

        if (isspace(c)) {
            token_type_ = token_type::whitespace;
            if (c == '\t') {
                --col_;
                col_ += 8 - (col_ - 1) % 8;
            }
            while (isspace(*input_) && *input_ != '\n') {
                if (*input_ == '\t')
                    col_ += 8 - (col_ - 1) % 8;
                else
                    ++col_;
                token_text_ += *input_++;
            }
            return;
        }

        ASSEMBLER_ERROR("Invalid character: " << c);
    }

    void expect(token_type expected)
    {
        if (token_type_ != expected)
            ASSEMBLER_ERROR("Expected " << token_type_string(expected) << " got " << token_type_string(token_type_));
        get_token();
    }

    void skip_whitespace()
    {
        while (token_type_ == token_type::whitespace)
            get_token();
    }


    void skip_to_eol()
    {
        for (;;) {
            if (token_type_ == token_type::eof || token_type_ == token_type::newline)
                return;
            expect(token_type::whitespace);
        }
    }

    struct ea_result {
        uint8_t type;
        uint32_t val = 0;
        uint32_t saved_val = 0;
        std::vector<std::pair<identifier_info_type*,bool>> fixups;

        bool has_fixups() const
        {
            return !fixups.empty();
        }
    };

    void add_fixups(const ea_result& r, token_type inst, opsize osize, uint32_t offset)
    {
        if (r.fixups.empty())
            return;

        if (!pending_fixups_.insert({ offset, pending_fixup { inst, line_, osize, r.saved_val } }).second)
            ASSEMBLER_ERROR("Fixups already exist");

        assert(!!(offset & 1) == (osize == opsize::b));
        for (auto [ii, negate] : r.fixups)
            ii->fixups.push_back(fixup_type { osize, negate, offset });
    }


    ea_result process_number(bool neg = false)
    {
        // TODO: More aritmetic
        ea_result res {};

        if (token_type_ == token_type::minus) {
            if (neg)
                ASSEMBLER_ERROR("Double minus not allowed");
            neg = true;
            get_token();
        }

        if (token_type_ == token_type::number) {
            res.val = neg ? -static_cast<int32_t>(token_number_) : token_number_;
            get_token();
        } else if (is_identifier(token_type_)) {
            auto& ii = identifier_info(token_type_);
            if (ii.has_value) {
                res.val = ii.value;
            }  else {
                res.val = 0;
                res.fixups.push_back({ &ii, neg });
            }
            get_token();
        } else {
            ASSEMBLER_ERROR("Invalid number");
        }

        // Only support very simple aritmetic for now
        while (token_type_ == token_type::minus || token_type_ == token_type::plus) {
            const auto saved_token = token_type_;
            get_token();

            uint32_t val = 0;
            if (token_type_ == token_type::number) {
                val = token_number_;
                get_token();
            } else if (is_identifier(token_type_)) {
                auto& ii = identifier_info(token_type_);
                if (ii.has_value) {
                    val = ii.value;
                } else {
                    res.fixups.push_back({ &ii, saved_token == token_type::minus });
                }
                get_token();
            } else {
                ASSEMBLER_ERROR("Expected number of identifier");
            }

            if (saved_token == token_type::minus)
                res.val -= val;
            else
                res.val += val;
        }
        res.saved_val = res.val;
        return res;
    }

    uint8_t process_areg()
    {
        if (!is_address_register(token_type_))
            ASSEMBLER_ERROR("Address register expected got " << token_type_string(token_type_));
        const auto reg = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::a0));
        get_token();
        return reg;
    }

    uint8_t process_reg()
    {
        if (!is_register(token_type_))
            ASSEMBLER_ERROR("Register expected got " << token_type_string(token_type_));
        const auto reg = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::d0));
        get_token();
        return reg;
    }

    ea_result process_ea()
    {
        skip_whitespace();

        bool neg = false;
        uint8_t reg = 0;
        ea_result res {};
        if (token_type_ == token_type::hash) {
            get_token();
            res = process_number();
            res.type = ea_immediate;
            return res;
        } else if (token_type_ == token_type::minus) {
            get_token();
            if (token_type_ != token_type::lparen) {
                neg = true;
                goto number;
            }

            get_token();
            res.type = ea_m_A_ind_pre << ea_m_shift | process_areg();
            expect(token_type::rparen);
            return res;
        } else if (token_type_ == token_type::lparen) {
            get_token();
            reg = process_areg();
            if (token_type_ == token_type::comma)
                goto has_reg;
            res.type = ea_m_A_ind << ea_m_shift | reg;
            expect(token_type::rparen);
            if (token_type_ == token_type::plus) {
                res.type = ea_m_A_ind_post << ea_m_shift | (res.type & 7);
                get_token();
            }
            return res;
        } else if (token_type_ == token_type::number || is_identifier(token_type_)) {
number:
            res = process_number(neg);
            if (token_type_ == token_type::lparen) {
                get_token();
                if (token_type_ == token_type::pc) {
                    reg = 0xff;
                    res.val -= pc_ + 2;
                    res.saved_val = res.val;
                    get_token();
                } else {
                    reg = process_areg();
                    if (!res.has_fixups() && !range16(res.val))
                        ASSEMBLER_ERROR(static_cast<int32_t>(res.val) << " is out of range for 16-bit displacement");
                }
has_reg:
                if (token_type_ == token_type::rparen) {
                    get_token();
                    if (reg == 0xff)
                        res.type = ea_m_Other << ea_m_shift | ea_other_pc_disp16;
                    else
                        res.type = ea_m_A_ind_disp16 << ea_m_shift | reg;
                    return res;
                }
                expect(token_type::comma);
                const auto reg2 = process_reg();
                const bool l = token_type_ == token_type::size_l;
                if (token_type_ == token_type::size_w || token_type_ == token_type::size_l)
                    get_token();
                expect(token_type::rparen);
                if (reg == 0xff)
                    res.type = ea_m_Other << ea_m_shift | ea_other_pc_index;
                else {
                    res.type = ea_m_A_ind_index << ea_m_shift | reg;
                }
                if (!res.has_fixups() && !range8(res.val))
                    ASSEMBLER_ERROR(static_cast<int32_t>(res.val) << " is out of range for 8-bit displacement");
                res.val = (res.val & 0xff) | (reg2 & 8 ? 0x8000 : 0) | (reg2 & 7) << 12 | (l ? 0x800 : 0);
                return res;
            }

            res.type = ea_m_Other << ea_m_shift | ea_other_abs_l;
            if (token_type_ == token_type::size_l) {
                get_token();
            } else if (token_type_ == token_type::size_w) {
                get_token();
                res.type = ea_m_Other << ea_m_shift | ea_other_abs_w;
                if (!range16(res.val))
                    ASSEMBLER_ERROR(static_cast<int32_t>(res.val) << " is out of range for 16-bit absolute address");
            }
            return res;
        } else if (is_register(token_type_)) {
            res.type = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::d0));
            get_token();
            if (token_type_ == token_type::minus || token_type_ == token_type::slash) {
                // register list
                auto first = res.type;
                res.type = ea_reglist;
                res.val = 0;
                for (;;) {
                    if (token_type_ == token_type::slash) {
                        if (first <= 15) {
                            res.val |= 1 << first;
                        }
                        first = 0xff;
                        get_token();
                    } else if (token_type_ == token_type::minus) {
                        if (first > 15)
                            ASSEMBLER_ERROR("Invalid register list");
                        get_token();
                        if (!is_register(token_type_))
                            ASSEMBLER_ERROR("Invalid register list");
                        const auto last = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::d0));
                        if (last <= first)
                            ASSEMBLER_ERROR("Invalid order in register list");
                        for (auto r = first; r <= last; ++r)
                            res.val |= 1 << r;
                        first = 0xff;
                        get_token();
                    } else if (is_register(token_type_)) {
                        if (first != 0xff)
                            ASSEMBLER_ERROR("Invalid register list");
                        first = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::d0));
                        get_token();
                    } else {
                        break;
                    }
                }
                if (first <= 15)
                    res.val |= 1 << first;
            }
            return res;
        } else if (token_type_ == token_type::sr) {
            get_token();
            return ea_result { .type = ea_sr };
        } else if (token_type_ == token_type::ccr) {
            get_token();
            return ea_result { .type = ea_ccr };
        } else if (token_type_ == token_type::usp) {
            get_token();
            return ea_result { .type = ea_usp };
        }
        ASSEMBLER_ERROR("Unexpected token: " << token_type_string(token_type_));
    }

    bool check_special_reg_size(const ea_result& ea, bool explicit_size, opsize& osize)
    {
        if (ea.type == ea_sr) {
            if (osize == opsize::none || !explicit_size)
                osize = opsize::w;
            else if (osize != opsize::w)
                ASSEMBLER_ERROR("SR is word sized");
            return true;
        } else if (ea.type == ea_ccr) {
            if (osize == opsize::none || !explicit_size)
                osize = opsize::b;
            else if (osize != opsize::b)
                ASSEMBLER_ERROR("CCR is byte sized");
            return true;            
        }
        return false;
    }

    void process_instruction(token_type inst)
    {
        auto info = instruction_info[static_cast<uint32_t>(inst) - static_cast<uint32_t>(token_type::instruction_start_minus_1) - 1];
        ea_result ea[2] = {};
        auto osize = info.default_size;
        bool explicit_size = true;

        if (info.num_operands == 0) {
            assert(info.opsize_mask == opsize_mask_none);
            goto operands_done;
        }

        if (token_type_ == token_type::size_b) {
            if (!(info.opsize_mask & opsize_mask_b))
                ASSEMBLER_ERROR("Byte size not allowed for " << info.name);
            osize = opsize::b;
            get_token();
        } else if (token_type_ == token_type::size_w) {
            if (!(info.opsize_mask & opsize_mask_w))
                ASSEMBLER_ERROR("Word size not allowed for " << info.name);
            osize = opsize::w;
            get_token();
        } else if (token_type_ == token_type::size_l) {
            if (!(info.opsize_mask & opsize_mask_l))
                ASSEMBLER_ERROR("Long size not allowed for " << info.name);
            osize = opsize::l;
            get_token();
        } else {
            explicit_size = false;
        }

        for (uint8_t arg = 0; arg < info.num_operands; ++arg) {
            expect(arg == 0 ? token_type::whitespace : token_type::comma);
            ea[arg] = process_ea();

            if (ea[arg].type == ea_reglist && inst != token_type::MOVEM)
                ASSEMBLER_ERROR("Register list not allowed for " + std::string { info.name });

            if (info.num_operands == 3) {
                if (token_type_ == token_type::comma) {
                    info.num_operands = 2;
                } else {
                    info.num_operands = 1;
                    break;
                }
            }
        }

operands_done:
        skip_to_eol();

        uint16_t iwords[max_instruction_words];
        uint8_t iword_cnt = 1;

        switch (inst) {
        case token_type::ABCD:
            iwords[0] = encode_abcd_sbcd(info, ea, false);
            break;
        case token_type::ADD:
            iwords[0] = encode_add_sub_cmp(info, ea, osize, 0x0600, 0xD000);
            break;
        case token_type::ADDQ:
            iwords[0] = encode_addsub_q(info, ea, osize, false);
            break;
        case token_type::ADDX:
            iwords[0] = encode_addsub_x(info, ea, osize, false);
            goto done;
        case token_type::AND:
            if (check_special_reg_size(ea[1], explicit_size, osize)) {
                // ANDI to SR/CCR
                if (ea[0].type != ea_immediate)
                    ASSEMBLER_ERROR("Immediate required as first argument");
                iwords[0] = ea[1].type == ea_sr ? 0x027c : 0x023c;
            } else {
                iwords[0] = encode_binop(info, ea, osize, 0x0200, 0xC000);
            }
            break;
        case token_type::ASL:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b00, true);
            break;
        case token_type::ASR:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b00, false);
            break;
        case token_type::BCHG:
            iwords[0] = encode_bitop(info, ea, osize, 0x0840, 0x0140);
            break;
        case token_type::BCLR:
            iwords[0] = encode_bitop(info, ea, osize, 0x0880, 0x0180);
            break;
        case token_type::BSET:
            iwords[0] = encode_bitop(info, ea, osize, 0x08C0, 0x01C0);
            break;
        case token_type::BTST:
            iwords[0] = encode_bitop(info, ea, osize, 0x0800, 0x0100);
            break;
        case token_type::BRA:
        case token_type::BSR:
        case token_type::BHI:
        case token_type::BLS:
        case token_type::BCC:
        case token_type::BCS:
        case token_type::BNE:
        case token_type::BEQ:
        case token_type::BVC:
        case token_type::BVS:
        case token_type::BPL:
        case token_type::BMI:
        case token_type::BGE:
        case token_type::BLT:
        case token_type::BGT:
        case token_type::BLE:
        {
            int32_t disp = handle_disp_bw(info, ea[0], osize);
            iwords[0] = 0x6000 | (static_cast<uint16_t>(inst) - static_cast<uint16_t>(token_type::BRA)) << 8;
            if (osize == opsize::b)
                iwords[0] |= disp & 0xff;
            else
                iwords[iword_cnt++] = disp & 0xffff;
            goto done;
        }
        case token_type::CHK:
            if (ea[0].type > ea_immediate || ea[1].type > ea_immediate || ea[0].type >> ea_m_shift == ea_m_An)
                ASSEMBLER_ERROR("Invalid source operand for " << info.name);
            if (ea[1].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("Destination must be data register for " << info.name);
            iwords[0] = 0x4180 | (ea[1].type & 7) << 9 | (ea[0].type & 0x3f);
            break;
        case token_type::CLR:
            iwords[0] = encode_unary(info, ea[0], osize, 0x4200);
            break;
        case token_type::CMP:
            if ((ea[1].type >> ea_m_shift > ea_m_An) && ea[0].type != ea_immediate) {
                ASSEMBLER_ERROR("Destination of CMP must be register");
            }
            iwords[0] = encode_add_sub_cmp(info, ea, osize, 0x0C00, 0xB000);
            break;
        case token_type::CMPM: {
            if (ea[0].type >> ea_m_shift != ea_m_A_ind_post)
                ASSEMBLER_ERROR("First operand must be address register in postincrement addressing mode");
            if (ea[1].type >> ea_m_shift != ea_m_A_ind_post)
                ASSEMBLER_ERROR("Second operand must be address register in postincrement addressing mode");
            constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };
            iwords[0] = 0xb108 | (ea[1].type & 7) << 9 | size_encoding[static_cast<uint8_t>(osize)] << 6 | (ea[0].type & 7);
            goto done;
        }
        case token_type::DBT:
        case token_type::DBF:
        case token_type::DBHI:
        case token_type::DBLS:
        case token_type::DBCC:
        case token_type::DBCS:
        case token_type::DBNE:
        case token_type::DBEQ:
        case token_type::DBVC:
        case token_type::DBVS:
        case token_type::DBPL:
        case token_type::DBMI:
        case token_type::DBGE:
        case token_type::DBLT:
        case token_type::DBGT:
        case token_type::DBLE: {
            if (ea[0].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("First operand to " << info.name << " must be a data register");
            if (ea[1].type >> ea_m_shift != ea_m_Other || (ea[1].type & 7) != ea_other_abs_l)
                ASSEMBLER_ERROR("Unsupported operand to " << info.name << ": " << ea_string(ea[1].type));
            const int32_t disp = ea[1].val - (pc_ + 2);
            ea[1].saved_val = disp;
            if (ea[1].has_fixups())
                add_fixups(ea[1], inst, opsize::w, static_cast<uint32_t>(result_.size() + 2));
            else if (!range16(disp))
                ASSEMBLER_ERROR("Displacement " << disp << " is out of range");
            iwords[0] = 0x50c8 | (static_cast<uint16_t>(inst) - static_cast<uint16_t>(token_type::DBT)) << 8 | (ea[0].type & 7);
            iwords[iword_cnt++] = disp & 0xffff;
            goto done;
        }
        case token_type::DIVS:
            iwords[0] = encode_muldiv(info, ea, true, false);
            break;
        case token_type::DIVU:
            iwords[0] = encode_muldiv(info, ea, false, false);
            break;
        case token_type::EOR:
            if (check_special_reg_size(ea[1], explicit_size, osize)) {
                // EORI to SR/CCR
                if (ea[0].type != ea_immediate)
                    ASSEMBLER_ERROR("Immediate required as first argument");
                iwords[0] = ea[1].type == ea_sr ? 0x0a7c : 0x0a3c;
            } else {
                iwords[0] = encode_binop(info, ea, osize, 0x0A00, 0xB000);
            }
            break;
        case token_type::EXG: {
            const uint8_t e0t = ea[0].type >> ea_m_shift;
            const uint8_t e1t = ea[1].type >> ea_m_shift;
            if (e0t != ea_m_Dn && e0t != ea_m_An)
                ASSEMBLER_ERROR("Invalid first operand for EXG");
            if (e1t != ea_m_Dn && e1t != ea_m_An)
                ASSEMBLER_ERROR("Invalid second operand for EXG");

            iwords[0] = 0xc100 | (ea[0].type & 7) << 9 | (ea[1].type & 7);
            if (e0t == ea_m_Dn && e1t == ea_m_Dn)
                iwords[0] |= 0b01000 << 3;
            else if (e0t == ea_m_An && e1t == ea_m_An)
                iwords[0] |= 0b01001 << 3;
            else
                iwords[0] |= 0b10001 << 3;
            goto done;
        }
        case token_type::EXT:
            assert(osize == opsize::w || osize == opsize::l);
            if (ea[0].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("Invalid operand to EXT");
            iwords[0] = 0x4800 | (osize == opsize::w ? 0b010 : 0b011) << 6 | (ea[0].type & 7);
            goto done;
        case token_type::ILLEGAL:
            iwords[0] = 0x4afc;
            break;
        case token_type::JMP:
            iwords[0] = encode_ea_instruction(info, ea[0], 0x4ec0);
            break;
        case token_type::JSR:
            iwords[0] = encode_ea_instruction(info, ea[0], 0x4e80);
            break;
        case token_type::LEA:
            assert(osize == opsize::l);
            if (ea[1].type >> ea_m_shift != ea_m_An)
                ASSEMBLER_ERROR("Destination register must be address register for LEA");
            iwords[0] = encode_ea_instruction(info, ea[0], 0x41c0 | (ea[1].type & 7) << 9);
            break;
        case token_type::LINK:
            if (ea[0].type >> ea_m_shift != ea_m_An)
                ASSEMBLER_ERROR("First operand must be address register");
            if (ea[1].type != ea_immediate)
                ASSEMBLER_ERROR("Second operand must be immediate");
            iwords[0] = 0x4e50 | (ea[0].type & 7);
            break;
        case token_type::LSL:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b01, true);
            break;
        case token_type::LSR:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b01, false);
            break;
        case token_type::MOVE: {
            // Unlike ANDI/EORI/ORI to CCR, MOVE to/from CCR is word sized
            // Note: MOVE to CCR is 68010+ only
            if ((ea[1].type >> ea_m_shift == ea_m_Other) && (ea[1].type & ea_xn_mask) > ea_other_abs_l)
                ASSEMBLER_ERROR("Invalid destination for MOVE");
            if (ea[0].type == ea_sr || ea[0].type == ea_ccr) {
                if (osize != opsize::w)
                    ASSEMBLER_ERROR("Operation must be word sized");
                if (ea[1].type > ea_immediate || ea[1].type >> ea_m_shift == ea_m_An || (ea[1].type >> ea_m_shift == ea_m_Other && (ea[1].type & 7) > ea_other_abs_l))
                    ASSEMBLER_ERROR("Invalid operand");
                iwords[0] = (ea[1].type == ea_sr ? 0x42c0 : 0x40c0) | (ea[1].type & 0x3f);
            } else if (ea[1].type == ea_sr || ea[1].type == ea_ccr) {
                if (osize != opsize::w)
                    ASSEMBLER_ERROR("Operation must be word sized");
                if (ea[0].type > ea_immediate || ea[0].type >> ea_m_shift == ea_m_An)
                    ASSEMBLER_ERROR("Invalid operand");
                iwords[0] = (ea[1].type == ea_sr ? 0x46c0 : 0x44c0) | (ea[0].type & 0x3f);
            } else if (ea[0].type == ea_usp) {
                if (ea[1].type >> ea_m_shift != ea_m_An)
                    ASSEMBLER_ERROR("Destination must be address register");
                if (explicit_size && osize != opsize::l)
                    ASSEMBLER_ERROR("Invalid operation size");
                iwords[0] = 0x4e68 | (ea[1].type & 7);
            } else if (ea[1].type == ea_usp) {
                if (ea[0].type >> ea_m_shift != ea_m_An)
                    ASSEMBLER_ERROR("Source must be address register");
                if (explicit_size && osize != opsize::l)
                    ASSEMBLER_ERROR("Invalid operation size");
                iwords[0] = 0x4e60 | (ea[0].type & 7);
            } else {
                constexpr uint8_t size_encoding[4] = { 0b00, 0b01, 0b11, 0b10 };
                const auto sz = size_encoding[static_cast<uint8_t>(osize)];
                iwords[0] = sz << 12 | (ea[1].type & 7) << 9 | (ea[1].type & (7 << 3)) << (6 - 3) | ea[0].type;
            }
            break;
        }
        case token_type::MOVEM: {
            iwords[0] = 0x4880;
            if (ea[0].type == ea_reglist || ea[0].type >> ea_m_shift <= ea_m_An) {
                // Reg-to-mem
                if (ea[0].type != ea_reglist) {
                    ea[0].val = 1 << ea[0].type;
                    ea[0].type = ea_reglist;
                }

                if (ea[1].type >= ea_immediate || ea[1].type >> ea_m_shift <= ea_m_An || ea[1].type >> ea_m_shift == ea_m_A_ind_post || (ea[1].type >> ea_m_shift == ea_m_Other && (ea[1].type & 7) > ea_other_abs_l))
                    ASSEMBLER_ERROR("Invalid destination for MOVEM");

                uint16_t rl = static_cast<uint16_t>(ea[0].val);

                if (ea[1].type >> ea_m_shift == ea_m_A_ind_pre) {
                    // Reverse bits
                    rl = ((rl >> 1) & 0x55555) | ((rl & 0x5555) << 1);
                    rl = ((rl >> 2) & 0x33333) | ((rl & 0x3333) << 2);
                    rl = ((rl >> 4) & 0xf0f0f) | ((rl & 0x0f0f) << 4);
                    rl = ((rl >> 8) & 0xf00ff) | ((rl & 0x00ff) << 8);
                }

                iwords[0] |= ea[1].type & 0x3f;
                iwords[iword_cnt++] = rl;
            } else if (ea[1].type == ea_reglist || ea[1].type >> ea_m_shift <= ea_m_An) {
                // Mem-to-reg
                if (ea[1].type != ea_reglist) {
                    ea[1].val = 1 << ea[1].type;
                    ea[1].type = ea_reglist;
                }

                // TODO: Check invalid

                if ((ea[0].type >> ea_m_shift == ea_m_Other) && ((ea[0].type & 7) == ea_other_pc_disp16 || (ea[0].type & 7) == ea_other_pc_index))
                    ea[0].val -= 2;

                iwords[0] |= 1 << 10;
                iwords[0] |= ea[0].type & 0x3f;
                iwords[iword_cnt++] = static_cast<uint16_t>(ea[1].val);
            } else {
                ASSEMBLER_ERROR("Invalid operands to MOVEM");
            }
            if (osize == opsize::l)
                iwords[0] |= 1 << 6;
            break;
        }
        case token_type::MOVEP: {
            // TODO: Turn (An) into 0(An) automatically
            const uint16_t mode = 1 << 8 | (osize == opsize::l ? 1 << 6 : 0);
            if (ea[0].type >> ea_m_shift == ea_m_Dn && ea[1].type >> ea_m_shift == ea_m_A_ind_disp16) {
                iwords[0] = (ea[0].type & 7) << 9 | 1 << 7 | mode | 1 << 3 | (ea[1].type & 7);
            } else if (ea[0].type >> ea_m_shift == ea_m_A_ind_disp16 && ea[1].type >> ea_m_shift == ea_m_Dn) {
                iwords[0] = (ea[1].type & 7) << 9 | mode | 1 << 3 | (ea[0].type & 7);
            } else {
                ASSEMBLER_ERROR("Invalid operands for MOVEP");
            }
            break;
        }
        case token_type::MOVEQ: {
            if (ea[0].type != ea_immediate || ea[0].has_fixups() || ea[1].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("Invalid operands to MOVEQ");
            if (!range8(ea[0].val))
                ASSEMBLER_ERROR("Immediate out of 8-bit range for MOVEQ: " << ea[0].val);
            iwords[0] = 0x7000 | (ea[1].type & 7) << 9 | (ea[0].val & 0xff);
            goto done;
        }
        case token_type::MULS:
            iwords[0] = encode_muldiv(info, ea, true, true);
            break;
        case token_type::MULU:
            iwords[0] = encode_muldiv(info, ea, false, true);
            break;
        case token_type::NBCD:
            assert(osize == opsize::b);
            iwords[0] = encode_unary(info, ea[0], opsize::b, 0x4800);
            break;
        case token_type::NEG:
            iwords[0] = encode_unary(info, ea[0], osize, 0x4400);
            break;
        case token_type::NEGX:
            iwords[0] = encode_unary(info, ea[0], osize, 0x4000);
            break;
        case token_type::NOP:
            iwords[0] = 0x4e71;
            break;
        case token_type::NOT:
            iwords[0] = encode_unary(info, ea[0], osize, 0x4600);
            break;
        case token_type::OR:
            if (check_special_reg_size(ea[1], explicit_size, osize)) {
                // ORI to SR/CCR
                if (ea[0].type != ea_immediate)
                    ASSEMBLER_ERROR("Immediate required as first argument");
                iwords[0] = ea[1].type == ea_sr ? 0x007c : 0x003c;
            } else {
                iwords[0] = encode_binop(info, ea, osize, 0x0000, 0x8000);
            }
            break;
        case token_type::PEA:
            assert(osize == opsize::l);
            iwords[0] = encode_ea_instruction(info, ea[0], 0x4840);
            break;
        case token_type::RESET:
            iwords[0] = 0x4e70;
            break;
        case token_type::ROL:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b11, true);
            break;
        case token_type::ROR:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b11, false);
            break;
        case token_type::ROXL:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b10, true);
            break;
        case token_type::ROXR:
            iwords[0] = encode_shift_rotate(info, ea, osize, 0b10, false);
            break;
        case token_type::RTE:
            iwords[0] = 0x4e73;
            break;
        case token_type::RTR:
            iwords[0] = 0x4e77;
            break;
        case token_type::RTS:
            iwords[0] = 0x4e75;
            break;
        case token_type::SBCD:
            iwords[0] = encode_abcd_sbcd(info, ea, true);
            break;
        case token_type::ST:
        case token_type::SF:
        case token_type::SHI:
        case token_type::SLS:
        case token_type::SCC:
        case token_type::SCS:
        case token_type::SNE:
        case token_type::SEQ:
        case token_type::SVC:
        case token_type::SVS:
        case token_type::SPL:
        case token_type::SMI:
        case token_type::SGE:
        case token_type::SLT:
        case token_type::SGT:
        case token_type::SLE: {
            if (ea[0].type >> ea_m_shift == ea_m_An || ea[0].type >= ea_immediate)
                ASSEMBLER_ERROR("Invalid operand to " << info.name);
            iwords[0] = 0x50c0 | (static_cast<uint16_t>(inst) - static_cast<uint16_t>(token_type::ST)) << 8 | (ea[0].type & 0x3f);
            break;
        }
        case token_type::STOP:
            if (ea[0].type != ea_immediate)
                ASSEMBLER_ERROR("Expeceted immediate for STOP");
            iwords[0] = 0x4e72;
            break;
        case token_type::SUB:
            iwords[0] = encode_add_sub_cmp(info, ea, osize, 0x0400, 0x9000);
            break;
        case token_type::SUBQ:
            iwords[0] = encode_addsub_q(info, ea, osize, true);
            break;
        case token_type::SUBX:
            iwords[0] = encode_addsub_x(info, ea, osize, true);
            goto done;
        case token_type::SWAP:
            assert(osize == opsize::w && info.num_operands == 1);
            if (ea[0].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("Invalid operand to SWAP");
            iwords[0] = 0x4840 | (ea[0].type & 7);
            goto done;
        case token_type::TAS:
            if (ea[0].type >> ea_m_shift == ea_m_An || ea[0].type >= ea_immediate)
                ASSEMBLER_ERROR("Invalid operand to " << info.name);
            iwords[0] = 0x4ac0 | (ea[0].type & 0x3f);
            break;
        case token_type::TRAP:
            if (ea[0].type != ea_immediate)
                ASSEMBLER_ERROR("Invalid operand to TRAP");
            if (ea[0].val > 15)
                ASSEMBLER_ERROR("TRAP vector out of 4-bit range");
            iwords[0] = 0x4e40 | (ea[0].val & 15);
            goto done;
        case token_type::TRAPV:
            iwords[0] = 0x4e76;
            goto done;
        case token_type::TST:
            iwords[0] = encode_unary(info, ea[0], osize, 0x4A00);
            break;
        case token_type::UNLK:
            if (ea[0].type >> ea_m_shift != ea_m_An)
                ASSEMBLER_ERROR("Operand must be address register");
            iwords[0] = 0x4e58 | (ea[0].type & 7);
            break;
        default:
            ASSEMBLER_ERROR("TODO: Encode " << info.name);
        }

        for (uint8_t arg = 0; arg < info.num_operands; ++arg) {
            uint32_t ofs = static_cast<uint32_t>(result_.size() + iword_cnt * 2);
            switch (ea[arg].type >> ea_m_shift) {
            case ea_m_Dn:
            case ea_m_An:
            case ea_m_A_ind:
            case ea_m_A_ind_post:
            case ea_m_A_ind_pre:
                assert(!ea[arg].has_fixups());
                break;
            case ea_m_A_ind_disp16: // (d16, An)
                add_fixups(ea[arg], inst, opsize::w, ofs);
                iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                break;
            case ea_m_A_ind_index: // (d8, An, Xn)
                assert(!ea[arg].has_fixups());
                iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                break;
            case ea_m_Other:
                switch (ea[arg].type & 7) {
                case ea_other_abs_w:
                    add_fixups(ea[arg], inst, opsize::w, ofs);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_abs_l:
                    add_fixups(ea[arg], inst, opsize::l, ofs);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val >> 16);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_pc_disp16: // (d16, PC)
                    add_fixups(ea[arg], inst, opsize::w, ofs);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_pc_index:  // (d8, PC, Xn)
                    add_fixups(ea[arg], inst, opsize::b, ofs + 1);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_imm:
                    if (ea[arg].has_fixups()) {
                        assert(osize != opsize::none);
                        if (osize == opsize::b)
                            ++ofs;
                        add_fixups(ea[arg], inst, osize, ofs);
                    }
                    if (osize == opsize::l)
                        iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val >> 16);
                    else if (osize == opsize::b)
                        ea[arg].val &= 0xff;
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                default:
                    ASSEMBLER_ERROR("TODO: Encoding for " << ea_string(ea[arg].type));
                }
                break;
            default:
                if (ea[arg].type == ea_sr || ea[arg].type == ea_ccr || ea[arg].type == ea_usp || ea[arg].type == ea_reglist)
                    break;
                ASSEMBLER_ERROR("TODO: Encoding for " << ea_string(ea[arg].type));
            }
        }

done:
        for (uint8_t i = 0; i < iword_cnt; ++i) {
            result_.push_back(static_cast<uint8_t>(iwords[i] >> 8));
            result_.push_back(static_cast<uint8_t>(iwords[i]));
            pc_ += 2;
        }
    }

    void process_dc()
    {
        get_token();
        opsize osize = opsize::w;
        if (token_type_ == token_type::size_b) {
            osize = opsize::b;
            get_token();
        } else if (token_type_ == token_type::size_w) {
            osize = opsize::w;
            get_token();
        } else if (token_type_ == token_type::size_l) {
            osize = opsize::l;
            get_token();
        }
      
        expect(token_type::whitespace);
        for (;;) {
            if (token_type_ == token_type::quote) {
                if (osize != opsize::b)
                    ASSEMBLER_ERROR("Strings only supported for DC.B");
                while (*input_ != '\'' ) {
                    if (!*input_ || *input_ == '\n')
                        ASSEMBLER_ERROR("Unexpected end of string");
                    result_.push_back(*input_++);
                    ++col_;
                    ++pc_;
                }
                get_token(); // quote char
                get_token();
            } else {
                auto num = process_number();
                add_fixups(num, token_type::dc, osize, static_cast<uint32_t>(result_.size()));

                if (osize == opsize::b) {
                    result_.push_back(static_cast<uint8_t>(num.val));
                    ++pc_;
                } else if (osize == opsize::w) {
                    result_.push_back(static_cast<uint8_t>(num.val >> 8));
                    result_.push_back(static_cast<uint8_t>(num.val));
                    pc_ += 2;
                } else {
                    result_.push_back(static_cast<uint8_t>(num.val >> 24));
                    result_.push_back(static_cast<uint8_t>(num.val >> 16));
                    result_.push_back(static_cast<uint8_t>(num.val >> 8));
                    result_.push_back(static_cast<uint8_t>(num.val));
                    pc_ += 4;
                }
            }

            skip_whitespace();
            if (token_type_ != token_type::comma)
                break;
            get_token();
            skip_whitespace();
        }
        skip_to_eol();
    }

    uint16_t encode_add_sub_cmp(const instruction_info_type& info, const ea_result* ea, opsize osize, uint16_t imm_code, uint16_t normal_code)
    {
        if (ea[0].type > ea_immediate || ea[1].type > ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        if (ea[1].type >> ea_m_shift == ea_m_An) {
            if (osize == opsize::b)
                ASSEMBLER_ERROR("Only word/long operations allowed with address destination");
            return normal_code | 0x00C0 | (ea[1].type & 7) << 9 | (osize == opsize::l ? 0x100 : 0x000) | (ea[0].type & 0x3f);
        }
        return encode_binop(info, ea, osize, imm_code, normal_code);
    }

    uint16_t encode_binop(const instruction_info_type& info, const ea_result* ea, opsize osize, uint16_t imm_code, uint16_t normal_code)
    {
        constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };

        if (ea[0].type > ea_immediate || ea[1].type > ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        if (/*ea[0].type >> ea_m_shift == ea_m_An || */ ea[1].type >> ea_m_shift == ea_m_An)
            ASSEMBLER_ERROR("Address register not allowed for " << info.name);
        if (ea[0].type == ea_immediate)
            return imm_code | size_encoding[static_cast<uint8_t>(osize)] << 6 | (ea[1].type & 0x3f);
        if (ea[0].type >> ea_m_shift != ea_m_Dn && ea[1].type >> ea_m_shift != ea_m_Dn)
            ASSEMBLER_ERROR("Unsupported operands to " << info.name);
        uint8_t eaidx;
        if (imm_code == 0x0A00) {
            // EOR (as opposed to CMP)
            eaidx = ea[0].type >> ea_m_shift == ea_m_Dn ? 1 : 0;
        } else {
            eaidx = ea[1].type >> ea_m_shift == ea_m_Dn ? 0 : 1;
        }
        return normal_code | (ea[!eaidx].type & 7) << 9 | eaidx << 8 | size_encoding[static_cast<uint8_t>(osize)] << 6 | (ea[eaidx].type & 0x3f);
    }

    uint16_t encode_bitop(const instruction_info_type& info, const ea_result* ea, opsize& osize, uint16_t imm_code, uint16_t dn_code)
    {
        if (ea[0].type > ea_immediate || ea[1].type > ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        if (ea[1].type >> ea_m_shift == ea_m_An)
            ASSEMBLER_ERROR("Address register not allowed for " << info.name);

        if (ea[1].type >> ea_m_shift == ea_m_Dn) {
            if (osize == opsize::b)
                ASSEMBLER_ERROR("Data register destination to " << info.name << " always has long size");
            assert(osize == opsize::l || osize == opsize::none);
        } else {
            if (osize == opsize::l)
                ASSEMBLER_ERROR("Memory destination to " << info.name << " always has byte size");
            assert(osize == opsize::b || osize == opsize::none);
        }

        if (ea[0].type == ea_immediate) {
            osize = opsize::b; // For encoding of the immediate
            return imm_code | (ea[1].type & 0x3f);
        } else if (ea[0].type >> ea_m_shift == ea_m_Dn) {
            return dn_code | (ea[0].type & 7) << 9 | (ea[1].type & 0x3f);
        } else {
            ASSEMBLER_ERROR("First operand to " << info.name << " is illegal");
        }
    }

    uint16_t encode_unary(const instruction_info_type& info, const ea_result& ea, opsize osize, uint16_t opcode)
    {
        if (ea.type >> ea_m_shift == ea_m_An)
            ASSEMBLER_ERROR("Address register not allowed for " << info.name);
        if (ea.type >> ea_m_shift == ea_m_Other && ((ea.type & 7) == ea_other_imm || (ea.type & 7) == ea_other_pc_disp16 || (ea.type & 7) == ea_other_pc_index))
            ASSEMBLER_ERROR("Invalid operand to " << info.name);
        if (ea.type > ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };

        return opcode | size_encoding[static_cast<uint8_t>(osize)] << 6 | (ea.type & 0x3f);
    }

    uint16_t encode_ea_instruction(const instruction_info_type& info, const ea_result& ea, uint16_t opcode)
    {
        if (ea.type >= ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        switch (ea.type >> ea_m_shift) {
        case ea_m_Dn:
        case ea_m_An:
        case ea_m_A_ind_post:
        case ea_m_A_ind_pre:
            ASSEMBLER_ERROR("Invalid operand to " << info.name);
        default:
            break;
        }
        return opcode | (ea.type & 0x3f);
    }

    uint16_t encode_addsub_q(const instruction_info_type& info, ea_result* ea, opsize osize, bool is_sub)
    {
        if (ea[0].type != ea_immediate)
            ASSEMBLER_ERROR("Source operand for " << info.name << " must be immediate");
        if (ea[0].val < 1 || ea[0].val > 8)
            ASSEMBLER_ERROR("Immediate out of range for " << info.name);
        if (osize == opsize::b && ea[1].type >> ea_m_shift == ea_m_An)
            ASSEMBLER_ERROR("Byte size not allowed for address register destination for " << info.name);
        if (ea[1].type > ea_immediate)
            ASSEMBLER_ERROR("Invalid operand to " << info.name);

        ea[0].type = 0; // Don't encode the immediate
        constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };
        return 0x5000 | (ea[0].val & 7) << 9 | is_sub << 8 | size_encoding[static_cast<uint8_t>(osize)] << 6 | (ea[1].type & 0x3f);
    }

    int32_t handle_disp_bw(const instruction_info_type& info, ea_result& ea, opsize& osize)
    {
        if (ea.type >> ea_m_shift != ea_m_Other || (ea.type & 7) != ea_other_abs_l)
            ASSEMBLER_ERROR("Unsupported operand to " << info.name << ": " << ea_string(ea.type));
        const int32_t disp = ea.val - (pc_ + 2);
        if (ea.has_fixups()) {
            const uint32_t ofs = static_cast<uint32_t>(result_.size());
            ea.saved_val = disp;
            if (osize == opsize::none || osize == opsize::w)
                add_fixups(ea, info.type, opsize::w, ofs + 2);
            else 
                add_fixups(ea, info.type, opsize::b, ofs + 1);
        } else {
            if (disp == 0 || !range8(disp)) {
                if (osize == opsize::b)
                    ASSEMBLER_ERROR("Displacement " << disp << " is out of range for byte size");
                if (!range16(disp))
                    ASSEMBLER_ERROR("Displacement " << disp << " is out of range");
                osize = opsize::w;
            } else if (osize == opsize::none)
                osize = opsize::b;
        }
        return disp;
    }

    uint16_t encode_addsub_x(const instruction_info_type& info, const ea_result* ea, opsize osize, bool is_sub)
    {
        bool memop = false;
        if (ea[0].type >> ea_m_shift == ea_m_Dn && ea[1].type >> ea_m_shift == ea_m_Dn) {
            // OK
        } else if (ea[0].type >> ea_m_shift == ea_m_A_ind_pre && ea[1].type >> ea_m_shift == ea_m_A_ind_pre) {
            memop = true;
        } else {
            ASSEMBLER_ERROR("Invalid operands to " << info.name);
        }
        constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };
        return 0x9100 | (!is_sub) << 14 | (ea[1].type & 7) << 9 | size_encoding[static_cast<uint8_t>(osize)] << 6 | memop << 3 | (ea[0].type & 7);
    }

    uint16_t encode_abcd_sbcd(const instruction_info_type& info, const ea_result* ea, bool is_sub)
    {
        bool memop = false;
        if (ea[0].type >> ea_m_shift == ea_m_Dn && ea[1].type >> ea_m_shift == ea_m_Dn) {
            // OK
        } else if (ea[0].type >> ea_m_shift == ea_m_A_ind_pre && ea[1].type >> ea_m_shift == ea_m_A_ind_pre) {
            memop = true;
        } else {
            ASSEMBLER_ERROR("Invalid operands to " << info.name);
        }
        return 0x8100 | (!is_sub) << 14 | (ea[1].type & 7) << 9 | memop << 3 | (ea[0].type & 7);
    }

    uint16_t encode_muldiv(const instruction_info_type& info, const ea_result* ea, bool is_signed, bool is_mul)
    {
        if (ea[0].type > ea_immediate || ea[1].type > ea_immediate || ea[0].type >> ea_m_shift == ea_m_An)
            ASSEMBLER_ERROR("Invalid source operand for " << info.name);
        if (ea[1].type >> ea_m_shift != ea_m_Dn)
            ASSEMBLER_ERROR("Destination must be data register for " << info.name);
        return 0x80c0 | is_mul << 14 | (ea[1].type & 7) << 9 | is_signed << 8 | (ea[0].type & 0x3f);
    }

    uint16_t encode_shift_rotate(const instruction_info_type& info, ea_result* ea, opsize osize, uint8_t code, bool left)
    {
        if (info.num_operands == 1) {
            if (osize != opsize::w)
                ASSEMBLER_ERROR("Must be word size for one-argument version of " << info.name);
            if (ea[0].type >> ea_m_shift <= ea_m_An || ea[0].type >= ea_immediate)
                ASSEMBLER_ERROR("Invalid operand for " << info.name);
            return 0xe000 | code << 9 | left << 8 | 0b11 << 6 | (ea[0].type & 0x3f);
        } else if (ea[1].type >> ea_m_shift != ea_m_Dn) {
            ASSEMBLER_ERROR("Destination must be data register for " << info.name);
        }
        constexpr uint8_t size_encoding[4] = { 0b00, 0b00, 0b01, 0b10 };
        const uint16_t op = 0xe000 | left << 8 | size_encoding[static_cast<uint8_t>(osize)] << 6 | code << 3 | (ea[1].type & 7);
        if (ea[0].type >> ea_m_shift == ea_m_Dn) {
            return op | (ea[0].type & 7) << 9 | 1 << 5;
        } else if (ea[0].type == ea_immediate) {
            if (ea[0].val < 1 || ea[0].val > 8)
                ASSEMBLER_ERROR("Immediate out of range for " << info.name);
            ea[0].type = 0; // Don't encode the immediate
            return op | (ea[0].val & 7) << 9;
        } else {
            ASSEMBLER_ERROR("Invalid source operand for " << info.name);
        }
    }
};

std::vector<uint8_t> assemble(uint32_t start_pc, const char* code, const std::vector<std::pair<const char*, uint32_t>>& predefines)
{
    assembler a { start_pc, code, predefines };
    return a.process();
}
