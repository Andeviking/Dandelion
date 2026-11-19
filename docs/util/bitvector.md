# BitVector

Status: **proposed value and operations**. The current node implementation
uses a sort and Integer payload; it has no standalone BitVector class or BV
arithmetic. This design covers the complete selected BV inventory.

## Invariant, construction and interpretation

A value is `(w,u)`, where `1 <= w <= UINT32_MAX` for the retained node
representation and `0 <= u < 2^w`. The mathematical theory permits larger
widths; a positive but unrepresentable width is a resource/size failure, not
an invalid width. Identity and hash include both width and unsigned value.
No signedness is stored: signed interpretation is `u` if its high bit is 0,
otherwise `u - 2^w`. Checked literal construction rejects negative or
oversized payloads using sign and bit length without allocating `2^w`.
Binary/hex digit counts establish width (`#x` has four bits per digit), with
checked length arithmetic. In contrast, `FromIntegerModulo(w,i)` returns
the unique residue of `i mod 2^w`, including negative `i`.

The initial owning representation is `uint32_t width` plus a canonical
nonnegative `Integer`. Thus zero and other small values need no limb allocation
even for a very wide sort; the Integer's signed small form means BV64 values
at least `2^63` require the big form. A borrowed view is a width plus an
`IntegerView`, so a current node can provide it without copying its payload.
An independent value owns its Integer. Copy, no-throw move/destruction,
borrowing and errors follow [design.md](design.md). A future width-tagged
`uint64_t`/limb alternative could make all BV64 values immediate, but it must
also preserve or deliberately migrate node `GetInteger`'s borrowed lifetime;
its speed and space tradeoff is unmeasured.

Expose width, bits, equality, hash and checked unsigned/signed Integer
conversion through read-only views. For diagnostic output, emit an exact
binary string padded to width, or an explicitly selected base with width
retained separately; SMT-LIB literal syntax is the frontend/model printer's
responsibility. Writing a very large representation can require substantial
storage or fail due to actual I/O or allocation, even for a small value.

## Ground operations

Unless specified, arguments have the same width and output is that width.
Independently callable APIs reject incompatible widths; checked node typing
establishes them for internal callers. Operations return exact values or an
explicit resource outcome. `M=2^w`, `U(x)` is unsigned and `S(x)` signed.

| Operation | Result and boundary rules |
| --- | --- |
| `not`, `and`, `or`, `xor`, `nand`, `nor`, `xnor` | Apply to exactly `w` bits; complement has no infinite sign extension. |
| `neg`, `add`, `sub`, `mul` | Mathematical result modulo `M`. Node n-ary add, multiply and the specified bitwise kinds are left folds. |
| `udiv`, `urem` | Ordinary nonnegative quotient/remainder for nonzero divisor; at zero return `M-1` and dividend respectively. |
| `sdiv`, `srem`, `smod` | Nonzero signed division truncates quotient toward zero; `srem = a - q*b` has dividend's sign or zero. `smod` is zero if the remainder is zero, otherwise the remainder adjusted by the divisor when their signs differ. Results wrap to width. Minimum signed divided by `-1` yields minimum signed, with `sdivo` true. For zero divisor: `sdiv` returns `1` for negative dividend and all ones otherwise; both remainders return the dividend. |
| `shl`, `lshr`, `ashr` | Treat the shift operand as its full unsigned value. For shift `>= w`, logical shifts return zero and arithmetic right shift returns all zeros or all ones according to the sign; otherwise shift and fill/truncate to `w`. Never narrow before this comparison. |
| Unsigned and signed `<`, `<=`, `>`, `>=`; `comp` | Compare `U` or `S`; `comp` returns BV1 `1` on equality and BV1 `0` otherwise, not Bool. |
| `nego`, `uaddo`, `saddo`, `umulo`, `smulo`, `usubo`, `ssubo`, `sdivo` | True exactly when the corresponding mathematical operation is outside the unsigned `[0,M-1]` or signed `[-2^(w-1),2^(w-1)-1]` range. `nego` is true only on signed minimum; `sdivo` only on minimum divided by `-1`. These are predicates, not wrapped-value comparisons. |
| `concat(a,b)` | Width `wa+wb`; unsigned result `U(a)*2^wb+U(b)`; check actual representation and size arithmetic before shifting. |
| `extract(h,l)` | `0 <= l <= h < w`; result width `h-l+1`, value `(u >> l) mod 2^(h-l+1)`. |
| `repeat(n)` | Positive exact `n`; concatenate the input `n` times, checked width `n*w`. |
| `zero_extend(n)`, `sign_extend(n)` | Nonnegative exact `n`; checked width `w+n`; preserve unsigned or signed interpretation, respectively. Zero extension by zero is identity. |
| `rotate_left(n)`, `rotate_right(n)` | Nonnegative arbitrary Integer index; reduce `n mod w` before shifting, including indices far above 64 bits. Zero and multiples of width return the same value. Node indices remain unchanged. |
| `ubv_to_int`, `sbv_to_int`, `int_to_bv` | Return `U`, `S`, and residue modulo `2^w`, respectively; exact and resource checked. |

All result-width sums and products are checked as exact integers before
narrowing. Invalid mathematical indices (`h<l`, `h>=w`, zero repeat or zero
width) differ from a mathematically valid size beyond an existing representation.
For rotations and `divisible`, an arbitrarily large index may still be usable
because no result width depends on its magnitude. The SMT-LIB
[FixedSizeBitVectors](https://smt-lib.org/theories-FixedSizeBitVectors.shtml)
and [QF_BV extensions](https://smt-lib.org/logics-all.shtml) are authoritative
for definitions; no host signed-overflow or divide-by-zero behavior is used.

## Implementation baseline and costs

For widths up to 64, checked operations may extract `uint64_t`, use unsigned
arithmetic and an explicitly computed mask (`w=64` is a separate case), then
construct a canonical Integer. Multiplication, signed interpretation,
division and overflow must not use undefined signed overflow. Wider values use
GMP-backed integer operations with finite-width masking; quotient, remainder
and bitwise operations are calculated as nonnegative magnitudes or via
mathematically specified signed conversions. GMP's infinite two's-complement
bitwise interpretation of negative integers must never leak into a BV result.
Do not construct `2^w` or an all-ones vector merely to reject a literal,
inspect a small value, or classify an oversized shift. Results that actually
contain `w` one bits can require O(w) storage and encounter an actual
allocation failure. Small-value sparse storage may save memory but repeated masks, large
finite bitwise operations, allocation, temporary size and node conversion costs
need measurement. A dedicated limb vector and inline unsigned 64-bit variant
are alternatives, not established improvements.
