# T03 — Quantum Algorithms

Each algorithm is given as: problem, circuit (in the little-endian conventions of T01 and the
gate definitions of T02), correctness argument, success probability, and resource scaling. The
resource table in §12 is the source for the estimator (spec 15) and the example programs in
`Assets/Programs/`.

Notation: $n$ qubits, $N = 2^n$, $H^{\otimes n}$ the Hadamard layer, $x\cdot y = \bigoplus_j x_jy_j$
the inner product over $\mathbb F_2$, $|x\rangle$ for $x\in\{0,\ldots,N-1\}$ the basis state with
bits $x_j = q_j$.

## 1. Oracles and the query model

A Boolean function $f:\{0,1\}^n\to\{0,1\}^m$ is made reversible as the **standard oracle**

$$
O_f\,|x\rangle|y\rangle = |x\rangle|y\oplus f(x)\rangle ,
\tag{1.1}
$$

a permutation matrix, hence unitary. For $m=1$, preparing the target in $|-\rangle$ turns $O_f$
into the **phase oracle** by phase kickback (T02 §4):

$$
O_f\,|x\rangle|-\rangle = (-1)^{f(x)}|x\rangle|-\rangle .
\tag{1.2}
$$

Query complexity counts applications of $O_f$. In the simulator, oracles are compiled from
classical descriptions (truth tables for $n\le 12$, arithmetic circuits otherwise); the example
programs implement them explicitly as gates so the compiler can count real resources.

## 2. Deutsch–Jozsa

**Problem.** $f:\{0,1\}^n\to\{0,1\}$ is promised constant or balanced; decide which.
Classically $2^{n-1}+1$ queries in the worst case; quantumly one.

**Circuit** (register $q_0\ldots q_{n-1}$ in $|0\rangle$, target $q_n$):
```
x q[n]; h q[0..n];           // register |+>^n, target |->
O_f;                          // phase oracle
h q[0..n-1]; measure q[0..n-1];
```

**Proof.** After the first layer the state is $\frac{1}{\sqrt N}\sum_x|x\rangle|-\rangle$. The oracle
gives $\frac{1}{\sqrt N}\sum_x(-1)^{f(x)}|x\rangle|-\rangle$. Using
$H^{\otimes n}|x\rangle = \frac{1}{\sqrt N}\sum_y(-1)^{x\cdot y}|y\rangle$, the amplitude of
$|y=0\rangle$ is $\frac1N\sum_x(-1)^{f(x)}$, which is $\pm1$ if $f$ is constant and $0$ if
balanced. Measuring all zeros ⇔ constant, with certainty.

Resources: $n+1$ qubits, $2n+2$ Hadamards, one oracle.

## 3. Bernstein–Vazirani

**Problem.** $f(x) = s\cdot x$ for an unknown $s\in\{0,1\}^n$; find $s$. Classically $n$
queries; quantumly one, with the same circuit as §2. After the oracle the state is
$\frac{1}{\sqrt N}\sum_x(-1)^{s\cdot x}|x\rangle = H^{\otimes n}|s\rangle$, so the final Hadamard
layer yields $|s\rangle$ deterministically. The oracle is a CNOT from each $q_j$ with $s_j = 1$
into the target; the program is a useful compiler test because the output must equal the
CNOT pattern exactly.

## 4. Simon's algorithm

**Problem.** $f:\{0,1\}^n\to\{0,1\}^n$ is 2-to-1 with $f(x) = f(x\oplus s)$ for a hidden
$s\neq0$; find $s$. Classical: $\Omega(2^{n/2})$ queries. Quantum: $O(n)$.

**One round.** Registers $A$ ($n$ qubits) and $B$ ($n$ qubits), standard oracle:
```
h A; O_f (A -> B); measure B (optional); h A; measure A -> y
```
After the oracle, $\frac{1}{\sqrt N}\sum_x|x\rangle|f(x)\rangle$. Conditioned on any value of
$B$, register $A$ is $\frac{1}{\sqrt2}(|x_0\rangle+|x_0\oplus s\rangle)$. The final Hadamard gives
amplitude on $|y\rangle$ proportional to $(-1)^{x_0\cdot y}\left(1+(-1)^{s\cdot y}\right)$, so
the measured $y$ satisfies $s\cdot y = 0$, uniformly over that subspace.

