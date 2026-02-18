/* Tcl in ~ 500 lines of code.
 *
 * IMPORTANT: this is Picol version 2! For the original code, check
 * the commit history of this repository.
 *
 * Copyright (c) 2007-2026, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =============================================================================
 * Memory allocation wrappers that abort on out of memory
 * ========================================================================== */

void *xrealloc(void *ptr, size_t size) {
    void *mem = realloc(ptr,size);
    if (!mem) {
        fprintf(stderr,"Out of memory realloc(%p,%zu)\n", ptr, size);
        exit(1);
    }
    return mem;
}

#define xmalloc(size) xrealloc(NULL,size)

char *xstrdup(const char *s) {
    size_t l = strlen(s);
    char *dup = xmalloc(l+1);
    memcpy(dup,s,l+1);
    return dup;
}

/* =============================================================================
 * Data structures
 * ========================================================================== */

#define PICOL_MAX_RECURSION_LEVEL 128

enum {PICOL_OK, PICOL_ERR, PICOL_RETURN, PICOL_BREAK, PICOL_CONTINUE};
enum {
    PT_ESC, // String that may contain escapes (that should be processed)
    PT_STR, // String without escapes, no post processing needed.
    PT_CMD, // Command, that is [.... something ...]
    PT_VAR, // Variable like $var
    PT_SEP, // Arguments separator
    PT_EOL, // End of command
    PT_EOF  // End of file (stops the parsing loop)
};

struct picolParser {
    char *text;         // The program to parse
    char *p;            // Current parsing position in 'text'
    int len;            // Remaining length
    char *start;        // Token start
    char *end;          // Token end
    int type;           // Token type, PT_...
    int insidequote;    // True if inside " "
};

struct picolVar {
    char *name, *val;
    struct picolVar *next;
};

struct picolInterp;     // Forward declarations
struct picolCmd;
typedef int (*picolCmdFunc)(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd);

struct picolCmd {
    char *name;
    picolCmdFunc func;
    struct picolCmd *next;
    // Aux data for user defined procedures:
    char *arglist;
    char *body;
};

struct picolCallFrame {
    struct picolVar *vars;
    struct picolCallFrame *parent; /* parent is NULL at top level */
};

struct picolInterp {
    int level; /* Level of nesting */
    struct picolCallFrame *callframe;
    struct picolCmd *commands;
    char *result;
};

void picolInitParser(struct picolParser *p, char *text) {
    p->text = p->p = text;
    p->len = strlen(text);
    p->start = 0; p->end = 0; p->insidequote = 0;
    p->type = PT_EOL;
}

int picolParseSep(struct picolParser *p) {
    p->start = p->p;
    while(*p->p == ' ' || *p->p == '\t') {
        p->p++; p->len--;
    }
    p->end = p->p-1;
    p->type = PT_SEP;
    return PICOL_OK;
}

int picolParseEol(struct picolParser *p) {
    p->start = p->p;
    while(*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r' ||
          *p->p == ';')
    {
        p->p++; p->len--;
    }
    p->end = p->p-1;
    p->type = PT_EOL;
    return PICOL_OK;
}

int picolParseCommand(struct picolParser *p) {
    int level = 1;
    int blevel = 0;
    p->start = ++p->p; p->len--;
    while (1) {
        if (p->len == 0) {
            break;
        } else if (*p->p == '[' && blevel == 0) {
            level++;
        } else if (*p->p == ']' && blevel == 0) {
            if (!--level) break;
        } else if (*p->p == '\\') {
            if (p->len >= 2) {
                p->p++; p->len--;
            }
        } else if (*p->p == '{') {
            blevel++;
        } else if (*p->p == '}') {
            if (blevel != 0) blevel--;
        }
        p->p++; p->len--;
    }
    p->end = p->p-1;
    p->type = PT_CMD;
    if (*p->p == ']') {
        p->p++; p->len--;
    }
    return PICOL_OK;
}

int picolParseVar(struct picolParser *p) {
    p->start = ++p->p; p->len--; /* skip the $ */
    while(1) {
        if ((*p->p >= 'a' && *p->p <= 'z') || (*p->p >= 'A' && *p->p <= 'Z') ||
            (*p->p >= '0' && *p->p <= '9') || *p->p == '_')
        {
            p->p++; p->len--; continue;
        }
        break;
    }
    if (p->start == p->p) { /* It's just a single char string "$" */
        p->start = p->end = p->p-1;
        p->type = PT_STR;
    } else {
        p->end = p->p-1;
        p->type = PT_VAR;
    }
    return PICOL_OK;
}

