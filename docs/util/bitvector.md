# BitVector

**Status.** The standalone value and operations are implemented in Phase 3.

## Representation and construction

A value is `(w,u)` with `1 <= w <= UINT32_MAX` and `0 <= u < 2^w`.
Width is part of core equality and hash. The unsigned interpretation is
`U(x)=u`. The signed interpretation `S(x)` is `u` when the high bit
is zero and `u-2^w` otherwise. The same stored bits serve both.

The object is 16 bytes on the supported 64-bit target. It stores a
`uint32_t width`, a `uint32_t limb_count`, and a union of `uint64_t word`
and exclusive `uint64_t* limbs`. A zero count selects the inline word. The
wide form has at least two little-endian significance limbs, with a
nonzero top limb and zero bits above width. Normalize by masking the
partial top limb when needed, trimming zero high limbs, and returning to
the word form whenever the result has at most 64 effective bits. Word
selection depends on the payload. Every BV1 through BV64 value, including
BV64 `2^63..2^64-1`, and a small payload at width `UINT32_MAX` fit
inline with no heap allocation. Inline construction, copies, inspection,
and word-only operations need no heap storage. The storage for a result
whose nonzero bits span the declared width can grow with that width.
Move resets to BV1 zero. [Shared ownership and errors](design.md)

Checked width construction accepts exact Integer input before narrowing.
A zero or negative width gives `kInvalidArgument`. Positive width
beyond the representation gives `kSizeLimit`. Checked literal construction
accepts unsigned words or Integers and uses sign and effective length to
reject values outside the width with `kValueOutOfRange`. Binary and hex
factories consume raw digit sequences and derive width from digit count,
including leading zeros. Validate the full text and check the width
multiplication before narrowing. Malformed digits or empty text give
`kInvalidText`. `FromIntegerModulo` implements `int_to_bv` with distinct
modulo semantics.

Expose width, effective bit length, unsigned word narrowing, bit
inspection, read-only 64-bit chunks, and equality and hash. Absent chunks
and bit positions beyond the width read as zero. Exact binary diagnostic
output pads to declared width. Check its length and allocation before
publishing a string. SMT-LIB spelling and output streaming belong to the
frontend.

## Ground operations

Unless specified, binary operands have equal width, independently
checked with `kInvalidArgument` on mismatch. A value's width invariant
holds after construction. Let `M=2^w`. Operator names in the table use
SMT-LIB spelling.

| Operation | Contract |
| --- | --- |
| `bvnot`, `bvand`, `bvor`, `bvxor`, `bvnand`, `bvnor`, `bvxnor` | Apply to exactly `w` bits. |
| `bvneg`, `bvadd`, `bvsub`, `bvmul` | Return the residue modulo `M` of the mathematical result. |
| `bvudiv`, `bvurem` | Use nonnegative quotient and remainder. At zero divisor return `M-1` and the dividend, respectively. |
| `bvsdiv`, `bvsrem`, `bvsmod` | For nonzero divisor, signed quotient truncates toward zero. Signed remainder is `a-q*b`. Signed modulus adjusts a nonzero remainder by adding the divisor if their signs differ. Encode results modulo `M`. At zero divisor, division returns 1 for a negative dividend and all ones otherwise. Both remainder forms return the dividend. Signed minimum divided by `-1` wraps to minimum. |
| `bvshl`, `bvlshr`, `bvashr` | Interpret the full shift operand as unsigned. For shift `>= w`, logical shifts return zero and arithmetic right shift returns all ones for a negative dividend or zero otherwise. Smaller shifts truncate to `w` and arithmetic right shift extends the sign. |
| `bvult`, `bvule`, `bvugt`, `bvuge`, `bvslt`, `bvsle`, `bvsgt`, `bvsge` | Return Bool from comparison of the corresponding unsigned or signed interpretations. |
| `bvcomp` | Return BV1 holding 1 for equal operands and 0 otherwise. |
| `bvnego`, `bvuaddo`, `bvsaddo`, `bvumulo`, `bvsmulo`, `bvusubo`, `bvssubo`, `bvsdivo` | Check the exact operation against unsigned `[0,M-1]` or signed `[-2^(w-1),2^(w-1)-1]` range as applicable. `bvnego` is true only for signed minimum. `bvsdivo` is true only for signed minimum divided by `-1`. |
| `concat(a,b)` | Width `wa+wb`, residue `U(a)*2^wb+U(b)`. |
| `extract(h,l)` | Require `0 <= l <= h < w`. Width is `h-l+1`, value is the low result-width bits of `u >> l`. |
| `repeat(n)` | Require positive exact `n`, concatenate the input `n` times, and check `n*w`. |
| `zero_extend(n)`, `sign_extend(n)` | Require nonnegative exact `n` and check `w+n`. Preserve the respective unsigned or signed interpretation. Zero extension count, in either operation, preserves the input value and width. |
| `rotate_left(n)`, `rotate_right(n)` | Require a nonnegative exact Integer index. Reduce it modulo `w` before shifting. Zero reduced amount returns the input value, including when the original index has more than 64 bits. |
| `ubv_to_int`, `sbv_to_int`, `int_to_bv` | Return `U(x)`, `S(x)`, or the checked-width residue of an Integer, respectively. |

