// 3-qubit bit-flip repetition code memory experiment: encode |1>, r rounds of syndrome extraction with
// two ancillas, decode by majority at the end (T09 §3). Ideal backend: logical 1 always.
// `rounds` is a const: `for` ranges are unrolled at compile time and must fold (spec 13 §3).
OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.shots 1024
const int rounds = 3;
qubit[3] d;
qubit[2] a;
bit[2] s;
bit[3] out;
x d[0];
cx d[0], d[1];
cx d[0], d[2];
for int r in [1:rounds] {
  reset a;
  cx d[0], a[0]; cx d[1], a[0];
  cx d[1], a[1]; cx d[2], a[1];
  s[0] = measure a[0];
  s[1] = measure a[1];
}
out = measure d;
