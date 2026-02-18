Picol is a Tcl-alike interpreter in 500 lines of code (true for the original version 1, a bit more for version 2) that I initially released 15th of March 2007. Recently I looked at the source code and realized this was a better C programming example than what I recalled, so I'm putting this on GitHub to archive it, together with the main points of the original article.

**Update:** in February 2026 the interpreter was updated with a few significant changes, I call this version **Picol 2**. You can find the original version in the commits history of this repository, however, the second version is almost as small as the first, but more instructive, less buggy, and more useful, so this `README` file now refers to the details of the second version. You can find a set of changes between v1 and v2 later in this file.

## Rules
When I built this code, I had some rules in mind:

* I wanted to use more or less my normal C style. In Picol you'll find normal C spacing and even comments.
* I wanted to write an interpreter with a design similar to a real one. One of the few useful things you can do with Picol is to learn how to write a Tcl interpreter if you are a newbie programmer, I guess, so the point was to write a simple to understand program, not just a *short* program. Short and understandable are very distinct things.
* The resulting interpreter should be able to run some kind of non trivial program: to just set few vars and print hello world was not an option. For instance, check the `mandelbrot.tcl` file in this repository: it is a short but real program.

## The resulting interpreter: Picol

The parser is very similar to the Tcl one, Picol supports interpolation as well, for example you can write:

```Tcl
set a "pu"
set b {ts}
$a$b "Hello World!"
```

Note that Picol has an interactive shell! So just launch it without arguments to start playing (to compile the code use `gcc -O2 -Wall -o picol picol.c`).

To run a program stored in a file, use: `picol filename.tcl`.

Probably the parser could be rewritten in order to take less space, currently it takes almost 250 lines of code: this is too much and leaves little room for all the rest. On the other side, it's a decent example about writing parsers by hand.

A raw list of the supported features:

* Interpolation, as seen above. You can also write `"2+2 = [expr 2+2]"` or `"My name is: $foobar"`.
* Procedures, with return. Like Tcl if return is missing the result of the last command executed is returned.
* `if`, `if ... elseif ... else ...`, `while` with `break` and `continue`.
* Recursion.
* Variables inside procedures are limited in scope like Tcl, i.e. there are real call frames in Picol.
* The interpreter has an `expr` implementation, and `if` and `while` both accept an expression as first argument. However Picol `expr` is not able to perform variables and commands interpolation, so please use `expr $a+$b` and not `expr {$a+$b}`.
* Global variables: if the variable name starts with a capital letter, the scope is global. Otherwise it is local.

This is an example of programs Picol can run:

```Tcl
proc fib {x} {
    if {$x <= 1} {
        return $x
    }
    expr [fib [expr $x-1]] + [fib [expr $x-2]]
}

puts [fib 20]
```

Or:

```
proc square {x} {
    expr $x * $x
}

set a 1
while {$a < 10} {
    if {$a == 5} {
        puts {Missing five!}
        set a [expr $a+1]
        continue
    }
    puts "I can compute that $a*$a = [square $a]"
    set a [expr $a+1]
}
```

## Picol v2

Version 2 added the following features without making the source code much larger or more complicated:

* Removed malloc() OOM checks and pointless recovery in this case. Now aborting wrappers are used instead: xmalloc() and xrealloc(). This allowed to reclaim a few tens of lines of code, later used to implement other features. Also, in this specific case, OOM recovery complicated the code driving away the attention of the reader on such details.
* A working `expr`! Also used for `if` and `while` conditions.
* Numbers are now floats, not integers. More real world programs can be written.
* Puts has a `-nonewline` option, like Tcl.
* `if` now supports `elseif` chains.
* Globals introduced (any variable starting with capital letter).
* picolFreeInterp() added so that in theory the project is usable as a library.
* Minimal escapes processing in strings, things like `\t`, `\n`, ...
* `set` with a single argument now works.
* The code is more commented and readable.
* Bugs were fixed.

The limit of the 500 lines of code is no longer strictly respected in this version. Currently `loc` reports 669 lines.

## Design

**Parsing**. The first important part you see in the source code is a hand written parser. The main function of the parser is `picolGetToken` that just calls functions able to parse the different parts of a Tcl program and return in the parsing structure the type of the token and start/end pointers in order to extract it.

