// Ramsey (T2*): sx, free evolution with a software detuning via rz, sx, measure.
// Fit: A exp(-t/T2*) cos(2π δ t + φ) + c with δ the intentional detuning (recipe t2_ramsey).
// t_delay is in µs and detuning_MHz in MHz, so their product in the rz angle is dimensionless.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
pragma qlab.layout physical
input float t_delay = 1.0;
input float detuning_MHz = 0.5;
pragma qlab.sweep t_delay from 0 to 60 step 1
bit c;
sx $0;
delay[t_delay * 1us] $0;
rz(2 * pi * detuning_MHz * t_delay) $0;
sx $0;
c = measure $0;
