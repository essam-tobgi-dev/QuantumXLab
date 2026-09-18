// Deutsch–Jozsa with a 4-bit balanced oracle f(x) = x0 ⊕ x2 (phase kickback on the ancilla).
// Theory: T03 §2. Expected: the query register never reads 0000 for a balanced oracle.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[4] x;
qubit anc;
bit[4] c;
x anc;
h anc;
h x;
cx x[0], anc;
cx x[2], anc;
h x;
c = measure x;
