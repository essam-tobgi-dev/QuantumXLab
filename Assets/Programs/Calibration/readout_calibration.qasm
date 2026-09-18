// Readout assignment calibration: prepare |0⟩ or |1⟩ (input prep) and measure; yields the assignment matrix M (recipe readout_calibration; T10 §9).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 4096
pragma qlab.layout physical
input int prep = 0;
pragma qlab.sweep prep in {0, 1}
bit c;
if (prep == 1) { x $0; }
c = measure $0;
