// Quantum Fourier transform on 5 qubits applied to |00101⟩ (x = 5).
// Theory: T03 §5. Expected: uniform magnitudes 1/√32 with phases e^{2πi·5k/32}; measured counts uniform.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
qubit[5] q;
bit[5] c;
x q[0];
x q[2];
for int i in [4:-1:0] {
  h q[i];
  for int j in [i-1:-1:0] {
    cp(pi / (1 << (i - j))) q[j], q[i];
  }
}
swap q[0], q[4];
swap q[1], q[3];
c = measure q;
