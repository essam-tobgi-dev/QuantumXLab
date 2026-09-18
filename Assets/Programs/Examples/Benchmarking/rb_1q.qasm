// Single-qubit RB on q[0] via the runtime sequence generator (T10 §2). Fit A p^m + B; r = (1-p)/2.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 512
pragma qlab.rb 1 1 2 4 8 16 32 64 128 256 20
qubit q;
bit c;
