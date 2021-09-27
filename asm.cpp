#include "asm.h"
#include "instruction.h"
#include "ioutil.h"
#include "disasm.h"
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>

constexpr uint8_t opsize_mask_none = 0;
constexpr uint8_t opsize_mask_b    = 1 << 0;
constexpr uint8_t opsize_mask_w    = 1 << 1;
constexpr uint8_t opsize_mask_l    = 1 << 2;
constexpr uint8_t opsize_mask_bw   = opsize_mask_b | opsize_mask_w; 
constexpr uint8_t opsize_mask_bwl  = opsize_mask_b | opsize_mask_w | opsize_mask_l;


constexpr bool range8(uint32_t val)
{
    return static_cast<int32_t>(val) >= -128 && static_cast<int32_t>(val) <= 127;
}

constexpr bool range16(uint32_t val)
{
    return static_cast<int32_t>(val) >= -32768 && static_cast<int32_t>(val) <= 32767;
}

#define INSTRUCTIONS(X)        \
    X(BRA   , bw   , none , 1) \
    X(MOVE  , bwl  , w    , 2) \
    X(MOVEQ , l    , l    , 2) \
    X(RTS   , none , none , 0) \


constexpr struct instruction_info_type {
    const char* const name;
    uint8_t opsize_mask;
    opsize default_size;
    uint8_t num_operands;
} instruction_info[] = {
#define INSTRUCTION_INFO(name, osize_mask, def_size, num_oper) { #name, opsize_mask_##osize_mask, opsize::def_size, num_oper },
    INSTRUCTIONS(INSTRUCTION_INFO)
#undef INSTRUCTION_INFO
};

#define ASSEMBLER_ERROR(...)                                                                                                                  \
    do {                                                                                                                                      \
        std::ostringstream oss;                                                                                                               \
        oss << "In functin " << __func__ << " line " << __LINE__ << ": Error in line " << line_ << " column " << col_ << ": " << __VA_ARGS__; \
        throw std::runtime_error { oss.str() };                                                                                               \
    } while (0)

class assembler {
public:
    explicit assembler(uint32_t start_pc, const char* text)
        : pc_ { start_pc }
        , input_ { text }
    {
    }


    std::vector<uint16_t> process()
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
                    // TODO: Check range
                    switch (f.size) {
                    case opsize::none:
                        ASSEMBLER_ERROR("Invalid fixup");
                    case opsize::b: {
                        assert(f.offset / 2 < result_.size());
                        assert(f.offset & 1);
                        auto v = (result_[f.offset / 2] & 0xff) + ii.value & 0xff;
                        result_[f.offset / 2] = (result_[f.offset / 2] & 0xff00) | (v & 0xff);
                        break;
                    }
                    case opsize::w: {
                        assert(f.offset % 2 == 0 && f.offset / 2 < result_.size());
                        result_[f.offset / 2] += static_cast<uint16_t>(ii.value);
                        break;
                    }
                    case opsize::l: {
                        assert(f.offset % 2 == 0 && f.offset / 2 + 1< result_.size());
                        auto v = (result_[f.offset / 2] << 16 | result_[f.offset / 2 + 1]) + ii.value;
                        result_[f.offset / 2] = static_cast<uint16_t>(v >> 16);
                        result_[f.offset / 2 + 1] = static_cast<uint16_t>(v);
                        break;
                    }
                    }
                }
                ii.fixups.clear();
            } else if (is_instruction(token_type_)) {
                const auto inst = token_type_;
                get_token();
                process_instruction(inst);
            } else if (token_type_ == token_type::dc) {
                process_dc();
            } else {
                ASSEMBLER_ERROR("Unexpected token: " << token_type_string(token_type_));
            }
        } while (token_type_ != token_type::eof);

        for (const auto& i : identifier_info_) {
            if (!i.has_value || !i.fixups.empty()) {
                ASSEMBLER_ERROR(i.id << " referenced but not definde");
            }
        }

        return std::move(result_);
    }

