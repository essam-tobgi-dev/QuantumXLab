OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.device sc_fixed_5
pragma qlab.backend statevector
pragma qlab.shots 100
pragma qlab.seed 42
pragma qlab.noise ideal
pragma qlab.layout trivial
pragma qlab.routing sabre
pragma qlab.optimize 2
pragma qlab.snapshot_cadence barrier
input float a = 0.1;
pragma qlab.sweep a from 0 to 1 step 0.5
pragma qlab.probe entanglement q[0] q[1]
qubit[2] q;
bit[2] c;
rx(a) q[0];
cx q[0], q[1];
c = measure q;
