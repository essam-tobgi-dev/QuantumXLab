OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.layout physical
cal { extern port d0; h d0; }
bit c;
x $0;
