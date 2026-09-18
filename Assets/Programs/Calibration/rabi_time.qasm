// Rabi time scan: a fixed-amplitude drive of swept length on the drive frame of $0, detuned from
// f01 by `detuning` (MHz). P1(t) = (Ω/Ω')² sin²(Ω' t/2) with Ω' = √(Ω² + Δ²) (T07 §2), a decaying
// oscillation at f = Ω'/2π: fit rabi_time A cos(2π f t + φ) e^(-t/τ) + c and report f, τ.
// Recipes: rabi_time (detuning = 0, f = Ω/2π is the Rabi rate) and chevron (detuning swept, giving
// the chevron pattern whose ridge sits at Δ = 0).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 500
pragma qlab.layout physical
pragma qlab.pulse_level on
input duration t_pulse = 40ns;
input float detuning = 0.0;
pragma qlab.sweep t_pulse from 8 to 400 step 8
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal rabi_drive $0 {
  set_frequency(df0, 4.8e9 + detuning * 1e6);
  play(df0, constant(0.5, t_pulse));
}
bit c;
rabi_drive $0;
c = measure $0;
