// DRAG β calibration: the sequence (x, -x)^n amplifies phase errors; sweep β and find the null (recipe drag_beta; T05 §7).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 1024
pragma qlab.layout physical
input float beta = 0.0;
pragma qlab.sweep beta from -1.0 to 1.0 step 0.1
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal xp(angle b) $0 { play(df0, drag(0.5, 32ns, 8ns, b)); }
defcal xm(angle b) $0 { shift_phase(df0, pi); play(df0, drag(0.5, 32ns, 8ns, b)); shift_phase(df0, -pi); }
bit c;
sx $0;
for int i in [1:4] {
  xp(beta) $0;
  xm(beta) $0;
}
c = measure $0;
