// Rabi amplitude calibration: a fixed-length DRAG pulse with swept amplitude on the drive frame of $0.
// Fit: P1(a) = A cos(π a / a_π) + c → a_π is the π-pulse amplitude (recipe rabi_amp; T07 §4).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 1024
pragma qlab.layout physical
input float amp = 0.5;
pragma qlab.sweep amp from 0.0 to 1.0 step 0.02
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal rabi_pulse(angle a) $0 {
  play(df0, drag(a, 32ns, 8ns, 0.0));
}
bit c;
rabi_pulse(amp) $0;
c = measure $0;