Invalid index bounds give `kInvalidArgument`. A mathematically valid
computed width beyond `UINT32_MAX` gives `kSizeLimit`. Check width sums
and products exactly before narrowing and before recognizing payload
shortcuts. The caller retains the original index on the node and applies
n-ary left folds where specified. [FixedSizeBitVectors, 2025-09-09](https://smt-lib.org/theories-FixedSizeBitVectors.shtml),
[QF_BV extensions](https://smt-lib.org/logics-all.shtml),
[node operator inventory](../node/operators.md#5-fixed-size-bit-vectors)

## Word and limb algorithms

On the inline path use unsigned arithmetic and checked wide intermediates.
Distinguish a full 64-bit mask from a shorter mask and separate zero
shift from shifts of a full word. Test the entire shift operand against
the width before reducing it to a machine count. Use sign bits and
unsigned magnitudes for signed operations, including signed minimum.
Apply overflow predicates to the mathematical result or an equivalent
precondition on the operands. Wrapped comparisons alone can lose the
relevant information.

Use trimmed limbs for wide arithmetic and allocate to effective input,
result, or scratch size. Add and subtract with carry or borrow and
apply finite-width masks to the result. Split shifts and contiguous
field operations into a whole-limb offset and intra-limb count. Use explicit
finite-width sign fill. Use masked shifts and OR for concat, extraction,
and rotation. Reduce a wide rotation index modulo the width through the
read-only GMP integer view, including when the limbs are borrowed. For
repeat, double already formed blocks and combine
according to the repetition count, after validating the final width.
Zero payloads and identity operations need no width-sized temporary.

The supported GMP ABI requires `mp_limb_t` to be `uint64_t` and
`GMP_NUMB_BITS` to be 64 for borrowing packed payloads. The direct
wide kernel uses `mpn_mul` for multiplication and `mpn_tdiv_qr` for
division. Handle zero operands and a numerator shorter than the divisor
before entry. With a nonzero divisor, shared wide operand storage gives
quotient one and remainder zero. Provide normalized nonzero divisors and
disjoint division inputs, satisfy operand order and positive counts, set
`qxn=0`, and allocate the required writable product or quotient and
remainder ranges without prohibited overlap. Check limb counts and byte
arithmetic and trim the result after masking. Signed division reduces to
unsigned magnitudes with sign and modulus adjustments.

The private test selector can force the portable conversion path on the
same ABI. This path converts the packed operands to GMP integers and uses
exact `mpz_*` arithmetic. The build still requires native 64-bit GMP
limbs for borrowed views.
[GMP low-level functions](https://gmplib.org/manual/Low_002dlevel-Functions),
[GMP integer import and export](https://gmplib.org/manual/Integer-Import-and-Export)

Integer conversions and literal checks inspect sign and effective bits
before building masks. For a negative Integer residue or a signed BV
interpretation use the finite-width complement-plus-one magnitude where
needed. A large logical width by itself requires no width-sized mask for
validating, comparing, hashing, or copying a small value. Operations
such as complement or unsigned division by zero may actually produce
dense results.
