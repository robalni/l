#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>

typedef enum {false, true} bool;

struct Str {
    const char* data;
    size_t len;
};
typedef struct Str Str;

// S must be a string literal or array with size == string length + 1
#define STR(s) ((Str){s, sizeof s - 1})

#define ARR_LEN(a) (sizeof (a) / sizeof *(a))

static bool
str_eq(Str a, Str b) {
    if (a.len != b.len) {
        return false;
    }
    for (size_t i = 0; i < a.len; i++) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

struct Segment {
    const char* name;
    char* data;
    size_t len;
    size_t addr;

    // Type is *Instr
    void* instrs;
    size_t n_instrs;
};
typedef struct Segment Segment;

static void
init_seg(Segment* seg, const char* name, size_t addr) {
    seg->data = mmap(NULL, 0x100000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    seg->len = 0;
    seg->name = name;
    seg->addr = addr;
    seg->instrs = NULL;
    seg->n_instrs = 0;
}

static void
print_segment(Segment* seg) {
    for (size_t i = 0; i < seg->len; i++) {
        if (i % 4 == 0) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "%02X ", (unsigned char)seg->data[i]);
    }
    fprintf(stderr, "\n");
}

static Segment seg_data;
static Segment seg_text;

struct Type {
    Str name;
    size_t size;
};
typedef struct Type Type;

static Type types[100];
static size_t n_types;

static const Type*
get_type(Str name) {
    for (size_t i = 0; i < n_types; i++) {
        if (str_eq(name, types[i].name)) {
            return types + i;
        }
    }
    return NULL;
}

static Type*
add_type(Str name, size_t size) {
    types[n_types] = (Type){name, size};
    n_types++;
    return types + n_types - 1;
}

struct Location {
    Segment* seg;
    size_t offset;
};
typedef struct Location Location;

struct Vreg;

struct Binding {
    Str name;
    const Type* type;
    struct Vreg* last_vreg;
};
typedef struct Binding Binding;

#define MAX_BINDINGS 100
static Binding bindings[MAX_BINDINGS];
static size_t n_bindings;

struct Vreg;

static Binding*
add_binding(Str name, const Type* type, struct Vreg* r) {
    if (n_bindings < MAX_BINDINGS) {
        bindings[n_bindings] = (Binding){
            .name = name,
            .type = type,
            .last_vreg = r,
        };
        n_bindings++;
        return bindings + n_bindings - 1;
    }
    return NULL;
}

static Binding*
get_binding(Str name) {
    for (size_t i = 0; i < n_bindings; i++) {
        Binding* b = bindings + i;
        if (str_eq(b->name, name)) {
            return b;
        }
    }
    return NULL;
}

static void
print_bindings() {
    for (size_t i = 0; i < n_bindings; i++) {
        Binding* b = bindings + i;
        fprintf(stderr, "%.*s %.*s\n",
                (int)b->name.len, b->name.data,
                (int)b->type->name.len, b->type->name.data);
    }
}

static size_t
add_data(Segment* segment, void* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        segment->data[segment->len + i] = ((char*)data)[i];
    }
    segment->len += size;
    return segment->len - size;
}

struct File {
    const char* name;
    char* content;
    size_t size;
};

struct State {
    const struct File* file;
    size_t offset;
};
typedef struct State State;

enum oper {
    OP_PLUS,
    OP_TIMES,
    OP_LESS,
};

static const char*
oper_to_str(enum oper o) {
    switch (o) {
    case OP_PLUS:   return "+";
    case OP_TIMES:  return "*";
    case OP_LESS:   return "<";
    }
}

enum result_type {
    LABEL, NUMBER, REGISTER,
};
struct Result {
    enum result_type type;
    union {
        Str label;
        uint64_t number;
        int reg;
        enum oper oper;
    };
};
typedef struct Result Result;

#include "mem.c"

#include "ast.c"

#include "regs.c"

#include "final_instructions.c"

#include "var_instructions.c"

static bool
is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool
is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static size_t
skip_whitespace(State* s) {
    size_t o;
    for (o = s->offset; o < s->file->size; o++) {
        char c = s->file->content[o];
        if (c != ' ' && c != '\n' && c != '\t') {
            break;
        }
    }
    size_t len = o - s->offset;
    s->offset = o;
    return len;
}

static size_t
read_label(State* s, Ast** r) {
    skip_whitespace(s);
    size_t o;
    for (o = s->offset; o < s->file->size; o++) {
        char c = s->file->content[o];
        if (o == s->offset && is_digit(c)) {
            return 0;
        }
        if (!is_letter(c) && !is_digit(c) && c != '_') {
            break;
        }
    }
    if (o == s->offset) {
        return 0;
    }
    size_t len = o - s->offset;
    Str lab = {s->file->content + s->offset, len};
    *r = ast_new_label(lab);
    s->offset = o;
    return len;
}

