// ZZ coupling from a conditional Ramsey on $0: sx – idle – rz(2π δ t) – sx with the spectator $1
// left in |0⟩ (spectator = 0) or excited to |1⟩ (spectator = 1). Both qubits idle inside the same
// delay, so the always-on ZZ term H_ZZ = (ħζ/4) Z⊗Z (T05 §5.4 (5.3)) accumulates and shifts $0's
// precession by −ζ/2 (spectator |0⟩) or +ζ/2 (|1⟩), a difference of ζ. Fit ramsey
// A e^(-t/T2*) cos(2π δ t + φ) + c for each spectator value; the
// difference of the fitted detunings is the static ZZ rate, ζ/2π = δ(1) − δ(0) (T10 §8.7 (8.5)).
// Recipe zz_ramsey. t_delay is in µs and detuning_MHz in MHz, so their product in the rz angle is
// dimensionless; the software detuning keeps both curves oscillating well inside T2*.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1000
pragma qlab.layout physical
input float t_delay = 1.0;
input int spectator = 0;
input float detuning_MHz = 0.2;
pragma qlab.sweep t_delay from 0.02 to 20 step 0.1998
pragma qlab.sweep spectator in {0, 1}
bit c;
if (spectator == 1) { x $1; }
sx $0;
delay[t_delay * 1us] $0, $1;
rz(2 * pi * detuning_MHz * t_delay) $0;
sx $0;
c = measure $0;
