// Rabi chevron: drive amplitude × pulse duration at a fixed detuning; P1 = (Ω²/Ω'²) sin²(Ω' t/2) (T07 §2).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 256
pragma qlab.layout physical
input float amp = 0.5;
input duration width = 40ns;
pragma qlab.sweep amp from 0.05 to 1.0 step 0.05
pragma qlab.sweep width in {16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256}
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal drive(angle a) $0 { play(df0, constant(a, width)); }
bit c;
drive(amp) $0;
c = measure $0;
