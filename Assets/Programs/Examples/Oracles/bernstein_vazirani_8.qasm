// Bernstein–Vazirani: recover the hidden string s = 0b10110101 in one query.
// Theory: T03 §3. Expected: measurement reads s with certainty.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
const int s = 181; // 0b10110101
qubit[8] x;
qubit anc;
bit[8] c;
x anc;
h anc;
h x;
for int i in [0:7] {
  if (((s >> i) & 1) == 1) { cx x[i], anc; }
}
h x;
c = measure x;
