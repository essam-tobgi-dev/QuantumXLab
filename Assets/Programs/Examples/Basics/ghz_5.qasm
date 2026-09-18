// GHZ state on 5 qubits: (|00000⟩ + |11111⟩)/√2
// Theory: T01 §5. Expected: only 00000 and 11111, 0.5 each.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2048
qubit[5] q;
bit[5] c;
h q[0];
for int i in [0:3] { cx q[i], q[i+1]; }
c = measure q;
