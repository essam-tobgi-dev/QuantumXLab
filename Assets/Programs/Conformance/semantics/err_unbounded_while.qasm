OPENQASM 3.0;
include "stdgates.inc";
qubit q;
float x = 0.5;
while (x < 3) { h q; }
