// A user-supplied calibration for x on $0 overriding the device pulse: a longer, weaker DRAG pulse
// carrying the same pi area (area theorem, T07 (7.2)) — 40 ns instead of the calibrated length, so
// the amplitude comes down in proportion. Runs pulse-level on the Lindblad backend (spec 07 §5).
// P1 falls short of 1 by the readout error of this chain, not by a pulse error.
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 1024
pragma qlab.layout physical
pragma qlab.pulse_level on
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal x $0 {
  play(df0, drag(0.2795, 40ns, 10ns, 0.18));
}
bit c;
x $0;
c = measure $0;