static size_t
read_number(State* s, Ast** r) {
    skip_whitespace(s);
    size_t o;
    uint64_t n = 0;
    int base = 10;
    for (o = s->offset; o < s->file->size; o++) {
        char c = s->file->content[o];
        if (!is_digit(c)) {
            break;
        }
        n = n * base + (c - '0');
    }
    if (o == s->offset) {
        return 0;
    }
    size_t len = o - s->offset;
    *r = ast_new_num_signed(n);
    s->offset = o;
    return len;
}

static size_t
read_char(State* s, char c) {
    skip_whitespace(s);
    if (s->file->content[s->offset] == c) {
        s->offset++;
        return 1;
    }
    return 0;
}

static size_t
read_binop(State* s, enum oper* r) {
    size_t size = 1;
    if (read_char(s, '+')) {
        *r = OP_PLUS;
    } else if (read_char(s, '*')) {
        *r = OP_TIMES;
    } else if (read_char(s, '<')) {
        *r = OP_LESS;
    } else {
        return 0;
    }
    return size;
}

static void
get_linecol(State* s, size_t* line, size_t* col) {
    *line = 1;
    *col = 0;
    for (size_t o = 0; o < s->offset; o++) {
        if (s->file->content[o] == '\n') {
            *line += 1;
            *col = 0;
        } else {
            *col += 1;
        }
    }
}

static Str
get_full_line(const char* code, size_t where) {
    size_t first = where;
    for (;;) {
        if (code[first] == '\n' && first != where) {
            first++;
            break;
        }
        if (first == 0) {
            break;
        }
        first--;
    }
    while (code[where] != '\0' && code[where] != '\n') {
        where++;
    }
    return (Str){code + first, where - first};
}

static void
print_error(const char* msg, State* s) {
    size_t line, col;
    get_linecol(s, &line, &col);
    fprintf(stderr, "%s:%lu:%lu %s\n", s->file->name, line, col, msg);
    Str codeline = get_full_line(s->file->content, s->offset);
    fprintf(stderr, " | %.*s\n", (int)codeline.len, codeline.data);
    fprintf(stderr, " | %*s\n", (int)col + 1, "^");
}

#include "elf.c"

static bool
higher_precedence(enum oper a, enum oper b) {
    struct Prec {
        enum oper op;
        int prec;
    };
    struct Prec p[] = {
        {OP_TIMES, 13},
        {OP_PLUS, 12},
        {OP_LESS, 10},
    };
    struct Prec pa, pb;
    for (size_t i = 0; i < ARR_LEN(p); i++) {
        if (p[i].op == a) {
            pa = p[i];
        } else if (p[i].op == b) {
            pb = p[i];
        }
    }
    return pa.prec > pb.prec;
}

static Ast*
compile_expr(State* state) {
    struct ExprFrame {
        Ast* l;
        enum oper op;
    };
#define MAX_N_EXPR_FRAMES 10
    struct ExprFrame expr_stack[MAX_N_EXPR_FRAMES];
    size_t expr_stack_len = 0;
    struct ExprFrame* frame;  // The current (top) one.

    Ast* return_ast = NULL;

    Ast* r1 = NULL;
    enum oper op;
    bool past_first_op = false;
    Ast* r2 = NULL;
    enum oper latest_oper;

    while (state->offset < state->file->size) {
        Ast** r;
        if (past_first_op) {
            r = &r2;
        } else {
            r = &r1;
        }

        if (read_label(state, r) || read_number(state, r)) {
        } else if (read_binop(state, &latest_oper)) {
            if (past_first_op) {
                if (higher_precedence(latest_oper, op)) {
                    assert(expr_stack_len < MAX_N_EXPR_FRAMES);
                    expr_stack[expr_stack_len] = (struct ExprFrame){
                        r1, op,
                    };
                    frame = &expr_stack[expr_stack_len];
                    expr_stack_len++;
                    r1 = r2;
                } else {
                    r1 = ast_new_oper(r1, op, r2);
                    op = latest_oper;
                    while (expr_stack_len > 0) {
                        frame = &expr_stack[expr_stack_len - 1];
                        if (higher_precedence(op, frame->op)) {
                            break;
                        }
                        expr_stack_len--;
                        r1 = ast_new_oper(frame->l, frame->op, r1);
                        op = frame->op;
                    }
                }
            }
            op = latest_oper;
            past_first_op = true;
        } else {
            if (!past_first_op) {
                return_ast = r1;
                break;
            }
            r2 = ast_new_oper(r1, op, r2);
            while (expr_stack_len > 0) {
                frame = &expr_stack[expr_stack_len - 1];
                r2 = ast_new_oper(frame->l, frame->op, r2);
                expr_stack_len--;
            }
            return_ast = r2;
            break;
        }
    }
    return return_ast;
}

