enum reg {
    REG_ZERO = 0,

    REG_T0 = 5,
    REG_T1 = 6,
    REG_T2 = 7,

    REG_A0 = 10,
    REG_A1 = 11,
    REG_A2 = 12,
    REG_A3 = 13,
    REG_A4 = 14,
    REG_A5 = 15,
    REG_A6 = 16,
    REG_A7 = 17,

    REG_T3 = 28,
    REG_T4 = 29,
    REG_T5 = 30,
    REG_T6 = 31,
};

struct reg_alloc_info {
    enum reg reg;
    bool used;
};

struct reg_alloc_info regs[] = {
    {REG_T0, false},
    {REG_T1, false},
    {REG_T2, false},
    {REG_T3, false},
    {REG_T4, false},
    {REG_T5, false},
    {REG_T6, false},
};

static const char*
reg_name(enum reg r) {
    static const char* names[] = {
        "zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
        "fp",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
        "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
        "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6",
    };
    assert(r < ARR_LEN(names));
    assert(r >= 0);
    return names[r];
}

static enum reg
alloc_reg() {
    for (size_t i = 0; i < ARR_LEN(regs); i++) {
        if (!regs[i].used) {
            regs[i].used = true;
            return regs[i].reg;
        }
    }
    assert(false);
}

static void
free_reg(enum reg reg) {
    for (size_t i = 0; i < ARR_LEN(regs); i++) {
        if (regs[i].reg == reg) {
            regs[i].used = false;
        }
    }
}
