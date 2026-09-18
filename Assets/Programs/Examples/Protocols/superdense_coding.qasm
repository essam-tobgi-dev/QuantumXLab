// Superdense coding: two classical bits (message = 0b10) sent with one qubit over a shared Bell pair.
// Theory: T03 §10.2. Bob's decoding leaves b1 on q[0] and b0 on q[1], so the bits are stored as
// c[1] = b1, c[0] = b0 and the register reads the message back. Expected: "10" with certainty.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
input int message = 2;
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
if (message == 1 || message == 3) { x q[0]; }
if (message == 2 || message == 3) { z q[0]; }
cx q[0], q[1];
h q[0];
c[1] = measure q[0];
c[0] = measure q[1];
