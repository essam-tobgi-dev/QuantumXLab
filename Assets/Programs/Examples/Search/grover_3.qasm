// Grover search on 3 qubits for a marked state given as an input (default 5 = |101⟩), 2 iterations.
// Theory: T03 §8. N = 8, M = 1: optimal k = 2, success probability sin²(5θ) with sin θ = 1/√8 → 0.9453.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
input int marked = 5;
qubit[3] q;
bit[3] c;
h q;
for int it in [1:2] {
  // oracle: phase flip |marked⟩
  for int i in [0:2] { if (((marked >> i) & 1) == 0) { x q[i]; } }
  h q[2];
  ccx q[0], q[1], q[2];
  h q[2];
  for int i in [0:2] { if (((marked >> i) & 1) == 0) { x q[i]; } }
  // diffusion
  h q;
  x q;
  h q[2];
  ccx q[0], q[1], q[2];
  h q[2];
  x q;
  h q;
}
c = measure q;
