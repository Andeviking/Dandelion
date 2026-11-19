# Numeric Representation Research

Status: **research evidence for the proposed design**, 2026-11-16. This is a
study of the linked versions and selected files, not an exhaustive audit of
every configuration or a benchmark. Revisions below are fixed tags or commits;
the standard pages state their theory revision. Source observations are not
measurements of Dandelion. The choices derived from them are independent.

## Bitwuzla 0.10.0

- [`Integer`](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/util/integer.h)
  owns an `mpz_t`; it is not the small/large tagged Integer proposed here.
  Its API exposes GMP for its own implementation. This is evidence of one
  design, not a reason for Dandelion to expose GMP.
- Its [`BitVector`](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/bv/bitvector.h)
  stores width and a union of an inline unsigned word or GMP integer. The
  inline threshold follows `unsigned long`: typically 64 bits on 64-bit Unix
  and 32 on 64-bit Windows, to avoid truncation at GMP interfaces. Its
  operators include the usual modular, signed and unsigned BV operations.
  This width-dependent split suggests a useful future comparison, not a
  measured advantage over Dandelion's compatibility-oriented baseline.
- [`FloatingPoint`](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/fp/floating_point.h)
  stores format sizes and an MPFR value; its
  [implementation](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/fp/floating_point.cpp)
  sets exponent ranges, handles nearest-away and subnormalization, and
  canonicalizes a NaN when producing bits. The separate
  [SymFPU representation](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/fp/floating_point_symfpu.h)
  and [symbolic wrapper](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/solver/fp/symfpu_wrapper.cpp)
  support word-blasting. These paths are related but distinct responsibilities;
  MPFR is not thereby adopted by Dandelion. The
  [0.10.0 SymFPU wrap](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/subprojects/symfpu.wrap)
  pins `40bdec00e99f8ea1b96c3dac0a05eed11c541639` and lists two local patches,
  including experimental-format fixes. Exact supported formats and behavior
  depend on that release's configuration; a solver's rejection of a tiny
  format is not evidence that Dandelion may ignore its own accepted sorts.

## cvc5 commit `282d5e0c84baf49c780704c532f87ba66d8b513d`

- Its GMP-backed [`Integer`](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/integer_gmp_imp.h)
  uses `mpz_class`, while its
  [Rational](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/rational_gmp_imp.h)
  uses canonicalized `mpq_class`; alternative CLN implementation files also
  exist. Its [`BitVector`](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/bitvector.h)
  pairs a `uint32_t` size with an Integer. This supports considering a direct
  Integer-backed BV, but neither GMP wrapper has Dandelion's present inline
  signed Integer contract.
- [`FloatingPointLiteral`](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/floatingpoint_literal.cpp)
  selects an MPFR or SymFPU concrete backend at build time. The
  [MPFR backend](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/floatingpoint_literal_mpfr.cpp)
  includes subnormal handling and a special nearest-away path; the
  [SymFPU backend](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/floatingpoint_literal_symfpu.h)
  uses an unpacked SymFPU literal. Symbolic FP also uses SymFPU. Its
  [dependency configuration](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/cmake/FindSymFPU.cmake)
  pins the same upstream commit as the Bitwuzla wrap for its downloaded
  dependency, though system installations and downstream patches can differ.
  A shared bug or shared semantics can therefore produce correlated answers.
  The [project release notes](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/NEWS.md)
  document previous SymFPU divider fixes and MPFR backend changes; this is a
  reason to pin and test, not a claim about the correctness of this commit.

## Z3 commit `430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e`

- [`mpz`](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/mpz.h)
  distinguishes an inline `int` from a large value and has internal and GMP
  build paths. [`rational`](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/rational.h)
  builds on its numeral managers. BV numerals use a rational-valued numeral
  parameter, with width recorded in the numeral declaration and the AST
  sort; BV rewrites operate with that size. See the
  [BV declaration plugin](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/ast/bv_decl_plugin.cpp)
  and [BV rewriter](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/ast/rewriter/bv_rewriter.cpp).
  This is close to Dandelion's *current node* arrangement, not proof that a
  standalone width-bearing BV value is unnecessary.
- Its [`mpf`](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/mpf.h)
  stores format, sign, multiprecision significand and `int64_t` exponent,
  and exposes five rounding modes through its own manager. It has explicit
  field-width limits. Its separate
  [FP-to-BV converter](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/ast/fpa/fpa2bv_converter.cpp)
  handles symbolic expressions and treats NaN in equality. An independent
  implementation is useful for differential testing within its actual format
  limits, but its behavior is not the standard and does not prove correctness.

