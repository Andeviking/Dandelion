# Rational

**Status.** The standalone value and arithmetic are implemented in Phase 3.

## Canonical value and storage

A `Rational` denotes `n/d` with `d > 0`, `gcd(abs(n),d)=1`, and unique
zero `0/1`. Normalize after construction and every operation. Move a
negative denominator's sign into the numerator before publishing the
result. Equal fractions have identical canonical components and equal
hashes.

Use 16 bytes on the supported 64-bit target. The first word is a union of
`int64_t` numerator and an exclusive opaque pointer to a holder for one
`mpq_t`. The second word holds the `uint64_t` denominator or the zero
sentinel for the pointer alternative. A real denominator is always
positive, so zero can select the pointer without sacrificing any valid
word fraction or consuming pointer bits. A reduced fraction fits inline
whenever its numerator fits `int64_t` and its denominator fits
`uint64_t`. This includes `INT64_MIN/1` and `1/UINT64_MAX`.
Inline storage, copying, borrowing, and the word fast paths use no heap
allocation. Wide copies own independent GMP components. Move resets to
`0/1`. [Shared ownership](design.md#ownership-and-public-operations)

`Rational::Borrow()` returns the numerator and denominator as
`IntegerView`s. Word views carry their values directly. Large components
borrow read-only GMP limbs. A published node can also supply a view of
its packed components. The component accessors allocate no storage.
A borrowed component shares its backing storage's lifetime bound.
An owning Rational reports the size of its holder and significant limbs
once.

## Operations

| Operation | Contract |
| --- | --- |
| `Make(n,d)` and Integer promotion | Copy the exact inputs and reduce. Zero denominator gives `kDivisionByZero`. Integer promotion uses denominator one. |
| `ParseDecimalFraction` | Decode two nonempty ASCII digit sequences separated by one dot as an exact fraction over a power of ten. Reject signs, spaces, exponents, and malformed forms with `kInvalidText`. Trim trailing fractional zeros before scaling. |
| `+`, `-`, `*`, `/` | Exact canonical arithmetic. Division by zero gives `kDivisionByZero`. |
| `Negate`, `Abs` | Exact, including promotion for signed minimum when necessary. |
| Sign, equality, hash, and `Compare` | Sign and equality inspect canonical components. Compare orders by exact numerical value. Hash combines the two component values. |
| `Floor`, `Ceil`, `Trunc`, `IsInteger` | Round exactly toward negative infinity, positive infinity, or zero, respectively. `IsInteger` tests denominator one. |
| Exact Integer conversion and canonical output | Integral values convert to their numerator. Checked exact conversion of a nonintegral value gives `kInvalidArgument`. Diagnostic text is a canonical integer or reduced fraction. |

The frontend handles the closed numeral/decimal/unary-minus/division
syntax in the Real-constant argument of `to_fp`. A zero denominator at
any nested division is outside that input form. Rational zero has no
sign. In rational-to-FP conversion it becomes positive zero. A symbolic
rounding mode remains a node child until a concrete mode is available.
[FP input profile](../node/operators.md#6-floating-point)

## Word arithmetic and GMP fallback

For word components, compute unsigned magnitudes safely and keep signs
separate where needed. `Make` divides out the machine-word GCD before
checking the canonical range. For addition and subtraction with
denominators `d1,d2`, set `g=gcd(d1,d2)` and scale the numerators by
`d2/g` and `d1/g`. Combine the signed products with checked
`__int128` or unsigned `__int128` magnitudes. For a nonzero sum
numerator `t`, the remaining common divisor is `gcd(abs(t),g)`, which
uses a machine-word remainder modulo `g`. Divide it out before checking
the stored numerator and denominator ranges. For multiplication, cancel
each numerator against the other denominator before multiplying. For
division, reject zero first, then handle the second fraction's reciprocal
and cancel across the product. Check signs and bounds before narrowing.
Comparison of word fractions uses exact cross-products. Integral rounding
uses quotient and remainder, adjusting by one only in the required
direction.

Use GMP when the checked word path cannot complete. Borrow large
components and use the Integer stack bridge for word operands when
calling integer primitives. For GMP rational arithmetic use canonical
inputs and protect each working limb count before any growth. Keep
scratch `mpq_t` and `mpz_t` objects under RAII. Where cross-product
comparison needs scratch, check the actual working count or compare via
exact quotient/remainder steps. Before publishing a result, inspect
the reduced components for word fit. For a large result, allocate the
opaque holder and transfer each GMP component into its canonical
`mpq_t` with `mpz_swap`. A failed holder allocation clears the scratch
components and preserves all inputs.
The FP rounder consumes exact components with a separate power-of-two
scale and avoids materializing a large Rational where a shifted
comparison suffices. [FP rounding kernel](floating-point.md#exact-rounding-kernel)