**Post-processing.** Collect $y^{(1)},\ldots,y^{(m)}$ and solve the homogeneous system
$y^{(i)}\cdot s = 0$ over $\mathbb F_2$ by Gaussian elimination; $n-1$ linearly independent
equations determine $s$ uniquely (as the nonzero solution). The probability that $m = n-1+k$
uniformly random vectors of an $(n-1)$-dimensional space span it is

$$
\prod_{i=0}^{n-2}\left(1-2^{\,i-m}\right)\ \ge\ 1-2^{-k},
\tag{4.1}
$$

so $n+2$ rounds succeed with probability $\ge 7/8$; the expected number of rounds is $n+O(1)$. Resources: $2n$
qubits, one oracle per round.

## 5. Quantum Fourier transform

**Definition** on $n$ qubits:

$$
\mathrm{QFT}\,|j\rangle = \frac{1}{\sqrt N}\sum_{k=0}^{N-1} e^{2\pi i\,jk/N}\,|k\rangle,
\qquad
\mathrm{QFT}_{kj} = \frac{1}{\sqrt N}\,\omega^{jk},\ \ \omega = e^{2\pi i/N}.
\tag{5.1}
$$

This is the unitary discrete Fourier transform matrix on amplitudes; the test suite compares
the compiled unitary to (5.1) directly (spec 25 §4).

**Product form.** With $j = j_{n-1}2^{n-1}+\cdots+j_0$ and binary fractions
$0.j_lj_{l-1}\ldots j_0 = \sum_{m=0}^{l} j_m 2^{m-l-1}$:

$$
\mathrm{QFT}\,|j\rangle = \bigotimes_{l=n-1}^{0}\frac{|0\rangle + e^{2\pi i\,0.j_l j_{l-1}\ldots j_0}\,|1\rangle}{\sqrt2},
\tag{5.2}
$$

where, comparing with (5.1) term by term, output qubit $q_m$ must carry the factor with
phase $0.j_{n-1-m}\ldots j_0$ (the least significant output qubit $q_0$ sees all bits of $j$).
The circuit below produces the factor $0.j_l\ldots j_0$ on qubit $q_l$, which is why it ends
with a reversal of qubit order.

**Circuit.** For $l = n-1$ down to $0$: `h q[l]`, then for $m = l-1$ down to $0$:
`cp(π/2^{l-m}) q[m], q[l]`. Finally `swap q[i], q[n-1-i]` for $i < n/2$.
Gate count: $n$ Hadamards, $n(n-1)/2$ controlled phases, $\lfloor n/2\rfloor$ swaps —
$\Theta(n^2)$ gates against $\Theta(n2^n)$ for the classical FFT on $2^n$ points, but the
amplitudes are not readable, so the QFT is useful only inside a larger algorithm.

**Approximate QFT.** Dropping every `cp` with $l - m > b$ leaves $O(nb)$ gates; with
$b = \lceil\log_2 n\rceil + 2$ the error in operator norm is $O(n\,2^{-b})$ (Coppersmith). The
compiler exposes this as a program-level option because on hardware the small-angle phases
are below calibration precision anyway.

**Inverse.** $\mathrm{QFT}^\dagger$: same circuit with negated angles in reverse order.

## 6. Quantum phase estimation

**Problem.** Given unitary $U$ with eigenstate $|u\rangle$, $U|u\rangle = e^{2\pi i\phi}|u\rangle$,
$0\le\phi<1$, estimate $\phi$ to $n$ bits.

**Circuit.** Counting register of $t$ qubits in $|+\rangle^{\otimes t}$; controlled-$U^{2^k}$
from counting qubit $k$ onto $|u\rangle$; inverse QFT on the counting register; measure. Before
the inverse QFT the counting register is $\frac{1}{\sqrt{2^t}}\sum_{x=0}^{2^t-1}e^{2\pi i\phi x}|x\rangle$,
the QFT of $|2^t\phi\rangle$ when $2^t\phi$ is an integer; then the measurement gives $\phi$
exactly. In general the measured $m/2^t$ satisfies $|m/2^t - \phi| \le 2^{-n}$ with probability
$\ge 1-\epsilon$ provided

$$
t = n + \left\lceil \log_2\!\left(2 + \frac{1}{2\epsilon}\right)\right\rceil .
\tag{6.1}
$$

For $\epsilon = 1/4$ this is $t = n+2$; for $\epsilon = 0.1$, $t = n+3$. If the input is a
superposition $\sum_u c_u|u\rangle$, the output is the corresponding mixture of phase estimates
with probabilities $|c_u|^2$.

