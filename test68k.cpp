#include <iostream>
#include <filesystem>
#include "ioutil.h"
#include "cpu.h"
#include "memory.h"
#include "disasm.h"

namespace fs = std::filesystem;

constexpr uint32_t get_test_field(uint32_t val, uint32_t start, uint32_t len)
{
    return (val >> (32 - (start + len))) & ((1 << len) - 1);
}

void run_test_case(const std::string& filename)
{
    const char signature[] = "binary\r";
    const auto signature_len = sizeof(signature) - 1;
    const auto data = read_file(filename);
    const auto size = data.size();
    if (size < signature_len + 4 || !memcmp(&data[0], signature, signature_len)) {
        throw std::runtime_error { filename + " has unexpected format" };
    }

    const uint32_t code_pos = 0x1000;
    const uint32_t code_size = 0x2000;
    memory_handler mem { code_pos + code_size };
    auto& ram = mem.ram();

    size_t pos = signature_len;
    std::string name;
    std::vector<uint16_t> inst_words;
    struct test_state {
        uint8_t  regs[5];
        uint32_t regvals[5];
        uint8_t  ccr;
    } states[3];

    unsigned state_pos = 0;
    unsigned test_count = 0;

    auto build_state = [](const test_state& s, const uint32_t pc_add = 0) {
        cpu_state st {};
        st.pc = code_pos + pc_add;
        st.sr = s.ccr;
        for (unsigned i = 0; i < 5; ++i) {
            const auto& r = s.regs[i];
            switch (r >> 3) {
            case 1:
                st.d[r & 7] = s.regvals[i];
                break;
            case 2:
                st.A(r & 7) = s.regvals[i];
                break;
            }
        }
        if (pc_add)
            st.instruction_count = 1;
        return st;
    };

    while (pos < size) {
        const auto w = get_u32(&data[pos]);
        pos += 4;
        //std::cout << "W: " << hexfmt(w) << " " << binfmt(w) << "\n";
        switch (get_test_field(w, 0, 2)) {
        case 0: {
            // 1 = end test, 2 = end series
            const auto type = get_test_field(w, 2, 2);
            switch (type) {
            case 1: { // Run test
                assert(state_pos == 3);
                ++test_count;
                const cpu_state input_state    = build_state(states[0]);
                const cpu_state expected_state = build_state(states[1], static_cast<uint32_t>(2 * inst_words.size())); 

                memset(&ram[0], 0, ram.size());
                for (unsigned i = 0; i < inst_words.size(); ++i)
                    put_u16(&ram[code_pos + i * 2], inst_words[i]);

                m68000 cpu { mem, input_state };
                const auto& outst = cpu.state();
                try {
                    cpu.step();
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                    goto report_error;
                }

                if (memcmp(&outst, &expected_state, sizeof(cpu_state))) {
                report_error:
                    std::cerr << "Test " << test_count << " failed for " << name;
                    for (const auto iw : inst_words)
                        std::cerr << " " << hexfmt(iw);
                    std::cerr << "\n";
                    std::cerr << "\n\nInput state:\n";
                    print_cpu_state(std::cerr, input_state);
                    disasm(std::cerr, code_pos, &inst_words[0], inst_words.size());
                    std::cerr << "\n";
                    std::cerr << "\n\nExpected state:\n";
                    print_cpu_state(std::cerr, expected_state);
                    std::cerr << "\n\nActual state:\n";
                    print_cpu_state(std::cerr, outst);
                    throw std::runtime_error { "Test failed for " + name };
                }

                state_pos = 0;
                break;
            }
            case 2:
                //std::cout << "\nEnd of test series\n\n";
                break;
            default:
                throw std::runtime_error { "Unsupported END type " + std::to_string(type) };
            }
            break;
        }
        case 1: { // Instruction
            const auto name_len    = get_test_field(w, 2, 3);
            const auto extra_longs = get_test_field(w, 5, 11);
            const auto opcode      = static_cast<uint16_t>(w);
            inst_words.clear();
            inst_words.push_back(opcode);
            for (unsigned i = 0; i < extra_longs; ++i) {
                assert(pos < size);
                const auto iw = get_u32(&data[pos]);
                pos += 4;
                inst_words.push_back(static_cast<uint16_t>(iw >> 16));
                inst_words.push_back(static_cast<uint16_t>(iw));
            }
            name.assign(reinterpret_cast<const char*>(&data[pos]), name_len);
            pos += name_len;
            /*std::cout << "Name: " << name << ", iwords: ";
            for (const auto& iw : inst_words) {
                std::cout << " " << hexfmt(iw);
            }
            std::cout << "\n";*/
            break;
        }
        case 2: { // CPU State
            assert(state_pos < 3);
            auto& s = states[state_pos++];
            s.ccr = static_cast<uint8_t>(get_test_field(w, 27, 5));

            for (unsigned i = 0, bitpos = 2; i < 5; ++i, bitpos += 5) {
                const auto type = get_test_field(w, bitpos, 2);
                const auto id   = get_test_field(w, bitpos+2, 3);
                s.regs[i] = static_cast<uint8_t>(type << 3 | id);
                // Type: None=0, Data=1, Addr=2
                if (type) {
                    assert(pos < size);
                    assert(type == 1 || type == 2);
                    s.regvals[i] = get_u32(&data[pos]);
                    pos += 4;
                }
            }
            break;
        }
        case 3: { // Results
            // Type: 0 = tests, 1 = errors
            //const auto type = get_test_field(w, 2, 1);
            //const auto count = get_test_field(w, 3, 29);
            //std::cout << "Results: type = " << type << " count = " << count << "\n";
            break;
        }
        }
    }

    assert(pos == size);
}

