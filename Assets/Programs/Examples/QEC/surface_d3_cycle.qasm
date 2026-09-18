// One syndrome-extraction round of the rotated distance-3 surface code (9 data + 8 ancilla qubits),
// with the stabilizers and CNOT orders of Assets/QEC/surface_rot_3.json (T09 §5): data d[0..8]
// row-major, X checks on the left/right boundaries, Z checks on top/bottom. X ancillas x[0..3] are
// the controls of their CNOTs, Z ancillas z[0..3] the targets.
// Ideal backend from |0>^9: every Z syndrome is 0 (|0>^9 is a +1 eigenstate of the Z checks, which
// commute with the X checks); the four X syndromes are uniform over 16 values.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
qubit[9] d;
qubit[4] x;
qubit[4] z;
bit[4] sx_;
bit[4] sz_;
h x;
// X-type checks (ancilla is control): plaquette {0,1,3,4}, right {2,5}, left {3,6}, plaquette {4,5,7,8}
cx x[0], d[1]; cx x[0], d[4]; cx x[0], d[0]; cx x[0], d[3];
cx x[1], d[2]; cx x[1], d[5];
cx x[2], d[3]; cx x[2], d[6];
cx x[3], d[5]; cx x[3], d[8]; cx x[3], d[4]; cx x[3], d[7];
// Z-type checks (data is control): top {0,1}, plaquette {1,2,4,5}, plaquette {3,4,6,7}, bottom {7,8}
cx d[1], z[0]; cx d[0], z[0];
cx d[2], z[1]; cx d[1], z[1]; cx d[5], z[1]; cx d[4], z[1];
cx d[4], z[2]; cx d[3], z[2]; cx d[7], z[2]; cx d[6], z[2];
cx d[8], z[3]; cx d[7], z[3];
h x;
sx_ = measure x;
sz_ = measure z;
