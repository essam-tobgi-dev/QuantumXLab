OPENQASM 3.0;
include "stdgates.inc";
const int n = 3;
qubit[3] q;
bit[3] c;
int k = 0;
float f = 1.5e-3;
angle[16] a = pi/7;
duration d = 100ns;
bool b = true;
gate g(t) x { rz(t) x; }
def f2(qubit r, int m) -> bit { bit r0; r0 = measure r; return r0; }
g(0.1) q[0];
box [500ns] { barrier q; delay[d] q[1]; }
for int i in [0:n-1] { x q[i]; }
while (k < 2) { k += 1; }
if (b) { z q[0]; } else { y q[0]; }
c[0] = f2(q[2], k);
measure q[0] -> c[1];
c[2] = measure q[1];
reset q;
end;