bool run_tests()
{
    const std::string test_path = "../../Misc/m68k-tester-work/m68k-tests/";
    const char* const testnames[] = {
        // OK
        "gen-opcode-abcd.bin",
        "gen-opcode-addb.bin",
        "gen-opcode-addl.bin",
        "gen-opcode-addw.bin",
        "gen-opcode-addxb.bin",
        "gen-opcode-addxl.bin",
        "gen-opcode-addxw.bin",
        "gen-opcode-andb.bin",
        "gen-opcode-andl.bin",
        "gen-opcode-andw.bin",
        "gen-opcode-aslb.bin",
        "gen-opcode-asll.bin",
        "gen-opcode-aslw.bin",
        "gen-opcode-asrb.bin",
        "gen-opcode-asrl.bin",
        "gen-opcode-asrw.bin",
        "gen-opcode-bchg.bin",
        "gen-opcode-bclr.bin",
        "gen-opcode-bset.bin",
        "gen-opcode-clrb.bin",
        "gen-opcode-clrl.bin",
        "gen-opcode-clrw.bin",
        "gen-opcode-cmpb.bin",
        "gen-opcode-cmpl.bin",
        "gen-opcode-cmpw.bin",
        "gen-opcode-divu.bin",
        "gen-opcode-divs.bin",
        "gen-opcode-eorb.bin",
        "gen-opcode-eorl.bin",
        "gen-opcode-eorw.bin",
        "gen-opcode-extl.bin",
        "gen-opcode-extw.bin",
        "gen-opcode-lslb.bin",
        "gen-opcode-lsll.bin",
        "gen-opcode-lslw.bin",
        "gen-opcode-lsrb.bin",
        "gen-opcode-lsrl.bin",
        "gen-opcode-lsrw.bin",
        "gen-opcode-nbcd.bin",
        "gen-opcode-negb.bin",
        "gen-opcode-negl.bin",
        "gen-opcode-negw.bin",
        "gen-opcode-negxb.bin",
        "gen-opcode-negxl.bin",
        "gen-opcode-negxw.bin",
        "gen-opcode-notb.bin",
        "gen-opcode-notl.bin",
        "gen-opcode-notw.bin",
        "gen-opcode-orb.bin",
        "gen-opcode-orl.bin",
        "gen-opcode-orw.bin",
        "gen-opcode-sbcd.bin",
        "gen-opcode-scc.bin",
        "gen-opcode-scs.bin",
        "gen-opcode-seq.bin",
        "gen-opcode-sge.bin",
        "gen-opcode-sgt.bin",
        "gen-opcode-shi.bin",
        "gen-opcode-sle.bin",
        "gen-opcode-slt.bin",
        "gen-opcode-sls.bin",
        "gen-opcode-smi.bin",
        "gen-opcode-sne.bin",
        "gen-opcode-spl.bin",
        "gen-opcode-subb.bin",
        "gen-opcode-subl.bin",
        "gen-opcode-subw.bin",
        "gen-opcode-subxb.bin",
        "gen-opcode-subxl.bin",
        "gen-opcode-subxw.bin",
        "gen-opcode-svc.bin",
        "gen-opcode-svs.bin",
#if 0
        // Not implemented
        "gen-opcode-tas.bin"
#endif
    };

    for (const auto& t : testnames) {
        std::cout << "Running " << t << "\n";
        try {
            run_test_case(test_path + t);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n\n";
            return false;
        }
    }
    return true;
}

class winuae_test_file {
public:
    explicit winuae_test_file(const fs::path& p)
        : p_ { p }
        , data_ { read_file(p_.string()) }
        , pos_ { 0 }
    {
    }
    
    const fs::path& path() const
    {
        return p_;
    }

    const std::vector<uint8_t>& data() const
    {
        return data_;
    }

    size_t size() const
    {
        return data_.size();
    }

    size_t pos() const
    {
        return pos_;
    }

    uint8_t peek_u8()
    {
        assert(pos_ < size());
        return data_[pos_];
    }

    uint8_t read_u8()
    {
        assert(pos_ < size());
        return data_[pos_++];
    }

    uint16_t read_u16()
    {
        assert(pos_ + 1 < size());
        const uint16_t n = get_u16(&data_[pos_]);
        pos_ += 2;
        return n;
    }

    uint32_t read_u32()
    {
        assert(pos_ + 3 < size());
        const uint32_t n = get_u32(&data_[pos_]);
        pos_ += 4;
        return n;
    }