Resources: $t$ counting qubits plus the size of $|u\rangle$; $2^t - 1$ applications of $U$
(as controlled powers), so the cost is dominated by implementing $U^{2^k}$ efficiently — which
is possible for modular multiplication (§7) and Hamiltonian simulation, not for a generic $U$.

## 7. Shor's algorithm

**Reduction.** To factor odd composite $N$ that is not a prime power: choose random
$1<a<N$; if $\gcd(a,N)>1$ done. Otherwise find the order $r$ (smallest $r>0$ with
$a^r\equiv1\pmod N$). If $r$ is even and $a^{r/2}\not\equiv-1\pmod N$, then
$\gcd(a^{r/2}\pm1, N)$ are nontrivial factors. For $N$ with $k$ distinct odd prime factors the
probability that a random $a$ passes both conditions is at least $1 - 2^{1-k} \ge 1/2$.

**Order finding as phase estimation.** With $U_a|y\rangle = |ay \bmod N\rangle$ on
$L = \lceil\log_2N\rceil$ qubits, the eigenstates $|u_s\rangle = \frac{1}{\sqrt r}\sum_{k=0}^{r-1}e^{-2\pi isk/r}|a^k\bmod N\rangle$
have eigenvalues $e^{2\pi is/r}$, and $|1\rangle = \frac{1}{\sqrt r}\sum_s|u_s\rangle$. Phase
estimation on $|1\rangle$ with $t = 2L+1$ counting qubits returns $m/2^t \approx s/r$ for a
uniformly random $s$, to precision $2^{-(2L+1)}$.

**Continued fractions.** Because $r<N\le 2^L$, the fraction $s/r$ is the unique convergent of
the continued-fraction expansion of $m/2^t$ with denominator $<N$ that lies within
$1/2^{2L+1} < 1/(2r^2)$ of it. If $\gcd(s,r)=1$ the denominator is $r$; the probability of that
over random $s$ is $\varphi(r)/r > 1/(4\ln\ln r)$ for $r\ge 19$ (Hardy–Wright), so a constant
number of repetitions suffices; in practice the algorithm also tries small multiples of the
denominator.

**Worked example, $N=15$, $a=7$.** $7^1=7,\ 7^2=4,\ 7^3=13,\ 7^4=1 \pmod{15}$: $r=4$, even,
$7^2 = 4\not\equiv-1$. Factors $\gcd(7^2-1,15) = \gcd(48,15) = 3$ and
$\gcd(7^2+1,15) = \gcd(50,15) = 5$. Phase estimation with $t=2L+1 = 9$ counting qubits returns
$m/512\in\{0, 1/4, 1/2, 3/4\}$ (exact, since $r\mid 2^t$); $m=128$ or $384$ give $r=4$ directly,
$m=256$ gives denominator 2 (fails, retry), $m=0$ fails. Success probability per run $1/2$
before the factor test. With $a=11$: $r=2$, $\gcd(10,15)=5$, $\gcd(12,15)=3$, success as well.

**Resources.** Modular exponentiation dominates: with the Beauregard construction the whole
circuit uses $2L+3$ qubits and $O(L^3\log L)$ gates ($O(L^3)$ Toffoli-equivalents); with
schoolbook arithmetic and no optimisation it is $O(L^3)$ gates on $\sim 3L$ qubits. For
$L = 2048$ the best published fault-tolerant estimate (Gidney–Ekerå 2019) is $\approx 2\times10^7$
physical qubits and $\approx 8$ hours at $10^{-3}$ physical error rate and 1 μs cycle time;
spec 15 §5 uses their Toffoli count $2.7\times10^9$ as the anchor for its scaling.

## 8. Grover's search and amplitude amplification

**Problem.** $f:\{0,\ldots,N-1\}\to\{0,1\}$ with $M$ marked inputs ($f=1$); find one.
Classically $\Theta(N/M)$ queries; quantumly $\Theta(\sqrt{N/M})$.

**Operators.** Phase oracle $O = I - 2\sum_{f(x)=1}|x\rangle\langle x|$ and diffusion
$D = H^{\otimes n}\left(2|0\rangle\langle0| - I\right)H^{\otimes n} = 2|s\rangle\langle s| - I$
with $|s\rangle = H^{\otimes n}|0\rangle$. The Grover iterate is $G = D\,O$. The diffusion is
implemented as `h; x; (multi-controlled z); x; h` on all qubits, $C^{n-1}Z$ costing per
T02 §3.4.

