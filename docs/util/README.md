# Foundation Numeric Values

**Status.** `dandelion_util` implements the standalone numeric module.
Node factories and payloads, the frontend, and benchmark workloads use
its numeric types.
[Node design](../node/design.md#5-exact-values),
[frontend implementation](../frontend/implementation.md)

| Document | Responsibility |
| --- | --- |
| [design.md](design.md) | Layer boundary, value ownership, checked errors, and FP outcomes |
| [integer.md](integer.md) | Exact signed integers, Euclidean arithmetic, and arbitrary exponentiation |
| [rational.md](rational.md) | Exact reduced fractions, arithmetic, and integral rounding |
| [bitvector.md](bitvector.md) | Width-bearing values, modular arithmetic, and the full BV operator set |
| [floating-point.md](floating-point.md) | Abstract FP values, exact concrete evaluation, and conversions |
| [implementation.md](implementation.md) | Module layout, dependency graph, migration, and acceptance |
| [research.md](research.md) | Versioned evidence, design rationale, and reference limitations |
| [Numeric testing](../testing/util.md) | Test-first coverage, independent oracles, and failure injection |

`dandelion_util` exposes `dandelion.util.numeric`. Node construction, the
frontend, and future evaluation and theory code consume the same concrete
semantics. The layer keeps common values inline, owns wide storage
exclusively, and uses GMP privately for exact arithmetic. The four value
types cover concrete operations for the selected Int, BV, and FP profile.
Rational values support exact conversions and integer algorithms.

Concrete results carry SMT-LIB value identity. Checked computation can
report an input error, an actual representation limit, or a C++ allocation
failure. A successful FP evaluation can also describe a set of permitted
results. The solver and model layers own the interpretation of those sets
across a query. [Solver architecture](../architecture.md),
[node operator inventory](../node/operators.md)
