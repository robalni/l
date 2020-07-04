typedef void (*Rv64FnR)(Segment*, enum reg, enum reg, enum reg);
typedef void (*Rv64FnI)(Segment*, enum reg, enum reg, int64_t);
typedef void (*Rv64FnRi64)(Segment*, enum reg, uint64_t);
typedef void (*Rv64FnB)(Segment*, enum reg, enum reg, uint64_t);
typedef void (*Rv64FnJ)(Segment*, int16_t);
typedef void (*Rv64FnNone)(Segment*);

enum Rv64Type {
    RV64_I,
    RV64_R,
    RV64_RI64,
    RV64_B,
    RV64_J,
    RV64_NONE,

    // Not real instructions
    FN_START,
    ASSIGN,
    PATCH,
    PATCH_BINDING,
};

struct Rv64Instr {
    enum Rv64Type type;
    union {
        struct {
            Rv64FnNone fn;
        } none;
        struct {
            Rv64FnR fn;
            Vreg* rd;
            Vreg* rs1;
            Vreg* rs2;
        } r;
        struct {
            Rv64FnI fn;
            Vreg* rd;
            Vreg* rs1;
            int64_t imm;
        } i;
        struct {
            Rv64FnRi64 fn;
            Vreg* rd;
            uint64_t imm;
        } ri64;
        struct {
            Rv64FnB fn;
            Vreg* rs1;
            Vreg* rs2;
            uint64_t imm;
        } b;
        struct {
            Rv64FnJ fn;
            int16_t imm;
        } j;
        struct {
            Binding* binding;
        } fn_start;
        struct {
            Vreg* dest;
            Vreg* val;
        } assign;
        struct {
            struct Rv64Instr* instr;
            struct Rv64Instr* target;
        } patch;
        struct {
            struct Rv64Instr* instr;
            Binding* binding;
        } patch_binding;
    };
    size_t offset;
};
typedef struct Rv64Instr Rv64Instr;

// Returns the bits of x between (including) position l and h.
// Example: BITS(0b1100101, 2, 5) == 1001
#define BITS(x, l, h) ((x & ((1 << ((h + 1) % (sizeof (x) * 8))) - 1)) >> l)

static void
rv64_patch(Segment* seg, Rv64Instr* instr, size_t imm) {
    imm = imm - instr->offset;
    switch (instr->type) {
    case RV64_B:
        *(uint32_t*)(seg->data + instr->offset) |= 0
            | (BITS(imm, 11, 11) << 7)
            | (BITS(imm, 1, 4) << 8)
            | (BITS(imm, 5, 10) << 25)
            | (BITS(imm, 12, 12) << 31);
        break;
    case RV64_J:
        *(uint32_t*)(seg->data + instr->offset) |= 0
            | (BITS(imm, 12, 19) << 12)
            | (BITS(imm, 11, 11) << 20)
            | (BITS(imm, 1, 10) << 21)
            | (BITS(imm, 20, 20) << 31);
        break;
    }
}

static void
rv64_write_add(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    if (rs2 == rd) {
        rs2 = rs1;
        rs1 = rd;
    }
    if (rs1 == REG_ZERO) {
        i = 0b1000000000000010 | (rs2 << 2) | (rd << 7);
        add_data(seg, &i, 2);
    } else if (rs1 == rd) {
        i = 0b1001000000000010 | (rs2 << 2) | (rd << 7);
        add_data(seg, &i, 2);
    } else {
        i = 0b0110011 | (rd << 7) | (rs1 << 15) | (rs2 << 20);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_sub(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    if (rs1 == rd) {
        i = 0b1000110000000001 | (rs2 << 2) | (rd << 7);
        add_data(seg, &i, 2);
    } else {
        i = (1 << 30) | 0b0110011 | (rd << 7) | (rs1 << 15) | (rs2 << 20);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_mul(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    i = 0b0110011 | rd << 7 | rs1 << 15 | rs2 << 20 | 1 << 25;
    add_data(seg, &i, 4);
}

static void
rv64_write_li(Segment* seg, enum reg rd, uint64_t n) {
    uint32_t i;
    if ((n & 0b111111) == n) {
        i = 0b0100000000000001 | (rd << 7) | (BITS(n, 0, 4) << 2)
            | (BITS(n, 5, 5) << 12);
        add_data(seg, &i, 2);
    } else {
        i = 0b0010011 | (rd << 7) | (BITS(n, 0, 11) << 20);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_lui(Segment* seg, enum reg rd, int32_t addr) {
    uint32_t i;
    if (addr < (1 << 18)) {
        i = 0b0110000000000001 | (rd << 7) | (BITS(addr, 12, 16) << 2)
            | (BITS(addr, 17, 17) << 12);
        add_data(seg, &i, 2);
    } else {
        i = 0b0110111 | (rd << 7) | (BITS(addr, 12, 31) << 12);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_lw(Segment* seg, enum reg rd, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000000011 | (rd << 7) | (rs1 << 15)
        | (BITS(addr, 0, 11) << 20);
    add_data(seg, &i, 4);
}

static void
rv64_write_sw(Segment* seg, enum reg rs2, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000100011 | (BITS(addr, 0, 4) << 7) | (rs1 << 15)
        | (rs2 << 20) | (BITS(addr, 5, 11) << 25);
    add_data(seg, &i, 4);
}

static void
rv64_write_jump_unknown(Segment* seg, int16_t off) {
    assert(off == 0);
    uint32_t n;
    n = 0b1101111
        | (BITS(off, 12, 19) << 12)
        | (BITS(off, 11, 11) << 20)
        | (BITS(off, 1, 10) << 21)
        | (BITS(off, 20, 20) << 31);
    add_data(seg, &n, 4);
}

static void
rv64_write_call_unknown(Segment* seg, int16_t off) {
    assert(off == 0);
    int rd = REG_RA;
    uint32_t n;
    n = 0b1101111
        | (rd << 7)
        | (BITS(off, 12, 19) << 12)
        | (BITS(off, 11, 11) << 20)
        | (BITS(off, 1, 10) << 21)
        | (BITS(off, 20, 20) << 31);
    add_data(seg, &n, 4);
}

static void
rv64_write_jalr(Segment* seg, enum reg rd, enum reg rs1, int16_t off) {
    uint32_t n;
    n = 0b1100111
        | (rd << 7)
        | (rs1 << 15)
        | (BITS(off, 0, 11) << 20);
    add_data(seg, &n, 4);
}

static void
rv64_write_beqz_unknown(Segment* seg, enum reg rs1, enum reg rs2, uint64_t imm) {
    assert(imm == 0);
    assert(rs2 == 0);

    uint32_t n;
    n = 0b1100011
        | (BITS(imm, 11, 11) << 7)
        | (BITS(imm, 1, 4) << 8)
        | (rs1 << 15)
        | (BITS(imm, 5, 10) << 25)
        | (BITS(imm, 12, 12) << 31);
    add_data(seg, &n, 4);
}

static void
rv64_write_ecall(Segment* seg) {
    uint32_t i;
    i = 0b1110011;
    add_data(seg, &i, 4);
}
