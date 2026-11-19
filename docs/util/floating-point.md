# FloatingPointValue and Concrete Evaluation

Status: **proposed design for review**. Node classification and NaN
normalization already exist; the value class, arithmetic and concrete
evaluator do not. This is a complete semantic design with an initial exact
algorithm, not a claim that symbolic FP lowering or FP solving is implemented.

The frontend's numeric bridge exposes checked `RoundRationalToFp` for
concrete model rendering. It returns a sign and exact stored exponent and
trailing significand, or a signed infinity. The implementation uses an
exact GMP quotient and remainder, including subnormal and overflow cases.
The broader ground evaluator described below remains future work.

## Value and format

`FloatingPointValue` owns `(e,s,category,finite_bits)` independently of any
node manager. `e,s >= 2`, and the retained node representation requires
`e+s <= UINT32_MAX`. An independently checked factory distinguishes invalid `e,s`
from a valid but unrepresentable size. A format can be accepted even when a
later operation encounters an actual allocation or representation failure.
There is no Float32/Float64 assumption. Category is NaN, positive/negative
infinity, positive/negative zero, or nonzero finite interchange bits. For
each format all NaN bit patterns
become one abstract NaN, while the two zeros differ under core equality.
For typed objects format is part of equality and hash; SMT-LIB's formal
description shares the abstract NaN across sorts, but nodes and values are
typed and comparisons require the same format.

`FromBits(format,bv)` requires exactly `e+s` bits and classifies the sign,
`e` exponent bits and `s-1` fraction bits. An alternate checked Integer
factory requires nonnegative bits with bit length at most `e+s`, matching
`MakeFpBits`. `Special(format,kind)` constructs special values without
materializing a huge bit string. `Classify`, sign, normal/subnormal, zero,
infinite and NaN predicates, core equality/hash and IEEE comparisons are
read-only. `fp.isNegative` and `fp.isPositive` are both false for NaN. IEEE
`fp.eq` is false for NaN and true for opposite zeros; ordered comparisons
are false if either operand is NaN. Core equality identifies same-format NaNs
and distinguishes zeros. A representative NaN bit string may be produced for
internal encoding only when a concrete bit-vector is actually needed; it is
not a unique inverse of `to_fp` or a supported `fp.to_ieee_bv` input.

Diagnostic output includes the format and category, or the exact sign,
exponent and fraction fields for finite nonzero values. The SMT-LIB model
printer can emit the standard special forms and `(fp sign exponent fraction)`
for other values; it must use exactly the declared field widths and preserve
signed zero. Printing a huge field can require substantial output and storage.
Neither a formatted decimal approximation nor an arbitrary NaN encoding is a
replacement for the value's core identity.

For nonzero finite bits, let `B=2^(e-1)-1`, exponent field `E`, fraction
integer `f` and sign bit `t`. The exact value is
`(-1)^t * f * 2^(2-B-s)` when `E=0`, and
`(-1)^t * (2^(s-1)+f) * 2^(E-B-(s-1))` otherwise. The field with all exponent
bits one is infinity when `f=0`, NaN otherwise. This definition includes
subnormals. The bias or exponent may itself be too large to materialize: use
checked symbolic/large-integer exponent comparisons or return a resource
outcome before an oversized shift. Classification and special construction
must not allocate a `2^e` mask merely to inspect a short bit payload.

`RoundingMode` has exactly RNE (ties to even), RNA (ties away), RTP, RTN and
RTZ. Validate an externally supplied enum. Move its single definition from
the node interface to a shared FP-facing module and re-export it, preserving
enumerator names, values and rebuilt-source compatibility, without promising
a binary ABI. Representation,
borrowed views and failure guarantees follow [design.md](design.md); a node
view pairs the existing sort and payload without allocating or extending its
lifetime.

## Ground operation contract

