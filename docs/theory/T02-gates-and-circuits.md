# T02 — Gates and Circuits

All gate matrices in this document are **Exact** (fidelity class of spec 00 §5). Matrices are
written in the little-endian basis of T01 (3.1): for a two-qubit gate `g a, b` in OpenQASM,
`a` is the first operand and the matrix rows/columns are indexed by $k = 2q_b + q_a$ only when
$a = q_0$, $b = q_1$; the general rule is that the matrix acts on the register, and the
simulator applies it to the selected qubits by index permutation (T11 §2.3).

Time order in circuit notation is left to right; the corresponding unitary is the matrix
product in reverse order: a circuit $G_1$ then $G_2$ then $G_3$ implements $U = G_3 G_2 G_1$.

## 1. Single-qubit gates

### 1.1 The OpenQASM 3 primitive

$$
U(\theta,\varphi,\lambda) =
\begin{pmatrix}
\cos\frac{\theta}{2} & -e^{i\lambda}\sin\frac{\theta}{2}\\[4pt]
e^{i\varphi}\sin\frac{\theta}{2} & e^{i(\varphi+\lambda)}\cos\frac{\theta}{2}
\end{pmatrix}
= e^{i(\varphi+\lambda)/2}\,R_z(\varphi)\,R_y(\theta)\,R_z(\lambda).
\tag{1.1}
$$

Every single-qubit unitary equals $e^{i\alpha}\,U(\theta,\varphi,\lambda)$ for some real
$\alpha,\theta,\varphi,\lambda$. The global phase $e^{i(\varphi+\lambda)/2}$ relative to the
rotation product is irrelevant for an uncontrolled gate but becomes a relative phase under
control; OpenQASM 3 tracks it with `gphase` and `ctrl @`.

### 1.2 Standard gates (`stdgates.inc` definitions)

| Gate | OpenQASM definition | Matrix |
|------|---------------------|--------|
| `p(λ)` | $U(0,0,\lambda)$ | $\begin{pmatrix}1&0\\0&e^{i\lambda}\end{pmatrix}$ |
| `x` | $U(\pi,0,\pi)$ | $\begin{pmatrix}0&1\\1&0\end{pmatrix}$ |
| `y` | $U(\pi,\pi/2,\pi/2)$ | $\begin{pmatrix}0&-i\\i&0\end{pmatrix}$ |
| `z` | $p(\pi)$ | $\begin{pmatrix}1&0\\0&-1\end{pmatrix}$ |
| `h` | $U(\pi/2,0,\pi)$ | $\frac{1}{\sqrt2}\begin{pmatrix}1&1\\1&-1\end{pmatrix}$ |
| `s` | $p(\pi/2)$ | $\begin{pmatrix}1&0\\0&i\end{pmatrix}$ |
| `sdg` | $p(-\pi/2)$ | $\begin{pmatrix}1&0\\0&-i\end{pmatrix}$ |
| `t` | $p(\pi/4)$ | $\begin{pmatrix}1&0\\0&e^{i\pi/4}\end{pmatrix}$ |
| `tdg` | $p(-\pi/4)$ | $\begin{pmatrix}1&0\\0&e^{-i\pi/4}\end{pmatrix}$ |
| `sx` | $\sqrt{X}$ | $\frac12\begin{pmatrix}1+i&1-i\\1-i&1+i\end{pmatrix} = e^{i\pi/4}R_x(\pi/2)$ |
| `rx(θ)` | $U(\theta,-\pi/2,\pi/2)$ | $\begin{pmatrix}\cos\frac\theta2&-i\sin\frac\theta2\\-i\sin\frac\theta2&\cos\frac\theta2\end{pmatrix}$ |
| `ry(θ)` | $U(\theta,0,0)$ | $\begin{pmatrix}\cos\frac\theta2&-\sin\frac\theta2\\\sin\frac\theta2&\cos\frac\theta2\end{pmatrix}$ |
| `rz(λ)` | `gphase(-λ/2); U(0,0,λ)` | $\begin{pmatrix}e^{-i\lambda/2}&0\\0&e^{i\lambda/2}\end{pmatrix}$ |
| `id` | $U(0,0,0)$ | $I$ |

