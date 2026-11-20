# Numeric Layer Contracts

**Status.** Phase 1B design specification implemented by the standalone
module in Phase 3.

## Responsibility and dependency direction

The static library `dandelion_util` owns exact concrete arithmetic and
numeric value identity. Its sole public module is `dandelion.util.numeric`.
The value and error declarations live in namespace `dandelion::numeric`.
The library depends on the C++ standard library and privately on GMP 6.3
or newer. Node, frontend, and future evaluator and theory code depend on
numeric services. NodeManager, NodeError, DatatypeReason, and ReferenceLimit
belong to the node layer and are outside the numeric import and link graph.
[Module layout and migration](implementation.md)

| Owner | Responsibility |
| --- | --- |
| Numeric layer | Check its own external inputs, preserve exact value invariants, and evaluate concrete operands |
| Node layer | Check manager membership, arity, sorts, and indices, retain immutable payloads, and intern structural keys |
| Frontend | Interpret syntax and logic profiles, attach source diagnostics, and render SMT-LIB values |
| Rewriter, theory, and model code | Apply sound rewrites, encode symbolic operations, interpret total functions consistently, and validate original queries |

`Integer` represents Z. `Rational` represents Q for exact conversions and
integer algorithms such as linear relaxations. The frontend profile admits
closed rational constants in the selected FP conversion form. Symbolic Real,
`fp.to_real`, and general Real solving are outside the profile. Polynomials,
algebraic numbers, intervals, and other algorithm-specific representations
belong to the procedures that require their invariants.
[Frontend profile](../frontend/logic-profile.md)

`BitVector` identifies a width and unsigned residue. `FloatingPointValue`
identifies a format and an abstract value, with one NaN per format and two
distinct signed zeros. Core equality and nonallocating hash are based on
canonical mathematical value and, for BV and FP, the width or format.
Equal values hash equally. Hash collisions require full equality. Hashes
serve in-memory lookup and carry no serialization or version-stability
contract. IEEE `fp.eq` is a separate operation. Core `=`, `distinct`, and
`ite` polymorphism, chainable comparisons, and n-ary folds are handled by
the caller according to the node operator inventory. [Node operators](../node/operators.md)

## Ownership and public operations

Values own independent payloads. Copying a wide value duplicates its backing
storage. Move and destruction are `noexcept`. Moving from a distinct
object resets it to Integer zero, Rational `0/1`, BV1 zero, or F(2,2)
positive zero, respectively. Self-copy and self-move preserve the value.
Move and reset allocate nothing. Copy assignment has the strong guarantee.
Checked value-producing functions use `NumericResult<T>`. C++ copy
construction and assignment can propagate `std::bad_alloc` where their
language-defined signatures require them. Existing throwing formatting and
copy conveniences, where retained for node callers, keep their contract.

`IntegerView` and `RationalView` provide the read-only access specified by
their type documents. `Borrow()` is lvalue-only. A borrow of backing storage
requires its owner to remain alive and unchanged, including when the owner
is a node or a Rational component. Inline Integer views can hold a signed
or unsigned word by value. BV and FP operations take read-only references
to their owning values. `NumericResult<T>` and `FpOutcome<T>` own any
contained numeric values. Numeric APIs expose no writable limbs or GMP
objects and retain no caller borrow beyond a call, except explicit views.
Infallible queries such as width, sign, core equality, and hash return
directly.

## Checked errors

```cpp
enum class NumericError : std::uint8_t {
  kInvalidText, kInvalidArgument, kValueOutOfRange, kDivisionByZero,
  kSizeLimit, kCppAllocation,
};

template <class T>
using NumericResult = std::expected<T, NumericError>;
```

| Code | Condition |
| --- | --- |
| `kInvalidText` | Text violates the numeric entry point's grammar |
| `kInvalidArgument` | Invalid width, format minimum, rounding-mode code, radix, index, operand shape, or exactness precondition |
| `kValueOutOfRange` | A valid value falls outside a requested checked value domain, such as a negative or oversized BV literal |
| `kDivisionByZero` | A checked Integer or Rational operation requires a nonzero divisor or denominator |
| `kSizeLimit` | A valid input or operation requires a size outside an actual representation or backend capacity |
| `kCppAllocation` | A project-controlled C++ allocation fails in a checked operation |

Errors are fixed-size and carry no allocated message. Callers attach
diagnostic context. Independently callable APIs validate widths, formats,
indices, modes, and divisors before entering kernels that rely on those
invariants. Multiple invalid conditions may be diagnosed in either order
when each reported condition applies. An optional narrowing query may
return an empty optional for a value outside its target range. For BV
division the standard specifies values for zero divisors. FP division has
its own defined special values, independently of Integer or Rational
division.

Validate lengths, sums, products, limb counts, byte counts, and backend
requirements before computing or narrowing them. Width and format limits
come from `uint32_t`, including the FP sum. Allocation sizes and count
representations come from `size_t` and the actual storage or GMP operation,
including required scratch and spare limbs. A conservative estimate alone
cannot establish that a result or an operation exceeds capacity. Use a
tighter bound or a capacity-safe computation when a bound straddles the
limit. Check the supported backend's allocation and entry-point types,
including `mp_bitcnt_t`, `mp_size_t`, and the allocation count used by
`mpz_t`. GMP 6.3 uses an `int` for that allocation count even when
`mp_size_t` is wider. The layer imposes no discretionary width, work, or
memory quota. [GMP integer internals](https://gmplib.org/manual/Integer-Internals)

Operations publish a complete canonical result only after successful
preparation. Errors leave input values and existing borrows intact.
Project-controlled allocation failures can be recovered at the checked
boundary. GMP limb allocation failure terminates the process. Production
code uses the default GMP allocator and allows neither exceptions nor
nonlocal exits through its allocation callbacks.
[GMP allocation contract](https://gmplib.org/manual/Custom-Allocation)

## Concrete FP outcomes and solver callers

`FpOutcome<T>` is an owning tagged sum. It carries one defined `T`,
two distinct allowed `T` values of the same destination type, or an
unspecified result constrained only by its destination type. Store the two
alternatives inline as value objects and the unspecified destination as
its checked BV width or `FpFormat`, with no candidate payload. Accessors
require the matching tag. There is no implicit conversion to `T`.

`Min` and `Max` return `NumericResult<FpOutcome<FloatingPointValue>>`.
Their opposite-zero cases are the two-choice outcome. `ToSignedBv` and
`ToUnsignedBv` return `NumericResult<FpOutcome<BitVector>>`, with an
unspecified BV destination for NaN, infinities, and rounded out-of-range
integers. Other FP operations with a unique result return that value
directly or in `NumericResult<T>` as appropriate. An invalid destination,
allocation failure, or size failure uses the outer error even when the
input would otherwise have a semantic outcome.
[Floating-point value and operation contracts](floating-point.md)

For a fixed operator, indices, and ordered semantic argument tuple, every
occurrence must agree on the result. This includes equal NaNs constructed
from different payloads, explicit rounding modes, and FP values carried
through arrays, UF, and datatypes. A rewriter can fold a defined outcome.
A model can choose an allowed result if it preserves congruence and
satisfies the original query. Integer `div` and `mod` by zero stay in the
caller’s total-function semantics and never invoke checked `DivMod`.

At an input boundary, report invalid input and actual resource failures
through that boundary's diagnostics. During solving, keep the original
term, use another sound procedure, or return `unknown` when computation
is needed and no sound result can be established. Numeric failure and
process termination give no basis for `sat` or `unsat`.
[Models and results](../architecture.md#8-models-and-results)
