OPENQASM 3.0;
include "stdgates.inc";
qubit[2] q;
bit m;
h q[0];
m = measure q[0];
if (m == 1) { x q[1]; }
while (m == 0) { reset q[0]; h q[0]; m = measure q[0]; }
