OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.layout physical
cal { extern port d0; frame df0 = newframe(d0, 4.8e9, 0.0); }
bit c;
x $0;
defcal x $0 { play(df0, constant(0.3, 32ns)); }
c = measure $0;
