OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.sweep t from 0 to 1 step 0.1
qubit q;
x q;