    std::string read_string(uint32_t len)
    {
        assert(pos_ + len <= size());
        std::string s;
        s.resize(len);
        s.assign(reinterpret_cast<const char*>(&data_[pos_]), len);
        pos_ += len;
        while (!s.empty() && s.back() == 0)
            s.pop_back();
        return s;
    }

    uint8_t get_at_u8(size_t offset) const
    {
        assert(offset < size());
        return data_[offset];
    }

private:
    fs::path p_;
    std::vector<uint8_t> data_;
    size_t pos_;
};

constexpr uint32_t expected_version = 20;

struct winuae_test_header {
    uint32_t version;
    uint32_t starttimeid;
    uint32_t hmem_lmem;
    uint32_t test_memory_addr;
    uint32_t test_memory_size;
    uint32_t opcode_memory_addr;
    uint32_t lvl_mask;
    uint32_t fpu_model;
    uint32_t test_low_memory_start;
    uint32_t test_low_memory_end;
    uint32_t test_high_memory_start;
    uint32_t test_high_memory_end;
    uint32_t safe_memory_start;
    uint32_t safe_memory_end;
    uint32_t user_stack_memory;
    uint32_t super_stack_memory;
    uint32_t exception_vectors;
    std::string inst_name;
};

struct winuae_test_state {
    uint32_t regs[16];
    uint32_t pc;
    uint32_t sr;

    uint32_t cycles;
	uint32_t srcaddr;
    uint32_t dstaddr;
    uint32_t branchtarget;
    uint8_t  branchtarget_mode;
    uint32_t endpc;
};

winuae_test_header read_winuae_test_header(const fs::path& dir)
{
    winuae_test_header h;
    winuae_test_file header_file { dir / "0000.dat" };
    h.version = header_file.read_u32();
    if (h.version != expected_version) {
        throw std::runtime_error { header_file.path().string() + " has invalid version " + std::to_string(h.version) };
    }

    h.starttimeid = header_file.read_u32();
    h.hmem_lmem = header_file.read_u32();
    h.test_memory_addr = header_file.read_u32();
    h.test_memory_size = header_file.read_u32();
    h.opcode_memory_addr = header_file.read_u32();
    h.lvl_mask = header_file.read_u32();
    h.fpu_model = header_file.read_u32();
    h.test_low_memory_start = header_file.read_u32();
    h.test_low_memory_end = header_file.read_u32();
    h.test_high_memory_start = header_file.read_u32();
    h.test_high_memory_end = header_file.read_u32();
    h.safe_memory_start = header_file.read_u32();
    h.safe_memory_end = header_file.read_u32();
    h.user_stack_memory = header_file.read_u32();
    h.super_stack_memory = header_file.read_u32();
    h.exception_vectors = header_file.read_u32();
    header_file.read_u32();
    header_file.read_u32();
    header_file.read_u32();
    h.inst_name = header_file.read_string(16);
    return h;
}


const uint8_t CT_FPREG          = 0;
const uint8_t CT_DREG           = 0;
const uint8_t CT_AREG           = 8;
const uint8_t CT_SSP            = 16;
const uint8_t CT_MSP            = 17;
const uint8_t CT_SR             = 18;
const uint8_t CT_PC             = 19;
const uint8_t CT_FPIAR          = 20;
const uint8_t CT_FPSR           = 21;
const uint8_t CT_FPCR           = 22;
const uint8_t CT_CYCLES         = 25;
const uint8_t CT_ENDPC          = 26;
const uint8_t CT_BRANCHTARGET   = 27;
const uint8_t CT_SRCADDR        = 28;
const uint8_t CT_DSTADDR        = 29;
const uint8_t CT_MEMWRITE       = 30;
const uint8_t CT_MEMWRITES      = 31;
const uint8_t CT_DATA_MASK      = 31;

const uint8_t CT_EXCEPTION_MASK = 63;

const uint8_t CT_SIZE_BYTE      = 0 << 5;
const uint8_t CT_SIZE_WORD      = 1 << 5;
const uint8_t CT_SIZE_LONG      = 2 << 5;
const uint8_t CT_SIZE_FPU       = 3 << 5; // CT_DREG -> CT_FPREG
const uint8_t CT_SIZE_MASK      = 3 << 5;

// if MEMWRITE or PC
const uint8_t CT_RELATIVE_START_WORD = 0 << 5; // word
const uint8_t CT_ABSOLUTE_WORD       = 1 << 5;
const uint8_t CT_ABSOLUTE_LONG       = 2 << 5;
// if MEMWRITES
const uint8_t CT_PC_BYTES            = 3 << 5;
// if PC
const uint8_t CT_RELATIVE_START_BYTE = 3 << 5;

const uint8_t CT_END            = 0x80;
const uint8_t CT_END_FINISH     = 0xff;
const uint8_t CT_END_INIT       = 0x80 | 0x40;
const uint8_t CT_END_SKIP       = 0x80 | 0x40 | 0x01;
const uint8_t CT_SKIP_REGS      = 0x80 | 0x40 | 0x02;
const uint8_t CT_EMPTY          = CT_END_INIT;
const uint8_t CT_OVERRIDE_REG   = 0x80 | 0x40 | 0x10;
const uint8_t CT_BRANCHED       = 0x40;


