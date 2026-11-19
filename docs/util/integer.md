# Integer

Status: **proposed extension of an existing implemented value**. The current
parse, ownership and view contracts are specified by [node design](../node/design.md#5-exact-values);
the general arithmetic below is design. The frontend bridge adds checked
`ParseRadix` for unsigned base-2 and base-16 digits and checked `Negate`.
Both return owning, exact integers.

## Value, interface and algorithms

`Integer` denotes exactly one member of Z. The selected baseline retains the
current canonical small `int64_t` or owned GMP `mpz_t` representation: any
value in the signed 64-bit range is small, including `INT64_MIN`; all others
are big. Normalize back to small after every operation. This is an internal
strategy, not a stable ABI. Comparisons, equality, sign, magnitude bit length
(`0` has length zero), little-endian 64-bit magnitude chunks, hash, decimal
output, narrowing and borrowing preserve the existing view semantics. Decimal
parsing accepts optional `-` and digits only; no `+`, whitespace, or empty
digits. Decimal output is canonical. Other bases for BV input belong to a
checked digit decoder, not to an undocumented expansion of this grammar.

| Operation | Inputs and output | Conditions and meaning |
| --- | --- | --- |
| Negate, absolute, add, subtract, multiply | Integers -> exact Integer | Never wrap. Checked signed fast paths fall back to GMP on overflow. Negating/absolute `INT64_MIN` produces `2^63`. |
| Compare and sign, parity, bit length and magnitude chunks | Integer -> order, predicate or unsigned chunks | Inspect magnitude safely without signed negation of `INT64_MIN`; out-of-range chunk is zero. |
| GCD and LCM | Integers -> nonnegative Integer | `gcd(0,0)=0`, `lcm(0,x)=0`; exact; useful for Rational normalization and integer algorithms. |
| Euclidean `DivMod` | `(a,b)` -> `(q,r)`, `b != 0` | `a=b*q+r`, `0 <= r < abs(b)`; div and mod use the same pair. A checked external call rejects zero; internal nonzero precondition is established by caller. |
| Exact quotient, divisibility | Integers -> Integer or Bool | Exact quotient requires a nonzero divisor and zero remainder. SMT `(_ divisible n)` has positive `n`; test remainder zero. |
| Power | `(base, exponent: Integer)` -> Integer or resource outcome | Implements the selected `**` semantics below, not a narrowed machine exponent. |
| Checked import/narrowing | Signed/unsigned machine values or exact digits -> Integer; Integer -> optional machine value | No silent conversion of a negative value to unsigned, or truncation of a large value. Provide unsigned-64 construction without assuming it fits signed 64. |

For nonzero `b`, compute a truncating quotient/remainder then, when `r < 0`,
set `r += abs(b)` and `q -= sign(b)`, using exact intermediates; an equivalent
GMP floor/ceiling implementation is acceptable if it meets this contract.
For example `(-7,3) -> (-3,2)`, `(7,-3) -> (-2,1)`,
`(-7,-3) -> (3,2)`. Avoid C++ `INT64_MIN / -1` and any signed overflow before
dispatching. The SMT terms `(div a 0)` and `(mod a 0)` are well-sorted,
underspecified total functions; they are not calls to `DivMod`, and distinct
occurrences with equal semantic arguments must have consistent interpretations.

For `n >= 0`, `Power(m,n)` is exact exponentiation by squaring and `0^0=1`.
For `n < 0`: base 0 gives 0, bases 1 and -1 give 1 and the parity-selected
sign respectively, all `abs(m)>1` give 0. For nonnegative arbitrarily large
`n`, short-circuit 0, 1 and -1 before estimating size; for other bases use
exact arithmetic and check actual representation and size calculations when
needed. Do not impose a work or intermediate-size quota. Never narrow the
exponent without proof it fits and
never claim a resource error proves a formula. The catalog excludes `**`
from `QF_NIA`, but Dandelion's proposed compatibility profile accepts it
when the full Ints signature is active, including under `QF_NIA`; see
[Ints 2026-01-16](https://smt-lib.org/theories-Ints.shtml) and
[frontend logic profiles](../frontend/logic-profile.md).

Copy, move, borrowing, allocation and GMP OOM follow [design.md](design.md).
The current numeric API already tests canonical small storage, `INT64_MIN`,
exact large values, ownership, failed assignment and moved-from zero; retain
those contracts during migration.

## Cost and unresolved measurements

Machine fast paths avoid GMP arithmetic when all checked intermediates fit;
big operations use GMP's input-size-dependent algorithms and allocations.
Small/big transitions, copying, normalized result size, parsing and hashing
costs, and frequency of such paths in integer solving are unmeasured. A
two-word tagged representation, reference sharing or inline limbs might save
space or allocations but add ownership or portability costs; none is chosen
without real measurements. GMP's `mp_bitcnt_t`, `size_t` and allocation limits
must be checked before narrowing a requested shift or allocation.
