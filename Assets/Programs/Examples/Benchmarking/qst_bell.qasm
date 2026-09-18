// State tomography of a Bell pair: measurement basis chosen by inputs (0 = Z, 1 = X, 2 = Y) on each qubit;
// the runtime sweep produces the 9 settings and reconstructs rho by linear inversion / MLE (T10 §4).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2048
input int basis0 = 0;
input int basis1 = 0;
pragma qlab.sweep basis0 in {0, 1, 2}
pragma qlab.sweep basis1 in {0, 1, 2}
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
if (basis0 == 1) { h q[0]; }
if (basis0 == 2) { sdg q[0]; h q[0]; }
if (basis1 == 1) { h q[1]; }
if (basis1 == 2) { sdg q[1]; h q[1]; }
c = measure q;
