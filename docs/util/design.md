# Numeric Layer: Scope and Shared Contracts

Status: **proposed design for review**. The contracts below describe the
recommended future layer. Implemented node contracts remain authoritative
until migration. See [implementation.md](implementation.md) for precise
compatibility requirements.

## Responsibility and dependency direction

The frontend accepts syntax and logic profiles; it validates literals and
indices before publishing them. The independently callable node and numeric
factories also validate their own external inputs. A `NodeManager` checks
sorts, managers, arities and identities, owns immutable payload copies, and
interns syntax. Numeric values have no manager, declaration, solver, SAT,
or frontend dependency. Rewriting and model evaluation invoke numeric
operations on concrete values; symbolic terms and type rules remain in the
node layer. The theory procedures and coordinator own symbolic encodings,
underspecified functions, model consistency and recovery. A numeric value is
not a theory solver or an expression.

| Value | Identity and purpose | Primary clients |
| --- | --- | --- |
| `Integer` | Mathematical element of Z; unbounded exact signed value. | Int constants and indices, IDL/LIA/NIA, BV conversions, rational and FP algorithms. |
| `BitVector` | Pair `(w,u)` with `w > 0` and `0 <= u < 2^w`; width is part of identity. | BV constants, constant folding, encoding and models, FP interchange. |
| `Rational` | Mathematical element of Q in canonical numerator/denominator form. | Closed rational FP input, exact rounding and arithmetic relaxations, not a user Real sort. |
| `FloatingPointValue` | A format `(e,s)` plus an abstract SMT-LIB FP value; one NaN within a format and distinct signed zeros. | FP constants, ground evaluation, model construction and checking. |
| `RoundingMode` | Five valid enum values, shared by FP and nodes. | FP evaluation, encoding and model. |

Value comparison and hashing are independent of manager identity, allocation
address, and storage representation. Equal values must hash equally; collisions
require full equality. Hashes are internal, need not be stable across versions,
and are not serialization. Values of different BV widths or FP formats are
different typed values even if their numeric interpretations coincide. This
does not make differently sorted SMT terms comparable. FP core identity and
IEEE `fp.eq` are separate operations.

Polynomials, linear forms, intervals, infinitesimals, algebraic numbers and
root isolation have invariants tied to specific algorithms. They belong with
the arithmetic procedure that actually needs them; their requirements and
exactness must be designed there. Rational relaxations do not create a Real
input sort or establish an integer model. An algebraic-number implementation
may be useful to a future NIA method even in a Real-free language, but is not
required merely because another solver has one. A format descriptor is a
supporting concept, not an extra numeric domain or a plugin framework.

## Common API and failure policy

The operation names in the type documents describe required callable behavior,
not final spelling. Values own their payload and can be copied independently.
Public borrowing is read-only, does not prolong an owner's lifetime, and is
available on lvalues only; `Borrow()` on an rvalue is deleted. The existing
`IntegerView` can also directly contain a small integer and be constructed
independently; a large view borrows its owner's storage. A copied view does
not own a GMP limb, node, manager or temporary. Calls may borrow inputs
only for the call; no result may retain a borrowed argument. Published node
values are immutable even if the caller later changes or destroys an input.
No writable GMP object or limb is exported. Moves and destruction are
`noexcept`; Integer moves leave zero and Rational moves leave 0/1, including
the existing self-move guarantees. Proposed BV and FP moves leave a valid
value, with the exact moved-from value selected and tested before implementation.
Copy assignment provides the strong guarantee if C++ allocation fails. The
initial proposed moved-from BV is BV1 zero, and moved-from FP is F(2,2)
positive zero; reset must not allocate. Self-move preserves the value.

New potentially allocating calculations use an expected-like
`NumericResult<T>` with a nonallocating error carrying invalid input,
representational size or C++ allocation failure. A separately specified
cancellation mechanism may report cancellation when actually requested; it
does not introduce a per-operation quota. A division API requiring a nonzero
operand has a checked external wrapper; a validated internal helper can rely
on that precondition.
Mismatched widths/formats are rejected at independently callable boundaries;
internally typed nodes establish operand compatibility. Do not repeat checks
already guaranteed by an immutable value's construction. APIs with no
recoverable failure, such as sign, equality, and nonallocating hash, return
directly. Existing `Integer::ParseDecimal`, `Rational::Make`, Node factory
`Result` types and exception guarantees are preserved through a compatibility
adapter; they are not silently changed to the new error type.

The following illustrative signatures make the intended value and failure
boundaries concrete; they do not fix final names or binary layout:

```cpp
NumericResult<std::pair<Integer, Integer>> DivMod(
    IntegerView dividend, IntegerView nonzero_divisor);
NumericResult<BitVector> FromIntegerModulo(
    std::uint32_t width, IntegerView value);
NumericResult<FpOutcome<FloatingPointValue>> Min(
    FloatingPointView left, FloatingPointView right);
NumericResult<FpOutcome<BitVector>> ToUnsignedBv(
    FloatingPointView value, RoundingMode mode, std::uint32_t width);
```

`FpOutcome<T>` is a tagged result: a defined value, two explicitly allowed
values, or an unspecified value of the stated destination type/format. Its
unspecified case retains the destination width or format when `T` is
runtime-sized. Accessors require the matching tag. It has no implicit
conversion to `T`.
The outer error distinguishes failure to compute from a valid underspecified
outcome. The checked `DivMod` still verifies a nonzero divisor at this
independently callable boundary; internal code may call an unchecked helper
after establishing it. The illustrative `uint32_t` widths match the retained
node representation boundary; final method names and error layout remain
proposals.