Relations: $S^2 = Z$, $T^2 = S$, $SX^2 = X$, $H^2 = I$, $H X H = Z$, $H Z H = X$,
$S X S^\dagger = Y$, $R_z(\lambda) = e^{-i\lambda/2}\,p(\lambda)$.

### 1.3 Rotations

For a unit vector $\hat n$ and $\vec\sigma = (X,Y,Z)$:

$$
R_{\hat n}(\theta) = e^{-i\theta\,\hat n\cdot\vec\sigma/2} = \cos\frac\theta2\, I - i\sin\frac\theta2\ \hat n\cdot\vec\sigma,
\tag{1.2}
$$

which rotates the Bloch vector by angle $\theta$ about $\hat n$ (right-hand rule).
$R_{\hat n}(2\pi) = -I$: a $2\pi$ rotation is the identity only up to a sign, which is
observable under control. Composition: $R_{\hat n}(\alpha)R_{\hat n}(\beta) = R_{\hat n}(\alpha+\beta)$.
Conjugation rotates the axis: $R_z(\phi)\,R_x(\theta)\,R_z(-\phi) = R_{\hat n(\phi)}(\theta)$ with
$\hat n(\phi) = (\cos\phi,\sin\phi,0)$; in particular $R_y(\theta) = R_z(\pi/2)R_x(\theta)R_z(-\pi/2)$.

### 1.4 Euler decompositions

**ZYZ.** Any $2\times2$ unitary is

$$
V = e^{i\alpha}\,R_z(\beta)\,R_y(\gamma)\,R_z(\delta),\qquad
V = e^{i\alpha}\begin{pmatrix}
e^{-i(\beta+\delta)/2}\cos\frac\gamma2 & -e^{-i(\beta-\delta)/2}\sin\frac\gamma2\\
e^{i(\beta-\delta)/2}\sin\frac\gamma2 & e^{i(\beta+\delta)/2}\cos\frac\gamma2
\end{pmatrix}.
\tag{1.3}
$$

Extraction from the matrix entries $V_{jk}$:

$$
\gamma = 2\,\mathrm{atan2}(|V_{10}|,|V_{00}|),\quad
\beta = \arg V_{10}-\arg V_{00},\quad
\delta = \arg V_{11}-\arg V_{00}-\beta,\quad
\alpha = \arg V_{00} + \tfrac{\beta+\delta}{2}.
\tag{1.4}
$$

When $\gamma = 0$ (or $\pi$) only $\beta+\delta$ (or $\beta-\delta$) is determined; the
compiler sets $\delta = 0$ in that case.

**U ↔ ZYZ.** $U(\theta,\varphi,\lambda)$ has $\gamma=\theta$, $\beta=\varphi$, $\delta=\lambda$,
$\alpha=(\varphi+\lambda)/2$. Conversely $e^{i\alpha}R_z(\beta)R_y(\gamma)R_z(\delta) = e^{i(\alpha-(\beta+\delta)/2)}\,U(\gamma,\beta,\delta)$.

**ZXZ.** Using $R_y(\gamma) = R_z(\pi/2)R_x(\gamma)R_z(-\pi/2)$:
$V = e^{i\alpha}R_z(\beta+\pi/2)\,R_x(\gamma)\,R_z(\delta-\pi/2)$.

**Native transmon form.** Since $R_x(\pi/2) = e^{-i\pi/4}\,\text{sx}$, any single-qubit gate is
two `sx` pulses and three frame changes:

$$
U(\theta,\varphi,\lambda) \doteq R_z(\varphi+\pi)\ \text{sx}\ R_z(\theta+\pi)\ \text{sx}\ R_z(\lambda),
\tag{1.5}
$$