private:
    enum class token_type {
        eof,
        whitespace,
        newline,
        number,
        // 33 ... 127 reserved for operators
        hash = '#',
        lparen = '(',
        rparen = ')',
        plus = '+',
        comma = ',',
        minus = '-',
        colon = ':',

        last_operator=127,
        size_b,
        size_w,
        size_l,
        d0, d1, d2, d3, d4, d5, d6, d7,
        a0, a1, a2, a3, a4, a5, a6, a7,
        pc,
        dc,

        instruction_start_minus_1,
        #define TOKEN_INST(n, m, d, no) n,
        INSTRUCTIONS(TOKEN_INST)
        #undef TOKEN_INST

        identifier_start,
    };

    std::unordered_map<std::string, token_type> id_map_ = {
        #define INST_ID_INIT(n, m, d, no) { #n, token_type::n },
        INSTRUCTIONS(INST_ID_INIT)
        #undef INST_ID_INIT
        { ".B", token_type::size_b },
        { ".W", token_type::size_w },
        { ".L", token_type::size_l },
        { "D0", token_type::d0 },
        { "D1", token_type::d1 },
        { "D2", token_type::d2 },
        { "D3", token_type::d3 },
        { "D4", token_type::d4 },
        { "D5", token_type::d5 },
        { "D6", token_type::d6 },
        { "D7", token_type::d7 },
        { "A0", token_type::a0 },
        { "A1", token_type::a1 },
        { "A2", token_type::a2 },
        { "A3", token_type::a3 },
        { "A4", token_type::a4 },
        { "A5", token_type::a5 },
        { "A6", token_type::a6 },
        { "A7", token_type::a7 },
        { "PC", token_type::pc },
        { "DC", token_type::dc },
    };

    struct fixup_type {
        opsize size;
        uint32_t offset;
    };

    struct identifier_info_type {
        std::string id;
        bool has_value;
        uint32_t value;
        std::vector<fixup_type> fixups;
    };

    std::vector<identifier_info_type> identifier_info_;

    std::vector<uint16_t> result_;
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
        return identifier_info_[id_idx];
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
        case token_type::size_b:
            return "SIZE.B";
        case token_type::size_w:
            return "SIZE.W";
        case token_type::size_l:
            return "SIZE.L";
        case token_type::pc:
            return "PC";
        case token_type::dc:
            return "DC";
        #define CASE_INST_TOKEN(n, m, d, no) case token_type::n: return #n;
            INSTRUCTIONS(CASE_INST_TOKEN)
        #undef CASE_INST_TOKEN
        default:
            const auto ti = static_cast<uint32_t>(tt);
            if (ti >= 33 && ti <= 127) {
                std::ostringstream oss;
                oss << "OPERATOR \"" << static_cast<char>(ti) << "\"";
                return oss.str();
            }
            if (ti >= static_cast<uint32_t>(token_type::d0) && ti <= static_cast<uint32_t>(token_type::d7))
                return "D" + std::to_string(ti - static_cast<uint32_t>(token_type::d0));
            else if (ti >= static_cast<uint32_t>(token_type::a0) && ti <= static_cast<uint32_t>(token_type::a7))
                return "A" + std::to_string(ti - static_cast<uint32_t>(token_type::a0));
            const auto id_idx = ti - static_cast<uint32_t>(token_type::identifier_start);
            if (id_idx < identifier_info_.size()) {
                std::ostringstream oss;
                oss << "INDENTIFIER \"" << identifier_info_[id_idx].id << "\"";
                return oss.str();
            }
            ASSEMBLER_ERROR("Unknown token type: " << ti);
        }
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
        case '#':
        case '(':
        case ')':
        case '+':
        case ',':
        case '-':
        case ':':
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

            std::string uc { token_text_ };
            for (auto& ch : uc)
                ch = static_cast<char>(toupper(ch));

            if (auto it = id_map_.find(uc); it != id_map_.end()) {
                token_type_ = it->second;
            } else {
                const uint32_t id = static_cast<uint32_t>(identifier_info_.size()) + static_cast<uint32_t>(token_type::identifier_start);
                identifier_info_type ii {};
                ii.id = uc;
                identifier_info_.push_back(ii);
                token_type_ = static_cast<token_type>(id);
                id_map_[ii.id] = token_type_;
            }
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
        uint32_t val;
        identifier_info_type* fixup;
    };

    ea_result process_number(bool neg = false)
    {
        // TODO: Aritmetic etc.
        ea_result res {};

        if (token_type_ == token_type::minus) {
            if (neg)
                ASSEMBLER_ERROR("Double minus not allowed");
            neg = true;
            get_token();
        }

        if (token_type_ == token_type::number) {
            res.val = token_number_;
            get_token();
        } else if (is_identifier(token_type_)) {
            auto& ii = identifier_info(token_type_);
            get_token();
            if (ii.has_value) {
                res.val = ii.value;
            }  else {
                res.fixup = &ii;
            }
        } else {
            ASSEMBLER_ERROR("Invalid number");
        }

        if (neg) {
            if (res.fixup)
                ASSEMBLER_ERROR("Not supported: Negating with fixup");
            res.val = -static_cast<int32_t>(res.val);
        }

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
        if (token_type_ == token_type::hash) {
            get_token();
            auto res = process_number();
            res.type = ea_immediate;
            return res;
        } else if (token_type_ == token_type::minus) {
            get_token();
            if (token_type_ != token_type::lparen) {
                neg = true;
                goto number;
            }

            get_token();
            ea_result res {};
            res.type = ea_m_A_ind_pre << ea_m_shift | process_areg();
            expect(token_type::rparen);
            return res;
        } else if (token_type_ == token_type::lparen) {
            get_token();
            ea_result res {};
            res.type = ea_m_A_ind << ea_m_shift | process_areg();
            expect(token_type::rparen);
            if (token_type_ == token_type::plus) {
                res.type = ea_m_A_ind_post << ea_m_shift | (res.type & 7);
                get_token();
            }
            return res;
        } else if (token_type_ == token_type::number || is_identifier(token_type_)) {
number:
            auto res = process_number(neg);
            if (token_type_ == token_type::lparen) {
                get_token();
                uint8_t reg;
                if (token_type_ == token_type::pc) {
                    reg = 0xff;
                    res.val -= pc_ + 2;
                    get_token();
                } else {
                    reg = process_areg();
                    if (!range16(res.val))
                        ASSEMBLER_ERROR(static_cast<int32_t>(res.val) << " is out of range for 16-bit displacement");
                }
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
                    if (!range8(res.val))
                        ASSEMBLER_ERROR(static_cast<int32_t>(res.val) << " is out of range for 8-bit displacement");
                }
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
            ea_result res {};
            res.type = static_cast<uint8_t>(static_cast<uint32_t>(token_type_) - static_cast<uint32_t>(token_type::d0));
            get_token();
            return res;
        }
        ASSEMBLER_ERROR("Unexpected token: " << token_type_string(token_type_));
    }

    void process_instruction(token_type inst)
    {
        const auto& info = instruction_info[static_cast<uint32_t>(inst) - static_cast<uint32_t>(token_type::instruction_start_minus_1) - 1];
        ea_result ea[2] = {};
        auto osize = info.default_size;
        assert(info.num_operands <= 2);

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
        }

        for (uint8_t arg = 0; arg < info.num_operands; ++arg) {
            expect(arg == 0 ? token_type::whitespace : token_type::comma);
            ea[arg] = process_ea();
        }

