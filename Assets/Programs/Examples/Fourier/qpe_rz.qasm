// Phase estimation of p(2π·phi) with a sweepable eigenphase; 4 counting qubits.
// Theory: T03 §6. For phi = 0.375 = 0.0110b the register reads 0110 with certainty; other phi spread over neighbours.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2048
input float phi = 0.375;
pragma qlab.sweep phi from 0.0 to 0.9375 step 0.0625
qubit[4] cnt;
qubit psi;
bit[4] c;
x psi;
h cnt;
for int k in [0:3] {
  cp((1 << k) * 2 * pi * phi) cnt[k], psi;
}
swap cnt[0], cnt[3];
swap cnt[1], cnt[2];
for int i in [0:3] {
  for int j in [0:i-1] {
    cp(-pi / (1 << (i - j))) cnt[j], cnt[i];
  }
  h cnt[i];
}
c = measure cnt;