where $\doteq$ means equal up to global phase and the product is in matrix order (the circuit
applies $R_z(\lambda)$ first). If $\theta = \pm\pi/2$ one `sx` suffices; if $\theta = 0$ none.

### 1.5 Virtual Z

$R_z$ commutes with the drive-frame definition: a $Z$ rotation by $\phi$ immediately before a
pulse equals advancing the phase of every subsequent drive on that qubit by $\phi$ (T07 §5).
Hence `rz` costs zero time and has no error on transmon and ion devices; the compiler pushes
all $Z$ rotations forward into pulse phases (spec 14 §6). The rule that makes this exact:
$R_z(\phi)\,R_{\hat n(\psi)}(\theta)\,R_z(-\phi) = R_{\hat n(\psi+\phi)}(\theta)$.

## 2. Two-qubit gates

Matrices below are for the gate applied as `g q[0], q[1]` (first operand $q_0$), basis order
$|q_1q_0\rangle$ = $|00\rangle,|01\rangle,|10\rangle,|11\rangle$.

### 2.1 Controlled-NOT

$$
\text{cx } q_0,q_1\ (\text{control } q_0,\ \text{target } q_1) =
\begin{pmatrix}1&0&0&0\\0&0&0&1\\0&0&1&0\\0&1&0&0\end{pmatrix},
\qquad
\text{cx } q_1,q_0 =
\begin{pmatrix}1&0&0&0\\0&1&0&0\\0&0&0&1\\0&0&1&0\end{pmatrix}.
\tag{2.1}
$$

The right-hand form is the textbook matrix (control is the more significant qubit). In
projector form $\text{CX}_{c,t} = |0\rangle\langle0|_c\otimes I_t + |1\rangle\langle1|_c\otimes X_t$.
Action on basis states: $|q_1q_0\rangle\to|q_1\oplus q_0,\ q_0\rangle$ for control $q_0$.

### 2.2 Gate table

| Gate | Matrix (basis $|00\rangle,|01\rangle,|10\rangle,|11\rangle$) | Generator form |
|------|------|------|
| `cz` | $\mathrm{diag}(1,1,1,-1)$ | $e^{i\pi(I-Z)\otimes(I-Z)/4}$; symmetric in its operands |
| `cp(λ)` | $\mathrm{diag}(1,1,1,e^{i\lambda})$ | symmetric |
| `swap` | $\begin{pmatrix}1&0&0&0\\0&0&1&0\\0&1&0&0\\0&0&0&1\end{pmatrix}$ | $\tfrac12(II+XX+YY+ZZ)$ |
| `iswap` | $\begin{pmatrix}1&0&0&0\\0&0&i&0\\0&i&0&0\\0&0&0&1\end{pmatrix}$ | $e^{i\pi(XX+YY)/4}$ |
| $\sqrt{\text{iswap}}$ | $\begin{pmatrix}1&0&0&0\\0&\frac{1}{\sqrt2}&\frac{i}{\sqrt2}&0\\0&\frac{i}{\sqrt2}&\frac{1}{\sqrt2}&0\\0&0&0&1\end{pmatrix}$ | $e^{i\pi(XX+YY)/8}$ |
| `rxx(θ)` | $\cos\frac\theta2\,I - i\sin\frac\theta2\,X\otimes X$ | $e^{-i\theta XX/2}$ |
| `ryy(θ)` | $\cos\frac\theta2\,I - i\sin\frac\theta2\,Y\otimes Y$ | $e^{-i\theta YY/2}$ |
| `rzz(θ)` | $\mathrm{diag}(e^{-i\theta/2},e^{i\theta/2},e^{i\theta/2},e^{-i\theta/2})$ | $e^{-i\theta ZZ/2}$ |
| `rzx(θ)` | $\cos\frac\theta2\,I - i\sin\frac\theta2\,Z_{q_0}X_{q_1}$ | $e^{-i\theta Z\otimes X/2}$ with $Z$ on $q_0$ |
| `ecr` | $\frac{1}{\sqrt2}\begin{pmatrix}0&1&0&i\\1&0&-i&0\\0&i&0&1\\-i&0&1&0\end{pmatrix}$ | $\frac{1}{\sqrt2}\left(X_{q_0} - Y_{q_0}X_{q_1}\right)$ |
| `fsim(θ,φ)` | $\begin{pmatrix}1&0&0&0\\0&\cos\theta&-i\sin\theta&0\\0&-i\sin\theta&\cos\theta&0\\0&0&0&e^{-i\varphi}\end{pmatrix}$ | iswap$^\dagger$-like exchange $\theta$ plus conditional phase $\varphi$ |

