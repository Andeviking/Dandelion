# Integer

**Status.** The standalone value and arithmetic are implemented in Phase 3.

## Representation and ownership

`Integer` denotes an exact element of Z. On the supported 64-bit target it
uses two words (16 bytes), an `int64_t` and an exclusive `BigInteger*`.
A null pointer selects the inline word and covers the full signed range,
including `INT64_MIN`. A non-null pointer owns an opaque holder for one
`mpz_t` outside the signed range. The selected layout needs neither pointer
tagging nor a reserved signed-word value.

Compute into a private temporary. If the result fits a signed word, return
it inline and clear any temporary limbs. Otherwise allocate the holder and
transfer the owning GMP buffer using `mpz_swap` with an initialized
`mpz_t`. Clear the remaining temporary on every path. Apply the same
canonicalization to each output of a multi-result operation. Copy duplicates
wide storage, move transfers it, and zero is the allocation-free reset.
[Shared ownership and failures](design.md)

For a word operand, put its unsigned magnitude in a native GMP limb on the
stack. Packed operands already have aligned 64-bit limbs. Both forms use
a read-only GMP descriptor from `mpz_roinit_n` for the duration of the
call. Owned wide operands borrow their existing GMP input. The bridge also
handles rational components. Compute the signed minimum's magnitude in
unsigned arithmetic.
[GMP special integer functions](https://gmplib.org/manual/Integer-Special-Functions)

## Construction, inspection, and arithmetic

| Operation | Contract |
| --- | --- |
| Signed and unsigned word construction | Any `int64_t` is inline, including zero. Checked unsigned construction promotes above `INT64_MAX`. |
| `ParseDecimal`, `ParseRadix` | Decimal accepts an optional minus and at least one ASCII digit. The unsigned radix form accepts nonempty base-2 or base-16 digit sequences, with either hex case. Leading zeros are valid. Signs in the unsigned form, prefixes, whitespace, and invalid digits give `kInvalidText`. An unsupported radix gives `kInvalidArgument`. |
| Sign, equality, comparison, parity, bit length, magnitude chunks, hash, narrowing | Exact and nonallocating. Sign and comparison give negative, zero, or positive. Zero has bit length zero. Chunks are 64-bit magnitude words in least-significant-first order, and out-of-range chunks are zero. `TryInt64`, `TryUint64`, and `TryUint32` return empty outside their ranges. `IsSmall` describes signed-word fit and `BackingBytes` reports significant owned backing where applicable. |
| Borrow, copy, and decimal output | Views can contain a signed or unsigned word or borrow read-only wide storage. Decimal output is canonical, with no leading zeros and zero rendered as `0`. |
| `+`, binary `-`, `*`, unary `-`, `Abs` | Exact results. Check machine overflow before dispatching to wider arithmetic. Negation or absolute value of `INT64_MIN` gives `2^63`. |
| `Gcd`, `Lcm` | Nonnegative. `Gcd(0,0)=0` and `Lcm(0,x)=0`. |
| `DivMod(a,b)` | For nonzero `b`, return the unique `(q,r)` with `a=b*q+r` and `0 <= r < abs(b)`. |
| `ExactDiv(a,b)`, `IsDivisible(a,b)` | For nonzero `b`, divide exactly or test for zero remainder. `ExactDiv` returns `kInvalidArgument` for nonzero remainder. Signed divisors are accepted by the numeric API. The SMT indexed predicate has a positive divisor. |
| `Power(base, exponent)` | Exact SMT-LIB Ints power with an arbitrary Integer exponent, as specified below. |

`DivMod`, `ExactDiv`, and `IsDivisible` report `kDivisionByZero` at
zero divisor. A GMP implementation can use floor division for a positive
divisor and ceiling division for a negative one. With a truncating pair,
adjust a negative remainder by adding `abs(b)` and subtracting `sign(b)`
from the quotient. In particular `(-7,3)` gives `(-3,2)`, `(7,-3)`
gives `(-2,1)`, and `(-7,-3)` gives `(3,2)`. Handle
`INT64_MIN / -1` before invoking machine division or remainder.
Use a checked divisibility test before calling the GMP exact-quotient
primitive.

For `n >= 0`, `Power(m,n)` is exact and `0^0=1`. For `n < 0`,
return zero for `m=0` or `abs(m)>1`, one for `m=1`, and the
parity-selected sign for `m=-1`. Handle the exponent-zero and base
`0, 1, -1` cases before interpreting exponent magnitude as a size.
Otherwise for positive `n`, compare a proven minimum result bit length
`n * (bit_length(abs(m)) - 1) + 1` to actual capacity using exact
arithmetic or division against the capacity bound. Use exponentiation by
squaring over exponent bits, skipping the final unused square. Guard each
required product and normalize the final value. The exponent is narrowed
only after a proof of fit. [SMT-LIB Ints, 2026-01-16](https://smt-lib.org/theories-Ints.shtml)

## Fast paths and capacity

Use `__builtin_add_overflow`, `__builtin_sub_overflow`, and
`__builtin_mul_overflow` or exact `__int128` intermediates on the supported
GCC and Clang targets. Keep signed-minimum cases in unsigned magnitudes or
wide intermediates. GCD operates on unsigned magnitudes and LCM divides out
the GCD before multiplying. Use exact comparison of signs and magnitudes
across representations. No operand is interpreted through the host
`unsigned long` width.

Validate the entire text grammar before converting. Ignore leading zeros
when checking the magnitude. Accumulate digits in checked machine chunks
and extend into GMP only when the value requires it. Check GMP's actual
result and working counts before calls that can grow storage. Near those
limits, use the operation's proven required count or capacity-safe limb
steps rather than rejecting on an imprecise bound. For operations with a
non-word result, wrapper allocation remains recoverable even after GMP
produces a temporary. The temporary's limbs are cleared if the holder
cannot be allocated.
