OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.layout physical
cal { extern port d0; frame df0 = newframe(d0, 4.8e9, 0.0); waveform w = drag(0.4, 32ns, 8ns, 0.1); }
defcal x $0 { play(df0, w); }
bit c;
x $0;
c = measure $0;
