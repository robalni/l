#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>

typedef enum {false, true} bool;

struct str {
    const char* data;
    size_t len;
};
typedef struct str Str;

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

struct segment {
    const char* name;
    char* data;
    size_t len;
    size_t addr;
};
typedef struct segment Segment;

static void
init_seg(Segment* seg, const char* name, size_t addr) {
    seg->data = mmap(NULL, 0x100000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    seg->len = 0;
    seg->name = name;
    seg->addr = addr;
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

struct type {
    Str name;
    size_t size;
};
typedef struct type Type;

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

struct location {
    Segment* seg;
    size_t offset;
};
typedef struct location Location;

struct binding {
    Str name;
    const Type* type;
    Location loc;
};
typedef struct binding Binding;

static Binding bindings[100];
static size_t n_bindings;

static Binding*
add_binding(Str name, const Type* type, Location loc) {
    if (n_bindings < 100) {
        bindings[n_bindings] = (Binding){name, type, loc};
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
        fprintf(stderr, "%.*s %.*s %s(%lX)\n",
                (int)b->name.len, b->name.data,
                (int)b->type->name.len, b->type->name.data,
                b->loc.seg->name, b->loc.offset);
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

struct file {
    const char* name;
    char* content;
    size_t size;
};

struct state {
    const struct file* file;
    size_t offset;
};
typedef struct state State;

enum oper {
    OP_PLUS,
    OP_TIMES,
};

static const char*
oper_to_str(enum oper o) {
    switch (o) {
    case OP_PLUS:   return "+";
    case OP_TIMES:  return "*";
    }
}

enum result_type {
    LABEL, NUMBER, OPER, REGISTER,
};
struct result {
    enum result_type type;
    union {
        Str label;
        uint64_t number;
        int reg;
        enum oper oper;
    };
};
typedef struct result Result;

#include "mem.c"

#include "ast.c"

#include "regs.c"

#include "instructions.c"

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
    struct prec {
        enum oper op;
        int prec;
    };
    struct prec p[] = {
        {OP_TIMES, 13},
        {OP_PLUS, 12},
    };
    struct prec pa, pb;
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
    struct expr_frame {
        Ast* l;
        enum oper op;
    };
#define MAX_N_EXPR_FRAMES 10
    struct expr_frame expr_stack[MAX_N_EXPR_FRAMES];
    size_t expr_stack_len = 0;
    struct expr_frame* frame;  // The current (top) one.

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
                    expr_stack[expr_stack_len] = (struct expr_frame){
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

static void
compile(struct file* file) {
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
            } else if (str_eq(result->label.name, STR("stop"))) {
                if (read_char(&state, ';')) {
                    ast_add(block, ast_new_stop());
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
                            size_t o = add_data(&seg_data, &result->num.i,
                                                t->size);
                            Location l = (Location){&seg_data, o};
                            add_binding(name, t, l);
                            end_of_statement = true;
                        }
                    } else if (read_char(&state, '(')) {
                        if (read_char(&state, ')')) {
                            if (read_char(&state, '{')) {
                                const Type* t = get_type(type);
                                Location l = (Location){
                                    &seg_text, seg_text.len,
                                };
                                Binding* b = add_binding(name, t, l);
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
    fprintf(stderr, "\nData segment:\n");
    print_segment(&seg_data);
    fprintf(stderr, "\nText segment:\n");
    print_segment(&seg_text);
    fprintf(stderr, "\nBindings:\n");
    print_bindings();
    fprintf(stderr, "\nAst:\n");
    print_ast(&ast_root);

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
    struct file file = {
        .name = filename,
        .content = (char*)mem,
        .size = size,
    };
    compile(&file);
    munmap(file.content, file.size);
    return 0;
}
