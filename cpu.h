#ifndef CPU_H
#define CPU_H

#include <memory>
#include <cassert>
#include <stdint.h>
#include <ostream>

enum sr_bit_index {
    // User byte
    sri_c     = 0, // Carry
    sri_v     = 1, // Overflow
    sri_z     = 2, // Zero
    sri_n     = 3, // Negative
    sri_x     = 4, // Extend
    // System byte
    sri_ipl   = 8,  // 3 bits Interrupt priority mask
    sri_m     = 12, // Master/interrupt state (not supported on 68000)
    sri_s     = 13, // Supervisor/user state
    sri_trace = 14, // 2 bits Trace (0b00 = No trace, 0b10 = Trace on any instruction, 0b01 = Trace on change of flow, 0b11 = Undefined) (only one trace level supported on 680000)
};

enum sr_mask : uint16_t {
    srm_c     = 1 << sri_c,
    srm_v     = 1 << sri_v,
    srm_z     = 1 << sri_z,
    srm_n     = 1 << sri_n,
    srm_x     = 1 << sri_x,
    srm_ipl   = 7 << sri_ipl,
    srm_m     = 1 << sri_m,
    srm_s     = 1 << sri_s,
    srm_trace = 3 << sri_trace,

    srm_illegal  = 1 << 5 | 1 << 6 | 1 << 7 | 1 << 11,
    srm_ccr_no_x = srm_c | srm_v | srm_z | srm_n,
    srm_ccr      = srm_ccr_no_x | srm_x,
};

enum class conditional : uint8_t {
    t  = 0b0000, // True                1
    f  = 0b0001, // False               0
    hi = 0b0010, // High                (not C) and (not Z)
    ls = 0b0011, // Low or Same         C or V
    cc = 0b0100, // Carray Clear (HI)   not C
    cs = 0b0101, // Carry Set (LO)      C
    ne = 0b0110, // Not Equal           not Z
    eq = 0b0111, // Equal               Z
    vc = 0b1000, // Overflow Clear      not V
    vs = 0b1001, // Overflow Set        V
    pl = 0b1010, // Plus                not N
    mi = 0b1011, // Minus               N
    ge = 0b1100, // Greater or Equal    (N and V) or ((not N) and (not V))
    lt = 0b1101, // Less Than           (N and (not V)) or ((not N) and V))
    gt = 0b1110, // Greater Than        (N and V and (not Z)) or ((not N) and (not V) and (not Z))
    le = 0b1111, // Less or Equal       Z or (N and (not V)) or ((not N) and V)
};


enum class interrupt_vector : uint8_t {
    reset_ssp = 0,
    reset_pc = 1,
    bus_error = 2,
    address_error = 3,
    illegal_instruction = 4,
    zero_divide = 5,
    chk_exception = 6,
    trapv_instruction = 7,
    privililege_violation = 8,
    trace = 9,
    line_1010 = 10,
    line_1111 = 11,
    level1 = 25,
    level2 = 26,
    level3 = 27,
    level4 = 28,
    level5 = 29,
    level6 = 30,
    level7 = 31,
};

struct cpu_state {
    uint32_t d[8];
    uint32_t a[7];
    uint32_t ssp;
    uint32_t usp;
    uint32_t pc;
    uint16_t sr;

    uint32_t& A(unsigned idx)
    {
        assert(idx < 8);
        return idx < 7 ? a[idx] : (sr & srm_s ? ssp : usp);
    }

    uint32_t A(unsigned idx) const
    {
        return const_cast<cpu_state&>(*this).A(idx);
    }

    void update_sr(sr_mask mask, uint16_t val)
    {
        assert((mask & srm_illegal) == 0);
        assert((val & ~mask) == 0);
        sr = (sr & ~mask) | val;
    }

    bool eval_cond(conditional c) const
    {
        const bool C = !!(sr & srm_c);
        const bool V = !!(sr & srm_v);
        const bool Z = !!(sr & srm_z);
        const bool N = !!(sr & srm_n);

        switch (c) {
        case conditional::t:
            return true;
        case conditional::f:
            return false;
        case conditional::hi: // (not C) and (not Z)
            return !C && !Z;
        case conditional::ls: // C or Z
            return C || Z;
        case conditional::cc: // not C
            return !C;
        case conditional::cs: // C
            return C;
        case conditional::ne: // not Z
            return !Z;
        case conditional::eq: // Z
            return Z;
        case conditional::vc: // not V
            return !V;
        case conditional::vs: // V
            return V;
        case conditional::pl: // not N
            return !N;
        case conditional::mi: // N
            return N;
        case conditional::ge: // (N and V) or ((not N) and (not V))
            return (N && V) || (!N && !V);
        case conditional::lt: // (N and (not V)) or ((not N) and V))
            return (N && !V) || (!N && V);
        case conditional::gt: // (N and V and (not Z)) or ((not N) and (not V) and (not Z))
            return (N && V && !Z) || (!N && !V && !Z);
        case conditional::le: // Z or (N and (not V)) or ((not N) and V)
            return Z || (N && !V) || (!N && V);
        default:
            assert(!"Condition not implemented");
        }
        return false;
    }
};

void print_cpu_state(std::ostream& os, const cpu_state& s);

class memory_handler;

class m68000 {
public:
    explicit m68000(memory_handler& mem, const cpu_state& state);
    explicit m68000(memory_handler& mem);
    ~m68000();

    const cpu_state& state() const;
    uint64_t instruction_count() const;
    void trace(std::ostream* os);
    void show_state(std::ostream& os);

    void step(uint8_t current_ipl = 0);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
