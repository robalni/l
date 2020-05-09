enum Rv64Type {
    RV64_I,
    RV64_R,
    RV64_RI64,
    RV64_NONE,

    // Not a real instruction
    FN_START,
};

typedef void (*Rv64FnR)(Segment*, enum reg, enum reg, enum reg);
typedef void (*Rv64FnRi64)(Segment*, enum reg, uint64_t);
typedef void (*Rv64FnNone)(Segment*);

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
            Rv64FnRi64 fn;
            Vreg* rd;
            uint64_t imm;
        } ri64;
        struct {
            Binding* binding;
        } fn_start;
    };
};
typedef struct Rv64Instr Rv64Instr;

#define MAX_VINSTRS 1000
static Rv64Instr vinstrs[MAX_VINSTRS];
static size_t n_vinstrs;

static void
rv64_add(Segment* seg, Rv64Instr instr) {
    if (n_vinstrs >= MAX_VINSTRS) {
        abort();
    }
    vinstrs[n_vinstrs] = instr;
    n_vinstrs++;
}

enum syscall {
    SYS_READ = 63,
    SYS_WRITE = 63,
    SYS_EXIT = 93,
};

// rd must be a register.
static void
rv64_add_load(Segment* seg, Vreg* rd, Binding* b) {
    assert(rd->state == VREG_USED || rd->state == VREG_EXACT);
    abort();
    // TODO: Load different sizes.
    // TODO: Add the instruction.
    //Rv64Instr instr = {
    //    .type = RV64_R,
    //    .r = {
    //        .fn = ,
    //        .rd = rd,
    //        .rs1 = l,
    //        .rs2 = r,
    //    },
    //};
    //rv64_add(seg, instr);
}

static void
rv64_add_li(Segment* seg, Vreg* rd, const Ast* val) {
    assert(val->type == AST_NUM);
    uint64_t imm = val->num.u;
    Rv64Instr instr = {
        .type = RV64_RI64,
        .ri64 = {
            .fn = rv64_write_li,
            .rd = rd,
            .imm = imm,
        },
    };
    rv64_add(seg, instr);
}

static void
rv64_add_li_static(Segment* seg, Vreg* rd, uint64_t val) {
    Rv64Instr instr = {
        .type = RV64_RI64,
        .ri64 = {
            .fn = rv64_write_li,
            .rd = rd,
            .imm = val,
        },
    };
    rv64_add(seg, instr);
}

static void
rv64_add_mv(Segment* seg, Vreg* rd, Vreg* r) {
    abort();
}

static Vreg*
into_reg(Segment* seg, Vreg* r) {
    Vreg* dest;
    switch (r->state) {
    case VREG_USED:
        dest = r;
        break;
    case VREG_MEM:
        dest = alloc_vreg();
        rv64_add_load(seg, dest, r->binding);
        break;
    case VREG_EXACT:
        dest = r;
        break;
    case VREG_STATIC:
        dest = alloc_vreg();
        rv64_add_li(seg, dest, r->val);
        break;
    }
    return dest;
}

static Vreg*
into_this_reg(Segment* seg, Vreg* r, enum reg into) {
    Vreg* dest;
    switch (r->state) {
    case VREG_USED:
        vreg_set_state_exact(r, into);
        break;
    case VREG_MEM:
        dest = alloc_this_reg(into);
        rv64_add_load(seg, dest, r->binding);
        break;
    case VREG_EXACT:
        dest = alloc_this_reg(into);
        if (r->reg != into) {
            rv64_add_mv(seg, dest, r);
        }
        break;
    case VREG_STATIC:
        dest = alloc_this_reg(into);
        rv64_add_li(seg, dest, r->val);
        break;
    }
    return dest;
}

static void
rv64_add_ecall(Segment* seg) {
    Rv64Instr instr = {
        .type = RV64_NONE,
        .none = {
            .fn = rv64_write_ecall,
        },
    };
    rv64_add(seg, instr);
}

static void
rv64_add_exit(Segment* seg, Vreg* r) {
    Vreg* a0 = into_this_reg(seg, r, REG_A0);
    Vreg* a7 = alloc_this_reg(REG_A7);
    rv64_add_li_static(seg, a7, SYS_EXIT);
    rv64_add_ecall(seg);
}

static Vreg*
rv64_add_add(Segment* seg, Vreg* l, Vreg* r) {
    l = into_reg(seg, l);
    r = into_reg(seg, r);
    Vreg* rd = alloc_vreg();
    Rv64Instr instr = {
        .type = RV64_R,
        .r = {
            .fn = rv64_write_add,
            .rd = rd,
            .rs1 = l,
            .rs2 = r,
        },
    };
    rv64_add(seg, instr);
    return rd;
}

static void
add_function_start(Segment* seg, Binding* binding) {
    Rv64Instr instr = {
        .type = FN_START,
        .fn_start = binding,
    };
    rv64_add(seg, instr);
}