In the `ecr` generator, $X_{q_0}$ means $I\otimes X$ in Kronecker order $q_1\otimes q_0$
(the Pauli string label `"IX"`), and $Y_{q_0}X_{q_1}$ is $X\otimes Y$ (label `"XY"`). ECR is the
echoed cross-resonance gate native to fixed-frequency transmons (T05 §8):
$\text{ecr} = R_{zx}(-\pi/4)\ X_{q_0}\ R_{zx}(\pi/4)$ in matrix order (circuit: $R_{zx}(\pi/4)$,
then $X$ on $q_0$, then $R_{zx}(-\pi/4)$). `iswap` $= \text{rxx}(-\pi/2)\,\text{ryy}(-\pi/2)$;
$\text{fsim}(\pi/2,0) = \text{iswap}^\dagger$ and $\text{fsim}(0,\pi) = \text{cz}$.
$\text{cp}(\lambda) \doteq \text{rzz}(-\lambda/2)\,[R_z(\lambda/2)\otimes R_z(\lambda/2)]$.

### 2.3 Conversions between entangling gates

Each line is a circuit in time order; $\doteq$ is equality up to global phase. Control is
$q_0$, target $q_1$ throughout.

| Target | Circuit (time order) | Cost |
|--------|----------------------|------|
| `cx q0,q1` from `cz` | `h q1; cz q0,q1; h q1` | 1 cz + 2 h |
| `cz q0,q1` from `cx` | `h q1; cx q0,q1; h q1` | 1 cx |
| `cx q0,q1` from `ecr` | `x q0; ecr q0,q1; s q0; sx q1` | 1 ecr + 3 single |
| `cx q0,q1` from `rxx(π/2)` (Mølmer–Sørensen) | `ry(π/2) q0; rxx(π/2) q0,q1; rx(-π/2) q0; rx(-π/2) q1; ry(-π/2) q0` | 1 MS + 4 single |
| `swap q0,q1` | `cx q0,q1; cx q1,q0; cx q0,q1` | 3 cx |
| `cx` from `iswap` | 2 iswap + single-qubit gates | 2 iswap |
| `cx` from $\sqrt{\text{iswap}}$ | 2 $\sqrt{\text{iswap}}$ + single-qubit gates | 2 |
| `cx` from `cp(π)` | `h q1; cp(π) q0,q1; h q1` | 1 |

The `ecr` and `rxx` rows were verified numerically to machine precision in the basis of
(2.1); the resulting global phases are $e^{i\pi/4}$ in both cases. The `iswap` and
$\sqrt{\text{iswap}}$ entries follow from the KAK bound of §5: `iswap` has Cartan coordinates
$(\pi/4,\pi/4,0)$ and CNOT $(\pi/4,0,0)$, so one `iswap` cannot produce a CNOT but two can.

## 3. Controlled gates

### 3.1 Controlled-$U$ and the ABC construction

For any single-qubit $V = e^{i\alpha}R_z(\beta)R_y(\gamma)R_z(\delta)$ define