#define ast_for(a, list) \
    for (const Ast* a = list.first; a; a = a->next)
static Vreg*
compile_ast_expr(const Ast* ast) {
    switch (ast->type) {
    case AST_NUM: {
        Vreg* v = alloc_vreg();
        v->state = VREG_STATIC;
        v->val = ast;
        return v;
    } break;
    case AST_LABEL: {
        Vreg* v = alloc_vreg();
        v->state = VREG_USED;
        v->binding = get_binding(ast->label.name);
        return v;
    } break;
    case AST_OPER: {
        const struct AstOper* oper = &ast->oper;
        Vreg* l = compile_ast_expr(oper->l);
        Vreg* r = compile_ast_expr(oper->r);
        if (0 && l->state == VREG_STATIC && r->state == VREG_STATIC) {
            l->val = ast;
            free_vreg(r);
            return l;
        } else {
            switch (oper->oper) {
            case OP_PLUS:
                return rv64_add_add(&seg_text, l, r);
                break;
            default:
                abort();
            }
        }
    } break;
    // These do not belong inside an expression.
    case AST_ROOT:
    case AST_FN:
    case AST_IF:
    case AST_EXIT:
    case AST_ASSIGN:
    case AST_VAR:
        abort();
        break;
    }
}

static void
compile_ast_fn(const struct AstFn* fn) {
    add_function_start(&seg_text, fn->name);
    ast_for(b, fn->children) {
        switch (b->type) {
        case AST_EXIT: {
            Vreg* r = compile_ast_expr(b->exit.val);
            rv64_add_exit(&seg_text, r);
        } break;
        // These do not belong inside a function.
        case AST_ROOT:
        case AST_FN:
        case AST_NUM:
        case AST_LABEL:
        case AST_OPER:
            abort();
            break;
        }
    }
}

static void
compile_ast_root(const Ast* root) {
    ast_for(a, root->root.children) {
        switch (a->type) {
        case AST_FN: {
            compile_ast_fn(&a->fn_block);
        } break;
        // These do not belong in the root.
        case AST_ROOT:
        case AST_NUM:
        case AST_LABEL:
        case AST_OPER:
        case AST_IF:
        case AST_EXIT:
        case AST_ASSIGN:
            abort();
            break;
        }
    }
}
#undef ast_for

// Decide the location of vregs.
static void
determine_vregs() {
    for (size_t i = 0; i < n_vinstrs; i++) {
        Rv64Instr *instr = &vinstrs[i];
        switch (instr->type) {
        case RV64_R:
            if (instr->r.rs1->state == VREG_USED) {
                abort();
            }
            if (instr->r.rs2->state == VREG_USED) {
                abort();
            }
            if (instr->r.rd->state == VREG_USED) {
                enum reg reg = use_free_reg();
                vreg_set_state_exact(instr->r.rd, reg);
            }
            break;
        case RV64_RI64:
            if (instr->ri64.rd->state != VREG_USED) {
                continue;
            }
            fprintf(stderr, "%d, %d\n", instr->ri64.rd->state, instr->ri64.imm);
            enum reg reg = use_free_reg();
            vreg_set_state_exact(instr->ri64.rd, reg);
            break;
        }
    }
}

static void
compile_instrs() {
    for (size_t i = 0; i < n_vinstrs; i++) {
        const Rv64Instr *instr = &vinstrs[i];
        switch (instr->type) {
        case RV64_R:
            fprintf(stderr, "VINSTR: RV64_R %d, %d, %d\n", instr->r.rd->reg, instr->r.rs1->reg, instr->r.rs2->reg);
            assert(instr->r.rd->state == VREG_EXACT);
            assert(instr->r.rs1->state == VREG_EXACT);
            assert(instr->r.rs2->state == VREG_EXACT);
            instr->r.fn(&seg_text,
                        instr->r.rd->reg,
                        instr->r.rs1->reg,
                        instr->r.rs2->reg);
            break;
        case RV64_RI64:
            fprintf(stderr, "VINSTR: RV64_RI64 %d %d\n", instr->ri64.rd->reg, instr->ri64.imm);
            assert(instr->ri64.rd->state == VREG_EXACT);
            instr->ri64.fn(&seg_text,
                           instr->ri64.rd->reg,
                           instr->ri64.imm);
            break;
        case RV64_NONE:
            fprintf(stderr, "VINSTR: RV64_NONE\n");
            instr->none.fn(&seg_text);
            break;
        case FN_START:
            fprintf(stderr, "VINSTR: FN_START\n");
            vreg_set_state_mem_addr(instr->fn_start.binding->last_vreg, &seg_text, seg_text.len);
            break;
        default:
            fprintf(stderr, "VINSTR: Unknown\n");
            break;
        }
    }
}

