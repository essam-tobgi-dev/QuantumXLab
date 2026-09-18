// Qubit spectroscopy: a long weak saturation tone at a swept drive frequency, then readout. Peak at f01 (recipe qubit_spectroscopy).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 256
pragma qlab.layout physical
pragma qlab.pulse_level on
input float f_d = 4.8e9;
pragma qlab.sweep f_d from 4.75e9 to 4.85e9 step 2.5e5
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
}
defcal saturate $0 {
  set_frequency(df0, f_d);
  play(df0, constant(0.02, 4us));
}
bit c;
saturate $0;
c = measure $0;
