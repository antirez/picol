proc mandel {cr ci maxiter} {
    set zr 0
    set zi 0
    set n 0
    while {$n < $maxiter} {
        set zr2 [expr $zr * $zr]
        set zi2 [expr $zi * $zi]
        if {[expr $zr2 + $zi2] > 4} { return $n }
        set zi [expr 2 * $zr * $zi + $ci]
        set zr [expr $zr2 - $zi2 + $cr]
        set n [expr $n + 1]
    }
    return $maxiter
}

proc char {n} {
    if {$n == 30} {
        return "#"
    } elseif {$n > 15} {
        return "@"
    } elseif {$n > 10} {
        return "%"
    } elseif {$n > 5} {
        return "+"
    } elseif {$n > 3} {
        return "-"
    } elseif {$n > 1} {
        return "."
    } else {
        return " "
    }
}

set y -1.2
while {$y < 1.2} {
    set x -2.0
    while {$x < 1.0} {
        set n [mandel $x $y 30]
        puts -nonewline [char $n]
        set x [expr $x + 0.0385]
    }
    puts ""
    set y [expr $y + 0.06]
}
