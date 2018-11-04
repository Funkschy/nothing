#include <assert.h>

#include "ebisp/gc.h"
#include "ebisp/interpreter.h"
#include "ebisp/parser.h"
#include "ebisp/scope.h"
#include "script.h"
#include "str.h"
#include "system/error.h"
#include "system/line_stream.h"
#include "system/lt.h"
#include "ui/console.h"

struct Script
{
    Lt *lt;
    Gc *gc;
    struct Scope scope;
};

Script *create_script_from_line_stream(LineStream *line_stream, Level *level)
{
    assert(line_stream);

    Lt *lt = create_lt();
    if (lt == NULL) {
        return NULL;
    }

    Script *script = PUSH_LT(lt, malloc(sizeof(Script)), free);
    if (script == NULL) {
        throw_error(ERROR_TYPE_LIBC);
        RETURN_LT(lt, NULL);
    }
    script->lt = lt;

    script->gc = PUSH_LT(lt, create_gc(), destroy_gc);
    if (script->gc == NULL) {
        RETURN_LT(lt, NULL);
    }

    script->scope = create_scope(script->gc);

    size_t n = 0;
    sscanf(line_stream_next(line_stream), "%lu", &n);

    char *source_code = NULL;
    for (size_t i = 0; i < n; ++i) {
        /* TODO(#466): maybe source_code should be constantly replaced in the Lt */
        source_code = string_append(
            source_code,
            line_stream_next(line_stream));
    }
    PUSH_LT(lt, source_code, free);

    set_scope_value(
        script->gc,
        &script->scope,
        SYMBOL(script->gc, "rect-apply-force"),
        NATIVE(script->gc, rect_apply_force, level));

    struct ParseResult parse_result =
        read_all_exprs_from_string(
            script->gc,
            source_code);
    if (parse_result.is_error) {
        fprintf(stderr, "Parsing error: %s\n", parse_result.error_message);
        RETURN_LT(lt, NULL);
    }

    struct EvalResult eval_result = eval(
        script->gc,
        &script->scope,
        CONS(script->gc,
             SYMBOL(script->gc, "begin"),
             parse_result.expr));
    if (eval_result.is_error) {
        print_expr_as_sexpr(stderr, eval_result.expr);
        fprintf(stderr, "\n");
        RETURN_LT(lt, NULL);
    }

    gc_collect(script->gc, script->scope.expr);

    free(RELEASE_LT(lt, source_code));

    return script;
}

void destroy_script(Script *script)
{
    assert(script);
    RETURN_LT0(script->lt);
}

int script_eval(Script *script, const char *source_code)
{
    assert(script);
    assert(source_code);

    struct ParseResult parse_result = read_expr_from_string(
        script->gc,
        source_code);
    if (parse_result.is_error) {
        fprintf(stderr, "Parsing error: %s\n", parse_result.error_message);
        return -1;
    }

    struct EvalResult eval_result = eval(
        script->gc,
        &script->scope,
        parse_result.expr);
    if (eval_result.is_error) {
        fprintf(stderr, "Evaluation error: ");
        print_expr_as_sexpr(stderr, eval_result.expr);
        fprintf(stderr, "\n");
        return -1;
    }

    gc_collect(script->gc, script->scope.expr);

    return 0;
}

bool script_has_scope_value(const Script *script, const char *name)
{
    return !nil_p(
        get_scope_value(
            &script->scope,
            SYMBOL(script->gc, name)));
}
