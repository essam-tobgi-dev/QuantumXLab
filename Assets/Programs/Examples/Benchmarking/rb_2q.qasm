// Two-qubit RB on (q[0], q[1]); r = (1-p)·3/4 (T10 §2).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 512
pragma qlab.rb 2 1 2 4 8 16 32 64 10
qubit[2] q;
bit[2] c;
