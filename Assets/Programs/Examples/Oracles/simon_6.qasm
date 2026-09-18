// Simon's problem, n = 3 (6 qubits), hidden period s = 110: f(x) = f(x ⊕ s).
// Theory: T03 §4. Expected: every outcome y satisfies y·s = 0 (mod 2): y ∈ {000, 001, 110, 111}.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[3] x;
qubit[3] y;
bit[3] c;
h x;
// oracle: y = f(x) with f(x) = x with bit1 xor bit2 folded so that f(x) = f(x ^ 110)
cx x[0], y[0];
cx x[1], y[1];
cx x[2], y[1];
cx x[1], y[2];
cx x[2], y[2];
h x;
c = measure x;