operands_done:
        skip_to_eol();
        
        uint16_t iwords[max_instruction_words];
        uint8_t iword_cnt = 1;

        switch (inst) {
        case token_type::BRA: {
            if (ea[0].type >> ea_m_shift != ea_m_Other || (ea[0].type & 7) != ea_other_abs_l)
                ASSEMBLER_ERROR("Unsupported operand to Bcc: " << ea_string(ea[0].type));
            int32_t disp = ea[0].val - (pc_ + 2);
            if (ea[0].fixup) {
                const uint32_t ofs = static_cast<uint32_t>(result_.size() * 2);
                if (osize == opsize::none || osize == opsize::w)
                    ea[0].fixup->fixups.push_back(fixup_type { opsize::w, ofs + 2 });
                else
                    ea[0].fixup->fixups.push_back(fixup_type { opsize::b, ofs + 1 });
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
            iwords[0] = 0x6000 /* | cc << 8 */;
            if (osize == opsize::b)
                iwords[0] |= disp & 0xff;
            else
                iwords[iword_cnt++] = disp & 0xffff;
            goto done;
        }
        case token_type::MOVE: {
            constexpr uint8_t size_encoding[4] = { 0b00, 0b01, 0b11, 0b10 };
            const auto sz = size_encoding[static_cast<uint8_t>(osize)];
            iwords[0] = sz << 12 | (ea[1].type & 7) << 9 | (ea[1].type & (7 << 3)) << (6 - 3) | ea[0].type;
            break;
        }
        case token_type::MOVEQ: {
            if (ea[0].type != ea_immediate || ea[0].fixup || ea[1].type >> ea_m_shift != ea_m_Dn)
                ASSEMBLER_ERROR("Invalid operands to MOVEQ");
            if (!range8(ea[0].val))
                ASSEMBLER_ERROR("Immediate out of 8-bit range for MOVEQ: " << ea[0].val);
            iwords[0] = 0x7000 | (ea[1].type & 7) << 9 | (ea[0].val & 0xff);
            goto done;
        }
        case token_type::RTS:
            iwords[0] = 0x4e75;
            break;
        default:
            ASSEMBLER_ERROR("TODO: Encode " << info.name);
        }

        // TODO: Register list always immediately follows instruction

        for (uint8_t arg = 0; arg < info.num_operands; ++arg) {
            switch (ea[arg].type >> ea_m_shift) {
            case ea_m_Dn:
            case ea_m_An:
            case ea_m_A_ind:
            case ea_m_A_ind_post:
            case ea_m_A_ind_pre:
                assert(!ea[arg].fixup);
                break;
            case ea_m_A_ind_disp16: // (d16, An)
            case ea_m_A_ind_index: // (d8, An, Xn)
                assert(!ea[arg].fixup);
                iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                break;
            case ea_m_Other: {
                uint32_t ofs = static_cast<uint32_t>((result_.size() + iword_cnt) * 2);
                switch (ea[arg].type & 7) {
                case ea_other_abs_w:
                    if (ea[arg].fixup)
                        ea[arg].fixup->fixups.push_back(fixup_type { opsize::w, ofs });
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_abs_l:
                    if (ea[arg].fixup)
                        ea[arg].fixup->fixups.push_back(fixup_type { opsize::l, ofs });
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val >> 16);
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_pc_disp16: // (d16, PC)
                    if (ea[arg].fixup)
                        ea[arg].fixup->fixups.push_back(fixup_type { opsize::w, ofs });
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_pc_index:  // (d8, PC, Xn)
                    if (ea[arg].fixup)
                        ea[arg].fixup->fixups.push_back(fixup_type { opsize::b, ofs+1 });
                    iwords[iword_cnt++] = static_cast<uint16_t>(ea[arg].val);
                    break;
                case ea_other_imm:
                    if (ea[arg].fixup) {
                        assert(osize != opsize::none);
                        if (osize == opsize::b)
                            ++ofs;
                        ea[arg].fixup->fixups.push_back(fixup_type { osize, ofs });
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
            }
            default:
                ASSEMBLER_ERROR("TODO: Encoding for " << ea_string(ea[arg].type));
            }
        }

done:
        result_.insert(result_.end(), iwords, iwords + iword_cnt);
        pc_ += iword_cnt * 2;
    }

    void process_dc()
    {
        get_token();
        opsize osize = opsize::w;
        if (token_type_ == token_type::size_b) {
            osize = opsize::b;
            get_token();
            ASSEMBLER_ERROR("TODO: DC.B");
        } else if (token_type_ == token_type::size_w) {
            osize = opsize::w;
            get_token();
        } else if (token_type_ == token_type::size_l) {
            osize = opsize::l;
            get_token();
        }
      
        expect(token_type::whitespace);
        for (;;) {
            auto num = process_number();
            if (num.fixup) {
                assert(osize != opsize::b);
                num.fixup->fixups.push_back(fixup_type { osize, static_cast<uint32_t>(result_.size() * 2) });
            }

            if (osize == opsize::l)
                result_.push_back(static_cast<uint16_t>(num.val >> 16));
            result_.push_back(static_cast<uint16_t>(num.val));

            skip_whitespace();
            if (token_type_ != token_type::comma)
                break;
            get_token();
        }
        skip_to_eol();
    }
};

std::vector<uint16_t> assemble(uint32_t start_pc, const char* code)
{
    assembler a { start_pc, code };
    return a.process();
}
