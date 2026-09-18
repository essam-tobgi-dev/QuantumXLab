// Readout-resonator spectroscopy: sweep the measurement frame frequency and record the digitizer IQ magnitude.
// Fit: notch resonator S21 (recipe resonator_spectroscopy; T05 §6.3, spec 12 §5).
// `power` is the drive power in dBm at the chip, amplitude = 10^(P/20) with 0 dBm = full scale;
// recipe punch_out sweeps it from -60 to 0 dBm to watch the resonance move by 2χ (T05 §6.2).
OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.shots 256
pragma qlab.layout physical
pragma qlab.pulse_level on
input float f_ro = 7.1e9;
input float power = -26.0;
pragma qlab.sweep f_ro from 7.09e9 to 7.11e9 step 5e4
cal {
  extern port m0;
  frame mf0 = newframe(m0, 7.1e9, 0.0);
}
defcal measure $0 -> bit {
  set_frequency(mf0, f_ro);
  play(mf0, constant(10.0 ** (power / 20.0), 2us));
  return capture_v2(mf0, 2us);
}
bit c;
c = measure $0;
