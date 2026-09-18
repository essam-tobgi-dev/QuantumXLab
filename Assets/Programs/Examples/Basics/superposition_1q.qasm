// Single-qubit superposition: ry(theta)|0⟩ = cos(θ/2)|0⟩ + sin(θ/2)|1⟩
// Theory: T01 §3 (Bloch sphere). With theta = pi/3: P(1) = sin²(π/6) = 0.25.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
pragma qlab.probe bloch q[0]
input float theta = pi/3;
qubit q;
bit c;
ry(theta) q;
c = measure q;
