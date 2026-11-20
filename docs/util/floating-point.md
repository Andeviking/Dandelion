# FloatingPointValue and Concrete Evaluation

**Status.** The standalone value and concrete evaluator are implemented
in Phase 3.

## Format, value, and identity

`FpFormat{e, s}` keeps two `uint32_t` fields with read-only access. The
exponent and significand widths satisfy `e,s >= 2`, and
`e+s <= UINT32_MAX`. The significand width includes the implicit leading
bit. A checked format factory establishes the invariant by validating
exact Integer widths before narrowing. A width below two
gives `kInvalidArgument`. A positive width pair beyond any component
or sum capacity gives `kSizeLimit`. A valid format can hold special
values even if a later computation requires large backing storage.

`FloatingPointValue` occupies 24 bytes on the supported 64-bit target.
It contains the format, a `uint32_t limb_count`, an `FpCategory`
with an 8-bit underlying type, and a union of `uint64_t word` and
exclusive `uint64_t* limbs`. The categories, in order from zero, are
`kNaN`, `kPositiveInfinity`, `kNegativeInfinity`, `kPositiveZero`,
`kNegativeZero`, and `kFiniteBits` for nonzero finite values. Specials
carry only category and format, with a zero count and zero word. For a
finite nonzero value the payload is the packed sign/exponent/fraction
encoding. A zero count selects an inline word for at most 64 effective
bits. The wide form uses trimmed little-endian 64-bit limbs, with a
nonzero top limb. The declared format and the effective payload size
are independent. Move resets to F(2,2) positive zero.
[Shared ownership and failures](design.md)

In interchange order the high bit is the sign, followed by `e` exponent
bits and `s-1` stored fraction bits. An all-one exponent with a nonzero
fraction denotes the canonical abstract NaN, irrespective of sign or
payload. An all-one exponent with a zero fraction denotes signed infinity.
A zero exponent and zero fraction denote signed zero. All other patterns
are finite and nonzero. With sign `t`, exponent field `E`, fraction `f`,
and bias `B=2^(e-1)-1`, the finite magnitude and scale are

| Case | `M` | `k` in `(-1)^t * M * 2^k` |
| --- | --- | --- |
| Subnormal, `E=0` | `f` | `1-B-(s-1)` |
| Normal | `2^(s-1)+f` | `E-B-(s-1)` |

`FromBits` takes a BV of width exactly `e+s`. A checked packed Integer
overload accepts a nonnegative value of effective length at most
`e+s`. An encoding outside that range gives `kValueOutOfRange`.
`FromFields` derives the format from a BV1 sign, exponent field of
width `e`, and fraction field of width `s-1`, validates the resulting
format, and then classifies. A field-width mismatch or invalid
special-category argument gives `kInvalidArgument`. `Special`
constructs each canonical special without materializing a bit string.
Use field ranges and actual payload chunks for classification. Sparse
or short arguments need no width-sized classification mask.

