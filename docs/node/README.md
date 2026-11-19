# Node and Node Manager

Status: **first-version node system implemented**. The
documents here specify the shared expression system for the complete project
scope. They are based on the node-design research and review, and are not a
claim of SMT-LIB compliance, solver support, or measured performance.

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
procedure. The documented contracts are the first-version baseline. See the
[test inventory](../testing/node-inventory.md) and
[acceptance record](../testing/node-acceptance.md) for scope and verification.
Concrete layout, table, allocator, and immediate reclamation are implementation
choices,
not a public ABI or performance result; later replacements must preserve the
semantic, ownership, and failure guarantees.
