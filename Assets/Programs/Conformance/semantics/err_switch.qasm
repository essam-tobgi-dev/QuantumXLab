OPENQASM 3.0;
include "stdgates.inc";
qubit q;
int k = 1;
switch (k) { case 1 { x q; } default { z q; } }
