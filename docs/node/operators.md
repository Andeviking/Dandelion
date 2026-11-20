# Node Kinds, Operators, and Input Profile

The first-version node representation and typing rules and the Phase 1
construction and definition boundary changes are implemented. The later
solver still must implement each accepted operator's semantics. [Node design](design.md)
The [implementation plan](implementation.md) and [node test design](../testing/node.md)
apply to every kind below.

## 1. Profile and conventions

The frontend uses the first-order, quantifier-free subset of
[SMT-LIB 2.7, release 2026-09-09](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf),
including compatible 2.6 forms within this profile. Pin Core, ArraysEx, and FP
to the 2024-07-21 revisions, FixedSizeBitVectors to 2025-09-09 with the QF_BV
2025-02-25 extensions, and Ints to 2026-01-16. The project aims at the locally
identified `benchmark-2025` snapshot, whose year does not define these
semantics.

The profile includes constant arrays, concrete rational-to-FP conversion as
defined below, and the current Ints exponentiation operator. It excludes
general Real or String terms, symbolic Real conversion and `fp.to_real`,
user quantifiers, global sort parameters and open polymorphic assertions,
higher-order maps/lambda/application, recursive function definitions, and
nonstandard recursive containers. Nonrecursive datatype fields and ordinary
arrays may use all supported concrete sorts. SMT-LIB metadata strings remain
allowed where applicable.

