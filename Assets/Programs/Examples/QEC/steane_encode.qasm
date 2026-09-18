// Steane [[7,1,3]] encoding of |0>_L from the CSS generator matrix (T09 §4). Checking all six stabilizers
// with ancillas returns 0000000-free syndromes; here we only encode and measure in Z: outcomes are codewords.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[7] q;
bit[7] c;
h q[4]; h q[5]; h q[6];
cx q[6], q[0]; cx q[6], q[1]; cx q[6], q[3];
cx q[5], q[0]; cx q[5], q[2]; cx q[5], q[3];
cx q[4], q[1]; cx q[4], q[2]; cx q[4], q[3];
c = measure q;
