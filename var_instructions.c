#define MAX_VINSTRS 1000
static Rv64Instr vinstrs[MAX_VINSTRS];
static size_t n_vinstrs;

#define MAX_POSTINSTRS 1000
static Rv64Instr postinstrs[MAX_POSTINSTRS];
static size_t n_postinstrs;

static Rv64Instr*
rv64_add(Segment* seg, Rv64Instr instr) {
    if (n_vinstrs >= MAX_VINSTRS) {
        abort();
    }
    vinstrs[n_vinstrs] = instr;
    n_vinstrs++;
    return &vinstrs[n_vinstrs - 1];
}

static Rv64Instr*
rv64_add_end(Segment* seg, Rv64Instr instr) {
    if (n_postinstrs >= MAX_POSTINSTRS) {
        abort();
    }
    postinstrs[n_postinstrs] = instr;
    n_postinstrs++;
    return &postinstrs[n_postinstrs - 1];
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
    case VREG_MEM_ADDR:
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
    case VREG_UNUSED:
        abort();
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
    case VREG_MEM_ADDR:
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
    case VREG_UNUSED:
        abort();
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
rv64_add_add(Segment* seg, Vreg* rd, Vreg* l, Vreg* r) {
    l = into_reg(seg, l);
    r = into_reg(seg, r);
    rd = into_reg(seg, rd);
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

static Vreg*
rv64_add_sub(Segment* seg, Vreg* rd, Vreg* l, Vreg* r) {
    l = into_reg(seg, l);
    r = into_reg(seg, r);
    rd = into_reg(seg, rd);
    Rv64Instr instr = {
        .type = RV64_R,
        .r = {
            .fn = rv64_write_sub,
            .rd = rd,
            .rs1 = l,
            .rs2 = r,
        },
    };
    rv64_add(seg, instr);
    return rd;
}

static Rv64Instr*
rv64_add_beqz(Segment* seg, Vreg* cond) {
    cond = into_reg(seg, cond);
    Rv64Instr instr = {
        .type = RV64_B,
        .b = {
            .fn = rv64_write_beqz_unknown,
            .rs1 = cond,
            .rs2 = get_vreg_zero(),
            .imm = 0,
        },
    };
    return rv64_add(seg, instr);
}

static Rv64Instr*
rv64_add_jump(Segment* seg) {
    Rv64Instr instr = {
        .type = RV64_J,
        .j = {
            .fn = rv64_write_jump_unknown,
            .imm = 0,
        },
    };
    return rv64_add(seg, instr);
}

static Rv64Instr*
rv64_add_call(Segment* seg, Binding* b) {
    Rv64Instr instr = {
        .type = RV64_J,
        .j = {
            .fn = rv64_write_call_unknown,
        },
    };
    return rv64_add(seg, instr);
}

static Rv64Instr*
rv64_add_ret(Segment* seg) {
    Rv64Instr instr = {
        .type = RV64_I,
        .i = {
            .fn = rv64_write_jalr,
            .rd = get_vreg_zero(),
            .rs1 = alloc_this_reg(REG_RA),
            .imm = 0,
        },
    };
    return rv64_add(seg, instr);
}

static void
add_function_start(Segment* seg, Binding* binding) {
    Rv64Instr instr = {
        .type = FN_START,
        .fn_start = binding,
    };
    rv64_add(seg, instr);
}

static void
rv64_add_patch_addr(Segment* seg, Rv64Instr* other_instr, Rv64Instr* target_instr) {
    Rv64Instr instr = {
        .type = PATCH,
        .patch = {
            .instr = other_instr,
            .target = target_instr,
        },
    };
    rv64_add(seg, instr);
}

static void
rv64_add_patch_addr_binding(Segment* seg, Rv64Instr* i, Binding* b) {
    Rv64Instr instr = {
        .type = PATCH_BINDING,
        .patch_binding = {
            .instr = i,
            .binding = b,
        },
    };
    rv64_add_end(seg, instr);
}

static void
add_assign(Segment* seg, Vreg* dest, Vreg* source) {
    Rv64Instr instr = {
        .type = ASSIGN,
        .assign = {
            .dest = dest,
            .val = source,
        },
    };
    rv64_add(seg, instr);
}