$$
A = R_z(\beta)\,R_y(\gamma/2),\quad
B = R_y(-\gamma/2)\,R_z\!\left(-\tfrac{\delta+\beta}{2}\right),\quad
C = R_z\!\left(\tfrac{\delta-\beta}{2}\right),
\qquad ABC = I,\quad e^{i\alpha}AXBXC = V.
\tag{3.1}
$$

Then, with control $c$ and target $t$ (circuit in time order):

$$
\text{C-}V = \big[\,C_t;\ \text{cx}_{c,t};\ B_t;\ \text{cx}_{c,t};\ A_t;\ p(\alpha)_c\,\big],
\tag{3.2}
$$

two CNOTs and four single-qubit gates. When $V$ has $\alpha = 0$ (a special unitary, e.g. any
$R_{\hat n}(\theta)$) the phase gate on the control is dropped. This is the decomposition the
compiler uses for `ctrl @ U(...)` (spec 14 §4).

Controlled rotations need no phase bookkeeping. Because $X R_z(-\theta/2) X = R_z(\theta/2)$
and $X R_y(-\theta/2) X = R_y(\theta/2)$,

$$
\text{C-}R_z(\theta) = [R_z(\tfrac\theta2)_t;\ \text{cx};\ R_z(-\tfrac\theta2)_t;\ \text{cx}],\qquad
\text{C-}R_y(\theta) = [R_y(\tfrac\theta2)_t;\ \text{cx};\ R_y(-\tfrac\theta2)_t;\ \text{cx}],
\tag{3.3}
$$

both exact (verified). $\text{C-}R_x$ does not follow this pattern ($X$ commutes with $R_x$);
it is obtained as $H_t\,\text{C-}R_z(\theta)\,H_t$.

### 3.2 Toffoli (`ccx`)

$\text{CCX}_{a,b;t}$ flips $t$ iff $q_a = q_b = 1$. The standard decomposition with 6 CNOTs
and T-count 7 (circuit in time order, $a,b$ controls, $t$ target):

```
h t; cx b,t; tdg t; cx a,t; t t; cx b,t; tdg t; cx a,t; t b; t t; h t;
cx a,b; t a; tdg b; cx a,b;
```

Verified exactly (no residual phase). The lower bound of 6 CNOTs for an exact Toffoli
(Shende–Markov) is met.

**Relative-phase Toffoli (Margolus).** Four $R_y$ and three CNOTs:

```
ry(π/4) t; cx b,t; ry(π/4) t; cx a,t; ry(-π/4) t; cx b,t; ry(-π/4) t;
```

equals $\text{CCX}\cdot\mathrm{diag}(1,1,1,1,1,-1,1,1)$, i.e. a Toffoli with an extra sign on
$|q_t q_b q_a\rangle = |101\rangle$. It is a valid Toffoli whenever that basis state is not
populated with a partner that would see the relative phase, which the compiler can prove for
compute/uncompute pairs.

### 3.3 Fredkin (`cswap`)

$\text{CSWAP}_{c;a,b} = [\text{cx}_{b,a};\ \text{CCX}_{c,a;b};\ \text{cx}_{b,a}]$: one Toffoli plus
two CNOTs, hence 8 CNOTs and T-count 7.

### 3.4 Multi-controlled gates

- $C^n X$ with $n-2$ clean ancillas: a ladder of $n-2$ Toffolis computing the AND of the
  controls into ancillas, one Toffoli onto the target, then uncompute: $2n-3$ Toffolis.
- $C^n U$ without ancillas by Gray code (Barenco et al. 1995, Lemma 7.1): $2^n-1$
  controlled-$V$ gates with $V^{2^{n-1}} = U$ and $2^n-2$ CNOTs. Exponential; used only for
  $n\le3$.
- $C^n X$ without ancillas: $O(n^2)$ elementary gates (Barenco Lemma 7.5); with one borrowed
  ancilla (any qubit in any state) $O(n)$ Toffoli-equivalents (Lemma 7.3). The compiler's
  default for $n\ge4$ is the borrowed-ancilla construction when a free qubit exists, else the
  quadratic one.
