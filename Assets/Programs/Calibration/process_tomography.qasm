// Process tomography of cx ($0 control, $1 target): the 4² = 16 product input states of T10 §4.2
// times the 3² = 9 measurement settings of T10 §3.1 = 144 configurations, swept as one input
// config = 9·prep + basis (basis varies fastest):
//   prep  = config / 9  (0…15): p($0) = prep % 4, p($1) = prep / 4, with 0:|0⟩ 1:|1⟩ 2:|+⟩ 3:|+i⟩
//                               (|+⟩ = h|0⟩, |+i⟩ = s·h|0⟩)
//   basis = config % 9  (0…8):  b($0) = basis / 3, b($1) = basis % 3, with 0:X 1:Y 2:Z, rotated
//                               into the eigenbasis by h (X), sdg then h (Y), nothing (Z)
// The runtime reconstructs the χ matrix from the 144 count records by maximum likelihood and
// reports F_e = χ₀₀ and the average gate fidelity (fit mle_process, T10 §4.1 (4.1), §1.3 (1.3)).
// Recipe process_tomography.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1000
pragma qlab.layout physical
input int config = 0;
pragma qlab.sweep config from 0 to 143 step 1
bit[2] c;
if ((config / 9) % 4 == 1) { x $0; }
if ((config / 9) % 4 == 2) { h $0; }
if ((config / 9) % 4 == 3) { h $0; s $0; }
if ((config / 9) / 4 == 1) { x $1; }
if ((config / 9) / 4 == 2) { h $1; }
if ((config / 9) / 4 == 3) { h $1; s $1; }
cx $0, $1;
if ((config % 9) / 3 == 0) { h $0; }
if ((config % 9) / 3 == 1) { sdg $0; h $0; }
if ((config % 9) % 3 == 0) { h $1; }
if ((config % 9) % 3 == 1) { sdg $1; h $1; }
c[0] = measure $0;
c[1] = measure $1;
