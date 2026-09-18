// Two-qubit randomized benchmarking on the pair ($0, $1); fit A p^m + B, r = (1 - p)·3/4
// (recipe rb_2q; T10 §2.2 (2.2), (2.3)). 20 random sequences per length, as the recipe averages.
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 512
pragma qlab.layout physical
pragma qlab.rb 2 1 2 4 8 16 32 64 128 20
bit[2] c;