- $C^n R_{\hat n}(\theta)$ for $\hat n\perp\hat z$: recursive halving,
  $C^nR(\theta) = [C^{n-1}R(\theta/2);\ C^{n-1}X\ \text{on control } n;\ \ldots]$, cost linear in
  the $C^{n-1}X$ cost.

## 4. Gate identities

Each row is an equality of unitaries (up to global phase where marked $\doteq$); $c,t$ denote
CNOT control/target.

| Identity | Statement | Use |
|----------|-----------|-----|
| Hadamard conjugation | $HXH = Z$, $HZH = X$, $HYH = -Y$ | basis change for measuring $X$ |
| Phase conjugation | $SXS^\dagger = Y$, $SYS^\dagger = -X$ | measuring $Y$ |
| CNOT / Z on control | $Z_c\,\text{CX} = \text{CX}\,Z_c$ | commute; enables virtual-Z pushing |
| CNOT / X on target | $X_t\,\text{CX} = \text{CX}\,X_t$ | commute |
| CNOT / X on control | $\text{CX}\,X_c = X_cX_t\,\text{CX}$ | Pauli propagation through CNOT |
| CNOT / Z on target | $\text{CX}\,Z_t = Z_cZ_t\,\text{CX}$ | Pauli propagation, error tracking in QEC |
| CZ symmetry | $\text{CZ}_{a,b} = \text{CZ}_{b,a}$ | operand order free |
| CNOT reversal | $\text{CX}_{t,c} = H_cH_t\,\text{CX}_{c,t}\,H_cH_t$ | routing on directed couplers |
| Phase kickback | $\text{CX}_{c,t}\,\big(|\psi\rangle|-\rangle\big) = \big(Z|\psi\rangle\big)|-\rangle$ | oracles (T03 §2) |
| Double CNOT | $\text{CX}^2 = I$ | cancellation pass |
| Rotation merge | $R_{\hat n}(\alpha)R_{\hat n}(\beta) = R_{\hat n}(\alpha+\beta)$ | single-qubit merge pass |
| ZZ through CNOT | $\text{CX}\,(Z_c Z_t)\,\text{CX} = Z_t$ | parity measurement |
| SWAP by CNOTs | $\text{SWAP} = \text{CX}_{c,t}\text{CX}_{t,c}\text{CX}_{c,t}$ | routing |
| Hadamard–SWAP | $\text{SWAP}\,(A\otimes B)\,\text{SWAP} = B\otimes A$ | qubit relabeling |

The four Pauli-propagation rules generalize: conjugating any Pauli string by a Clifford yields
a Pauli string (§5.1), which is what the stabilizer backend computes.

## 5. Gate sets, universality, and the Clifford hierarchy

### 5.1 Pauli and Clifford groups

The $n$-qubit Pauli group $\mathcal P_n = \{i^k\,P_{n-1}\otimes\cdots\otimes P_0\}$, $P_j\in\{I,X,Y,Z\}$,
$k\in\{0,1,2,3\}$, has $4^{n+1}$ elements. The Clifford group is its normalizer,
$\mathcal C_n = \{U : U\mathcal P_nU^\dagger = \mathcal P_n\}$, generated by $H$, $S$, and CNOT
(modulo phases). Sizes modulo global phase: $|\mathcal C_1| = 24$, $|\mathcal C_2| = 11\,520$,
$|\mathcal C_n| = 2^{n^2+2n}\prod_{j=1}^n(4^j-1)$. Conjugation table of the generators:

| $U$ | $UXU^\dagger$ | $UZU^\dagger$ |
|-----|---------------|---------------|
| $H$ | $Z$ | $X$ |
| $S$ | $Y$ | $Z$ |
| $\text{CX}_{c,t}$ | $X_c\to X_cX_t$, $X_t\to X_t$ | $Z_c\to Z_c$, $Z_t\to Z_cZ_t$ |