int picolParseBrace(struct picolParser *p) {
    int level = 1;
    p->start = ++p->p; p->len--;
    while(1) {
        if (p->len >= 2 && *p->p == '\\') {
            p->p++; p->len--;
        } else if (p->len == 0 || *p->p == '}') {
            level--;
            if (level == 0 || p->len == 0) {
                p->end = p->p-1;
                if (p->len) {
                    p->p++; p->len--; /* Skip final closed brace */
                }
                p->type = PT_STR;
                return PICOL_OK;
            }
        } else if (*p->p == '{')
            level++;
        p->p++; p->len--;
    }
    return PICOL_OK; /* unreached */
}

int picolParseString(struct picolParser *p) {
    int newword = (p->type == PT_SEP || p->type == PT_EOL || p->type == PT_STR);
    if (newword && *p->p == '{') return picolParseBrace(p);
    else if (newword && *p->p == '"') {
        p->insidequote = 1;
        p->p++; p->len--;
    }
    p->start = p->p;
    while(1) {
        if (p->len == 0) {
            p->end = p->p-1;
            p->type = PT_ESC;
            return PICOL_OK;
        }
        switch(*p->p) {
        case '\\':
            if (p->len >= 2) {
                p->p++; p->len--;
            }
            break;
        case '$': case '[':
            p->end = p->p-1;
            p->type = PT_ESC;
            return PICOL_OK;
        case ' ': case '\t': case '\n': case '\r': case ';':
            if (!p->insidequote) {
                p->end = p->p-1;
                p->type = PT_ESC;
                return PICOL_OK;
            }
            break;
        case '"':
            if (p->insidequote) {
                p->end = p->p-1;
                p->type = PT_ESC;
                p->p++; p->len--;
                p->insidequote = 0;
                return PICOL_OK;
            }
            break;
        }
        p->p++; p->len--;
    }
    return PICOL_OK; /* unreached */
}

int picolParseComment(struct picolParser *p) {
    while(p->len && *p->p != '\n') {
        p->p++; p->len--;
    }
    return PICOL_OK;
}

int picolGetToken(struct picolParser *p) {
    while(1) {
        if (!p->len) {
            if (p->type != PT_EOL && p->type != PT_EOF)
                p->type = PT_EOL;
            else
                p->type = PT_EOF;
            return PICOL_OK;
        }
        switch(*p->p) {
        case ' ': case '\t':
            if (p->insidequote) return picolParseString(p);
            return picolParseSep(p);
        case '\n': case '\r': case ';':
            if (p->insidequote) return picolParseString(p);
            return picolParseEol(p);
        case '[':
            return picolParseCommand(p);
        case '$':
            return picolParseVar(p);
        case '#':
            if (p->type == PT_EOL) {
                picolParseComment(p);
                continue;
            }
            return picolParseString(p);
        default:
            return picolParseString(p);
        }
    }
    return PICOL_OK; /* unreached */
}

/* =============================================================================
 * Eval and related functions
 * ========================================================================== */

struct picolInterp *picolInitInterp(void) {
    struct picolInterp *i = xmalloc(sizeof(*i));
    i->level = 0;
    i->callframe = xmalloc(sizeof(struct picolCallFrame));
    i->result = xstrdup("");
    i->callframe->vars = NULL;
    i->callframe->parent = NULL;
    i->commands = NULL;
    return i;
}

void picolSetResult(struct picolInterp *i, char *s) {
    free(i->result);
    i->result = xstrdup(s);
}

struct picolVar *picolGetVar(struct picolInterp *i, char *name) {
    struct picolCallFrame *cf = i->callframe;
    if (isupper(name[0])) while(cf->parent) cf = cf->parent;
    struct picolVar *v = cf->vars;
    while(v) {
        if (strcmp(v->name,name) == 0) return v;
        v = v->next;
    }
    return NULL;
}

void picolSetVar(struct picolInterp *i, char *name, char *val) {
    struct picolVar *v = picolGetVar(i,name);
    if (v) {
        free(v->val);
        v->val = xstrdup(val);
    } else {
        v = xmalloc(sizeof(*v));
        v->name = xstrdup(name);
        v->val = xstrdup(val);
        struct picolCallFrame *cf = i->callframe;
        if (isupper(name[0])) while(cf->parent) cf = cf->parent;
        v->next = cf->vars;
        cf->vars = v;
    }
}