Arithmetic requires equal operand formats except explicit conversion.
Every operation either returns a **defined** value, an allowed **choice**, or
an **unspecified** result; the enclosing `NumericResult` separately reports
invalid input, limits, cancellation and allocation failure. A choice is a
semantic outcome, never an allocation error. The caller must not turn a
choice or unspecified result into an unconditional rewrite.

| Input operations | Concrete result and special requirements |
| --- | --- |
| `fp`, `to_fp(bits)` | Combine fields or reinterpret packed bits, then classify and canonicalize NaN. These do not perform numeric rounding. |
| `abs`, `neg` | Exact sign transformation, with abstract NaN remaining NaN and signed zero/infinity handled explicitly. |
| `add`, `sub`, `mul`, `div` | IEEE/SMT-LIB exact special-case rules; otherwise calculate the exact mathematical result and round once in the requested mode. Preserve signed-zero rules, including exact cancellation under RTN. FP division by zero has specified FP results and is not integer division by zero. |
| `fma` | Compute `a*b+c` with unbounded exact intermediate and **one** final rounding, including its special cases. Never round the product first. |
| `sqrt` | Exact square-root rounding; negative finite nonzero and negative infinity produce NaN; `sqrt(-0)=-0`. |
| `rem` | IEEE remainder `x - n*y`, `n` nearest integer to exact `x/y` with ties to even, and specified NaN/infinity/zero cases; a zero result has the dividend's sign. No RM argument. |
| `roundToIntegral` | Round the exact finite value to an integer according to the RM and encode it as FP; preserve the required sign of zero. NaN and infinities remain their categories. |
| `min`, `max` | For a single NaN return the other argument, both NaN return NaN; ordered unequal numbers select the appropriate operand. Opposite signed zeros return a choice restricted to those two values. Do not assume the reversed argument order has the same choice unless justified by the applicable formal semantics. |
| `eq`, `lt`, `leq`, `gt`, `geq`; classification | IEEE comparison as above; node evaluator folds chainable comparisons over adjacent arguments. |
| FP-to-FP, signed/unsigned BV-to-FP, Rational-to-FP | Numeric conversion with explicitly supplied RM and target format, exact source interpretation and one correct rounding; preserve FP source signed zero and specials. A Real/rational zero has no negative zero. |
| Finite FP to exact dyadic or Rational | Return the exact finite mathematical value; expose zero's sign separately if the caller needs it. NaN and infinity have no Rational value. Materializing a numerator or power-of-two denominator may require substantial storage even when the FP value is already representable. |
| `fp.to_sbv`, `fp.to_ubv` | Round finite source to an integer first, then check the respective signed/unsigned `w`-bit range and encode. NaN, infinity, or a rounded integer outside the range yield an unspecified `BV(w)` value, not a chosen fixed bit pattern. A negative fraction rounded to zero can be in range. |

For the arithmetic rows, the special-case dispatch is part of the contract:

- A NaN input produces NaN in arithmetic, including fma, sqrt, rem and
  integral rounding. In addition, adding opposite infinities, multiplying
  zero by infinity, dividing zero by zero or infinity by infinity, and fma
  with a zero-times-infinity product or an infinite product plus the
  opposite infinity produce NaN. Subtraction applies the corresponding
  addition rule after negating its second operand.
- Otherwise, multiplication and division use the XOR of operand signs for
  zero and infinity results. Finite nonzero divided by signed zero gives
  signed infinity; zero divided by finite nonzero or finite divided by
  infinity gives signed zero; an infinite product or quotient has the
  corresponding signed infinity. Addition of a lone infinity yields it.
- For an exact zero sum, equal-sign zero operands retain their common sign;
  otherwise the result is negative zero under RTN and positive zero under
  the other modes. Apply the same rule to fma's exact product-plus-addend,
  including signed zero products, with only one rounding.
