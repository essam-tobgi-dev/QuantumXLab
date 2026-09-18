// Phase kickback: a controlled phase on a |−⟩ target kicks the phase back onto the control.
// Theory: T02 §8 (identities). Control ends in |1⟩ deterministically: cp(pi) with target |1⟩ flips control's H-basis.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[2] q;
bit c;
x q[1];
h q[0];
cp(pi) q[0], q[1];
h q[0];
c = measure q[0];
