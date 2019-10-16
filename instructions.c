// Returns the bits of x between (including) position l and h.
// Example: BITS(0b1100101, 2, 5) == 1001
#define BITS(x, l, h) ((x & ((1 << ((h + 1) % (sizeof (x) * 8))) - 1)) >> l)

enum syscall {
    SYS_READ = 63,
    SYS_WRITE = 63,
    SYS_EXIT = 93,
};

static void
instr_rv64_add(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
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
instr_rv64_mul(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    i = 0b0110011 | rd << 7 | rs1 << 15 | rs2 << 20 | 1 << 25;
    add_data(seg, &i, 4);
}

static void
add_instr_li(Segment* seg, enum reg rd, int16_t n) {
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
add_instr_lui(Segment* seg, enum reg rd, int32_t addr) {
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
add_instr_lw(Segment* seg, enum reg rd, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000000011 | (rd << 7) | (rs1 << 15)
        | (BITS(addr, 0, 11) << 20);
    add_data(seg, &i, 4);
}

static void
add_instr_sw(Segment* seg, enum reg rs2, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000100011 | (BITS(addr, 0, 4) << 7) | (rs1 << 15)
        | (rs2 << 20) | (BITS(addr, 5, 11) << 25);
    add_data(seg, &i, 4);
}

static void
add_instr_jump(Segment* seg, int16_t off) {
    uint16_t n;
    n = 0b1010000000000001
        | ((off >> 5 & 1) << 2)
        | ((off >> 1 & 7) << 3)
        | ((off >> 7 & 1) << 6)
        | ((off >> 6 & 1) << 7)
        | ((off >> 10 & 1) << 8)
        | ((off >> 8 & 3) << 9)
        | ((off >> 4 & 1) << 11)
        | ((off >> 11 & 1) << 12);
    add_data(seg, &n, 2);
}

static void
add_instr_ecall(Segment* seg) {
    uint32_t i;
    i = 0b1110011;
    add_data(seg, &i, 4);
}

static void
add_instr_set_mem(Segment* seg, Location loc, const Result* res) {
    if (res->type == NUMBER) {
        add_instr_li(seg, REG_T0, res->number);
    } else if (res->type == REGISTER && res->reg != REG_T0) {
        instr_rv64_add(seg, REG_T0, REG_ZERO, res->reg);
    }
    size_t addr = loc.seg->addr + loc.offset;
    add_instr_lui(seg, REG_T1, addr);
    add_instr_sw(seg, REG_T0, addr, REG_T1);
}

static void
add_instr_load(Segment* seg, enum reg dest, const Binding* src) {
    size_t addr = src->loc.seg->addr + src->loc.offset;
    add_instr_lui(seg, REG_T1, addr);
    add_instr_lw(seg, dest, addr, REG_T1);
}

static void
add_instr_copy(Segment* seg, enum reg dest, const Result* src) {
    switch (src->type) {
    case LABEL: {
        Binding* b = get_binding(src->label);
        add_instr_load(seg, dest, b);
    } break;
    case REGISTER:
        instr_rv64_add(seg, dest, REG_ZERO, src->reg);
        break;
    case NUMBER:
        add_instr_li(seg, dest, src->number);
        break;
    }
}

static enum reg
into_reg(Segment* seg, const Result* r) {
    enum reg dest;
    switch (r->type) {
    case LABEL: {
        Binding* b = get_binding(r->label);
        dest = alloc_reg();
        add_instr_load(seg, dest, b);
    } break;
    case REGISTER:
        dest = r->reg;
        break;
    case NUMBER:
        dest = alloc_reg();
        add_instr_li(seg, dest, r->number);
        break;
    }
    return dest;
}

static void
add_instr_exit(Segment* seg, const Result* r) {
    enum reg reg = into_reg(seg, r);
    instr_rv64_add(seg, REG_A0, REG_ZERO, reg);
    add_instr_li(seg, REG_A7, SYS_EXIT);
    add_instr_ecall(seg);
}

static enum reg
add_instr_op(Segment* seg, enum oper o, const Result* l, const Result* r) {
    enum reg reg_l, reg_r;
    if (l->type == REGISTER) {
        reg_l = l->reg;
    } else {
        reg_l = alloc_reg();
        add_instr_copy(seg, reg_l, l);
    }
    if (r->type == REGISTER) {
        reg_r = r->reg;
    } else {
        reg_r = alloc_reg();
        add_instr_copy(seg, reg_r, r);
    }
    switch (o) {
    case OP_PLUS:
        instr_rv64_add(seg, reg_l, reg_l, reg_r);
        break;
    case OP_TIMES:
        instr_rv64_mul(seg, reg_l, reg_l, reg_r);
        break;
    }
    free_reg(reg_r);
    return reg_l;
}

static void
add_instr_add(Segment* seg, const Result* l, const Result* r) {
    if (l->type == LABEL) {
        Binding* b = get_binding(l->label);
        size_t addr = b->loc.seg->addr + b->loc.offset;
        add_instr_lui(seg, REG_T3, addr);
        add_instr_lw(seg, REG_T0, addr, REG_T3);
    } else if (l->type == NUMBER) {
        add_instr_li(seg, REG_T0, l->number);
    }
    if (r->type == LABEL) {
        Binding* b = get_binding(r->label);
        size_t addr = b->loc.seg->addr + b->loc.offset;
        add_instr_lui(seg, REG_T4, addr);
        add_instr_lw(seg, REG_T2, addr, REG_T4);
    } else if (r->type == NUMBER) {
        add_instr_li(seg, REG_T2, r->number);
    }
    instr_rv64_add(seg, REG_T0, REG_T0, REG_T2);
}