struct memwrite_info {
    uint32_t addr;
    uint32_t old;
    uint32_t val;
    uint8_t size; // 0-2
};

constexpr uint32_t low_memory = 0x00000000;
constexpr uint32_t high_memory = 0xffff8000;

winuae_test_header test_header;
std::vector<uint8_t> test_lowmem;
std::vector<uint8_t> test_testmem;
std::vector<uint8_t>* testram;
std::vector<memwrite_info> memwrite_restore_list;
bool debug_winuae_tests = false;

void sized_write(uint32_t addr, uint32_t val, int size)
{
    auto& ram = *testram;
    assert(addr < ram.size() && addr + (1 << size) <= ram.size());

    switch (size) {
    case 0:
        ram[addr] = static_cast<uint8_t>(val);
        break;
    case 1:
        put_u16(&ram[addr], static_cast<uint16_t>(val));
        break;
    case 2:
        put_u32(&ram[addr], val);
        break;
    default:
        assert(!"Invalid size");
    }
}

uint32_t sized_read(uint32_t addr, int size)
{
    auto& ram = *testram;
    assert(addr < ram.size() && addr + (1 << size) <= ram.size());

    switch (size) {
    case 0:
        return ram[addr];
        break;
    case 1:
        return get_u16(&ram[addr]);
    case 2:
        return get_u32(&ram[addr]);
    default:
        assert(!"Invalid size");
        return 0;
    }
}

void do_memwrite(const memwrite_info& mwi, bool store)
{
    if (store)
        memwrite_restore_list.push_back(mwi);
    sized_write(mwi.addr, mwi.val, mwi.size);
}

void do_memwrite_restore()
{
    for (size_t i = memwrite_restore_list.size(); i--;) {
        const auto& mwi = memwrite_restore_list[i];
        sized_write(mwi.addr, mwi.old, mwi.size);
    }
    memwrite_restore_list.clear();
}

void restore_value(winuae_test_file& tf, uint32_t& val, uint8_t ct)
{
    switch (ct & CT_SIZE_MASK) {
    case CT_SIZE_BYTE:
        val &= 0xffffff00;
        val |= tf.read_u8();
        return;
    case CT_SIZE_WORD:
        val &= 0xffff0000;
        val |= tf.read_u16();
        return;
    case CT_SIZE_LONG:
        val = tf.read_u32();
        return;
    }
    throw std::runtime_error { "Invalid CT in restore_value: $" + hexstring(ct) };
}

void restore_rel(winuae_test_file& tf, uint32_t& val, uint8_t ct)
{
    switch (ct & CT_SIZE_MASK) {
    case CT_RELATIVE_START_WORD:
        val += static_cast<int16_t>(tf.read_u16());
        return;
    case CT_ABSOLUTE_WORD:
        val = static_cast<int16_t>(tf.read_u16());
        return;
    case CT_ABSOLUTE_LONG:
        val = tf.read_u32();
        return;
    case CT_RELATIVE_START_BYTE:
        val += static_cast<int8_t>(tf.read_u8());
        return;
    }
    throw std::runtime_error { "Invalid CT in restore_rel: $" + hexstring(ct) + " size = " + std::to_string((ct>>5)&3) };
}

void restore_value_new_ct(winuae_test_file& tf, uint32_t& val)
{
    restore_value(tf, val, tf.read_u8());
}

uint32_t get_addr(winuae_test_file& tf, uint8_t ct)
{
    switch (ct & CT_SIZE_MASK) {
    case CT_ABSOLUTE_WORD: {
        const int16_t ofs = static_cast<int16_t>(tf.read_u16());
        if (ofs < 0) {
            return high_memory + (32768U + ofs);
        } else {
            return low_memory + ofs;
        }
    }
    case CT_ABSOLUTE_LONG:
        return tf.read_u32();
    case CT_RELATIVE_START_WORD:
        return test_header.opcode_memory_addr + static_cast<int16_t>(tf.read_u16());
    }
    throw std::runtime_error { "Not supported in get_addr: ct=$" + hexstring(ct) };
}

memwrite_info get_memwrite_info(winuae_test_file& tf, uint8_t ct)
{
    memwrite_info mwi {};
    mwi.addr = get_addr(tf, ct);
    restore_value_new_ct(tf, mwi.old);
    mwi.size = ((tf.peek_u8() >> 5) & 3);
    restore_value_new_ct(tf, mwi.val);
    return mwi;
}

