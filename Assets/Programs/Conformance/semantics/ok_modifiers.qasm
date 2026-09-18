OPENQASM 3.0;
include "stdgates.inc";
qubit[3] q;
ctrl @ x q[0], q[1];
negctrl @ ctrl @ rz(0.3) q[0], q[1], q[2];
inv @ s q[0];
pow(2) @ t q[1];
pow(0.5) @ rx(pi) q[2];
ctrl(2) @ x q[0], q[1], q[2];
