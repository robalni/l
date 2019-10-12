struct Ast;

struct AstList {
    struct Ast* first;
    struct Ast* last;
};
typedef struct AstList AstList;

enum AstType {
    AST_ROOT,
    AST_FN,
    AST_IF,
    AST_EXIT,
    AST_STOP,
};

struct Ast {
    enum AstType type;
    struct Ast* next;  // When in a list.
    union {
        struct {
            AstList children;
        } root;
        struct {
            struct Ast* parent;
            AstList children;
            Binding* name;
        } fn_block;
        struct {
            struct Ast* parent;
            AstList children;
        } if_block;
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

// The `next` and `parent` fields in `item` does not need to be filled in.
static Ast*
ast_add(Ast* block, Ast item) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = item;
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

static Ast
ast_new_exit() {
    return (Ast) {
        .type = AST_EXIT,
    };
}

static Ast
ast_new_stop() {
    return (Ast) {
        .type = AST_STOP,
    };
}

static Ast
ast_new_fn(Binding* binding) {
    return (Ast) {
        .type = AST_FN,
        .fn_block = {
            .name = binding,
        },
    };
}

static Ast
ast_new_if() {
    return (Ast) {
        .type = AST_IF,
    };
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
        fprintf(stderr, "%*sexit;\n", insp, "");
    } break;
    }
}

static void
print_ast(Ast* ast) {
    return print_ast_part(ast, 0);
}
