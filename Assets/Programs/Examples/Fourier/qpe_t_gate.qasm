// Phase estimation of T = diag(1, e^{iπ/4}) on eigenstate |1⟩ with 4 counting qubits.
// Theory: T03 §6. Eigenphase φ = 1/8 → binary 0.0010 → counting register reads 0010 exactly.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[4] cnt;
qubit psi;
bit[4] c;
x psi;
h cnt;
// controlled-T^(2^k) on counting qubit k: T^m = p(m*pi/4)
for int k in [0:3] {
  cp((1 << k) * pi / 4) cnt[k], psi;
}
// inverse QFT on cnt (little-endian: cnt[0] is the least significant estimate bit)
swap cnt[0], cnt[3];
swap cnt[1], cnt[2];
for int i in [0:3] {
  for int j in [0:i-1] {
    cp(-pi / (1 << (i - j))) cnt[j], cnt[i];
  }
  h cnt[i];
}
c = measure cnt;
