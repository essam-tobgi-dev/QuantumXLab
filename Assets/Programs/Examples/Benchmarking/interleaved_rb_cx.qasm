// Interleaved RB: reference two-qubit Clifford sequences and sequences interleaved with cx.
// r_cx = (1 - p_int/p_ref)·3/4 (T10 §2.2). The runtime runs both families from this one program.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 512
pragma qlab.rb 2 1 2 4 8 16 32 10
pragma qlab.assert interleave == cx
qubit[2] q;
bit[2] c;