void restore_mem(winuae_test_file& tf, uint8_t ct, bool store)
{
    if ((ct & CT_DATA_MASK) == CT_MEMWRITES) {
        assert(store == false);
        assert((ct & CT_SIZE_MASK) == CT_PC_BYTES);
        const uint8_t next_byte = tf.read_u8();
        const uint32_t offset = (next_byte >> 5);
        uint16_t count = next_byte & 31;
        if (count == 31) {
            count = tf.read_u8();
            if (!count)
                count = 256;
        } else if (count == 0)
            count = 32;

        auto& ram = *testram;
        const uint32_t addr = test_header.opcode_memory_addr + offset;
        assert(addr < ram.size() && addr + count < ram.size());

        if (debug_winuae_tests)
            std::cout << "Writing " << (int)count << " bytes to $" << hexfmt(addr) << ": ";
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t b = tf.read_u8();
            if (debug_winuae_tests)
                std::cout << hexfmt(b);
            ram[addr + i] = b;
        }
        if (debug_winuae_tests)
            std::cout << "\n";
        
        return;
    } else {
        const memwrite_info mwi = get_memwrite_info(tf, ct);
        if (debug_winuae_tests)
            std::cout << "Restore mem (ct=$" << hexfmt(ct) << ") addr=$" << hexfmt(mwi.addr) << " old = $" << hexfmt(mwi.old) << " mv = $" << hexfmt(mwi.val) << " size=" << (int)mwi.size << " store=" << store << "\n"; 
        do_memwrite(mwi, store);
    }
}

void restore(winuae_test_file& tf, winuae_test_state& state, uint8_t ct)
{
    assert(!(ct & CT_END));

    const uint8_t mode = ct & CT_DATA_MASK;
    uint32_t val = 0;

    //std::cout << "Restore ct=$" << hexfmt(ct) << " mode = " << (int)mode << " size = " << (ct >> 5) << "\n";

    if (mode < 16) {
        restore_value(tf, state.regs[mode], ct);
        if (debug_winuae_tests)
            std::cout << "Restoring " << (mode < CT_AREG ? "D" : "A") << (int)(mode & 7) << " = $" << hexfmt(state.regs[mode]) << "\n";
        return;
    }
    switch (mode) {
    case CT_SSP:
    case CT_MSP:
    case CT_SR:
    case CT_PC:
        break;
    case CT_FPIAR:
        restore_value(tf, val, ct);
        if (debug_winuae_tests)
            std::cout << "Ignoring FPIAR=$" << hexfmt(val) << "\n";
        return;
        //        const uint8_t CT_FPSR = 21;
        //        const uint8_t CT_FPCR = 22;
    case CT_CYCLES:
        restore_value(tf, state.cycles, ct);
        if (debug_winuae_tests)
            std::cout << "TODO: cycles=$" << hexfmt(state.cycles) << "\n";
        return;
    case CT_ENDPC:
        restore_value(tf, state.endpc, ct);
        if (debug_winuae_tests)
            std::cout << "TODO: endpc=$" << hexfmt(state.endpc) << "\n";
        return;
    case CT_BRANCHTARGET:
        restore_value(tf, state.branchtarget, ct);
        if (debug_winuae_tests)
            std::cout << "TODO: branchtarget=$" << hexfmt(state.branchtarget) << "\n";
        state.branchtarget_mode = tf.read_u8();
        return;
    case CT_SRCADDR:
        restore_value(tf, state.srcaddr, ct);
        if (debug_winuae_tests)
            std::cout << "TODO: srcaddr=$" << hexfmt(state.srcaddr) << "\n";
        return;
    case CT_DSTADDR:
        restore_value(tf, state.dstaddr, ct);
        if (debug_winuae_tests)
            std::cout << "TODO: dstaddr=$" << hexfmt(state.dstaddr) << "\n";
        return;
    case CT_MEMWRITE:
        restore_mem(tf, ct, true);
        return;
    case CT_MEMWRITES:
        restore_mem(tf, ct, false);
        return;
    }
    throw std::runtime_error { "Not handled in restore: ct=$" + hexstring(ct) + " mode=" + std::to_string(mode) };
}