Frontend `ALL` denotes this supported profile. A named logic selects its
active signature, but the frontend compatibility profile intentionally admits
some formulas beyond the catalog's assertion grammar. For example, the
catalog QF_NIA excludes `**`, whereas Dandelion's QF_NIA profile
accepts this supported Ints operator. The operator inventory does not enable
an inactive theory signature under every label. Reject unknown names rather
than deriving their meaning by concatenating theory names. A solver must
classify or otherwise justify the actual formula before using a specialized
procedure; an accepted expression need not be decidable by one.
[SMT-LIB logics](https://smt-lib.org/logics-all.shtml)

The exact registry, local extensions and QF_UFIDL model-value exception are
specified in [frontend logic profiles](../frontend/logic-profile.md). The [acceptance record](../frontend/implementation.md) reports the tested
corpus slice.

In the tables, `B` is Bool, `Z` is Int, `R` is RoundingMode, `V(w)` is BV(w),
and `F(e,s)` is FP(e,s). `T` is an arbitrary supported concrete sort. Equal
letters in a row require equal concrete sorts or widths. Children are ordered.
`n`-ary kinds retain the input order and are not automatically flattened.
No extra index, qualifier, rational value, or other payload is accepted where
a row does not specify it.

The kinds listed below are the entire first-version inventory. Dedicated
literal and declaration constructors create value kinds and `kApply`;
`MakeBuiltin` accepts the other operator kinds with exactly their specified
payload. All numeric indices are exact nonnegative integers unless the row
says otherwise; result widths are checked before narrowing to `uint32_t`.
Define `Kind` with `uint16_t` underlying storage, `kInvalid = 0`, then assign
consecutive values beginning at one in the order of the tables below, visiting
each grouped row's kinds left to right. Check at compile time that all kinds
fit; `kInvalid` is never a publishable node kind.

In Phase 1, `MakeWordIndexedBuiltin` supplies the exact indices as
`std::uint64_t` words. The ordinary arity, payload, operand, and range checks
apply with the qualifier and rational fields absent. For every index
sequence representable by both APIs, the same kind and children produce
the same structural key. The general factory remains available for wider
indices and for the qualifier or rational fields. `RebuildNode` preserves
the already established operator rank and payload with same-sort
replacement children. [Phase 1 rebuild contract](design.md#phase-1-payload-preserving-rebuild)

## 2. Values and declaration applications

| Input or category | Kind | Children, payload, result |
| --- | --- | --- |
| `true`, `false` | `kBoolValue` | No children; Bool payload; `B`. |
| Integer value | `kIntValue` | No children; canonical exact integer; `Z`. |
| BV value | `kBvValue` | No children; exact `v` with `0 <= v < 2^w`; `V(w)`. |
| FP value | `kFpValue` | No children; canonical FP category and optional finite bits; `F(e,s)`. |
| Rounding mode | `kRoundingModeValue` | No children; one of five values; `R`. |
| Resolved declaration application | `kApply` | Ordered domain arguments; `DeclInstanceId`; instance range. |

`#bX` has one bit per digit, and `#xX` has four bits per digit.
The Node value factories require `w > 0` and `0 <= v < 2^w`.
The [frontend profile](../frontend/logic-profile.md) reduces the unsigned
numeral in `(_ bvN w)` modulo `2^w` before calling these factories. Equivalent
literal spellings with the same width and exact value share.
The surface form `(- numeral)` is an Int negation operation where unary minus
is admitted. The QF_UFIDL value-query exception directly constructs its
negative Int value. Programmatic
`kIntNeg` remains an operation node.
[QF_BV definition](https://smt-lib.org/logics-all.shtml)

`kApply` distinguishes ordinary UF, internal symbols, formals, DT constructors,
selectors, and testers through the referenced declaration category. Its
arguments must exactly match the instance domain and manager. A wrong-DT-
constructor selector application remains well-sorted when its argument has the
selector's domain sort; its value is underspecified, not a type error.

The Phase 1 frontend definition boundary requires an internal declaration,
concrete instance, and owning definition-table entry for calls to definitions
with formal parameters. A closed nullary definition may resolve directly to
its typed body. Source name and occurrence information stay in the frontend.
Any retained calls reached through that body still need the definition table.

Phase 1's `MakeBvWordValue` constructs the same `kBvValue` key and enforces
the same exact width bound as `MakeBvValue`. A word wider than the target
sort is rejected before publication.

## 3. Core

| Input | Kind | Rule and semantics |
| --- | --- | --- |
| `not` | `kNot` | One `B` -> `B`. |
| `and` | `kAnd` | `n >= 2` `B` children -> `B`, conjunction. |
| `or` | `kOr` | `n >= 2` `B` children -> `B`, disjunction. |
| `xor` | `kXor` | `n >= 2` `B` children -> `B`, left fold. |
| `=>` | `kImplies` | `n >= 2` `B` children -> `B`, right fold. |
| `=` | `kEqual` | `n >= 2` same-sort `T` children -> `B`, adjacent equalities conjoined. |
| `distinct` | `kDistinct` | `n >= 2` same-sort `T` children -> `B`, all pairs unequal. |
| `ite` | `kIte` | `B,T,T` -> `T`. |

The frontend also accepts zero- and one-argument `and` and `or` as explicit
compatibility forms: empty `and` is `true`, empty `or` is `false`, and a
single argument is returned. Other rows retain their stated minimum arity.
Retain `kDistinct` as one n-ary node instead of creating all pairwise
comparisons in the parser. [Core theory](https://smt-lib.org/theories-Core.shtml)

## 4. Integer arithmetic

| Input | Kind | Rule and result |
| --- | --- | --- |
| Unary `-` | `kIntNeg` | One `Z` -> `Z`. |
| `+`, n-ary `-`, `*`, `div` | `kIntAdd`, `kIntSub`, `kIntMul`, `kIntDiv` | `n >= 2` `Z` children -> `Z`, left fold. |
| `mod` | `kIntMod` | Two `Z` -> `Z`. |
| `abs` | `kIntAbs` | One `Z` -> `Z`. |
| `**` | `kIntPow` | Two `Z` -> `Z`. |
| `<`, `<=`, `>`, `>=` | `kIntLt`, `kIntLe`, `kIntGt`, `kIntGe` | `n >= 2` `Z` -> `B`; chainable adjacent comparisons. |
| `(_ divisible n)` | `kIntDivisible` | One `Z` -> `B`; exact index `n > 0`. |

For nonzero divisors `m = n*q + r` and `0 <= r < abs(n)` for
`q = div(m,n)` and `r = mod(m,n)`. Division or modulo by zero remains a
well-sorted term with an underspecified but consistent total interpretation.
For nonnegative exponents `**` is integer exponentiation and `0^0 = 1`.
For negative `n`, the selected Ints revision defines `0^n = 0`; for
`abs(m) = 1`, `m^n = m^(-n)`; for `abs(m) > 1`, `m^n = 0`. Preserve the
operator rather than eagerly computing it in the node factory.
[Ints theory](https://smt-lib.org/theories-Ints.shtml)

## 5. Fixed-size bit-vectors

| Input | Kind | Children, parameters and result |
| --- | --- | --- |
| `bvnot`, `bvneg` | `kBvNot`, `kBvNeg` | One `V(w)` -> `V(w)`. |
| `bvand`, `bvor`, `bvxor` | `kBvAnd`, `kBvOr`, `kBvXor` | `n >= 2` `V(w)` -> `V(w)`, left fold. |
| `bvadd`, `bvmul` | `kBvAdd`, `kBvMul` | `n >= 2` `V(w)` -> `V(w)`, left fold. |
| `bvnand`, `bvnor`, `bvxnor` | `kBvNand`, `kBvNor`, `kBvXnor` | Two `V(w)` -> `V(w)`. |
| `bvsub` | `kBvSub` | Two `V(w)` -> `V(w)`. |
| `bvudiv`, `bvurem` | `kBvUdiv`, `kBvUrem` | Two `V(w)` -> `V(w)`. |
| `bvsdiv`, `bvsrem`, `bvsmod` | `kBvSdiv`, `kBvSrem`, `kBvSmod` | Two `V(w)` -> `V(w)`. |
| `bvshl`, `bvlshr`, `bvashr` | `kBvShl`, `kBvLshr`, `kBvAshr` | Two `V(w)` -> `V(w)`. |
| `bvcomp` | `kBvComp` | Two `V(w)` -> `V(1)`, not Bool. |
| `bvult`, `bvule`, `bvugt`, `bvuge` | `kBvUlt`, `kBvUle`, `kBvUgt`, `kBvUge` | Two `V(w)` -> `B`. |
| `bvslt`, `bvsle`, `bvsgt`, `bvsge` | `kBvSlt`, `kBvSle`, `kBvSgt`, `kBvSge` | Two `V(w)` -> `B`. |
| `bvnego` | `kBvNego` | One `V(w)` -> `B`. |
| `bvuaddo`, `bvsaddo` | `kBvUaddo`, `kBvSaddo` | Two `V(w)` -> `B`. |
| `bvumulo`, `bvsmulo` | `kBvUmulo`, `kBvSmulo` | Two `V(w)` -> `B`. |
| `bvusubo`, `bvssubo`, `bvsdivo` | `kBvUsubo`, `kBvSsubo`, `kBvSdivo` | Two `V(w)` -> `B`. |
| `concat` | `kBvConcat` | `V(a),V(b)` -> `V(a+b)`; exactly two children. |
| `(_ extract h l)` | `kBvExtract` | One `V(w)`; `0 <= l <= h < w`; `V(h-l+1)`. |
| `(_ repeat n)` | `kBvRepeat` | One `V(w)`; `n > 0`; `V(n*w)`. |
| `(_ zero_extend n)` | `kBvZeroExtend` | One `V(w)`; `n >= 0`; `V(w+n)`. |
| `(_ sign_extend n)` | `kBvSignExtend` | Same shape as zero extension. |
| `(_ rotate_left n)` | `kBvRotateLeft` | One `V(w)`; `n >= 0`; `V(w)`. |
| `(_ rotate_right n)` | `kBvRotateRight` | One `V(w)`; `n >= 0`; `V(w)`. |
| `ubv_to_int` | `kUbvToInt` | `V(w)` -> `Z`, unsigned interpretation. |
| `sbv_to_int` | `kSbvToInt` | `V(w)` -> `Z`, two's-complement interpretation. |
| `(_ int_to_bv w)` | `kIntToBv` | `Z` -> `V(w)`; `w > 0`; modulo `2^w`. |

All listed numeric indices enter the structural payload, even where an index
is also derivable from the result sort. For BV8, rotate-left indices 1 and
18446744073709551617 are different structural keys even though the resulting
rotations are semantically equal. Preserve `repeat`, signed division, and
other extensions as high-level operations. `bvxnor` and BV comparisons are
binary; do not infer the n-ary attributes of another operator.

BV division, remainder, shifts, overflow predicates and signed interpretations
are total with the selected SMT-LIB semantics, not C++ arithmetic. In
particular, unsigned division by zero yields all ones, unsigned remainder by
zero yields the dividend, and `bvsdivo` detects minimum signed value divided
by minus one. The frontend accepts explicit legacy spellings `int2bv` for
`int_to_bv` and `bv2nat` for `ubv_to_int`; it rejects the historically
ambiguous `bv2int` spelling. Conversion use is still restricted by the chosen
logic and the presence of the applicable theories.
[FixedSizeBitVectors](https://smt-lib.org/theories-FixedSizeBitVectors.shtml)

## 6. Floating point

All `F` arguments in an arithmetic or comparison row have the same format,
unless a conversion explicitly permits different source and destination
formats. Rounding-mode aliases map to the five canonical values:
`roundNearestTiesToEven`/`RNE`, `roundNearestTiesToAway`/`RNA`,
`roundTowardPositive`/`RTP`, `roundTowardNegative`/`RTN`, and
`roundTowardZero`/`RTZ`. `Float16`, `Float32`, `Float64`, and `Float128`
resolve respectively to `F(5,11)`, `F(8,24)`, `F(11,53)`, and `F(15,113)`,
not new sort identities.

`numeric::FpCategory` distinguishes NaN, positive and negative infinity,
positive and negative zero, and finite nonzero bits. For packed input, use
`numeric::FloatingPointValue::FromBits` to validate and classify the bits.
For an explicit special, use `numeric::FloatingPointValue::Special` with
its category. Pass the resulting value's borrowed view to `MakeFpValue`
with a matching FP sort. [Typed literal API](design.md#phase-1c-typed-literal-and-sort-api)

| Input | Kind | Ordered children, indices and result |
| --- | --- | --- |
| `fp` | `kFpConstruct` | `V(1),V(e),V(s-1)` -> `F(e,s)`. |
| `fp.abs`, `fp.neg` | `kFpAbs`, `kFpNeg` | `F` -> same `F`. |
| `fp.add`, `fp.sub`, `fp.mul`, `fp.div` | `kFpAdd`, `kFpSub`, `kFpMul`, `kFpDiv` | `R,F,F` -> `F`. |
| `fp.fma` | `kFpFma` | `R,F,F,F` -> `F`, one rounding. |
| `fp.sqrt`, `fp.roundToIntegral` | `kFpSqrt`, `kFpRoundToIntegral` | `R,F` -> `F`. |
| `fp.rem`, `fp.min`, `fp.max` | `kFpRem`, `kFpMin`, `kFpMax` | `F,F` -> `F`, no RM child. |
| `fp.eq`, `fp.lt`, `fp.leq`, `fp.gt`, `fp.geq` | `kFpEq`, `kFpLt`, `kFpLeq`, `kFpGt`, `kFpGeq` | `n >= 2` same-format `F` -> `B`, chainable. |
| `fp.isNormal`, `fp.isSubnormal`, `fp.isZero` | `kFpIsNormal`, `kFpIsSubnormal`, `kFpIsZero` | One `F` -> `B`. |
| `fp.isInfinite`, `fp.isNaN`, `fp.isNegative`, `fp.isPositive` | `kFpIsInfinite`, `kFpIsNaN`, `kFpIsNegative`, `kFpIsPositive` | One `F` -> `B`. |
| `((_ to_fp e s) bits)` | `kFpFromBits` | `V(e+s)`; indices `e,s`; `F(e,s)`. |
| `((_ to_fp e s) rm x)` with FP `x` | `kFpConvert` | `R,F(source)`; indices `e,s`; `F(e,s)`. |
| Same form with BV `x` | `kFpFromSbv` | `R,V(m)`; signed BV; indices `e,s`; `F(e,s)`. |
| `((_ to_fp_unsigned e s) rm x)` | `kFpFromUbv` | `R,V(m)`; unsigned BV; indices `e,s`; `F(e,s)`. |
| Same `to_fp` form with accepted rational constant | `kFpFromRational` | One `R`; indices `e,s`; normalized exact rational payload; `F(e,s)`. |
| `((_ fp.to_sbv w) rm x)` | `kFpToSbv` | `R,F` -> `V(w)`; exact index `w > 0`. |
| `((_ fp.to_ubv w) rm x)` | `kFpToUbv` | `R,F` -> `V(w)`; exact index `w > 0`. |

The `kFpFromRational` frontend compatibility argument is recognized only in
the Real constant position of `to_fp`, using the closed grammar and the
[binding-precedence rule](../frontend/logic-profile.md#language-names-and-the-compatibility-policy):

```text
Q ::= numeral | decimal | (- Q) | (/ Q Q)
```

Each denominator must be exactly nonzero. Normalize to a rational with
positive denominator and gcd one; zero is `0/1`. The rounding-mode child can
be any supported `R` expression. Symbolic Real terms, Real declarations,
`fp.to_real`, and other general Real expressions are unsupported. A rational
expression with a zero denominator, such as `(/ 1 0)`, is outside this
compatibility profile and reports `kUnsupportedFeature`; it is not assigned a
fixed value. The nonstandard `fp.to_ieee_bv` input is also unsupported.

Core `=` and `fp.eq` are distinct kinds. Core equality distinguishes signed
zeros; `fp.eq` treats them as equal and is false for NaN. FP minimum/maximum
on opposite zeros and invalid FP-to-BV conversions have underspecified choices
which must remain consistent for equal semantic arguments. For FP-to-BV, the
formal semantics determines range on the rounded integer; do not assume every
negative finite input to unsigned conversion is out of range merely because
it is negative. [FloatingPoint theory and its referenced formal semantics](https://smt-lib.org/theories-FloatingPoint.shtml)

## 7. Arrays and datatype applications

| Input | Kind | Rule |
| --- | --- | --- |
| `select` | `kArraySelect` | `Array(I,E), I` -> `E`. |
| `store` | `kArrayStore` | `Array(I,E), I, E` -> `Array(I,E)`. |
| `((as const (Array I E)) v)` | `kArrayConst` | One `E`; qualifier is full `Array(I,E)` result sort. |
| UF or DT constructor application | `kApply` | Exact resolved declaration-instance domain and range. |
| DT selector application | `kApply` | One matching concrete DT; result is instantiated field sort. |
| `((_ is ctor) value)` | `kApply` | Resolved tester instance; concrete DT -> Bool. |

The `ctor` tester index is a source symbol, not a numeric index. Resolve it to
the associated tester declaration instance. Array constants are an accepted
extension, not a claim that ArraysEx itself declares them. General array
models need total interpretations and are not represented solely by these
syntax constructors. [ArraysEx](https://smt-lib.org/theories-ArraysEx.shtml)

