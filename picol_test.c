#define PICOL_NO_MAIN
#include "picol.c"

int passed = 0, failed = 0;

void test(int num, const char *name, int ok) {
    printf("%d: \"%s\": %s\n", num, name, ok ? "OK" : "ERR");
    if (ok) passed++; else failed++;
}

/* Helper: eval and check result string. */
int eval_ok(struct picolInterp *i, char *code, const char *expected) {
    int rc = picolEval(i, code);
    return rc == PICOL_OK && strcmp(i->result, expected) == 0;
}

/* Helper: eval code, then check a variable's value. */
int var_ok(struct picolInterp *i, char *code, const char *varname, const char *expected) {
    if (picolEval(i, code) != PICOL_OK) return 0;
    struct picolVar *v = picolGetVar(i, (char*)varname);
    return v && strcmp(v->val, expected) == 0;
}

int main(void) {
    struct picolInterp *interp = picolInitInterp();
    picolRegisterCoreCommands(interp);
    int t = 0;

    /* Basic variable set/get. */
    test(++t, "set returns value",
        eval_ok(interp, "set x 42", "42"));
    test(++t, "read variable back",
        var_ok(interp, "set y hello", "y", "hello"));

    /* Expr basics. */
    test(++t, "expr addition",
        eval_ok(interp, "expr 2 + 3", "5"));
    test(++t, "expr precedence",
        eval_ok(interp, "expr 2 + 3 * 4", "14"));
    test(++t, "expr parentheses",
        eval_ok(interp, "expr (2 + 3) * 4", "20"));
    test(++t, "expr float",
        eval_ok(interp, "expr 1.5 + 2.5", "4"));
    test(++t, "expr unary minus",
        eval_ok(interp, "expr -5 + 3", "-2"));
    test(++t, "expr comparison true",
        eval_ok(interp, "expr 3 > 2", "1"));
    test(++t, "expr comparison false",
        eval_ok(interp, "expr 2 > 3", "0"));
    test(++t, "expr logical and",
        eval_ok(interp, "expr 1 && 0", "0"));
    test(++t, "expr logical or",
        eval_ok(interp, "expr 0 || 1", "1"));
    test(++t, "expr with var substitution",
        eval_ok(interp, "set a 10; expr $a + 5", "15"));

    /* If / elseif / else. */
    test(++t, "if true branch",
        eval_ok(interp, "if {1 > 0} { set r yes }", "yes"));
    test(++t, "if false no else",
        picolEval(interp, "if {0 > 1} { set r no }") == PICOL_OK);
    test(++t, "if else branch",
        eval_ok(interp, "if {0} { set r a } else { set r b }", "b"));
    test(++t, "if elseif",
        eval_ok(interp, "if {0} { set r a } elseif {1} { set r b } else { set r c }", "b"));

    /* While / break / continue. */
    test(++t, "while loop",
        var_ok(interp, "set i 0; while {$i < 5} { set i [expr $i+1] }", "i", "5"));
    test(++t, "while break",
        var_ok(interp, "set j 0; while {1} { set j [expr $j+1]; if {$j == 3} { break } }", "j", "3"));
    test(++t, "while continue",
        var_ok(interp, "set s 0; set k 0; while {$k < 5} { set k [expr $k+1]; if {$k == 3} { continue }; set s [expr $s+$k] }", "s", "12"));

    /* Procedures. */
    test(++t, "proc definition and call",
        eval_ok(interp, "proc double {x} { expr $x * 2 }; double 7", "14"));
    test(++t, "proc with return",
        eval_ok(interp, "proc f {x} { if {$x > 0} { return yes }; return no }; f 1", "yes"));
    test(++t, "proc recursion",
        eval_ok(interp, "proc fact {n} { if {$n <= 1} { return 1 }; expr $n * [fact [expr $n-1]] }; fact 6", "720"));
    test(++t, "proc local scope",
        (picolEval(interp, "set z outer") == PICOL_OK &&
         picolEval(interp, "proc lf {} { set z inner }") == PICOL_OK &&
         picolEval(interp, "lf") == PICOL_OK &&
         strcmp(picolGetVar(interp, "z")->val, "outer") == 0));
    test(++t, "proc wrong arity",
        picolEval(interp, "proc g {a b} { expr $a+$b }; g 1") == PICOL_ERR);
    test(++t, "proc redefine",
        eval_ok(interp, "proc h {} { return 1 }; proc h {} { return 2 }; h", "2"));
    test(++t, "proc uppercase param rejected",
        picolEval(interp, "proc bad {X} { set X 1 }") == PICOL_OK &&
        picolEval(interp, "bad 1") == PICOL_ERR);

    /* Global variables (uppercase). */
    test(++t, "global var set from proc",
        (picolEval(interp, "proc setg {} { set G 99 }; setg") == PICOL_OK &&
         strcmp(picolGetVar(interp, "G")->val, "99") == 0));
    test(++t, "global var read from proc",
        eval_ok(interp, "set H world; proc readg {} { set r $H }; readg", "world"));
    test(++t, "global var modified by proc",
        (picolEval(interp, "set Counter 0") == PICOL_OK &&
         picolEval(interp, "proc inc {} { set Counter [expr $Counter+1] }") == PICOL_OK &&
         picolEval(interp, "inc; inc; inc") == PICOL_OK &&
         strcmp(picolGetVar(interp, "Counter")->val, "3") == 0));

    /* String interpolation. */
    test(++t, "interpolation in double quotes",
        eval_ok(interp, "set name picol; set r \"hello $name\"", "hello picol"));
    test(++t, "command substitution in string",
        eval_ok(interp, "set r \"2+2=[expr 2+2]\"", "2+2=4"));
    test(++t, "braces prevent substitution",
        eval_ok(interp, "set r {$notavar}", "$notavar"));

    /* Escape handling. */
    test(++t, "escape newline",
        eval_ok(interp, "set r \"a\\nb\"", "a\nb"));
    test(++t, "escape tab",
        eval_ok(interp, "set r \"a\\tb\"", "a\tb"));
    test(++t, "escape backslash",
        eval_ok(interp, "set r \"a\\\\b\"", "a\\b"));
    test(++t, "no escape in braces",
        eval_ok(interp, "set r {a\\nb}", "a\\nb"));

    /* Error cases. */
    test(++t, "undefined variable",
        picolEval(interp, "set r $undefined") == PICOL_ERR);
    test(++t, "undefined command",
        picolEval(interp, "nosuchcmd") == PICOL_ERR);
    test(++t, "expr syntax error",
        picolEval(interp, "expr 1 +") == PICOL_ERR);

    /* Puts -nonewline (just check it doesn't error). */
    test(++t, "puts -nonewline",
        picolEval(interp, "puts -nonewline \"\"") == PICOL_OK);

    /* Comments. */
    test(++t, "comment at start of line",
        eval_ok(interp, "# this is a comment\nset r 1", "1"));
    test(++t, "comment after command",
        eval_ok(interp, "set r 2\n# comment\nset r 3", "3"));

    /* Nested command substitution. */
    test(++t, "nested command substitution",
        eval_ok(interp, "expr [expr 2 + 3] * [expr 1 + 1]", "10"));

    /* Concatenation interpolation. */
    test(++t, "var concatenation no spaces",
        eval_ok(interp, "set p hello; set q world; set r $p$q", "helloworld"));
    test(++t, "var concat in double quotes",
        eval_ok(interp, "set p aa; set q bb; set r \"$p$q\"", "aabb"));
    test(++t, "text and var concat",
        eval_ok(interp, "set v X; set r pre$v-post", "preX-post"));
    test(++t, "command concat with text",
        eval_ok(interp, "set r val=[expr 1+1]end", "val=2end"));

    /* Expr single number. */
    test(++t, "expr single number",
        eval_ok(interp, "expr 42", "42"));
    test(++t, "expr single negative",
        eval_ok(interp, "expr -7", "-7"));

    /* Empty string and braces. */
    test(++t, "empty string arg",
        eval_ok(interp, "set r \"\"", ""));
    test(++t, "empty braces arg",
        eval_ok(interp, "set r {}", ""));

    /* Return with no value. */
    test(++t, "return no value",
        eval_ok(interp, "proc retnil {} { return }; retnil", ""));

    /* Escape: backslash-quote and unknown escapes. */
    test(++t, "escape double quote",
        eval_ok(interp, "set r \"a\\\"b\"", "a\"b"));
    test(++t, "unknown escape passes through",
        eval_ok(interp, "set r \"a\\zb\"", "azb"));

    /* Multiple elseif all false, no else. */
    test(++t, "elseif chain all false no else",
        picolEval(interp, "if {0} { set r a } elseif {0} { set r b } elseif {0} { set r c }") == PICOL_OK);

    /* Nested proc calls. */
    test(++t, "nested proc calls",
        eval_ok(interp, "proc add1 {x} { expr $x+1 }; proc add2 {x} { add1 [add1 $x] }; add2 5", "7"));

    /* Redefine a builtin. */
    test(++t, "redefine builtin",
        eval_ok(interp, "proc puts {x} { return got_$x }; puts hello", "got_hello"));
    /* Restore puts for later tests. */
    picolRegisterCommand(interp, "puts", picolCommandPuts);

    /* While with return from proc. */
    test(++t, "return inside while",
        eval_ok(interp, "proc findthree {} { set i 0; while {1} { set i [expr $i+1]; if {$i == 3} { return $i } } }; findthree", "3"));

    /* \r\n line endings. */
    test(++t, "crlf line endings",
        eval_ok(interp, "set r 1\r\nset r 2", "2"));

    /* Braces inside command substitution. */
    test(++t, "braces inside command sub",
        eval_ok(interp, "if {[expr 2 > 1]} { set r yes }", "yes"));
    test(++t, "braced arg in command sub",
        eval_ok(interp, "set r [set v {hello world}]", "hello world"));

    /* Security: OOB read in picolParseCommand with trailing backslash. */
    test(++t, "parse cmd trailing backslash no crash",
        picolEval(interp, "[\\") != -1); /* Must not crash. */

    /* Security: unbounded recursion via nested command substitution. */
    {
        char deep[512];
        memset(deep, '[', 200);
        memcpy(deep+200, "set x 1", 7);
        memset(deep+207, ']', 200);
        deep[407] = '\0';
        test(++t, "deep command nesting returns error",
            picolEval(interp, deep) == PICOL_ERR);
    }

    /* Security: unbounded recursion via self-recursive proc. */
    test(++t, "self-recursive proc returns error",
        picolEval(interp, "proc bomb {} { bomb }; bomb") == PICOL_ERR);

    /* Security: unbounded recursion via nested expression parentheses. */
    {
        const int depth = 2000;
        /* len is: "expr " + '(' * n + '1' + ')' * n + '\0' */
        size_t len = 5 + (size_t)depth * 2 + 2; /* +2 for '1' and '\0' */
        char *deep_expr = malloc(len);
        char *p = deep_expr;
        memcpy(p, "expr ", 5); p += 5;
        memset(p, '(', depth); p += depth;
        *p++ = '1';
        memset(p, ')', depth); p += depth;
        *p = '\0';
        test(++t, "deep expr nesting returns error",
            picolEval(interp, deep_expr) == PICOL_ERR);
        free(deep_expr);
    }

    /* set with one argument (read). */
    test(++t, "set read existing var",
        eval_ok(interp, "set myvar 123; set myvar", "123"));
    test(++t, "set read nonexistent var",
        picolEval(interp, "set nosuchvar") == PICOL_ERR);

    picolFreeInterp(interp);

    printf("\n%d tests passed, %d failed.\n", passed, failed);
    if (failed) printf("*** %d ERRORS FOUND ***\n", failed);
    return failed ? 1 : 0;
}
