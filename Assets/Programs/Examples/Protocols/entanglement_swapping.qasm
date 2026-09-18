// Entanglement swapping: Bell pairs (0,1) and (2,3); a Bell measurement on (1,2) entangles (0,3).
// Theory: T03 §12. After corrections, q[0] and q[3] are in |Φ+⟩: outcomes 00/11 only.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2048
qubit[4] q;
bit[2] m;
bit[2] c;
h q[0]; cx q[0], q[1];
h q[2]; cx q[2], q[3];
cx q[1], q[2];
h q[1];
m[0] = measure q[1];
m[1] = measure q[2];
if (m[1] == 1) { x q[3]; }
if (m[0] == 1) { z q[3]; }
c[0] = measure q[0];
c[1] = measure q[3];
