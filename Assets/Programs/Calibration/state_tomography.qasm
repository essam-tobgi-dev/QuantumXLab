// State tomography of the Bell pair (|00⟩ + |11⟩)/√2 on ($0, $1): one two-qubit Pauli setting per
// run, so the 3² = 9 settings of T10 §3.1 are the sweep. basis = 3·b($0) + b($1) with b = 0:X, 1:Y,
// 2:Z, i.e. basis 0…8 spells XX, XY, XZ, YX, YY, YZ, ZX, ZY, ZZ with the leftmost letter on $0
// (the repo's index order for Pauli strings). Each factor is rotated into its eigenbasis before the
// computational-basis readout — h for X, sdg then h for Y, nothing for Z — and the runtime
// reconstructs ρ from the nine count records by maximum likelihood (fit mle_state, T10 §3.2 (3.2),
// (3.3)), reporting the fidelity to the target Bell state and the purity. Recipe state_tomography.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 2000
pragma qlab.layout physical
input int basis = 0;
pragma qlab.sweep basis in {0, 1, 2, 3, 4, 5, 6, 7, 8}
bit[2] c;
h $0;
cx $0, $1;
if (basis / 3 == 0) { h $0; }
if (basis / 3 == 1) { sdg $0; h $0; }
if (basis % 3 == 0) { h $1; }
if (basis % 3 == 1) { sdg $1; h $1; }
c[0] = measure $0;
c[1] = measure $1;