void validate_test(winuae_test_file& tf, const cpu_state& after, winuae_test_state& check_state)
{
    assert(tf.peek_u8() != CT_END_SKIP);

    (void)after; // TODO

    auto check = [](const std::string& desc, auto expected, auto actual) {
        if (expected == actual)
            return;
        std::ostringstream oss;
        oss << "Test failed for " << test_header.inst_name << ": " << desc << " expected=$" << hexfmt(expected) << " actual=$" << hexfmt(actual);
        throw std::runtime_error { oss.str() };
    };

    uint8_t exc = 0;
    for (;;) {
        const uint8_t ct = tf.read_u8();
        if (ct & CT_END) {
            if (debug_winuae_tests)
                std::cout << "End of validation. ct=$" << hexfmt(ct) << "\n";
            exc = ct & CT_EXCEPTION_MASK;
            if (ct & CT_BRANCHED) {
                assert(!exc);
                check("Branched", check_state.branchtarget, after.pc);
            }
            if (exc) {
                const uint8_t excdatalen = tf.read_u8();
                if (debug_winuae_tests)
                    std::cout << "Exception exc = " << (int)exc << " datalen = " << (int)excdatalen << "\n";
                assert(exc != static_cast<uint8_t>(interrupt_vector::trace));
                assert(excdatalen == 1);
                const uint8_t excextra = tf.read_u8();
                if (debug_winuae_tests)
                    std::cout << "Exception extra = $" << hexfmt(excextra) << "\n";
                assert(!excextra);
            }
            break;
        }

        const uint8_t mode = ct & CT_DATA_MASK;
        if (mode < 16) {
            restore_value(tf, check_state.regs[mode], ct);
            if (debug_winuae_tests)
                std::cout << "Validate restored " << (mode < 8 ? 'D' : 'A') << (int)(mode & 7) << "=$" << hexfmt(check_state.regs[mode]) << "\n";
            continue;
        } else if (mode == CT_SR) {
            restore_value(tf, check_state.sr, ct);
            if (debug_winuae_tests)
                std::cout << "Validate restored SR=$" << hexfmt(check_state.sr) << "\n";
            continue;
        } else if (mode == CT_PC) {
            restore_rel(tf, check_state.pc, ct);
            if (debug_winuae_tests)
                std::cout << "Validate restored PC=$" << hexfmt(check_state.pc) << "\n";
            continue;
        } else if (mode == CT_CYCLES) {
            restore_value(tf, check_state.cycles, ct);
            continue;
        } else if (mode == CT_MEMWRITE) {
            const memwrite_info mwi = get_memwrite_info(tf, ct);
            if (debug_winuae_tests)
                check("Mem compare at $" + hexstring(mwi.addr) + " size=" + std::to_string(1 << mwi.size), mwi.val, sized_read(mwi.addr, mwi.size));
            sized_write(mwi.addr, mwi.old, mwi.size);
            continue;
        }
        std::cout << "TODO: validate mode=" << (int)mode << "\n";

        assert(0);
    }

    for (int i = 0; i < 8; ++i)
        check("D" + std::to_string(i), check_state.regs[i], after.d[i]);
    for (int i = 0; i < 7; ++i)
        check("A" + std::to_string(i), check_state.regs[CT_AREG+i], after.A(i));
    if (exc) {
        // TODO: This is pretty insufficient
        check("PC (after exception)", 0xECC000U | exc << 4, after.pc);
        // TODO: SR&SP...
    } else {
        check("SP", check_state.regs[CT_AREG + 7], after.usp);
        check("PC", check_state.pc, after.pc);
        check("SR", static_cast<uint16_t>(check_state.sr), after.sr);
    }
    // TODO: Check cycles
}

bool run_winuae_mnemonic_test(const fs::path& dir)
{
    test_header = read_winuae_test_header(dir);
    assert(test_header.test_low_memory_end < test_header.test_memory_addr);
    assert(test_header.test_low_memory_start == 0 && test_header.test_low_memory_end == test_lowmem.size());
    assert(test_header.test_memory_size == test_testmem.size());
    assert(test_header.opcode_memory_addr >= test_header.test_memory_addr && test_header.opcode_memory_addr < test_header.test_memory_addr + test_header.test_memory_size);

    std::cout << "Testing " << test_header.inst_name << " from dir " << dir.filename() << "\n";

    memory_handler mem { test_header.test_memory_addr + test_header.test_memory_size };
    auto& ram = mem.ram();
    testram = &ram;
    memcpy(&ram[test_header.test_low_memory_start], &test_lowmem[0], test_lowmem.size());
    memcpy(&ram[test_header.test_memory_addr], &test_testmem[0], test_testmem.size());

    // XXX: For checking exceptions (see validate_test)
    for (int exc = 2; exc < 12; ++exc)
        put_u32(&ram[exc * 4], 0xECC000 | (exc << 4)); 
    for (int exc = 32; exc < 48; ++exc)
        put_u32(&ram[exc * 4], 0xECC000 | (exc << 4)); // Trap vectors

    winuae_test_state cur_state;
    memset(&cur_state, 0, sizeof(cur_state));
    cur_state.sr = 0; //interrupt_mask << 8;
    cur_state.srcaddr = 0xffffffff;
    cur_state.dstaddr = 0xffffffff;
    cur_state.branchtarget = 0xffffffff;
    cur_state.pc = test_header.opcode_memory_addr;
    cur_state.endpc = test_header.opcode_memory_addr;

    for (unsigned test_file_num = 1;; ++test_file_num) {
        char filename[32];
        snprintf(filename, sizeof(filename), "%04u.dat", test_file_num);
        winuae_test_file test_file { dir / filename };
        const auto version = test_file.read_u32();
        const auto starttimeid = test_file.read_u32();
        const auto flags = test_file.read_u32();
        if (version != expected_version) {
            throw std::runtime_error { test_file.path().string() + " has invalid version " + std::to_string(version) };
        }
        if (starttimeid != test_header.starttimeid) {
            throw std::runtime_error { test_file.path().string() + " has invalid timestamp" };
        }
        if (test_file.get_at_u8(test_file.size() - 2) != CT_END_FINISH) {
            throw std::runtime_error { test_file.path().string() + " has invalid footer" };
        }
        test_file.read_u32();
        assert(test_file.pos() == 16);

        //std::cout << "instruction_size = " << (flags & 3) << "\n"; // 0=b 1=w 2=l

        unsigned test_count = 0;
        while (test_file.pos() < test_file.size()-1) {
            for (;;) {
                const uint8_t ct = test_file.read_u8();
                if (ct == CT_END_FINISH)
                    goto done;
                if (ct == CT_END_INIT)
                    break;
                restore(test_file, cur_state, ct);
            }
            
            winuae_test_state check_state = cur_state;

            uint8_t extraccr = 0;
            for (;;) {
                const uint8_t ccrmode = test_file.read_u8();
                const uint8_t maxccr = (ccrmode & 0x3f);
                for (uint8_t ccr = 0; ccr < maxccr; ++ccr) {
                    if (debug_winuae_tests)
                        std::cout << "Testing CCR=$" << hexfmt(ccr) << "\n";
                    assert(test_file.peek_u8() != CT_OVERRIDE_REG);

                    if (test_file.peek_u8() == CT_END_SKIP) {
                        test_file.read_u8();
                        if (debug_winuae_tests)
                            std::cout << "Skipping\n";
                        continue;
                    }

                    cpu_state input_state {};
                    memcpy(input_state.d, &cur_state.regs[0], sizeof(input_state.d));
                    memcpy(input_state.a, &cur_state.regs[8], sizeof(input_state.a));
                    input_state.pc = cur_state.pc;
                    if (maxccr >= 32)
                        input_state.sr = ccr & 0xff;
                    else
                        input_state.sr = ccr & 1 ? 31 : 0;

                    if (extraccr & 1)
                        input_state.sr |= srm_s;
                    input_state.ssp = test_header.super_stack_memory - 0x80;
                    input_state.usp = cur_state.regs[15];

                    m68000 cpu { mem, input_state };

                    try {
                        cpu.step();
                        validate_test(test_file, cpu.state(), check_state);
                    } catch (const std::exception& e) {
                        std::cerr << "Test failed after " << test_count << " tests\n";
                        std::cerr << "\n\nInput state:\n";
                        print_cpu_state(std::cerr, input_state);
                        std::vector<uint16_t> iwords;
                        for (uint32_t addr = cur_state.pc; addr < cur_state.endpc + 4 /*HACK*/; addr += 2) {
                            iwords.push_back(get_u16(&ram[addr]));
                        }

                        disasm(std::cerr, cur_state.pc, &iwords[0], iwords.size());
                        std::cerr << "\n";
                        std::cerr << "\n\nActual state:\n";
                        print_cpu_state(std::cerr, cpu.state());
                        std::cerr << "\n\n" << e.what() << "\n\n";
                        return false;
                    }
                    ++test_count;
                }

                extraccr = test_file.read_u8();
                if (extraccr == CT_END)
                    break;
                if (debug_winuae_tests)
                    std::cout << "extraccr = $" << hexfmt(extraccr) << "\n";
                assert(extraccr == 1);
            }
        }
    done:
        assert(test_file.pos() == test_file.size() - 1);
        if (test_file.get_at_u8(test_file.size() - 1) == CT_END_FINISH)
            break; // Last file
    }

    return true;
}

