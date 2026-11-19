# Foundation Numeric Values

Status: **design and research proposal for review, 2026-11-16**. The four
value types and their full operations described here are not implemented by
this work. The existing `Integer`, `Rational`, and node literal behavior are
implemented and remain governed by the current node documents. No solver,
rewriter, FP evaluator, or frontend is implied to exist.

| Document | Responsibility |
| --- | --- |
| [design.md](design.md) | Boundaries, shared contracts, operation ownership, resources, and node integration. |
| [integer.md](integer.md) | Exact integers, Euclidean arithmetic and exponentiation. |
| [bitvector.md](bitvector.md) | Width-bearing values and the complete fixed-size BV operations. |
| [rational.md](rational.md) | Canonical exact fractions and their callers. |
| [floating-point.md](floating-point.md) | SMT-LIB FP values, concrete evaluation and underspecification. |
| [implementation.md](implementation.md) | Source/module organization, migration, dependency order and acceptance. |
| [research.md](research.md) | Versioned evidence, alternatives and independence of references. |
| [../testing/util.md](../testing/util.md) | Test-first coverage, concrete oracles and integration gates. |

These documents propose a baseline for the complete project scope, not just a
sequence of early features. They use the selected theory revisions and input
forms in [node operators](../node/operators.md), and complement the
[architecture](../architecture.md), [node design](../node/design.md),
[development principles](../development-principles.md), and
[planning standard](../planning.md). Existing contracts take effect until a
future, explicitly tested and documented change replaces them. The source
organization here is a proposal, not an assertion about the current tree.

The principal review choices are: keeping legacy node-compatible numeric
entry points while introducing a neutral computation error; an Integer-backed
initial BV representation; an exact concrete FP evaluator without MPFR; and
leaving the symbolic FP encoding implementation to a separate theory design.
No discretionary work or memory quota is proposed. The existing 32-bit BV
width and FP format representation is retained as the project baseline.
None of these choices is a measured performance conclusion.
