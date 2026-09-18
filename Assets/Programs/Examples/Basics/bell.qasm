// Bell state |Φ+⟩ = (|00⟩ + |11⟩)/√2
// Theory: T01 §5 (entanglement), T02 §3 (CNOT). Expected: counts 00 and 11 at 0.5 each.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
c = measure q;