An operation is atomic from its caller's perspective: it returns the exact
value or an actual representational, allocation or externally requested
cancellation outcome; it never returns a truncated number. Do not impose
discretionary input, intermediate-size, work or memory quotas. Checked
`uint32_t`, `size_t`, container and GMP index capacities remain applicable
where actually used. A valid value that an existing representation cannot
hold is not an invalid literal. For an accepted query, propagate inability to
compute to the rewriter/solver so it can retain the term, use another sound
path, or return `unknown`; an input or size error is reported at its owning
boundary.

GMP >= 6.3 is the existing private implementation dependency. Its default
limb allocation failure may terminate the process, so neither expected nor
C++ exceptions promise recovery from arbitrary GMP OOM. Do not install a
process-wide GMP allocator or throw through GMP callbacks. Check actual sizes
and representation arithmetic before GMP operations where needed; this cannot
guarantee survival of actual allocation failure. No fatal exit justifies a
solver answer.
The process-wide allocator and reference-count failure behavior of the
current node API remain unchanged.

## Operator ownership and cross-type paths

This table covers the inventory in [node operators](../node/operators.md).
For each listed numerical family, the corresponding value document specifies
the ground operation. `fold` means that a rewriter or evaluator applies the
binary operation in the node's specified order; the node factory continues to
retain the operation. A value operation alone never proves a symbolic rewrite
or a mixed-theory result.

| Input kinds | Concrete responsibility | Other responsibility |
| --- | --- | --- |
| Int literals, `-`, `+`, `-`, `*`, `div`, `mod`, `abs`, `**`, comparisons, `divisible` | Integer operations; fold n-ary forms and handle negative exponent as specified. | Preserve `div`/`mod` with zero divisor as an underspecified total function, including congruence; logic restrictions are frontend-owned. |
| BV literals, Boolean/bitwise arithmetic, signed and unsigned div/rem/mod, shifts, comparisons and overflow predicates | BitVector operations with exact width and total BV corner cases. | Node type checking, n-ary folds and symbolic BV encoding remain separate. |
| `concat`, `extract`, `repeat`, extensions, rotations, `bvcomp` | BitVector operations and checked result-width computations. | Node retains exact original indices, even semantically equivalent rotation indices. |
| `ubv_to_int`, `sbv_to_int`, `int_to_bv` | Explicit unsigned/signed interpretation and modulo conversion. | Coordinator/encoding must enforce these relations on symbolic values. Literal construction is not modulo conversion. |
| `fp`, bit reinterpretation, FP/Rational/BV conversions, FP arithmetic, comparisons and classification | FP value operations with explicit format, rounding and choice outcomes; use BV and Rational as required. | Symbolic FP-to-BV lowering, consistent underspecified interpretations and model reconstruction belong to FP theory and model. |
| Rounding-mode values, core `=`, `distinct`, `ite` | RM identity; per-sort value equality, including FP core equality. | Node and general evaluator handle polymorphism, n-ary chain/pair rules, and conditionals. |
| Arrays, UF, DT and their applications | No compound value or function interpretation is invented here. | Other components retain total interpretations, extensionality, congruence, datatype and shared-value obligations. |

`Integer -> Rational` is exact with denominator one. `Rational -> Integer`
requires an explicit rounding rule or exact-integral check. `Integer <-> BV`
distinguishes modulo conversion from checked literal construction. `BV -> FP`
chooses signed or unsigned interpretation explicitly. FP construction from
bits is reinterpretation, not numeric rounding. FP-to-BV first rounds to an
integer and checks the rounded result's range. An exact finite FP value may be
viewed as a dyadic value internally; materializing it as a Rational can require
substantial storage and is not mandatory for classifying a value.

## Current boundary and the proposed bridge

Today `src/node/numeric.cppm` exports `Integer`, `IntegerView`, `Rational`,
`RationalView`, `NodeError`, `Result`, and `ReferenceLimit`; `numeric.cc`
implements the numbers and reference-limit message. Small Integer values use
signed `int64_t`; bigger ones own a private GMP integer. Rational stores two
Integers and uses GMP rational normalization. Numeric arithmetic beyond the
existing parse, compare, normalization and inspection APIs is not implemented.

BV nodes currently store an Integer plus a BV sort, not a BitVector object.
FP nodes store a category plus Integer finite bits and an FP sort, not an FP
value object. Rational conversion nodes own normalized rational payloads. A
proposed borrowed `BitVectorView` can pair the existing sort width with the
existing Integer view; a proposed `FloatingPointView` can pair the sort format
with the existing category and finite-bit view. Neither view owns its node.
This avoids an immediate node payload change and allows `LiteralView`'s
existing `GetInteger`, `GetFpCategory`, and `GetFpBits` to keep their contracts.
Standalone values own the same semantic information independently of a node.
If future storage differs, these accessors still need valid borrowed results;
an adapter that returns a view of a temporary is invalid.

Node structural identity includes kind, sort and canonical payload; it is not
general term equality. The exact index on `rotate` remains part of the node
key even if reduced modulo width during evaluation. NaN bit encodings share
one node per sort, signed zeros remain distinct, and constant-child `fp` or
`to_fp` applications remain operator nodes. New numeric comparisons or
folding do not alter these facts. Existing factory rollback, failure mapping,
references and tests remain in force.
