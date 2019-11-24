enum Rv64Type {
    RV64_I,
    RV64_R,
};

typedef void (*Rv64FnR)(Segment*, enum reg, enum reg, enum reg);

struct Rv64Instr {
    Rv64Type type;
    union {
        struct {
            Rv64FnR fn;
            int rd;
            int rs1;
            int rs2;
        } r;
    };
};
typedef struct Rv64Instr Rv64Instr;

enum syscall {
    SYS_READ = 63,
    SYS_WRITE = 63,
    SYS_EXIT = 93,
};

static void
rv64_add_set_mem(Segment* seg, Location loc, const Result* res) {
    if (res->type == NUMBER) {
        rv64_add_li(seg, REG_T0, res->number);
    } else if (res->type == REGISTER && res->reg != REG_T0) {
        instr_rv64_add(seg, REG_T0, REG_ZERO, res->reg);
    }
    size_t addr = loc.seg->addr + loc.offset;
    rv64_add_lui(seg, REG_T1, addr);
    rv64_add_sw(seg, REG_T0, addr, REG_T1);
}

static void
rv64_add_load(Segment* seg, Var dest, const Binding* src) {
    size_t addr = src->loc.seg->addr + src->loc.offset;
    rv64_add_lui(seg, REG_T1, addr);
    rv64_add_lw(seg, dest, addr, REG_T1);
}

static void
rv64_add_copy(Segment* seg, enum reg dest, const Result* src) {
    switch (src->type) {
    case LABEL: {
        Binding* b = get_binding(src->label);
        rv64_add_load(seg, dest, b);
    } break;
    case REGISTER:
        instr_rv64_add(seg, dest, REG_ZERO, src->reg);
        break;
    case NUMBER:
        rv64_add_li(seg, dest, src->number);
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
        rv64_add_load(seg, dest, b);
    } break;
    case REGISTER:
        dest = r->reg;
        break;
    case NUMBER:
        dest = alloc_reg();
        rv64_add_li(seg, dest, r->number);
        break;
    }
    return dest;
}

static enum reg
into_this_reg(Segment* seg, const Result* r, enum reg into) {
    enum reg dest;
    switch (r->type) {
    case LABEL: {
        Binding* b = get_binding(r->label);
        dest = alloc_this_reg(into);
        rv64_add_load(seg, dest, b);
    } break;
    case REGISTER:
        dest = alloc_this_reg(into);
        if (r->reg != into) {
            rv64_add_mv(seg, into, r->reg);
        }
        break;
    case NUMBER:
        dest = alloc_this_reg(into);
        rv64_add_li(seg, dest, r->number);
        break;
    }
    return dest;
}

static void
rv64_add_exit(Segment* seg, const Result* r) {
    Reg a0 = into_this_reg(seg, r);
    reg_should_be(a0, REG_A0);
    Reg a7 = alloc_reg();
    reg_should_be(a7, REG_A7);
    rv64_add_li(seg, a7, SYS_EXIT);
    rv64_add_ecall(seg);
    reg_last_use(seg, a0);
    reg_last_use(seg, a7);
}

static enum reg
rv64_add_op(Segment* seg, enum oper o, const Result* l, const Result* r) {
    enum reg reg_l, reg_r;
    if (l->type == REGISTER) {
        reg_l = l->reg;
    } else {
        reg_l = alloc_reg();
        rv64_add_copy(seg, reg_l, l);
    }
    if (r->type == REGISTER) {
        reg_r = r->reg;
    } else {
        reg_r = alloc_reg();
        rv64_add_copy(seg, reg_r, r);
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
rv64_add_add(Segment* seg, const Result* l, const Result* r) {
    if (l->type == LABEL) {
        Binding* b = get_binding(l->label);
        size_t addr = b->loc.seg->addr + b->loc.offset;
        rv64_add_lui(seg, REG_T3, addr);
        rv64_add_lw(seg, REG_T0, addr, REG_T3);
    } else if (l->type == NUMBER) {
        rv64_add_li(seg, REG_T0, l->number);
    }
    if (r->type == LABEL) {
        Binding* b = get_binding(r->label);
        size_t addr = b->loc.seg->addr + b->loc.offset;
        rv64_add_lui(seg, REG_T4, addr);
        rv64_add_lw(seg, REG_T2, addr, REG_T4);
    } else if (r->type == NUMBER) {
        rv64_add_li(seg, REG_T2, r->number);
    }
    instr_rv64_add(seg, REG_T0, REG_T0, REG_T2);
}