bool run_winuae_tests()
{
    const fs::path basedir = R"(c:\dev\WinUAE\od-win32\winuae_msvc15\Release\data\68000_Basic\)";

    test_lowmem = read_file((basedir/"lmem.dat").string());
    test_testmem = read_file((basedir / "tmem.dat").string());

    //debug_winuae_tests = true;
    //run_winuae_mnemonic_test(basedir / "TRAP");
    //assert(0);

    const std::vector<const char*> skip = {
        // Not checked
        "RTE",
        // Not implemented
        "RESET", "CHK", "MVPMR", "MVPRM", "RTR", "TAS",
        // TODO (Undefinde flags?)
        "ABCD", "SBCD", "NBCD",
    };
    std::vector<std::string> failed;
    int errors = 0;
    for (auto& p : fs::directory_iterator(basedir)) {
        if (!p.is_directory())
            continue;
        if (auto it = std::find(skip.begin(), skip.end(), p.path().stem().string()); it != skip.end()) {
            std::cout << "SKIPPING " << p.path().stem().string() << "\n";
            continue;
        }
        if (!run_winuae_mnemonic_test(basedir / p)) {
            failed.push_back(p.path().filename().string());
            ++errors;
        }
    }
    if (errors) {
        std::cout << "Failed:";
        for (const auto& f : failed)
            std::cout << ' ' << f;
        std::cout << "\n";
    }

    return errors == 0;
}

