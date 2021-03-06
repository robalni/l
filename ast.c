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
    AST_WHILE,
    AST_LABEL,
    AST_EXIT,
    AST_RET,
    AST_ASSIGN,
    AST_VAR,
    AST_CALL,
};

struct AstBlock {
    struct Ast* parent;
    AstList children;
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
            struct AstBlock block;
            Binding* name;
        } fn_block;
        struct AstIf {
            struct AstBlock block;
            struct Ast* head;
        } if_block;
        struct AstWhile {
            struct AstBlock block;
            struct Ast* head;
        } while_block;
        struct AstExit {
            struct Ast* val;
        } exit;
        struct AstRet {
            struct Ast* val;
        } ret;
        struct AstLabel {
            Str name;
        } label;
        struct AstAssign {
            Binding* binding;
            struct Ast* val;
        } assign;
        struct AstVar {
            struct Ast* value;
            Binding* binding;
        } var;
        struct AstCall {
            Binding* binding;
        } call;
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
        ast_list_add(&block->if_block.block.children, a);
        break;
    case AST_WHILE:
        ast_list_add(&block->while_block.block.children, a);
        break;
    case AST_ROOT:
        ast_list_add(&block->root.children, a);
        break;
    case AST_FN:
        ast_list_add(&block->fn_block.block.children, a);
        break;
    default:
        abort();
        break;
    }
    switch (a->type) {
    case AST_IF:
        a->if_block.block.parent = block;
        break;
    case AST_WHILE:
        a->while_block.block.parent = block;
        break;
    case AST_FN:
        a->fn_block.block.parent = block;
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
ast_new_ret(Ast* val) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_RET,
        .ret = {
            .val = val,
        },
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
ast_new_if(Ast* head) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_IF,
        .if_block = {
            .head = head,
        },
    };
    return a;
}

static Ast*
ast_new_while(Ast* head) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_WHILE,
        .while_block = {
            .head = head,
        },
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

static Ast*
ast_new_assign(Binding* b, Ast* val) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_ASSIGN,
        .assign = {
            .binding = b,
            .val = val,
        },
    };
    return a;
}

static Ast*
ast_new_var(Ast* val, Binding* b) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_VAR,
        .var = {
            .value = val,
            .binding = b,
        },
    };
    return a;
}

static Ast*
ast_new_call(Binding* b) {
    Ast* a = mem_alloc(&ast_mem, Ast);
    *a = (Ast) {
        .type = AST_CALL,
        .call = {
            .binding = b,
        },
    };
    return a;
}

static uint64_t
ast_calc_static_value(Ast* ast) {
    switch (ast->type) {
    case AST_NUM: {
        return ast->num.u;
    } break;
    case AST_OPER: {
        uint64_t l = ast_calc_static_value(ast->oper.l);
        uint64_t r = ast_calc_static_value(ast->oper.r);
        return l + r;
    } break;
    default:
        assert(false);
    }
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
    case AST_VAR: {
        Binding* b = a->var.binding;
        fprintf(stderr, "%*s%.*s %.*s ", insp, "",
                (int)b->name.len, b->name.data,
                (int)b->type->name.len, b->type->name.data);
        print_ast_part(a->var.value, indent);
        fprintf(stderr, ";\n");
    } break;
    case AST_IF: {
        fprintf(stderr, "%*sif ", insp, "");
        print_ast_part(a->if_block.head, indent);
        fprintf(stderr, " {\n");
        print_ast_children(&a->if_block.block.children, indent + 1);
        fprintf(stderr, "%*s}\n", insp, "");
    } break;
    case AST_WHILE: {
        fprintf(stderr, "%*swhile ", insp, "");
        print_ast_part(a->while_block.head, indent);
        fprintf(stderr, " {\n");
        print_ast_children(&a->while_block.block.children, indent + 1);
        fprintf(stderr, "%*s}\n", insp, "");
    } break;
    case AST_FN: {
        Binding* b = a->fn_block.name;
        fprintf(stderr, "%*sfn %.*s {\n",
                insp, "", (int)b->name.len, b->name.data);
        print_ast_children(&a->fn_block.block.children, indent + 1);
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
            fprintf(stderr, "%ld", ast->num.i);
        } else {
            fprintf(stderr, "%lu", ast->num.u);
        }
    } break;
    case AST_LABEL: {
        fprintf(stderr, "%.*s", (int)a->label.name.len, a->label.name.data);
    } break;
    case AST_OPER: {
        struct AstOper* o = &a->oper;
        fprintf(stderr, "%s(", oper_to_str(o->oper));
        print_ast_part(o->l, indent);
        fprintf(stderr, ", ");
        print_ast_part(o->r, indent);
        fprintf(stderr, ")");
    } break;
    case AST_ASSIGN: {
        Str bn = a->assign.binding->name;
        fprintf(stderr, "%*s%.*s = ", insp, "", (int)bn.len, bn.data);
        print_ast_part(a->assign.val, indent);
        fprintf(stderr, "\n");
    } break;
    case AST_CALL: {
        Str name = a->call.binding->name;
        fprintf(stderr, "%.*s()", (int)name.len, name.data);
    } break;
    }
}

static void
print_ast(Ast* ast) {
    return print_ast_part(ast, 0);
}
