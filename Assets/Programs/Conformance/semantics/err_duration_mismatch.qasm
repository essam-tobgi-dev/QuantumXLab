OPENQASM 3.0;
include "stdgates.inc";
qubit q;
duration d = 10ns + 3;
delay[d] q;