bool run_simple_tests()
{
    const uint32_t code_pos = 0x1000;
    const uint32_t code_size = 0x2000;
    memory_handler mem { code_pos + code_size };
    auto& ram = mem.ram();

    const struct {
        std::vector<uint16_t> insts;
        uint32_t in_d[8];
        uint16_t in_sr;
        uint32_t out_d[8];
        uint16_t out_sr;
    } simple_tests[] = {
        { // MULU.W #$0010, D0
            { 0xC0FC, 0x0010 },
            { 0x55aaffc0 }, srm_x | srm_c | srm_n,
            { 0x000ffc00 }, srm_x
        },
        { // MULU.W D1, D0
            { 0xC0C1 },
            { 0x55aa00c9, 0x8888FFFF }, 0,
            { 0x00c8ff37, 0x8888FFFF }, 0
        },
        { // MULU.W D1, D0
            { 0xC0C1 },
            { 0x55aa00c9, 0x89ab0000 }, 0,
            { 0x00000000, 0x89ab0000 }, srm_z
        },
        { // MULU.W #-2, D0
            { 0xC0FC, 0xFFFE },
            { 0xaa55cc01 }, srm_x | srm_c | srm_n,
            { 0xcbff67fe }, srm_x | srm_n
        },
        { // MULS.W D1, D0
            { 0xC1C1 },
            { 0x55aa00c9, 0x8888FFFF }, 0,
            { 0xFFFFFF37, 0x8888FFFF }, srm_n
        },
        { // MULS.W D1, D0
            { 0xC1C1 },
            { 0x55aa00c9, 0x88880000 }, 0,
            { 0x00000000, 0x88880000 }, srm_z
        },
        { // MULS.W #-2, D0
            { 0xC1FC, 0xFFFE },
            { 0xaa55cc01 }, srm_x | srm_c | srm_n,
            { 0x000067fe }, srm_x
        },
        { // ROXL.W D1, D0  
            { 0xE370 },
            { 0xABCDABCD, 0 }, 0,
            { 0xABCDABCD, 0 }, srm_n,
        },
        { // ROXL.W D1, D0  
            { 0xE370 },
            { 0xABCDABCD, 32 }, srm_x,
            { 0xABCDEAF3, 32 }, srm_n,
        },
        { // ROXL.W D1, D0  
            { 0xE370 },
            { 0xABCDABCD, 3 }, 0,
            { 0xABCD5E6A, 3 }, srm_x | srm_c,
        },
        { // ROXL.W D1, D0  
            { 0xE370 },
            { 0xABCDABCD, 2 }, srm_x,
            { 0xABCDAF37, 2 }, srm_n,
        },
        { // ROXL.W #$7, D0
            { 0xEF50 },
            { 0xFFFF0000, 2 }, srm_x,
            { 0xFFFF0040, 2 }, 0,
        },
        { // ROXL.W #$7, D0
            { 0xEF50 },
            { 0xFFFF0000, 2 }, srm_c,
            { 0xFFFF0000, 2 }, srm_z,
        },
        { // ROXR.W D1, D0  
            { 0xE270 },
            { 0xABCDABCD, 0 }, 0,
            { 0xABCDABCD, 0 }, srm_n,
        },
        { // ROXR.W D1, D0  
            { 0xE270 },
            { 0xABCDABCD, 32 }, srm_x,
            { 0xABCDAF37, 32 }, srm_n,
        },
        { // ROXR.W D1, D0  
            { 0xE270 },
            { 0xABCDABCD, 3 }, 0,
            { 0xABCD5579, 3 }, srm_x | srm_c,
        },
        { // ROXR.W D1, D0  
            { 0xE270 },
            { 0xABCDABCD, 2 }, srm_x,
            { 0xABCDEAF3, 2 }, srm_n,
        },
        { // ROXR.W #$7, D0
            { 0xEE50 },
            { 0xFFFF0000, 2 }, srm_x,
            { 0xFFFF0200, 2 }, 0,
        },
        { // ROXR.W #$7, D0
            { 0xEE50 },
            { 0xFFFF0000, 2 }, srm_c,
            { 0xFFFF0000, 2 }, srm_z,
        },
        { // MOVE.B #7, CCR
            { 0x44FC, 0x0007 },
            {}, 0,
            {}, srm_c | srm_v | srm_z,
        },
        { // NOP
            { 0x4E71 },
            {}, 0,
            {}, 0,
        },
    };

    auto make_state = [](const uint32_t d[8], uint16_t ccr, uint32_t pc, uint64_t icount) {
        cpu_state st {};
        memcpy(st.d, d, sizeof(st.d));
        st.sr = ccr;
        st.pc = pc;
        st.instruction_count = icount;
        return st;
    };

    for (const auto& t : simple_tests) {
        memset(&ram[0], 0, ram.size());
        for (unsigned i = 0; i < std::size(t.insts); ++i)
            put_u16(&ram[code_pos + i * 2], t.insts[i]);

        const auto input_state = make_state(t.in_d, t.in_sr, code_pos, 0);
        const auto expected_state = make_state(t.out_d, t.out_sr, code_pos + static_cast<uint32_t>(2*t.insts.size()), 1);
        m68000 cpu { mem, input_state };
        const auto& outst = cpu.state();
        try {
            cpu.step();
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            goto report_error;
        }

        if (memcmp(&outst, &expected_state, sizeof(cpu_state))) {
        report_error:
            std::cerr << "Test failed for";
            for (const auto iw : t.insts) {
                if (!iw)
                    break;
                std::cerr << " " << hexfmt(iw);
            }
            std::cerr << "\n";
            std::cerr << "\n\nInput state:\n";
            print_cpu_state(std::cerr, input_state);
            disasm(std::cerr, code_pos, &t.insts[0], std::size(t.insts));
            std::cerr << "\n";
            std::cerr << "\n\nExpected state:\n";
            print_cpu_state(std::cerr, expected_state);
            std::cerr << "\n\nActual state:\n";
            print_cpu_state(std::cerr, outst);
            throw std::runtime_error { "Test failed" };
        }
    }
    return true;
}

int main()
{
    try {
        if (!run_simple_tests())
            return 1;

        if (!run_winuae_tests())
            return 1;

        if (!run_tests())
            return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