**Gottesman–Knill theorem.** A circuit consisting of state preparation in the computational
basis, Clifford gates, Pauli measurements, and classical control conditioned on outcomes can be
simulated classically in time polynomial in $n$ and the number of gates (the tableau method,
T11 §3). This is what makes the stabilizer backend and QEC simulation (T09) tractable at
thousands of qubits.

### 5.2 Universality

- $\{H, S, T, \text{CX}\}$ (Clifford + $T$) is universal: dense in $SU(2^n)$.
- $\{U(\theta,\varphi,\lambda), \text{CX}\}$ is exactly universal: any $n$-qubit unitary is a
  finite product. The generic count is $\Theta(4^n)$ CNOTs (lower bound
  $\lceil\tfrac14(4^n-3n-1)\rceil$, Shende–Bullock–Markov).
- $\{R_z, \text{sx}, \text{cx}\}$ (transmon native) and $\{R_z, R_x, R_y, \text{rxx}\}$ (ion
  native) are exactly universal by (1.5) and §2.3.

**Solovay–Kitaev.** For any universal finite gate set closed under inverses, any single-qubit
unitary can be approximated to operator-norm distance $\varepsilon$ with
$O(\log^{c}(1/\varepsilon))$ gates, $c \approx 3.97$ for the original algorithm. For the
Clifford+$T$ set, number-theoretic synthesis (Ross–Selinger) achieves $T$-count
$3\log_2(1/\varepsilon) + O(\log\log(1/\varepsilon))$ for $R_z$ rotations, which is the figure
spec 15 §5 uses for fault-tolerant resource estimates.

### 5.3 Clifford hierarchy

$\mathcal C^{(1)} = \mathcal P_n$, $\mathcal C^{(k+1)} = \{U : U\mathcal P_nU^\dagger \subseteq \mathcal C^{(k)}\}$.
$T$, CCX, and $\text{cs}$ are in $\mathcal C^{(3)}$. Gates in $\mathcal C^{(3)}$ can be applied
by gate teleportation with a magic state, the basis of fault-tolerant $T$ gates (T09 §8).

## 6. Two-qubit KAK (Cartan) decomposition

Every $U\in SU(4)$ can be written

$$
U = (A_1\otimes A_0)\ \exp\!\big(i(k_xXX + k_yYY + k_zZZ)\big)\ (B_1\otimes B_0),
\qquad \tfrac\pi4\ge k_x\ge k_y\ge|k_z|,
\tag{6.1}
$$

with single-qubit unitaries $A_j,B_j$ and canonical coordinates $(k_x,k_y,k_z)$ in the Weyl
chamber. Two gates are locally equivalent iff their coordinates agree. Coordinates of common
gates: identity $(0,0,0)$; CNOT and CZ $(\pi/4,0,0)$; iSWAP $(\pi/4,\pi/4,0)$;
$\sqrt{\text{iSWAP}}$ $(\pi/8,\pi/8,0)$; SWAP $(\pi/4,\pi/4,\pi/4)$; $\sqrt{\text{SWAP}}$
$(\pi/8,\pi/8,\pi/8)$; ECR $(\pi/4,0,0)$. The compiler computes the coordinates of a
parametrised gate such as `fsim` numerically rather than from a table.

**CNOT count of an arbitrary two-qubit unitary** (Vatan–Williams, Shende–Markov–Bullock):
0 iff $(0,0,0)$; 1 iff $(\pi/4,0,0)$; 2 iff $k_z = 0$; 3 otherwise. Three CNOTs always
suffice. The compiler's `TwoQubitSynthesis` pass (spec 14 §4) computes (6.1) via the magic
basis and emits the minimal count, choosing between the target device's native entangler
(`cx`, `cz`, `ecr`, or `rxx`) by the equivalences of §2.3.

## 7. Circuits as unitaries

