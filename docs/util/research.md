# Numeric Architecture Research

**Status.** Research supporting the active Phase 1B design. The current
design choices are specified in the type documents. The observations below
describe pinned sources and small historical experiments.

## Exact value representations

| Reference | Observation and design implication |
| --- | --- |
| Bitwuzla 0.10.0 | Integer owns an `mpz_t`. BV uses an unsigned word for small declared widths selected according to the GMP-facing machine word type, with GMP backing for wider widths. Dandelion stores up to 64 effective bits inline independently of declared width, and gives wide values a fixed 64-bit limb representation. [Integer](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/util/integer.h), [BV](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/bv/bitvector.h) |
| cvc5 `282d5e0c84baf49c780704c532f87ba66d8b513d` | The GMP build wraps Integers and Rationals through GMP C++ types. Its BV value pairs width with an Integer. Dedicated unsigned BV storage in Dandelion keeps BV64's high half inline while preserving a full signed word for Integer. [Integer](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/integer_gmp_imp.h), [Rational](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/rational_gmp_imp.h), [BV](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/bitvector.h) |
| Z3 `430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e` | Its small/large Integer and rational machinery illustrate how local representation choices can avoid allocations. Its FP implementation uses a multiprecision significand, an `int64_t` exponent, and explicit format bounds. Dandelion keeps the complete signed word inline and uses a wide FP working exponent to match its own format boundary. [Integer](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/mpz.h), [Rational](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/rational.h), [FP](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/util/mpf.h) |
| Yices 2 `ccdf8998809c82763c683c5e4d9f78eabe6a24e3` | Its Rational supports a small fraction or a GMP rational, and its BV constants use separate word and limb implementations. Positivity of the canonical denominator gives Dandelion a zero tag with no lost denominator bits or pointer tagging. [Rational](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/rationals.h), [BV64](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/bv64_constants.h), [Wide BV](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/bv_constants.h) |

The GMP C API provides exact integer and rational primitives and
integer square root. An owning result can take over an existing
buffer by swap. Stack-backed read-only GMP headers let word
operands reach exact fallback routines without creating owning GMP
word buffers. The BV limb fast path additionally requires exact
limb-type and bit-width compatibility. GMP's actual allocation
counts can be narrower than its public index types, and its allocator
requires fatal behavior for OOM. [GNU MP 6.3 manual](https://gmplib.org/gmp-man-6.3.0.pdf)

## FP semantics and reference independence

The selected SMT-LIB Int, BV, and FP documents govern mathematical
results. The FP declaration gives the formal report precedence where
they differ. In particular, FP-to-BV checks the rounded integer for
range, and each opposite-zero min/max tuple has two permitted results.
Integral rounding uses the integral subset of the FP domain, including
infinity when rounding crosses the top integral endpoint of tiny
formats. [Ints](https://smt-lib.org/theories-Ints.shtml),
[FixedSizeBitVectors](https://smt-lib.org/theories-FixedSizeBitVectors.shtml),
[FloatingPoint](https://smt-lib.org/theories-FloatingPoint.shtml),
[formal FP semantics](https://smt-lib.org/papers/BTRW15.pdf)

Bitwuzla 0.10.0 uses MPFR-backed concrete FP and a separate SymFPU
representation for symbolic lowering. The pinned cvc5 source can
use an MPFR or SymFPU concrete backend. Both snapshots pin the same
upstream SymFPU commit with project-specific integration. A later
Bitwuzla patch documents small-format issues in integral rounding and
conversion. Two solver runs can therefore share assumptions,
implementation, or bugs. Test oracle records need actual formats,
backend selections, revisions, and flags. [Bitwuzla FP](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/fp/floating_point.cpp),
[Bitwuzla SymFPU pin](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/subprojects/symfpu.wrap),
[cvc5 concrete selection](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/util/floatingpoint_literal.cpp),
[cvc5 dependency build](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/cmake/FindSymFPU.cmake),
[Bitwuzla tiny-format fixes](https://repo.or.cz/bitwuzla.git/commit/ed2819a17af7fd2559770c58dce9f3f91e9d9dd6)

The concrete runtime uses GMP and exact dyadics so its exponent
semantics follow `FpFormat` under project control. MPFR is an optional
test oracle for its supported precision and exponent ranges. A valid
comparison must account for target subnormals, signed zeros,
rounding-mode support, and overflow. Berkeley SoftFloat Release 3e
offers another reference for supported IEEE formats. Choice sets,
arbitrary formats, and the selected SMT-LIB conversion semantics
need their own exact expectations. [MPFR 4.2.1 manual](https://www.mpfr.org/mpfr-4.2.1/mpfr.pdf),
[SoftFloat 3e documentation](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)

The chosen layouts and direct paths describe storage and expected
allocation behavior. The following examples are semantic cross-checks.
They provide no Dandelion performance measurements.

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
measurement, or a proof of the planned evaluator.

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
