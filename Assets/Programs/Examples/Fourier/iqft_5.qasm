// Inverse QFT: QFT followed by QFT† returns the input |00101⟩.
// Theory: T03 §5. Expected: 00101 with certainty.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
gate qft5 a0, a1, a2, a3, a4 {
  h a4; cp(pi/2) a3, a4; cp(pi/4) a2, a4; cp(pi/8) a1, a4; cp(pi/16) a0, a4;
  h a3; cp(pi/2) a2, a3; cp(pi/4) a1, a3; cp(pi/8) a0, a3;
  h a2; cp(pi/2) a1, a2; cp(pi/4) a0, a2;
  h a1; cp(pi/2) a0, a1;
  h a0;
  swap a0, a4; swap a1, a3;
}
qubit[5] q;
bit[5] c;
x q[0];
x q[2];
qft5 q[0], q[1], q[2], q[3], q[4];
inv @ qft5 q[0], q[1], q[2], q[3], q[4];
c = measure q;
