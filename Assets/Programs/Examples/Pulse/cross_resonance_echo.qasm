// Echoed cross-resonance on ($0 control, $1 target): CR(+), x on control, CR(-), x on control (T05 §8).
// With the device's calibrated amplitude this realises ZX(pi/2); with the control in |+> the target shows a Ramsey-like fringe vs amplitude.
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 512
pragma qlab.layout physical
pragma qlab.pulse_level on
input float cr_amp = 0.3;
pragma qlab.sweep cr_amp from 0.0 to 0.6 step 0.05
cal {
  extern port u01;
  extern port d0;
  frame crf = newframe(u01, 5.0e9, 0.0);
}
defcal cr_echo(angle a) $0, $1 {
  play(crf, gaussian_square(a, 200ns, 160ns, 10ns));
  barrier crf;
  shift_phase(crf, pi);
  play(crf, gaussian_square(a, 200ns, 160ns, 10ns));
  shift_phase(crf, -pi);
}
bit[2] c;
cr_echo(cr_amp) $0, $1;
x $0;
c = measure $0, $1;