Core equality and hash include the format and category, plus the packed
finite payload when present. Same-format NaNs compare equal under core
equality. The two zeros are distinct. Expose format, category, field
inspection, and the seven `fp.is*` predicates. Both sign predicates
are false on NaN and report the recorded sign on zeros and infinities.
Numerical comparison of finite same-format values uses ordered exponent
and fraction fields with sign handling and requires no dyadic expansion.
A checked representative-bits operation encodes exact interchange bits
for all non-NaNs and can encode NaN as sign zero, all-one exponent and
fraction one. That internal encoding convention gives no SMT-LIB
`fp.to_ieee_bv` operation and recovers no original NaN payload.
The frontend owns SMT-LIB rendering. [SMT-LIB FloatingPoint, 2024-07-21](https://smt-lib.org/theories-FloatingPoint.shtml)

## Ground semantics

`RoundingMode` has an 8-bit underlying type with `kRne`, `kRna`,
`kRtp`, `kRtn`, `kRtz` in that order starting at zero.
Validate supplied enum values. RNE rounds to nearest with ties to even,
RNA rounds to nearest with ties away from zero, RTP toward positive
infinity, RTN toward negative infinity, and RTZ toward zero. Arithmetic
and IEEE predicates require equal operand formats except for explicit
conversions. Mismatched formats and invalid modes give
`kInvalidArgument`. Apply each operation's special cases before
invoking finite arithmetic. The formal semantics determines the results
where the theory declaration is informal. [Formal FP semantics, §§ IV–VI](https://smt-lib.org/papers/BTRW15.pdf)

| Operation | Contract |
| --- | --- |
| `fp`, `to_fp(bits)` | Assemble fields or reclassify interchange bits, without rounding. |
| `fp.abs`, `fp.neg` | Clear or toggle the sign on non-NaNs, including specials. NaN remains canonical. |
| `fp.add`, `fp.sub`, `fp.mul`, `fp.div` | Evaluate the exact finite operator and round once. Subtraction uses addition with the second sign reversed. Apply the special cases below. |
| `fp.fma` | Round the exact product-plus-addend once. Apply the special rules for zero times infinity and for an infinite product combined with the opposite infinity. |
| `fp.sqrt` | Round the exact positive root. NaN, negative finite nonzero, and negative infinity give NaN. Positive infinity and each signed zero return themselves. |
| `fp.rem` | For finite operands and nonzero divisor, return the exact `x - n*y`, with `n` the nearest integer to exact `x/y`, ties to even. A zero result has the dividend's sign. |
| `fp.roundToIntegral` | Round to the integral FP lattice in the requested mode. Preserve the source sign on zero and return existing zeros, infinities, and NaN as supplied. When a rounded integer crosses beyond the largest finite integral value, map the virtual upper endpoint to signed infinity. |
| `fp.min`, `fp.max` | One NaN returns the other value, two NaNs return NaN. Ordered unequal operands select the appropriate one. Identical operands return that value. Opposite signed zeros give exactly the two allowed results. |
| `fp.eq`, `fp.lt`, `fp.leq`, `fp.gt`, `fp.geq` | False if either argument is NaN. Compare numerical values otherwise, treating both zeros as equal. The node evaluator handles adjacent chains. |
| `fp.isNormal`, `fp.isSubnormal`, `fp.isZero`, `fp.isInfinite`, `fp.isNaN`, `fp.isNegative`, `fp.isPositive` | Classify by the format and categories above. |
| `RoundRationalToFp` | Round an exact Rational in the specified target format and mode. Exact rational zero gives positive zero. |
| FP-to-FP, signed and unsigned BV-to-FP | Round the exact source value in the requested target format and mode. FP conversion preserves source signed zero, infinity sign, and canonical NaN. Interpret BV input explicitly as signed or unsigned. |
| Finite FP to exact Rational | Checked utility conversion returns the exact finite value. Both zeros map to `0/1`, with sign available separately from the source. NaN and infinity give `kInvalidArgument`. |
| `fp.to_sbv`, `fp.to_ubv` | Validate target width and mode, then round the source to an integer. Return a defined residue when the rounded integer fits the selected signed or unsigned range. NaN, infinities, and rounded out-of-range values give an unspecified result of that width. |

A NaN operand gives NaN for addition, subtraction, multiplication,
division, fma, and remainder. Opposite infinities in a sum, zero times
infinity, zero divided by zero, and infinity divided by infinity give
NaN. The fma cases above also give NaN. Otherwise, adding a lone
infinity or two infinities of the same sign returns that infinity.
Products and quotients use the XOR of signs, including finite nonzero
divided by zero, infinity divided by a finite operand, zero divided
by finite nonzero, and finite divided by infinity. For a zero exact
sum, equal-sign zero operands preserve their sign. Other exact zero
sums give negative zero only under RTN. Apply that rule to the exact
fma product and addend, taking the zero product's sign from its
multiplicands. A nonzero finite result rounded to zero keeps its
mathematical sign. Remainder gives NaN for infinite dividend or zero
divisor, and returns a finite dividend unchanged for an infinite
divisor.

For FP-to-unsigned-BV the range test applies to the rounded integer.
A negative fraction can round to zero and produce a defined BV.
`Min`, `Max`, `ToSignedBv`, and `ToUnsignedBv` use the tagged
outcomes in the shared design. Model callers keep choices consistent for
equal ordered semantic arguments. Opposite argument orders are separate
tuples and may choose independently. [FP outcomes and solver callers](design.md#concrete-fp-outcomes-and-solver-callers)

## Exact rounding kernel

Use GMP-backed integer arithmetic and a private finite `Dyadic` form
`(-1)^sign * M * 2^k`, with nonnegative integer magnitude `M` and
wide signed Integer exponent `k`. Keep the zero sign separately.
Operations may retain a fraction as `N/D * 2^k` with positive `N,D`
and separate sign. Use these representations of exponents and scales
through comparison and rounding. An exponent may exceed a machine word
while its signed binary representation is small. Compare exact
exponents and bit lengths before materializing shifts.

For a positive exact magnitude use bit lengths and a shifted
comparison to find its leading exponent `L=floor(log2(N/D))+k`.
Let `emin=1-B` and `emax=B`. The rounding quantum is `2^q`, with
`q=max(L,emin)-(s-1)`. For magnitudes well below half a subnormal
quantum or with `L>emax`, choose the result by mode, sign, and the
proven range before scaling. Otherwise compute the floor coefficient
and classify its fractional part as zero, below half, half, or above
half. Quotient/remainder of appropriately scaled operands gives this
classification by comparing twice the remainder to its divisor.
Use exact shifted comparisons when the same classification needs
no materialized shift. Round to the low or high coefficient by mode.
At a tie, RNE chooses the coefficient with an even low bit and RNA
chooses away from zero. Directed modes increase magnitude only on an
inexact result in their outward direction.

A rounded coefficient may carry into the next binade or from the
subnormal grid into normal range. Encode zero with the required sign.
At the top binade, use the virtual next power of two above maximum
finite as the outward endpoint. For nearest modes the midpoint
between maximum finite and that endpoint is the overflow threshold.
Round to signed infinity if the selected unbounded-exponent result
exceeds the finite exponent range. For directed overflow, outward
rounding produces signed infinity and inward rounding gives signed
maximum finite. Close to underflow and overflow, use exact half
comparisons rather than pre-rounding to a host FP format.

| Operator | Finite reduction |
| --- | --- |
| Add, subtract, fma | Sum two signed dyadics with the procedure below. For fma one term is the exact product. |
| Multiply, divide | Multiply magnitudes and add exponents. For division keep a numerator, denominator, and exponent difference and round the quotient once. |
| Square root | Halve the leading exponent, rounded downward, to locate the target binade. After scaling the radicand to the squared quantum, use integer square root of the floored radicand to get the low coefficient. Compare with the square of that coefficient for exactness and with the exact square of the midpoint for nearest rounding. |
| Remainder | Work with magnitudes and write `abs(x/y)=Mx*2^d/My`, where `d=kx-ky`. For `d>=0`, compute the numerator residue modulo `2*My` by modular exponentiation. The upper half of that residue gives the floor quotient's odd parity and its residue modulo `My` gives the remainder coefficient. Subtract `My` from the coefficient when nearest-even rounds the quotient upward. Apply the dividend's sign to the adjusted coefficient and scale by `2^ky`. For `d<0`, return the dividend directly when the quotient magnitude is at most one half. Otherwise use the same parity and adjustment rules with divisor `My<<(-d)` and scale `2^kx`. This shift is bounded by the significand lengths. The resulting dyadic is exact. |
| Integral rounding and FP-to-BV | Round against quantum one. An already integral source can stay in dyadic form. For `roundToIntegral` the rounded integer is either exactly encodable or the virtual integer endpoint at the next binade, which encodes as signed infinity. In F(2,4), RNE maps `3.5` to positive infinity and RTZ maps it to `3`. For FP-to-BV, compare the rounded magnitude and sign against the integer bounds before materializing a wide result. |
| Numeric conversions | Reuse the same rounder with the target format, exact components, and source sign. For exact conversion to Rational shift the numerator or denominator according to the exponent after cancelling powers of two, checking the actual count. |

For a sum, handle zero terms and order the remaining terms by leading
exponent. Let `q` be the rounding quantum at the term with the higher
leading exponent, and let `k` be that term's dyadic scale. Within
one eighth of the quantum, rounding boundaries lie on the
`2^(q-2)` lattice, including immediately below a binade boundary.
The dominant term lies on the `2^k` lattice. Their minimum lattice
spacing, `2^min(k,q-2)`, bounds every positive distance from the
dominant term to a rounding boundary in that interval.

When the tail is smaller than `2^(min(k,q-2)-1)`, use that power of two
with the tail's original sign. The replacement stays on the same side
of every rounding boundary, including one at the dominant term itself.
Otherwise align the original terms exactly. The materialized shift is
bounded by their significand lengths and the target precision. This
also covers cancellation and rounding after the exact product in FMA.

Check GMP and C++ storage requirements for every materialized shift,
product, and result. Keep the working exponent wide until a checked
size or shift count is needed. A calculation may report an actual
representation or C++ allocation failure under the shared policy.
The interval path must prove the same rounded result as exact
arithmetic. MPFR is an optional test-only oracle. The runtime kernel
uses GMP and has no MPFR, host FP environment, exception-flag, or
symbolic lowering dependency. [Numeric tests](../testing/util.md),
[research evidence](research.md)
