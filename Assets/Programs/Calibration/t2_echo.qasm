// Hahn echo (T2 echo): sx – τ/2 – x – τ/2 – sx. Fit: A exp(-t/T2E) + c (recipe t2_echo).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
pragma qlab.layout physical
input duration t_echo = 10us;
pragma qlab.sweep t_echo from 0 to 300 step 10
bit c;
sx $0;
delay[t_echo / 2] $0;
x $0;
delay[t_echo / 2] $0;
sx $0;
c = measure $0;
