# Frontend Design

The C++23 module `dandelion.frontend` implements the selected SMT-LIB input
profile. Its static library depends on Node and the standard library.

The [project architecture](../architecture.md) describes the frontend's role
in the solver as a whole. The separate [frontend testing design](../testing/frontend.md)
describes testing requirements and remains part of the testing documentation.

The [implementation record](implementation.md) documents the entry points,
coverage, and bounded corpus evidence. Solving and production model semantics
remain responsibilities of future providers.

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
