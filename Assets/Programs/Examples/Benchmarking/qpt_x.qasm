// Process tomography of x: 4 input states (0:|0>, 1:|1>, 2:|+>, 3:|+i>) × 3 measurement bases; chi-matrix reconstruction (T10 §5).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2048
input int prep = 0;
input int basis = 0;
pragma qlab.sweep prep in {0, 1, 2, 3}
pragma qlab.sweep basis in {0, 1, 2}
qubit q;
bit c;
if (prep == 1) { x q; }
if (prep == 2) { h q; }
if (prep == 3) { h q; s q; }
x q;
if (basis == 1) { h q; }
if (basis == 2) { sdg q; h q; }
c = measure q;
