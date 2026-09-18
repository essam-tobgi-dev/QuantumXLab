OPENQASM 3.0;
include "stdgates.inc";
qubit q;
bit c;
h q;
c = measure q;
