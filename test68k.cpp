#include <iostream>
#include "ioutil.h"
#include "cpu.h"
#include "memory.h"
#include "disasm.h"

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
                    put_u16(&ram[static_cast<size_t>(code_pos) + i * 2], inst_words[i]);

                m68000 cpu { mem, input_state };
                cpu.step();

                const auto& outst = cpu.state();
                if (memcmp(&outst, &expected_state, sizeof(cpu_state))) {
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
            "gen-opcode-addb.bin",
            "gen-opcode-addl.bin",
            "gen-opcode-addw.bin",
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
            "gen-opcode-negb.bin",
            "gen-opcode-negl.bin",
            "gen-opcode-negw.bin",
            "gen-opcode-notb.bin",
            "gen-opcode-notl.bin",
            "gen-opcode-notw.bin",
            "gen-opcode-orb.bin",
            "gen-opcode-orl.bin",
            "gen-opcode-orw.bin",
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
            "gen-opcode-svc.bin",
            "gen-opcode-svs.bin",
#if 0
            // Not implemented
            "gen-opcode-abcd.bin",
            "gen-opcode-addxb.bin",
            "gen-opcode-addxl.bin",
            "gen-opcode-addxw.bin",
            "gen-opcode-divs.bin",
            "gen-opcode-divu.bin",
            "gen-opcode-nbcd.bin",
            "gen-opcode-negxb.bin",
            "gen-opcode-negxl.bin",
            "gen-opcode-negxw.bin",
            "gen-opcode-sbcd.bin",
            "gen-opcode-subxb.bin",
            "gen-opcode-subxl.bin",
            "gen-opcode-subxw.bin",
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

int main()
{
    try {
        if (!run_tests())
            return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
