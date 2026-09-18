// Single-qubit randomized benchmarking: Clifford sequences of the listed lengths, 20 random samples each,
// inverted to identity; fit A p^m + B, r = (1 - p)/2 (recipe rb_1q; T10 §2.2 (2.2), (2.3)).
// Lengths and sample count are the default design of T10 §2.5 and are what recipe rb_1q sweeps.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 512
pragma qlab.layout physical
pragma qlab.rb 1 1 2 4 8 16 32 64 128 256 512 20
bit c;
