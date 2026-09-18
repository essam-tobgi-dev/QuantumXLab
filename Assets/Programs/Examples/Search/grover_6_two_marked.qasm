// Grover search on 6 qubits with two marked states |000011⟩ and |110000⟩ (M = 2, N = 64), 4 iterations.
// Theory: T03 §8: θ = arcsin(√(2/64)); k_opt = floor(π/4·√32) = 4 → P = sin²(9θ) ≈ 0.9992 (≈ 0.4996 per
// marked state); iterations = 5 over-rotates to P = sin²(11θ) ≈ 0.860. `iterations` is a const because
// `for` ranges are unrolled at compile time and must fold (spec 13 §3).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
const int iterations = 4;
qubit[6] q;
qubit[4] anc;
bit[6] c;
gate mcz_6 a0, a1, a2, a3, a4, a5, w0, w1, w2, w3 {
  ccx a0, a1, w0; ccx a2, w0, w1; ccx a3, w1, w2; ccx a4, w2, w3;
  cz w3, a5;
  ccx a4, w2, w3; ccx a3, w1, w2; ccx a2, w0, w1; ccx a0, a1, w0;
}
h q;
for int it in [1:iterations] {
  // oracle for 000011: flip where bits 2..5 are 0
  x q[2]; x q[3]; x q[4]; x q[5];
  mcz_6 q[0], q[1], q[2], q[3], q[4], q[5], anc[0], anc[1], anc[2], anc[3];
  x q[2]; x q[3]; x q[4]; x q[5];
  // oracle for 110000: flip where bits 0..3 are 0
  x q[0]; x q[1]; x q[2]; x q[3];
  mcz_6 q[0], q[1], q[2], q[3], q[4], q[5], anc[0], anc[1], anc[2], anc[3];
  x q[0]; x q[1]; x q[2]; x q[3];
  // diffusion
  h q; x q;
  mcz_6 q[0], q[1], q[2], q[3], q[4], q[5], anc[0], anc[1], anc[2], anc[3];
  x q; h q;
}
c = measure q;