static void
compile(struct File* file) {
    State state = {
        .file = file,
    };
    // Will be filled in by later function calls.
    Ast* result = NULL;

    Ast ast_root = {0};
    Ast* block = &ast_root;

    init_seg(&seg_text, ".text", 0x20b0);
    init_seg(&seg_data, ".data", 0x3000);

    add_type(STR("void"), 0);
    add_type(STR("bool"), 1);
    add_type(STR("i8"), 1);
    add_type(STR("u8"), 1);
    add_type(STR("i16"), 2);
    add_type(STR("u16"), 2);
    add_type(STR("i32"), 4);
    add_type(STR("u32"), 4);
    add_type(STR("i64"), 8);
    add_type(STR("u64"), 8);
    add_type(STR("isize"), sizeof (size_t));
    add_type(STR("usize"), sizeof (size_t));

    Binding* inside_function = NULL;

    while (state.offset < state.file->size) {
        bool end_of_statement = false;

        if (read_label(&state, &result)) {
            if (str_eq(result->label.name, STR("if"))) {
                Ast* rd = compile_expr(&state);
                if (read_char(&state, '{')) {
                    block = ast_add(block, ast_new_if(rd));
                    end_of_statement = true;
                }
            } else if (str_eq(result->label.name, STR("exit"))) {
                Ast* val = compile_expr(&state);
                if (read_char(&state, ';')) {
                    ast_add(block, ast_new_exit(val));
                    end_of_statement = true;
                }
            } else {
                Str name = result->label.name;
                if (read_label(&state, &result)) {
                    Str type = result->label.name;
                    if (read_number(&state, &result)) {
                        if (read_char(&state, ';')) {
                            const Type* t = get_type(type);
                            Vreg* r = alloc_vreg_mem();
                            Binding* b = add_binding(name, t, r);
                            r->binding = b;
                            ast_add(block, ast_new_var(result, b));
                            end_of_statement = true;
                        }
                    } else if (read_char(&state, '(')) {
                        if (read_char(&state, ')')) {
                            if (read_char(&state, '{')) {
                                const Type* t = get_type(type);
                                Vreg* r = alloc_vreg_mem();
                                Binding* b = add_binding(name, t, r);
                                r->binding = b;
                                inside_function = b;
                                block = ast_add(block, ast_new_fn(b));
                                end_of_statement = true;
                            }
                        }
                    }
                } else if (read_char(&state, '=')) {
                    Ast* rd = compile_expr(&state);
                    if (read_char(&state, ';')) {
                        Binding* b = get_binding(name);
                        ast_add(block, ast_new_assign(b, rd));
                        end_of_statement = true;
                    }
                }
            }
        } else if (block && read_char(&state, '}')) {
            switch (block->type) {
            case AST_IF:
                block = block->if_block.parent;
                break;
            case AST_FN:
                block = block->fn_block.parent;
                inside_function = NULL;
                break;
            default:
                print_error("Too many `}`", &state);
                goto after_loop;
            }
            end_of_statement = true;
        }

        skip_whitespace(&state);
        if (!end_of_statement) {
            print_error("Syntax error", &state);
            break;
        }
    }

after_loop:

    compile_ast_root(&ast_root);
    fprintf(stderr, "\nAst:\n");
    print_ast(&ast_root);

    determine_vregs();
    compile_instrs();

    fprintf(stderr, "\nData segment:\n");
    print_segment(&seg_data);
    fprintf(stderr, "\nText segment:\n");
    print_segment(&seg_text);
    fprintf(stderr, "\nBindings:\n");
    print_bindings();

    write_elf_file("a");
}

int
main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Please specify filename\n");
        return 1;
    }
    const char* filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Could not open file %s\n", filename);
        perror("open");
        return 1;
    }
    off_t size_off = lseek(fd, 0, SEEK_END);
    if (size_off == (off_t)-1) {
        fprintf(stderr, "Could not seek in the sourcefile."
                        "Is it a regular file?\n");
        perror("lseek");
        close(fd);
        return 1;
    }
    size_t size = size_off;
    void* mem = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "Could not map the source file to memory.\n");
        perror("mmap");
        return 1;
    }
    struct File file = {
        .name = filename,
        .content = (char*)mem,
        .size = size,
    };
    compile(&file);
    munmap(file.content, file.size);
    return 0;
}
