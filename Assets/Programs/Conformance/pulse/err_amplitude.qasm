OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.layout physical
cal { extern port d0; frame df0 = newframe(d0, 4.8e9, 0.0); }
defcal x $0 { play(df0, gaussian(1.2, 32ns, 8ns)); }
bit c;
x $0;
c = measure $0;
