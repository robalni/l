enum reg {
    REG_ZERO = 0,
    REG_RA = 1,

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

static uint32_t used_regs;

static enum reg
use_free_reg() {
    for (size_t i = 1; i < 32; i++) {
        if (!((used_regs >> i) & 1)) {
            used_regs |= 1 << i;
            return i;
        }
    }
    abort();
}

static void
unuse_reg(enum reg reg) {
    used_regs &= ~(1 << reg);
}

struct Vreg;
struct RegAllocInfo {
    enum reg reg;
    struct Vreg* used_in;
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

enum VregState {
    VREG_UNUSED,
    VREG_AST,
    VREG_USED,
    VREG_EXACT,    // This vreg must be a specific register.
    VREG_STATIC,   // Value known at compile time.
    VREG_MEM,      // The value is in memory at unknown address.
    VREG_MEM_ADDR, // The value is in memory and has an address.
};

// Variable register.  This is not any specific register.  It doesn't
// even need to be a real register but can be memory or anything.
struct Vreg {
    enum VregState state;
    union {
        const Ast* ast;  // VREG_AST
        enum reg reg;    // VREG_EXACT
        const Ast* val;  // VREG_STATIC
        Location loc;    // VREG_MEM_ADDR
    };
    Binding* binding;
};
typedef struct Vreg Vreg;

#define MAX_VREGS 100
static Vreg vregs[MAX_VREGS] = {
    {
        .state = VREG_EXACT,
        .reg = REG_ZERO,
    },
};

static size_t
find_first_free_vreg_index() {
    for (size_t i = 0; i < MAX_VREGS; i++) {
        if (vregs[i].state == VREG_UNUSED) {
            return i;
        }
    }
    abort();
}

static Vreg*
alloc_vreg() {
    Vreg* v = &vregs[find_first_free_vreg_index()];
    v->state = VREG_USED;
    return v;
}

static Vreg*
alloc_vreg_mem() {
    Vreg* v = &vregs[find_first_free_vreg_index()];
    v->state = VREG_MEM;
    return v;
}

static Vreg*
alloc_vreg_ast() {
    Vreg* v = &vregs[find_first_free_vreg_index()];
    v->state = VREG_AST;
    return v;
}

static Vreg*
alloc_this_reg_assume_unused(enum reg r) {
    Vreg* v = &vregs[find_first_free_vreg_index()];
    v->state = VREG_EXACT;
    v->reg = r;
    return v;
}

// Returns the vreg that has allocated this specific reg or NULL.
static Vreg*
get_used_reg(enum reg r) {
    for (size_t i = 0; i < MAX_VREGS; i++) {
        if (vregs[i].state == VREG_EXACT && vregs[i].reg == r) {
            return &vregs[i];
        }
    }
    return NULL;
}

static Vreg*
move_vreg(Vreg* r) {
    Vreg* new_vreg = alloc_vreg();
    *new_vreg = *r;
    return new_vreg;
}

static Vreg*
alloc_this_reg(enum reg r) {
    Vreg* v = get_used_reg(r);
    if (v == NULL) {
        // r is unused; we can use it.
        v = alloc_this_reg_assume_unused(r);
    } else {
        // r is used; we must move it to another register.
        Vreg* new_vreg = move_vreg(v);
    }
    return v;
}

static Vreg*
get_vreg_zero() {
    return &vregs[0];
}

static void
free_vreg(Vreg* v) {
    v->state = VREG_UNUSED;
}

static void
vreg_set_state_exact(Vreg* v, enum reg reg) {
    v->state = VREG_EXACT;
    v->reg = reg;
}

static void
vreg_set_state_mem_addr(Vreg* v, Segment* seg, uint64_t addr) {
    v->state = VREG_MEM_ADDR;
    v->loc = (Location){seg, addr};
}
