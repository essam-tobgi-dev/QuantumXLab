OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.layout physical
cal { extern port m0; frame mf0 = newframe(m0, 7.1e9, 0.0); }
defcal measure $0 -> bit { play(mf0, constant(0.05, 1us)); return capture_v2(mf0, 1us); }
bit c;
c = measure $0;