**Geometry.** Let $|\beta\rangle$ be the normalised marked superposition and $|\alpha\rangle$ the
unmarked one; $|s\rangle = \cos\theta|\alpha\rangle + \sin\theta|\beta\rangle$ with
$\sin\theta = \sqrt{M/N}$. $O$ reflects about $|\alpha\rangle$, $D$ about $|s\rangle$; the product
rotates by $2\theta$ toward $|\beta\rangle$:

$$
G^k|s\rangle = \cos\big((2k+1)\theta\big)|\alpha\rangle + \sin\big((2k+1)\theta\big)|\beta\rangle,
\qquad
P_{\rm success}(k) = \sin^2\big((2k+1)\theta\big).
\tag{8.1}
$$

The optimal iteration count is the integer nearest $(\pi/(2\theta) - 1)/2$,

$$
k_{\rm opt} = \left\lfloor \frac{\pi}{4}\sqrt{\frac{N}{M}}\right\rfloor
\quad(\text{for } M \ll N),
\tag{8.2}
$$

and over-rotating *reduces* the success probability. For unknown $M$, run with $k$ drawn
uniformly from $[0, \lambda^j)$ for increasing $\lambda^j$ (Boyer et al.), still $O(\sqrt{N/M})$
expected queries.

**Worked example, $n=3$, $M=1$.** $\theta = \arcsin(1/\sqrt8) = 0.3614$ rad,
$k_{\rm opt} = \lfloor\tfrac\pi4\sqrt8\rfloor = 2$.

| $k$ | $(2k+1)\theta$ | $P_{\rm success}$ |
|----:|---------------:|------------------:|
| 0 | 0.361 | 0.125 |
| 1 | 1.084 | 0.781 |
| 2 | 1.807 | **0.945** |
| 3 | 2.530 | 0.330 |

For $n=2$, $M=1$: $\theta = \pi/6$, one iteration gives $\sin^2(\pi/2) = 1$ exactly.

**Amplitude amplification** generalises: for any $\mathcal A$ with $\mathcal A|0\rangle = \sqrt{p}|\text{good}\rangle + \sqrt{1-p}|\text{bad}\rangle$,
$Q = \mathcal A S_0\mathcal A^\dagger S_{\rm good}$ boosts $p$ to $\sin^2((2k+1)\theta)$, $\sin^2\theta = p$.

**Amplitude estimation.** Phase estimation on $Q$ (eigenphases $\pm2\theta$) with $t$ counting
qubits estimates $p = \sin^2\theta$ with error $O(1/2^t)$ using $O(2^t)$ applications of
$\mathcal A$ — a quadratic improvement over the $O(1/\epsilon^2)$ samples of Monte-Carlo
estimation.

## 9. Variational algorithms

### 9.1 VQE

Given a Hamiltonian as a Pauli sum $H = \sum_\ell c_\ell P_\ell$ (real $c_\ell$) and a
parametrised circuit $|\psi(\vec\theta)\rangle = U(\vec\theta)|0\rangle$, minimise

$$
E(\vec\theta) = \langle\psi(\vec\theta)|H|\psi(\vec\theta)\rangle = \sum_\ell c_\ell\,\langle P_\ell\rangle_{\vec\theta}
\ \ge\ E_0 .
\tag{9.1}
$$

Each $\langle P_\ell\rangle$ is measured per T01 §5.4; commuting strings sharing a measurement
basis are grouped. Shot cost for precision $\epsilon$ on $E$: $\sum_\ell |c_\ell|^2/\epsilon^2$
shots (worst case, no grouping). Gradients by the parameter-shift rule for a gate
$e^{-i\theta P/2}$: $\partial_\theta E = \tfrac12\left[E(\theta+\tfrac\pi2) - E(\theta-\tfrac\pi2)\right]$.

**H$_2$ example.** In the STO-3G basis, after the Bravyi–Kitaev transform and removal of two
qubits by symmetry, the molecular Hamiltonian is a two-qubit operator

$$
H_{\rm H_2} = g_0 I + g_1 Z_0 + g_2 Z_1 + g_3 Z_0Z_1 + g_4 X_0X_1 + g_5 Y_0Y_1,
\tag{9.2}
$$