## Yices 2 commit `ccdf8998809c82763c683c5e4d9f78eabe6a24e3`

- [`rational_t`](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/rationals.h)
  stores a small signed numerator and denominator or a tagged pointer to GMP
  `mpq`; it offers explicitly Euclidean SMT division. This combines small-value
  arithmetic and a general rational domain. Its pointer tagging, allocation,
  mutability and C portability assumptions are specific to that implementation.
  The idea of testing a fast small representation is useful; copying the
  tagging scheme is not a Dandelion decision.
- [Small BV constants](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/bv64_constants.h)
  use a `uint64_t` with a separately supplied width; larger
  [BV constants](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/bv_constants.h)
  use least-significant-first 32-bit words, normalization and explicit size.
  Their existence demonstrates another storage option, not a performance
  ranking. The files provide conversion and arithmetic primitives; the width
  and ownership rules must accompany any use of the raw arrays.
- The inspected snapshot does not provide an FP value implementation or
  symbolic FP lowering comparable to the above systems; no claim of a Yices
  FP oracle is made. Its [README](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/README.md)
  identifies GMP and optional libpoly/CUDD for MCSAT. Its
  [source overview](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/doc/NOTES)
  locates polynomial and interval machinery in term/theory-specific code,
  supporting an algorithm-owned boundary rather than a universal numeric type.

## Standards and libraries

