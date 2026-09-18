// QAOA p = 1 for MaxCut on the 4-ring (edges 01, 12, 23, 30). The cut operator
// C = Σ_edges (1 - Z_iZ_j)/2 enters as exp(-2iγC), i.e. rzz(-2γ) = cx, rz(-2γ), cx per edge;
// rzz(+2γ) would amplify the *minimum* cut. Mixer: rx(2β).
// Theory: T03 §9.2. At (γ, β) = (π/8, π/8): <C> = 3.0 of max 4, with 0101 and 1010 dominating at
// 0.2656 each.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
input float gamma = 0.3927;
input float beta = 0.3927;
pragma qlab.sweep gamma from 0.0 to 1.5708 step 0.19635
pragma qlab.sweep beta from 0.0 to 0.7854 step 0.19635
qubit[4] q;
bit[4] c;
h q;
for int i in [0:3] {
  cx q[i], q[(i+1) % 4];
  rz(-2 * gamma) q[(i+1) % 4];
  cx q[i], q[(i+1) % 4];
}
rx(2 * beta) q;
c = measure q;
