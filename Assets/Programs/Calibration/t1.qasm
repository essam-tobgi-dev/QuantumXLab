// T1 relaxation: excite, wait a swept delay, measure. Fit: P1(t) = A exp(-t/T1) + c (recipe t1, spec 22 §6).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
pragma qlab.layout physical
input duration t_delay = 10us;
pragma qlab.sweep t_delay from 0 to 400 step 20
bit c;
x $0;
delay[t_delay] $0;
c = measure $0;