with bond-length-dependent coefficients tabulated by O'Malley et al., Phys. Rev. X 6,
031007 (2016), Table I. The Hamiltonian is block-diagonal: $|00\rangle$ and $|11\rangle$ are
eigenstates, and the ground state lives in the $\{|01\rangle,|10\rangle\}$ block with real
coefficients, so the one-parameter ansatz `x q[0]; ry(θ) q[1]; cx q[1], q[0]`, which prepares
$\cos\frac\theta2|01\rangle + \sin\frac\theta2|10\rangle$, reaches the exact ground state.
The FCI/STO-3G ground energy at the
equilibrium bond length $0.735$ Å is $-1.137$ Ha. The shipped coefficient file
`Assets/Programs/vqe/h2_sto3g.json` MUST be regenerated from an electronic-structure package
rather than transcribed from the table, and the test oracle compares the VQE minimum against
the exact diagonalisation of the *shipped* coefficients, not against a hard-coded energy.

### 9.2 QAOA

For MaxCut on a graph $(V,E)$:

$$
C = \sum_{(i,j)\in E}\tfrac12\left(1 - Z_iZ_j\right),\qquad
B = \sum_{i\in V} X_i,
\qquad
|\vec\gamma,\vec\beta\rangle = \prod_{l=1}^{p} e^{-i\beta_l B}\,e^{-i\gamma_l C}\ |+\rangle^{\otimes n}.
\tag{9.3}
$$

$e^{-i\gamma C}$ is a product of `rzz(-γ)` (up to phase) over edges; $e^{-i\beta B}$ is
`rx(2β)` on every qubit. Depth per layer: one `rzz` per edge (routed on hardware), one `rx`
layer. The expectation $\langle C\rangle$ is estimated from $Z$-basis shots as the mean cut
value. As $p\to\infty$ the optimum approaches $\max C$; for $p=1$ on a triangle-free
$d$-regular graph the optimal $\langle C\rangle/|E|$ has a closed form (Wang et al. 2018) that
spec 25 uses as an oracle.

## 10. Communication protocols

### 10.1 Teleportation

Qubits: $q_0$ message $|\psi\rangle$, $q_1$ Alice's half of a Bell pair, $q_2$ Bob's half.

```
h q[1]; cx q[1], q[2];          // |Φ+> on (q1,q2)
cx q[0], q[1]; h q[0];          // Bell measurement basis change
measure q[0] -> c[0]; measure q[1] -> c[1];
if (c[1]) x q[2];               // X correction from Alice's half
if (c[0]) z q[2];               // Z correction from the message qubit
```

After the two CNOT/H operations the state is
$\tfrac12\sum_{c_0c_1}|c_0\rangle|c_1\rangle\ X^{c_1}Z^{c_0}|\psi\rangle_{q_2}$, so each of the four
outcomes occurs with probability $1/4$ and the correction table is

| $c_0$ | $c_1$ | Bob's state | Correction |
|:-:|:-:|:-:|:-:|
| 0 | 0 | $|\psi\rangle$ | none |
| 0 | 1 | $X|\psi\rangle$ | `x` |
| 1 | 0 | $Z|\psi\rangle$ | `z` |
| 1 | 1 | $XZ|\psi\rangle$ | `x` then `z` |