struct picolCmd *picolGetCommand(struct picolInterp *i, char *name) {
    struct picolCmd *c = i->commands;
    while(c) {
        if (strcmp(c->name,name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

void picolRegisterCommand(struct picolInterp *i, char *name, picolCmdFunc f) {
    struct picolCmd *c = picolGetCommand(i,name);
    int existing = c != NULL;

    if (!existing) {
        c = xmalloc(sizeof(*c));
        c->name = NULL;
        c->arglist = NULL;
        c->body = NULL;
    } else {
        free(c->arglist);
        free(c->body);
        c->arglist = NULL;
        c->body = NULL;
    }
    if (!c->name) c->name = xstrdup(name);
    c->func = f;
    if (!existing) {
        c->next = i->commands;
        i->commands = c;
    }
}

/* EVAL! */
int picolEval(struct picolInterp *i, char *t) {
    struct picolParser p;
    int argc = 0, j;
    char **argv = NULL;
    char errbuf[1024];
    int retcode = PICOL_OK;
    picolSetResult(i,"");
    if (++i->level > PICOL_MAX_RECURSION_LEVEL) {
        i->level--;
        picolSetResult(i,"Nesting too deep");
        return PICOL_ERR;
    }
    picolInitParser(&p,t);
    while(1) {
        char *t;
        int tlen;
        int prevtype = p.type;
        picolGetToken(&p);
        if (p.type == PT_EOF) break;
        tlen = p.end-p.start+1;
        if (tlen < 0) tlen = 0;
        t = xmalloc(tlen+1);
        memcpy(t, p.start, tlen);
        t[tlen] = '\0';
        if (p.type == PT_VAR) {
            struct picolVar *v = picolGetVar(i,t);
            if (!v) {
                snprintf(errbuf,sizeof(errbuf),"No such variable '%s'",t);
                free(t);
                picolSetResult(i,errbuf);
                retcode = PICOL_ERR;
                goto err;
            }
            free(t);
            t = xstrdup(v->val);
        } else if (p.type == PT_CMD) {
            retcode = picolEval(i,t);
            free(t);
            if (retcode != PICOL_OK) goto err;
            t = xstrdup(i->result);
        } else if (p.type == PT_ESC) {
            /* Process escapes turning \<something> into
             * a single char. No need for a second buffer, the result
             * is always equal or shorter than the original string. */
            char *src = t, *dst = t;
            while (*src) {
                if (*src == '\\' && *(src+1)) {
                    src++; // skip the "\"
                    switch(*src) {
                    case 'n': *dst++ = '\n'; break;
                    case 't': *dst++ = '\t'; break;
                    case 'r': *dst++ = '\r'; break;
                    default: *dst++ = *src; break;
                    }
                } else *dst++ = *src;
                src++;
            }
            *dst = '\0';
        } else if (p.type == PT_SEP) {
            prevtype = p.type;
            free(t);
            continue;
        }
        /* We have a complete command + args. Call it! */
        if (p.type == PT_EOL) {
            struct picolCmd *c;
            free(t);
            prevtype = p.type;
            if (argc) {
                if ((c = picolGetCommand(i,argv[0])) == NULL) {
                    snprintf(errbuf,sizeof(errbuf),"No such command '%s'",argv[0]);
                    picolSetResult(i,errbuf);
                    retcode = PICOL_ERR;
                    goto err;
                }
                retcode = c->func(i,argc,argv,c);
                if (retcode != PICOL_OK) goto err;
            }
            /* Prepare for the next command */
            for (j = 0; j < argc; j++) free(argv[j]);
            free(argv);
            argv = NULL;
            argc = 0;
            continue;
        }
        /* We have a new token, append to the previous or as new arg? */
        if (prevtype == PT_SEP || prevtype == PT_EOL) {
            /* New argument of the current command. */
            argv = xrealloc(argv, sizeof(char*)*(argc+1));
            argv[argc] = t;
            argc++;
        } else {
            /* Interpolation: concatenate to the old argument. */
            int oldlen = strlen(argv[argc-1]), tlen = strlen(t);
            argv[argc-1] = xrealloc(argv[argc-1], oldlen+tlen+1);
            memcpy(argv[argc-1]+oldlen, t, tlen);
            argv[argc-1][oldlen+tlen]='\0';
            free(t);
        }
        prevtype = p.type;
    }
err:
    for (j = 0; j < argc; j++) free(argv[j]);
    free(argv);
    i->level--;
    return retcode;
}

/* This is a "Pratt style parser" for expressions: precedence is encoded in a
 * single recursive function. Basically the C call stack replaces the explicit
 * stack here.
 *
 * Precedences:
 *  0 ||, 1 &&, 2 comparisons, 3 add/sub, 4 mul/div, 5 unary.
 *
 * Note: picolExpr() is designed to be simple, not fully functional, so it
 * does not expand $vars and [commands]. [expr $a + [foo]] works, but
 * [expr {$a + [foo]}] will not. Also: no short circuits with && ||
 */
double picolExpr(struct picolInterp *i, char **p, int *err, int prec) {
    double a; char *e;

    if (++i->level > PICOL_MAX_RECURSION_LEVEL) {
        i->level--;
        *err = 1; // Will be reported as error in expression, instead of
        return 0; // recursion limit. Requires a pathological expression anyway.
    }

    /* Step 1: parse the left operand. */
    while (**p && strchr(" \t\r\n", **p)) (*p)++;
    if (**p == '(') {
        (*p)++; a = picolExpr(i,p,err,0);
        while (**p && strchr(" \t\r\n", **p)) (*p)++;
        if (**p == ')') (*p)++; else *err = 1;
    } else if (**p == '-') { (*p)++; a = -picolExpr(i,p,err,5);
    } else if (**p == '+') { (*p)++; a = picolExpr(i,p,err,5);
    } else { a = strtod(*p,&e); if (e == *p) *err = 1; *p = e; }
    while (**p && strchr(" \t\r\n", **p)) (*p)++;

    while (1) {
        /* Step 2: parse the operator */
        int op, oprec, len = 1;
        if (**p == '|' && *(*p+1) == '|') { op = 'o'; oprec = 0; len = 2; }
        else if (**p == '&' && *(*p+1) == '&') { op = 'a'; oprec = 1; len = 2; }
        else if (**p == '*' || **p == '/') { op = **p; oprec = 4; }
        else if (**p == '+' || **p == '-') { op = **p; oprec = 3; }
        else if (**p == '<' && *(*p+1) == '=') { op = 'L'; oprec = 2; len = 2; }
        else if (**p == '>' && *(*p+1) == '=') { op = 'G'; oprec = 2; len = 2; }
        else if (**p == '=' && *(*p+1) == '=') { op = 'E'; oprec = 2; len = 2; }
        else if (**p == '!' && *(*p+1) == '=') { op = 'N'; oprec = 2; len = 2; }
        else if (**p == '<') { op = '<'; oprec = 2; }
        else if (**p == '>') { op = '>'; oprec = 2; }
        else break; // No more operators to consume: \0, ), or syntax error.

        /* Step 3: if the operator has high enough precedence, parse the right
         * operand with a recursive call (effectively processing higher
         * precedence operators in the recursive call), and execute the
         * operation. */
        if (oprec < prec) break;
        *p += len;
        double b = picolExpr(i,p,err,oprec+1);
        switch(op) {
        case '+': a += b; break; case '-': a -= b; break;
        case '*': a *= b; break; case '/': a /= b; break;
        case '<': a = a < b; break; case '>': a = a > b; break;
        case 'L': a = a <= b; break; case 'G': a = a >= b; break;
        case 'E': a = a == b; break; case 'N': a = a != b; break;
        case 'o': a = a || b; break; case 'a': a = a && b; break;
        }
        while (**p && strchr(" \t\r\n", **p)) (*p)++;
    }
    i->level--;
    return a;
}

/* Trick: wrap 's' as "expr <s>" and evaluate it, so that picolEval handles
 * $var and [cmd] substitution before expr parses pure math expression.
 * This is used in [if] and [while] condition evaluation. */
int picolExprExpansion(struct picolInterp *i, char *s) {
    int len = strlen(s);
    char *e = xmalloc(len+6); /* "expr " + s + \0 */
    memcpy(e,"expr ",5);
    memcpy(e+5,s,len+1);
    int retcode = picolEval(i,e);
    free(e);
    return retcode;
}

/* =============================================================================
 * Standard library of commands
 * ========================================================================== */

int picolArityErr(struct picolInterp *i, char *name) {
    char buf[1024];
    snprintf(buf,sizeof(buf),"Wrong number of args for %s",name);
    picolSetResult(i,buf);
    return PICOL_ERR;
}

/* expr a + b * c ... (no var or command expansions! Don't quote expressions) */
int picolCommandExpr(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    char buf[64]; int err = 0, j, len = 0;
    if (argc < 2) return picolArityErr(i,argv[0]);
    for (j = 1; j < argc; j++) len += strlen(argv[j]) + 1;
    char *expr = xmalloc(len), *p = expr;
    for (j = 1; j < argc; j++) {
        int l = strlen(argv[j]);
        if (j > 1) *p++ = ' ';
        memcpy(p,argv[j],l); p += l;
    }
    *p = '\0'; p = expr;
    double v = picolExpr(i,&p,&err,0);
    while (*p == ' ') p++;
    if (*p != '\0') err = 1;
    free(expr);
    if (err) { picolSetResult(i,"Error in expression"); return PICOL_ERR; }
    snprintf(buf,sizeof(buf),"%.12g",v);
    picolSetResult(i,buf); return PICOL_OK;
}

/* set var ?value? */
int picolCommandSet(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    if (argc == 3) {
        picolSetVar(i,argv[1],argv[2]);
        picolSetResult(i,argv[2]);
    } else if (argc == 2) {
        struct picolVar *v = picolGetVar(i,argv[1]);
        if (v == NULL) {
            char buf[1024];
            snprintf(buf,sizeof(buf),
                "Can't read \"%s\": no such variable",argv[1]);
            picolSetResult(i,buf);
            return PICOL_ERR;
        } else {
            picolSetResult(i,v->val);
        }
    } else {
        return picolArityErr(i,argv[0]);
    }
    return PICOL_OK;
}

/* puts ?-nonewline? string */
int picolCommandPuts(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    int nonl = (argc == 3 && !strcmp(argv[1],"-nonewline"));
    if (argc != 2 && !nonl) return picolArityErr(i,argv[0]);
    printf("%s%s", argv[nonl?2:1], nonl ? "" : "\n");
    return PICOL_OK;
}

/* if cond body ?elseif cond body ...? ?else body? */
int picolCommandIf(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    int retcode, j = 1;
    while (1) {
        if (j >= argc) return picolArityErr(i,argv[0]);
        /* Evaluate condition of this branch. */
        retcode = picolExprExpansion(i,argv[j]);
        if (retcode != PICOL_OK) return retcode;
        if (j+1 >= argc) return picolArityErr(i,argv[0]);
        /* True? Eval the corresponding branch and return. */
        if (strtod(i->result,NULL)) return picolEval(i,argv[j+1]);
        j += 2;
        if (j >= argc) return PICOL_OK; // No more branches.
        /* Else statement? Evaluate the else branch (condition was false)
         * if we are here. */
        if (!strcmp(argv[j],"else"))
            return (j+1 < argc) ? picolEval(i,argv[j+1]) :
                                  picolArityErr(i,argv[0]);
        /* We expect elseif now, or there is a syntax error. */
        if (strcmp(argv[j],"elseif")) return picolArityErr(i,argv[0]);
        j++;
    }
}

/* while cond body */
int picolCommandWhile(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    if (argc != 3) return picolArityErr(i,argv[0]);
    while(1) {
        int retcode = picolExprExpansion(i,argv[1]);
        if (retcode != PICOL_OK) return retcode;
        if (!strtod(i->result,NULL)) return PICOL_OK;
        retcode = picolEval(i,argv[2]);
        if (retcode == PICOL_CONTINUE || retcode == PICOL_OK) continue;
        else if (retcode == PICOL_BREAK) return PICOL_OK;
        else return retcode;
    }
}

/* break and continue. */
int picolCommandRetCodes(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    if (argc != 1) return picolArityErr(i,argv[0]);
    if (strcmp(argv[0],"break") == 0) return PICOL_BREAK;
    else if (strcmp(argv[0],"continue") == 0) return PICOL_CONTINUE;
    return PICOL_OK;
}

void picolDropCallFrame(struct picolInterp *i) {
    struct picolCallFrame *cf = i->callframe;
    struct picolVar *v = cf->vars, *t;
    while(v) {
        t = v->next;
        free(v->name);
        free(v->val);
        free(v);
        v = t;
    }
    i->callframe = cf->parent;
    free(cf);
}

void picolFreeInterp(struct picolInterp *i) {
    while(i->callframe) picolDropCallFrame(i);
    while(i->commands) {
        struct picolCmd *c = i->commands;
        i->commands = c->next;
        free(c->name);
        free(c->arglist);
        free(c->body);
        free(c);
    }
    free(i->result);
    free(i);
}

/* The callback used for user defined procedures. */
int picolCommandCallProc(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    char *alist=cmd->arglist, *body=cmd->body, *p=xstrdup(alist), *tofree;
    struct picolCallFrame *cf = xmalloc(sizeof(*cf));
    int arity = 0, done = 0, errcode = PICOL_OK;
    char errbuf[1024];
    cf->vars = NULL;
    cf->parent = i->callframe;
    i->callframe = cf;
    tofree = p;
    while(1) {
        char *start = p;
        while(*p != ' ' && *p != '\0') p++;
        if (*p != '\0' && p == start) {
            p++; continue;
        }
        if (p == start) break;
        if (*p == '\0') done=1; else *p = '\0';
        if (++arity > argc-1) goto arityerr;
        if (isupper(start[0])) {
            snprintf(errbuf,sizeof(errbuf),"Procedure parameter '%s' can't be a global (upcase first character)", start);
            goto err;
        }
        picolSetVar(i,start,argv[arity]);
        p++;
        if (done) break;
    }
    free(tofree);
    tofree = NULL;
    if (arity != argc-1) goto arityerr;
    errcode = picolEval(i,body);
    if (errcode == PICOL_RETURN) errcode = PICOL_OK;
    picolDropCallFrame(i); /* remove the called proc callframe */
    return errcode;
arityerr:
    snprintf(errbuf,sizeof(errbuf),"Proc '%s' called with wrong arg num",argv[0]);
err:
    picolSetResult(i,errbuf);
    free(tofree);
    picolDropCallFrame(i); /* remove the called proc callframe */
    return PICOL_ERR;
}

int picolCommandProc(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    if (argc != 4) return picolArityErr(i,argv[0]);

    picolRegisterCommand(i,argv[1],picolCommandCallProc);
    struct picolCmd *c = picolGetCommand(i,argv[1]);
    c->arglist = xstrdup(argv[2]);
    c->body = xstrdup(argv[3]);
    return PICOL_OK;
}

int picolCommandReturn(struct picolInterp *i, int argc, char **argv, struct picolCmd *cmd) {
    if (argc != 1 && argc != 2) return picolArityErr(i,argv[0]);
    picolSetResult(i, (argc == 2) ? argv[1] : "");
    return PICOL_RETURN;
}

void picolRegisterCoreCommands(struct picolInterp *i) {
    picolRegisterCommand(i,"expr",picolCommandExpr);
    picolRegisterCommand(i,"set",picolCommandSet);
    picolRegisterCommand(i,"puts",picolCommandPuts);
    picolRegisterCommand(i,"if",picolCommandIf);
    picolRegisterCommand(i,"while",picolCommandWhile);
    picolRegisterCommand(i,"break",picolCommandRetCodes);
    picolRegisterCommand(i,"continue",picolCommandRetCodes);
    picolRegisterCommand(i,"proc",picolCommandProc);
    picolRegisterCommand(i,"return",picolCommandReturn);
}

/* =============================================================================
 * Main and REPL
 * ========================================================================== */

#ifndef PICOL_NO_MAIN // In case you want to include it as a library.
int main(int argc, char **argv) {
    struct picolInterp *interp = picolInitInterp();
    picolRegisterCoreCommands(interp);
    if (argc == 1) {
        while(1) {
            char clibuf[1024];
            int retcode;
            printf("picol> "); fflush(stdout);
            if (fgets(clibuf,1024,stdin) == NULL) return 0;
            retcode = picolEval(interp,clibuf);
            if (interp->result[0] != '\0')
                printf("[%d] %s\n", retcode, interp->result);
        }
    } else if (argc == 2) {
        char buf[1024*16];
        FILE *fp = fopen(argv[1],"r");
        if (!fp) {
            perror("open"); exit(1);
        }
        size_t bytesRead = fread(buf,1,1024*16-1,fp);
        buf[bytesRead] = '\0';
        fclose(fp);
        if (picolEval(interp,buf) != PICOL_OK) printf("%s\n", interp->result);
    }
    picolFreeInterp(interp);
    return 0;
}
#endif
