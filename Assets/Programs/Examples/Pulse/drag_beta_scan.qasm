// DRAG beta scan on the Lindblad backend: leakage to |2> vs beta (T05 §7). Minimum leakage near beta = -1/alpha·(scale).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 256
pragma qlab.layout physical
pragma qlab.pulse_level on
pragma qlab.probe state
input float beta = 0.0;
pragma qlab.sweep beta from -1.0 to 1.0 step 0.1
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal xb(angle b) $0 { play(df0, drag(0.5, 24ns, 6ns, b)); }
bit c;
xb(beta) $0;
c = measure $0;