Two classical bits and one shared Bell pair transmit one qubit; no information travels
faster than the classical bits (Bob's reduced state is $I/2$ before the corrections).

### 10.2 Superdense coding

Bob prepares $|\Phi^+\rangle$ on $(q_0,q_1)$ and sends $q_0$ to Alice. Alice encodes two bits
$(b_1b_0)$ by applying $Z^{b_1}X^{b_0}$ to $q_0$, mapping $|\Phi^+\rangle$ to
$|\Phi^+\rangle,|\Psi^+\rangle,|\Phi^-\rangle,|\Psi^-\rangle$ (up to sign) for
$00, 01, 10, 11$, and returns $q_0$. Bob decodes with `cx q[0],q[1]; h q[0]` and measures:
$q_1$ yields $b_0$, $q_0$ yields $b_1$. One qubit sent, two classical bits received.

### 10.3 Entanglement swapping

Given $|\Phi^+\rangle_{01}\otimes|\Phi^+\rangle_{23}$, a Bell measurement on $(q_1,q_2)$
projects $(q_0,q_3)$ — which never interacted — onto the Bell state labelled by the outcome.
This is the repeater primitive; in the lab it is the example program for mid-circuit
measurement with feed-forward.

## 11. HHL (sketch)

For a Hermitian $N\times N$ matrix $A$ with sparsity $s$ and condition number $\kappa$, and
$|b\rangle$ encoding $\vec b$: phase-estimate $e^{iAt}$ on $|b\rangle$ to obtain eigenvalues
$\lambda_j$ in a register, rotate an ancilla by $\arcsin(C/\lambda_j)$, uncompute, and
post-select on the ancilla. The result is $|x\rangle\propto A^{-1}|b\rangle$ with cost
$\tilde O(\log(N)\,s^2\kappa^2/\epsilon)$ — exponential in $\log N$ over classical methods only
when $|b\rangle$ is cheaply preparable and only global properties of $\vec x$ are needed. The
example program uses a $2\times2$ system to exercise controlled rotations and uncomputation.

## 12. Complexity and resource summary

| Algorithm | Qubits | Queries / dominant gates | Success | Classical comparison |
|-----------|--------|--------------------------|---------|----------------------|
| Deutsch–Jozsa | $n+1$ | 1 oracle, $2n+2$ H | 1 | $2^{n-1}+1$ queries |
| Bernstein–Vazirani | $n+1$ | 1 oracle | 1 | $n$ queries |
| Simon | $2n$ | $n+O(1)$ oracles | $\ge1-2^{-k}$ after $n-1+k$ | $\Omega(2^{n/2})$ |
| QFT | $n$ | $n(n+1)/2$ gates | exact | $\Theta(n2^n)$ FFT (different task) |
| Phase estimation | $t + \dim|u\rangle$ | $2^t-1$ controlled-$U$ | $\ge1-\epsilon$, (6.1) | — |
| Shor ($L$-bit $N$) | $2L+3$ | $O(L^3\log L)$ | $\ge1/2$ per run before repetition | $\exp(O(L^{1/3}\log^{2/3}L))$ (GNFS) |
| Grover | $n$ | $\lfloor\frac\pi4\sqrt{N/M}\rfloor$ oracles | (8.1) | $\Theta(N/M)$ |
| Amplitude estimation | $n+t$ | $O(2^t)$ of $\mathcal A$ | error $O(2^{-t})$ | $O(1/\epsilon^2)$ samples |
| VQE | $n$ | $O(\sum_\ell|c_\ell|^2/\epsilon^2)$ shots per energy | heuristic | — |
| QAOA | $|V|$ | $p(|E| + |V|)$ gates | heuristic | — |
| Teleportation | 3 | 2 CX, 2 H, 2 measurements | 1 | — |

**Resource-count anchors** used by spec 15 for fault-tolerant estimates (gate counts in
terms of the non-Clifford budget, which dominates under error correction):

| Algorithm / primitive | T-count scaling | CNOT-count scaling | Source of constants |
|-----------------------|-----------------|--------------------|---------------------|
| Toffoli | 7 (exact), 4 with a measured ancilla | 6 | T02 §3.2; Jones 2013 |
| $R_z(\theta)$ to precision $\epsilon$ | $3\log_2(1/\epsilon)+O(\log\log)$ | 0 | Ross–Selinger 2016 |
| QFT on $n$ qubits at precision $\epsilon$ | $O(n^2\log(n/\epsilon))$ exact; $O(n\log n\log(n/\epsilon))$ approximate | $O(n^2)$ | §5 |
| Grover diffusion on $n$ qubits | $O(n)$ Toffolis → $O(n)$ T | $O(n)$ | T02 §3.4 |
| Modular exponentiation, $L$ bits | $O(L^3)$ Toffoli → $\approx 2.7\times10^9$ Toffoli at $L=2048$ | — | Gidney–Ekerå 2019 |
| Trotter step of a $k$-local Hamiltonian with $m$ terms | $O(m)$ rotations, each $3\log_2(1/\epsilon)$ T | $O(mk)$ | standard Pauli-exponential circuits |

## Where this is used

| Result | Used by |
|--------|---------|
| (1.1)–(1.2) oracles | Spec 13 example programs, spec 14 oracle synthesis from truth tables |
| §2–4 exact-output algorithms | Spec 25 §4 compiler equivalence corpus (deterministic outputs) |
| (5.1) QFT matrix | Spec 25 QFT-vs-DFT oracle; spec 14 approximate-QFT option |
| (6.1) counting-register size | Spec 15 §4 qubit-count estimates |
| §7 Shor worked example | `Assets/Programs/shor_15.qasm` and its expected-distribution file |
| (8.1)–(8.2) Grover | Spec 25 Grover success-probability oracle; spec 15 iteration-count estimator |
| §9 VQE/QAOA | Spec 15 shot-budget estimator, spec 22 optimiser plots, spec 25 H$_2$ and MaxCut oracles |
| §10 protocols | Spec 13 classical-control conformance tests, spec 14 feed-forward scheduling |
| §12 tables | Spec 15 §4–5 resource and fault-tolerant estimates |
