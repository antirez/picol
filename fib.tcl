proc fib {x} {
    if {$x <= 1} { return $x }
    expr [fib [expr $x-1]] + [fib [expr $x-2]]
}

puts [fib 20]
