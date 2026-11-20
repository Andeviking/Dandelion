# Node and Node Manager

The **first-version node system is implemented**. Its baseline includes
checked builtin and declaration factories, exact numeric values, and
structural interning. The frontend currently represents nonrecursive
definitions with formal parameters using retained applications.

**Phase 1 is implemented and has a recorded verified run.** It adds payload-preserving
`RebuildNode`, word entry points for indexed builtins, BV values, and BV
sorts, and a shared BV width cache. It allows default-initialized child
pointer tails. Under the revised Node boundary, the frontend can bind
closed nullary definitions directly to their typed bodies. Definitions with
formal parameters require retained declarations and instances. See the
[Phase 1 test design](../testing/node.md#phase-1-construction-and-rollback).

**Phase 2 is implemented and has a recorded verified run.** Checked
[consuming factories](design.md#phase-2-consuming-construction) can move
disposable child owners into new parent edges. Hits and recoverable
failures preserve input owners. The fused frontend supplies its operand
buffer directly. The [consuming construction cases](../testing/node.md#phase-2-consuming-construction)
document the test coverage and acceptance requirements.

**Phases 4A and 4B are implemented.** The Phase 1C numeric design landed in
Phase 4A. Checked factories accept typed views from `dandelion.util.numeric`,
and the node allocation contains the numeric literal and rational conversion
payloads. Phase 4B migrated frontend construction, rendering, and benchmark
replay to the typed APIs. [Numeric boundary](design.md#10-phase-1c-numeric-boundary-and-typed-literals)

**Phases 5 and 6 are implemented.** Phase 5 added standalone numeric
workloads, typed Node interning workloads, and a benchmark-local constant
folder for prebuilt DAGs. Phase 6 optimized standalone arithmetic and the
Node Int interning path. The [benchmark record](../testing/benchmark.md#numeric-and-constant-evaluation-benchmarks)
and [Node acceptance record](../testing/node-acceptance.md#phases-4b-5-and-6)
describe the workload boundaries and historical verification.

| Document | Responsibility |
| --- | --- |
| [design.md](design.md) | Semantic contracts, identities, ownership, API, datatype declarations, values, and error behavior. |
| [operators.md](operators.md) | Selected input profile and complete first-version operator, payload, and type rules. |
| [implementation.md](implementation.md) | Storage, interning, construction, rollback, reclamation, traversal, frontend, and build plan. |
| [../testing/node.md](../testing/node.md) | Test-first inventory, independent oracles, concrete cases, and acceptance. |
| [Node Optimization Tradeoffs](../benchmarks/node-optimization-tradeoffs.md) | Measured gains, regressions, experimental candidates, and evidence for performance investigations. |

The expression system is shared by the frontend and solver; see the
[project architecture](../architecture.md) and [frontend design](../frontend/README.md).
Representation of a theory combination does not establish a complete solving
procedure. See the first-version [test inventory](../testing/node-inventory.md) and
[acceptance record](../testing/node-acceptance.md) for scope and verification.
Concrete layout, table, allocator, and immediate reclamation are implementation
choices,
not a public ABI or performance result; later replacements must preserve the
semantic, ownership, and failure guarantees.
