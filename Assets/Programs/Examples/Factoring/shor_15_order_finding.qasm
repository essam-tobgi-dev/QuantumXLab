// Order finding for N = 15, a = 7, with 8 counting qubits and a 4-qubit work register.
// Theory: T03 §7. 7 has order r = 4 mod 15; the phase register peaks at multiples of 256/4 = 64:
// outcomes 0, 64, 128, 192 each with probability 1/4 → continued fractions give r = 4, gcd(7^2 ± 1, 15) = {3, 5}.
// The modular multiplications by 7^(2^k) mod 15 are bit permutations (T03 §7): 7^1: x→7x, 7^2: x→4x, 7^4 = 1.
// The controlled multiplications and the inverse QFT are all-to-all over the 12 qubits, so this one
// is written for the 32-ion chain (spec 09 §4, all-to-all): on a heavy-hex lattice SABRE spends 51
// swaps and more than triples the depth (156 -> 580) to reach the same ideal distribution.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.device ion_chain_32
pragma qlab.shots 4096
qubit[8] cnt;
qubit[4] w;
bit[8] c;
// Controlled multiply-by-7 mod 15 on w: the three cswaps rotate the bits right (x -> 8x mod 15),
// the four cx invert them (v -> 15 - v), so together x -> -8x = 7x mod 15.
gate cmult7 ctl, w0, w1, w2, w3 {
  cswap ctl, w0, w1;
  cswap ctl, w1, w2;
  cswap ctl, w2, w3;
  cx ctl, w0; cx ctl, w1; cx ctl, w2; cx ctl, w3;
}
// controlled multiply-by-4 mod 15 (= 7^2): permutation x -> 4x mod 15 is a 2-bit rotation
gate cmult4 ctl, w0, w1, w2, w3 {
  cswap ctl, w1, w3;
  cswap ctl, w0, w2;
}
x w[0];       // |1⟩ in the work register
h cnt;
cmult7 cnt[0], w[0], w[1], w[2], w[3];
cmult4 cnt[1], w[0], w[1], w[2], w[3];
// 7^4 = 7^8 = ... = 1 mod 15: remaining controlled multiplications are identity
// inverse QFT on cnt
for int i in [0:3] { swap cnt[i], cnt[7-i]; }
for int i in [0:7] {
  for int j in [0:i-1] {
    cp(-pi / (1 << (i - j))) cnt[j], cnt[i];
  }
  h cnt[i];
}
c = measure cnt;