A circuit on $n$ qubits is a sequence of gates $G_1,\ldots,G_m$ (each embedded per T01 (3.2));
its unitary is $U = G_m\cdots G_2G_1$. Properties used throughout the specs:

- **Depth**: the length of the longest path in the gate DAG where two gates are connected if
  they share a qubit and neither commutes past the other. **Moments** (layers) partition the
  DAG into sets of gates on disjoint qubits; depth = number of moments in an ASAP schedule.
- **Equivalence**: two circuits are equivalent iff $U_1^\dagger U_2 \doteq I$, i.e.
  $|\mathrm{Tr}(U_1^\dagger U_2)| = 2^n$. The test suite checks $1 - |\mathrm{Tr}(U_1^\dagger U_2)|/2^n < 10^{-10}$
  for all programs with $n\le 10$ (spec 25 §4); for larger $n$ it checks output-distribution
  agreement on random inputs.
- **Inverse**: reverse the order and invert each gate; $R_{\hat n}(\theta)^{-1} = R_{\hat n}(-\theta)$,
  $T^{-1} = T^\dagger$, CNOT and SWAP are self-inverse.
- **Measurement placement**: a measurement followed only by gates that commute with $Z$ on the
  measured qubit (diagonal gates, or controls of controlled gates) can be moved earlier
  (deferred-measurement principle in reverse), which the scheduler uses to shorten readout
  idle time (spec 14 §7).

## 8. Native gate sets of the shipped devices

| Technology | Native set | Single-qubit cost | Entangler | Conversion to `cx` |
|------------|-----------|-------------------|-----------|--------------------|
| Fixed-frequency transmon, cross-resonance | `rz` (virtual), `sx`, `x`, `ecr` | ≤ 2 pulses of 20–40 ns | `ecr` 300–500 ns | §2.3 row 3 |
| Tunable-coupler transmon | `rz` (virtual), `sx`, `x`, `cz` | ≤ 2 pulses of 20–40 ns | `cz` 25–60 ns | `h` on target both sides |
| Tunable-coupler transmon (exchange type) | `rz`, `sx`, `x`, $\sqrt{\text{iswap}}$ or `fsim` | as above | 20–40 ns | 2 entanglers |
| Trapped ion (hyperfine) | `rz` (virtual), `rx(θ)`, `ry(θ)`, `rxx(θ)` (MS) | 1–20 μs | `rxx(π/2)` 100–300 μs | §2.3 row 4 |

Durations are `Model`-class ranges from published 2023–2025 devices and are overridden by the
device calibration file (spec 09). Every native gate carries an exact matrix from §1–2; the
compiler never emits a gate outside the target's native set.

## Where this is used

| Result | Used by |
|--------|---------|
| (1.1)–(1.2) gate table | Spec 07 gate kernels, spec 13 `stdgates.inc`, spec 21 gate labels |
| (1.4)–(1.5) Euler and native form | Spec 14 `SingleQubitMerge` and `NativeDecompose` passes |
| §1.5 virtual Z | Spec 14 §6 phase pushing, spec 10 frame semantics |
| (2.1)–(2.2) two-qubit matrices | Spec 07 kernels, spec 25 gate-matrix oracles |
| §2.3 conversions | Spec 14 `NativeDecompose`, spec 09 native gate sets |
| (3.1)–(3.2) ABC construction | Spec 14 `ctrl @` lowering |
| §3.2 Toffoli decompositions | Spec 14, spec 15 T-count resource estimates |
| §4 identities | Spec 14 optimization passes, spec 07 stabilizer conjugation table |
| §5.1 Gottesman–Knill | Spec 07 backend selection rules |
| §5.2 Solovay–Kitaev / Ross–Selinger | Spec 15 §5 fault-tolerant estimates, spec 16 |
| §6 KAK | Spec 14 `TwoQubitSynthesis` |
| §7 equivalence criterion | Spec 25 §4 compiler equivalence tests |
| §8 native sets | Spec 09 device descriptors |
