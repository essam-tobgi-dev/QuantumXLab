// Quantum teleportation of ry(0.7)|0⟩ from q[0] to q[2] with feed-forward corrections.
// Theory: T03 §11. Expected: q[2] measured in the computational basis gives P(1) = sin²(0.35) ≈ 0.1176.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
qubit[3] q;
bit[2] m;
bit out;
ry(0.7) q[0];
h q[1];
cx q[1], q[2];
cx q[0], q[1];
h q[0];
m[0] = measure q[0];
m[1] = measure q[1];
if (m[1] == 1) { x q[2]; }
if (m[0] == 1) { z q[2]; }
out = measure q[2];