[SMT-LIB Ints, 2026-01-16](https://smt-lib.org/theories-Ints.shtml),
[FixedSizeBitVectors, 2025-09-09](https://smt-lib.org/theories-FixedSizeBitVectors.shtml),
the [QF_BV extensions](https://smt-lib.org/logics-all.shtml), and
[FloatingPoint, 2024-07-21](https://smt-lib.org/theories-FloatingPoint.shtml)
define the target semantics in the selected profile. The FP declaration
explicitly gives its [formal semantics](https://www.ccs.neu.edu/home/wahl/Publications/btrw15.pdf)
precedence over its informal text. The formal document describes the
opposite-zero min/max choice and the FP equality distinction.

[GMP 6.3](https://gmplib.org/manual/) supplies exact integers, rationals,
division variants and integer square root; it does not by itself implement
the complete FP format, five rounding modes, specials, or underspecification.
Its [allocation policy](https://gmplib.org/manual/Custom-Allocation) limits
recoverability. [MPFR 4.2.1](https://www.mpfr.org/mpfr-4.2.1/mpfr.pdf)
provides correctly rounded arithmetic with specified precision and exponent
limits; simulating target subnormals and nearest-away still requires careful
use of its interface. No MPFR is installed or selected here.

The [SymFPU README at the shared commit](https://github.com/martin-cs/symfpu/blob/40bdec00e99f8ea1b96c3dac0a05eed11c541639/README.md)
describes both concrete and symbolic instantiations and says its concrete
instantiation is not expected to match a dedicated software FP library's
performance. That is an upstream author statement, not a Dandelion measurement.
[Berkeley SoftFloat Release 3e](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)
is a useful additional IEEE concrete reference for its supported fixed formats,
rounding and special-case comparison, not an arbitrary-format SMT-LIB or
underspecified-function oracle. License, build and common ancestry must be
checked before future integration; it is not a dependency of this proposal.

No Dandelion numeric benchmark or performance experiment was run. Statements
about allocation, complexity and potential fast paths in these documents are
mechanistic expectations, not measured speedups. Reproducible tests and future
measurements must record versions, configured backends and actual independence.

## Small hand-case cross-check

On 2026-11-16, Python 3.8.10's exact `Fraction` and the independently listed
positive F(2,3) values were used to check the rounding examples in
[numeric testing](../testing/util.md#fp-cases-and-independent-oracle). The
following standalone command is the complete experiment. The last grid entry
`4` is the next unbounded-exponent value after maximum finite and is rendered
as infinity when selected; the list index has the low significand bit as its
parity. The tested inputs are bounded by that entry.

```sh
python3 - <<'PY'
from bisect import bisect_left
from fractions import Fraction as F
v = [F(0), F(1,4), F(1,2), F(3,4), F(1), F(5,4), F(3,2),
     F(7,4), F(2), F(5,2), F(3), F(7,2), F(4)]
def rnd(x, mode):
    sign = -1 if x < 0 else 1
    a = abs(x)
    hi = bisect_left(v, a)
    if hi < len(v) and v[hi] == a:
        i = hi
    elif mode in ('RNE', 'RNA'):
        lo = hi - 1
        dl, dh = a - v[lo], v[hi] - a
        i = (lo if dl < dh else hi if dh < dl else
             hi if mode == 'RNA' else lo if lo % 2 == 0 else hi)
    else:
        away = ((mode == 'RTP' and sign > 0) or
                (mode == 'RTN' and sign < 0))
        i = hi if away else hi - 1
    if i == len(v) - 1:
        return '-inf' if sign < 0 else '+inf'
    if i == 0:
        return '-0' if sign < 0 else '+0'
    return str(sign * v[i])
for x in (F(9,8), F(-9,8), F(1,8), F(-1,8), F(15,4), F(-15,4)):
    print(str(x), *(rnd(x, m) for m in ('RNE','RNA','RTP','RTN','RTZ')))
print('fma exact', F(3,2)*F(3,2)-2, 'rounded product', rnd(F(9,4),'RNE'))
print('sqrt midpoint squared', F(13,8)**2, 'input', F(5,2))
print('remainder', F(7,2)-4)
PY
```

The six rows, in mode order RNE, RNA, RTP, RTN, RTZ, were respectively
`1 5/4 5/4 1 1`, `-1 -5/4 -1 -5/4 -1`,
`+0 1/4 1/4 +0 +0`, `-0 -1/4 -0 -1/4 -0`,
`+inf +inf +inf 7/2 7/2`, and `-inf -inf -7/2 -inf -7/2`.
The other lines gave exact fma `1/4`, rounded product `2`, sqrt midpoint
square `169/64` greater than input `5/2`, and remainder `-1/2`. This checks
only those arithmetic examples and depends on the hand-derived grid; it is
not an independent SMT solver, an FP implementation test, a performance
measurement, or a proof of the proposed evaluator.

## Small local differential check

The following single-query formula asks whether the F(2,3) FMA result differs
from `1/4`. The arguments encode `3/2`, `3/2`, and `-2`; their exact fused
result is `9/4 - 2 = 1/4`, representable without rounding. On 2026-11-16,
local `Z3 version 4.13.1 - 64 bit` returned `sat`; local Bitwuzla `0.7.0`
returned `unsat`. Z3's `get-value` for the FMA returned `(fp #b0 #b01 #b00)`,
which denotes `1`. The existing local cvc5 1.4.2, git `282d5e0c84`, returned
`unsat` with `--fp-exp`; its default mode rejected this format. The cvc5
version matches the fixed source snapshot above; the local Z3 and Bitwuzla
versions are different from those snapshots. No tool was installed or modified.

```sh
query='(set-logic QF_FP)
(assert (not (= (fp.fma RNE ((_ to_fp 2 3) #b00110)
                             ((_ to_fp 2 3) #b00110)
                             ((_ to_fp 2 3) #b11000))
                ((_ to_fp 2 3) #b00001))))
(check-sat)'
printf '%s\n' "$query" | z3 -in
printf '%s\n' "$query" | bitwuzla
cvc5_bin=/home/zhangx/solvers/SMT/cvc5/build-1.4.2-static/bin/cvc5
printf '%s\n' "$query" | "$cvc5_bin" --lang smt2 --fp-exp
```

Six other one-formula local Z3 4.13.1 checks for F(2,3) agreed with the
hand-derived RNE/RNA `9/8` rounding, RNE `15/4` overflow, RTZ conversion of
`-1/4` to unsigned BV2, `sqrt(5/2)`, and `rem(7/2,1)`. The FMA disagreement
is a concrete reason to preserve a hand-derived tiny-format test and to record
oracle versions; it is not a claim about newer Z3 versions, all Z3 paths,
Bitwuzla correctness, or Dandelion's unimplemented evaluator.

The cvc5 executable was found outside `PATH` at the path above after the
initial `PATH` check. Its `--version` reports cvc5 1.4.2 and SymFPU, and its
build's `flags.make` defines `CVC5_USE_MPFR`; the executable also contains
MPFR symbols. This records build evidence, not which backend discharged each
particular query. `--fp-exp` explicitly enables experimental formats and the
tool warns of known issues. Under `QF_FP`, cvc5 rejected the three Real `/`
examples at parsing; with `ALL` and `--fp-exp` they returned `unsat` for the
negations of the hand-derived RNE/RNA `9/8` and RNE `15/4` results. Under
`QF_FP` with `--fp-exp`, the negations of the FMA, `sqrt(5/2)`,
`rem(7/2,1)`, and RTZ unsigned conversion of `-1/4` each returned `unsat`.
Those parser rejections are not passing comparisons.

The individual comparisons can be repeated with the following command. The
first three cvc5 queries deliberately use `ALL`; all other queries use
`QF_FP`, as did the local Z3 runs. Each invocation has one `check-sat`.

```sh
python3 - <<'PY'
import subprocess
cvc5 = '/home/zhangx/solvers/SMT/cvc5/build-1.4.2-static/bin/cvc5'
engines = {'z3': ['z3', '-in'], 'cvc5': [cvc5, '--lang', 'smt2', '--fp-exp']}
checks = {
  'rne_9_8': '(= ((_ to_fp 2 3) RNE (/ 9.0 8.0)) ((_ to_fp 2 3) #b00100))',
  'rna_9_8': '(= ((_ to_fp 2 3) RNA (/ 9.0 8.0)) ((_ to_fp 2 3) #b00101))',
  'rne_15_4': '(= ((_ to_fp 2 3) RNE (/ 15.0 4.0)) (_ +oo 2 3))',
  'negative_ubv': '(= ((_ fp.to_ubv 2) RTZ (fp #b1 #b00 #b01)) #b00)',
  'fma': '(= (fp.fma RNE ((_ to_fp 2 3) #b00110) '
         '((_ to_fp 2 3) #b00110) ((_ to_fp 2 3) #b11000)) '
         '((_ to_fp 2 3) #b00001))',
  'sqrt': '(= (fp.sqrt RNE ((_ to_fp 2 3) #b01001)) ((_ to_fp 2 3) #b00110))',
  'remainder': '(= (fp.rem ((_ to_fp 2 3) #b01011) '
               '((_ to_fp 2 3) #b00100)) ((_ to_fp 2 3) #b10010))',
}
for engine, cmd in engines.items():
    for i, (name, formula) in enumerate(checks.items()):
        logic = 'ALL' if engine == 'cvc5' and i < 3 else 'QF_FP'
        script = f'(set-logic {logic})\n(assert (not {formula}))\n(check-sat)\n'
        p = subprocess.run(cmd, input=script, text=True, capture_output=True,
                           timeout=10)
        print(engine, name, logic, p.returncode, p.stdout.strip(), p.stderr.strip())
PY
```

For the following additional one-query-at-a-time checks, local Z3 and cvc5
returned `sat`, `sat`, `unsat`, respectively. Bitwuzla 0.7.0 rejected the
first two as unsupported experimental formats and returned `unsat` on the
third. Its ability to fold the preceding FMA therefore does not establish
general F(2,3) support. The queries test preservation of both opposite-zero
choices and congruence on two encodings of semantic NaN:

```sh
python3 - <<'PY'
import subprocess
engines = {
    'z3': ['z3', '-in'],
    'bitwuzla': ['bitwuzla'],
    'cvc5': ['/home/zhangx/solvers/SMT/cvc5/build-1.4.2-static/bin/cvc5',
             '--lang', 'smt2', '--fp-exp'],
}
checks = {
    'min_not_positive':
        '(distinct (fp.min (_ +zero 2 3) (_ -zero 2 3)) (_ +zero 2 3))',
    'min_not_negative':
        '(distinct (fp.min (_ +zero 2 3) (_ -zero 2 3)) (_ -zero 2 3))',
    'nan_conversion_congruence':
        '(distinct ((_ fp.to_ubv 2) RNE ((_ to_fp 2 3) #b01101)) '
        '((_ fp.to_ubv 2) RNE ((_ to_fp 2 3) #b11111)))',
}
for name, cmd in engines.items():
    for case, assertion in checks.items():
        script = '(set-logic QF_FP)\n(assert ' + assertion + ')\n(check-sat)\n'
        p = subprocess.run(cmd, input=script, text=True, capture_output=True,
                           timeout=10)
        print(name, case, p.returncode, p.stdout.strip(), p.stderr.strip())
PY
```

These are nine tiny executions, not a comprehensive differential suite. The
cvc5 and Bitwuzla paths can share SymFPU-derived logic; agreement is not two
fully independent proofs. Their formats, options, parser profiles, and actual
evaluation paths must be recorded in any future comparison.