**Eval**. This parsing function is in turn used by `picolEval` in order to execute the program. Every token is used either to form a new argument if a separator token was found before, or concatenated to the last argument (this is how interpolation is performed in Picol). Once an EOL (end of line) token is returned picolEval will call the command looking it up in a linked list of commands stored inside the interpreter structure.

**Substitutions**. Variables and commands substitution is performed by `picolEval` itself. The parser is able to return variables and commands tokens already stripped by `$` and `[]`, so all that's required is to lookup the variable in the call frame and substitute the value with the token, or to recursively call `picolEval` if it's a command substitution, using the result instead of the original token.

**C-coded and user defined commands**. Commands are described by a name and a pointer to a C function implementing the command. In the command structure there is also a pointer used in order to store the procedure arguments list and body. This makes you able to implement multiple Picol commands using a single C function. User defined procedures are just like commands, but they are implemented by passing the argument list and the body of the procedure as additional pointers, so a single C function is able to implement all the existing user defined procedures.

**Call frames**. Procedures call is trivial. The interpreter structure contains a call frame structure having more or less just a pointer to a linked list of variables (that are in turn structures with two fields: name and value). When a procedure is called a new call frame is created and put at the top of the old one. When the procedure returns the top call frame is destroyed.

## Expr implementation

The usual way to implement an expression evaluator in C is to use two stacks, one for the operators and one for the operands. This is efficient and straightforward to implement, but here the need was to write a good enough Tcl `expr` in a lot less space (41 lines of code in total). So I resorted to a *Pratt style parser*, likely the most compact way to write a parser as a recursive function. In this parser the grammar is modeled as a set of mutually recursive functions, but in this specific case it is just *a single* recursive function.

The evaluator works in two steps (three in the code comments, to simplify the understanding even more, but they are logically two):

1. We parse the left operand, with special handling for unary operators, since the parser is written for binary infix operators like a + b, so special handling for unary simplifies it.
2. Then we parse the operator, and check its precedence.
3. If the precedence is greater than the previous (the calling recursive function) operator, we parse the second operand with a recursive function: this will, in turn, greedily process all the operators on the right that have a precedence greater than the current operator, and so forth.

Final trick: there was no space for `expr` to handle `$var` and `[commands]` interpolation, so for functions like `if` and `while` a trick is used, their expression argument is rewritten in the form of `expr <expression>`, and passed to picolEval(). This way, we can reuse the common code path, and if {$a > $b} will be like if [expr $a > $b], which is fine (but `&&` and `||` short circuits will not work).

## Limitations

Picol is a very simple code base that allows the willing programmer to get exposed to interpreters with the minimum amount of complexity. However, because of the simplicity and code size, the interpreter has many limitations:

* Values are not just *semantically* strings, they are really represented as strings, forcing a continuous conversion and string manipulation gym. Moreover, code is continuously reparsed to be interpreted. So Picol is not the fastest language out there: on mixed workloads it is about 5-10 times slower than Tcl. Surprisingly, Picol can be faster in certain pathological cases, like in the case of the `mandelbrot.tcl` program, where unbraced `expr` math creates many issues to the Tcl official implementation (if you put all the expressions into braces then Tcl can execute the same script 25x faster). Notably, Picol is 4x faster at startup, so when the task at hand is running small/fast programs, it can be faster than Tcl. I must admit I expected much worse!
* `expr` is not able to handle variables and commands interpolation, so while `[expr $a+$b]` works, `[expr {$a+$b}]` will not.
* The interpreter lacks a list type. Since Tcl is an homoiconic programming language, this does not just mean you don't have a serious type to perform computation, but also that `uplevel` and other commands that are based on the ability to execute Tcl programs composed at runtime would be hard to handle.
* Picol is not a strict subset of Tcl. Globals variables are handled differently, `expr` does not expand vars and commands, and a few more corner cases. Yet it is similar enough that, for instance, the `mandelbrot.tcl` example in this repository can be executed by both Tcl and Picol.
* And... many others, you get the idea.

For a more compelling example check the [Jim TCL](https://jim.tcl.tk/home/doc/www/www/index.html) interpreter. I started this project myself many years ago, and it was continued by a community of developers in the embedded space. It shows how to go from Picol to a more serious and performing interpreter.

## License

This program is released under the BSD two clause license.

**Inside every large program there is a small program trying to get out -- Sir Tony Hoare.**
