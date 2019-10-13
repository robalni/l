struct Ast;

struct AstList {
    struct Ast* first;
    struct Ast* last;
};
typedef struct AstList AstList;

enum AstType {
    AST_ROOT,
    AST_NUM,
    AST_OPER,
    AST_FN,
    AST_IF,
    AST_LABEL,
    AST_EXIT,
    AST_STOP,
};

struct Ast {
    enum AstType type;
    struct Ast* next;  // When in a list.
    union {
        struct AstRoot {
            AstList children;
        } root;
        struct AstNum {
            bool sign;
            union {
                uint64_t u;
                int64_t i;
            };
        } num;
        struct AstOper {
            enum oper oper;
            struct Ast* l;
            struct Ast* r;
        } oper;
        struct AstFn {
            struct Ast* parent;
            AstList children;
            Binding* name;
        } fn_block;
        struct AstIf {
            struct Ast* parent;
            AstList children;
        } if_block;
        struct AstExit {
            struct Ast* val;
        } exit;
        struct AstLabel {
            Str name;
        } label;
    };
};
typedef struct Ast Ast;

static Mem ast_mem;

static void
ast_list_add(AstList* list, Ast* a) {
    if (list->last) {
        list->last->next = a;
    }
    list->last = a;
    if (list->first == NULL) {
        list->first = a;
    }
}

// The `next` and `parent` fields in `a` does not need to be filled in.
static Ast*
ast_add(Ast* block, Ast* a) {
    a->next = NULL;
    switch (block->type) {
    case AST_IF:
        ast_list_add(&block->if_block.children, a);
        break;
    case AST_ROOT:
        ast_list_add(&block->root.children, a);
        break;
    case AST_FN:
        ast_list_add(&block->fn_block.children, a);
        break;
    default:
        abort();
        break;
    }
    switch (a->type) {
    case AST_IF:
        a->if_block.parent = block;
        break;
    case AST_FN:
        a->fn_block.parent = block;
        break;
    default:
        break;
    }
    return a;
}

static Ast*
ast_new_exit(Ast* val) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_EXIT,
        .exit = {
            .val = val,
        },
    };
    return a;
}

static Ast*
ast_new_stop() {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_STOP,
    };
    return a;
}

static Ast*
ast_new_fn(Binding* binding) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_FN,
        .fn_block = {
            .name = binding,
        },
    };
    return a;
}

static Ast*
ast_new_if() {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_IF,
    };
    return a;
}

static Ast*
ast_new_oper(Ast* l, enum oper oper, Ast* r) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_OPER,
        .oper = {
            .oper = oper,
            .l = l,
            .r = r,
        },
    };
    return a;
}

static Ast*
ast_new_num_signed(int64_t n) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_NUM,
        .num = {
            .sign = true,
            .i = n,
        },
    };
    return a;
}

static Ast*
ast_new_label(Str label) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_LABEL,
        .label = {
            .name = label,
        },
    };
    return a;
}

static void
print_ast_part(Ast* ast, int indent);

static void
print_ast_children(AstList* list, int indent) {
    Ast* a = list->first;
    while (a) {
        print_ast_part(a, indent);
        a = a->next;
    }
}

static void
print_ast_part(Ast* ast, int indent) {
    Ast* a = ast;
    int insp = indent * 4;
    switch (a->type) {
    case AST_IF: {
        fprintf(stderr, "%*sif {\n", insp, "");
        print_ast_children(&a->if_block.children, indent + 1);
        fprintf(stderr, "%*s}\n", insp, "");
    } break;
    case AST_FN: {
        Binding* b = a->fn_block.name;
        fprintf(stderr, "%*sfn %.*s {\n",
                insp, "", b->name.len, b->name.data);
        print_ast_children(&a->fn_block.children, indent + 1);
        fprintf(stderr, "%*s}\n", insp, "");
    } break;
    case AST_ROOT: {
        fprintf(stderr, "%*sroot {\n", insp, "");
        print_ast_children(&a->root.children, indent + 1);
        fprintf(stderr, "%*s}\n", insp, "");
    } break;
    case AST_EXIT: {
        fprintf(stderr, "%*sexit ", insp, "");
        print_ast_part(a->exit.val, indent);
        fprintf(stderr, ";\n");
    } break;
    case AST_NUM: {
        if (ast->num.sign) {
            fprintf(stderr, "%lld", ast->num.i);
        } else {
            fprintf(stderr, "%llu", ast->num.u);
        }
    } break;
    case AST_OPER: {
        struct AstOper* o = &a->oper;
        fprintf(stderr, "%s(", oper_to_str(o->oper));
        print_ast_part(o->l, indent);
        fprintf(stderr, ", ");
        print_ast_part(o->r, indent);
        fprintf(stderr, ")");
    } break;
    }
}

static void
print_ast(Ast* ast) {
    return print_ast_part(ast, 0);
}