- Remainder is NaN for an infinite dividend or zero divisor; a finite
  dividend with an infinite divisor returns the dividend. An exact zero
  remainder carries the dividend's sign. `roundToIntegral` preserves the sign
  of a zero result, and leaves infinity and existing zeros unchanged.

The [formal FP semantics](https://www.ccs.neu.edu/home/wahl/Publications/btrw15.pdf)
has precedence where the [theory declaration](https://smt-lib.org/theories-FloatingPoint.shtml)
is inconsistent: its informal note calls all negative inputs to unsigned
conversion out of range, while the formal rule checks the rounded integer.
The latter is the existing Dandelion node contract. No exception flags,
signaling NaN payloads, host FP environment, or general Real expression is
part of this value domain.

For opposite-zero min/max the caller retains the allowed two-result set; for
invalid FP-to-BV it retains the whole destination domain. The solver/model
must assign one interpretation per operation, format/indices and equal
**semantic argument tuple**, including RM and canonical NaN, across distinct
node identities, UF, arrays and DT fields. Separate operation symbols and
ordered argument tuples cannot be equated without proof. A model may choose
an allowed result, but a rewrite that commits to one choice can remove other
valid models and change satisfiability. Symbolic encoding must preserve the
same constraints and congruence, including when multiple NaN bit patterns
encode the same value.

## Exact baseline without MPFR

Use GMP-backed Integer/Rational machinery, with a private signed dyadic
`(sign, significand, exponent)` working form for finite FP inputs. It is an
algorithm-local representation, not a fifth public numeric domain. Handle
special values before finite arithmetic. Compute exact dyadic sums/products,
an exact quotient for division and rational conversion, and a common rounding
routine with guard/remainder or exact comparisons. The input sign and signed
zero must be carried separately because rational zero loses that information.
For sqrt, scale the positive rational/dyadic to the target binade and compare
integer squares against exact midpoint squares; integer square root plus
remainder suffices to select adjacent representable values without host FP.
For `rem`, determine the nearest-even integer quotient exactly before the
subtraction. Operations may exploit early exponent comparisons and sticky
bits to avoid enormous shifts, but an optimization must prove equivalence to
exact rounding, especially for cancellation and ties.

The rounding routine identifies the target normal binade or subnormal quantum,
computes floor quotient and exact remainder, and selects adjacent candidates:
RNE compares twice the remainder with the denominator and breaks a tie by an
even low bit; RNA breaks a tie away from zero; directed modes choose according
to sign and direction. Normalize a carry into the next binade, preserve
required zero signs, and apply IEEE overflow after precision rounding: nearest
and outward-directed rounding can produce infinity, inward-directed rounding
produces the signed maximum finite value. Test both sides of each boundary.
`fma` must retain enough exact intermediate information to avoid double
rounding. Exponent differences, shifts, intermediate products, square roots
and output size are checked against actual representation and allocation
requirements. Do not add a discretionary computation budget. If exact
computation cannot complete, retain a term symbolically or use another sound
path, subject to the solver's result contract. Never
round an approximation without an error bound proving the same answer.

This baseline is feasible using GMP's integer primitives but may be expensive,
particularly for huge exponent differences, division, fma and sqrt; no
performance claim is made. A future MPFR backend is an alternative only after
an explicit dependency decision. MPFR precision and exponent range are
finite; emulate target subnormals and overflow with the correct ternary
rounding information, support RNA correctly, and isolate any library exponent
range/thread-local state. Verify NaN, signed zero, all SMT underspecified
cases and target formats independently; MPFR alone does not supply symbolic
FP-to-BV encoding or guarantee support for every representable format. See
[MPFR 4.2.1 manual](https://www.mpfr.org/mpfr-4.2.1/mpfr.pdf).

The proposed architecture's symbolic word-blaster is a separate component.
SymFPU is a candidate, not a dependency selected here; it requires its own
version, license, format support and consistency evaluation. Even if the same
library can evaluate concrete and symbolic operations, those paths are not
independent correctness oracles.
