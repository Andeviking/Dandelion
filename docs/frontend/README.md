# Frontend Design

The C++23 module `dandelion.frontend` implements the selected SMT-LIB input
profile. Its static library depends on Node and the standard library.

The [project architecture](../architecture.md) describes the frontend's role
in the solver as a whole. The separate [frontend testing design](../testing/frontend.md)
describes testing requirements and remains part of the testing documentation.

The frontend binds nullary definitions and `:named` directly to typed
bodies and exposes a semantic `QueryContext`. Source history and strict
metadata validation are independent session options. Model export uses
canonical symbols, semantic ranks and available sort abbreviations.

[`Session::ReadCommand`](design.md#phase-2-fused-command-reading) reads and
publishes one typed command at a time. It preserves grammar precedence
during recovery. The execution loop uses the same reader for prompt
check responses and model-aware admission. `SyntaxValidator` serves
grammar-only tools and tests. The
[Phase 2 test design](../testing/frontend.md#phase-2-test-design) specifies
the acceptance cases.

The [implementation record](implementation.md) reports Phase 1 verification
and the earlier bounded baseline evidence. Future providers will establish
solving and production model semantics.

| Document | Responsibility |
| --- | --- |
| [Implementation and acceptance](implementation.md) | Library entry points, build and test evidence, and reproducing the bounded corpus checks. |
| [Frontend design](design.md) | Overall architecture, syntax, binding, typing, datatype handling and review. |
| [Logic profiles](logic-profile.md) | Exact registered labels, signatures and compatibility extensions. |
| [Semantic interfaces](semantic-interfaces.md) | Streaming command protocol, definition and query ownership, solver selection and model inspection. |
| [Frontend tests](../testing/frontend.md) | Test-first cases, independent oracles, boundary and acceptance requirements. |

The [frontend research plan](research-plan.md) defines the assigned topic
research and the conditions for a later implementation-ready design. Overall
frontend design and cross-topic integration are a separate effort. The research
plan is not itself a frontend design or an implementation plan. The
[topic research](topic-research.md) is historical evidence; its recommendations
are not authoritative when superseded by the frontend design.
