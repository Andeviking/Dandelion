# Agent Instructions

## Project

- Dandelion aims to build a high-performance SMT solver from scratch, with independent design choices, targeting quantifier-free formulas over the following theories and their combinations:
  - Fixed-size bit-vectors (BV).
  - Floating-point arithmetic (FP).
  - Integer arithmetic, including integer difference logic and linear and nonlinear integer arithmetic.
  - Extensional arrays.
  - Uninterpreted functions and sorts (UF).
  - Algebraic datatypes (DT).
- General Real and String theories are outside the current scope; this does not exclude SMT-LIB metadata strings.
- The current user-facing target is a single query, without incremental `push`/`pop` or multiple-`check-sat` sessions. Internal backtracking, theory refinement, and multiple SAT calls remain allowed.
- Study existing solvers, including Bitwuzla, and learn from effective ideas and implementations while making independent design decisions.
- Prioritize solver correctness. Validate changes to semantics and solving logic with appropriate tests, and support performance claims with reproducible measurements.
- Return `sat` or `unsat` only when justified for the original query. Return `unknown` when an accepted query cannot be decided, and report invalid or unsupported input explicitly.

## Language

- Write all project-authored content in English, including documentation, source comments, identifiers, tests, build files, user-facing messages, and agent instructions.
- Communicate with the user in their preferred language.

## Development

- The intended toolchain is C++23, CMake, and Ninja. Prefer C++ modules and `import std` where supported and verified.
- Maintain compatibility with both GCC and Clang; GCC is the primary development compiler.
- Design from first principles and fix underlying causes. Validate inputs at the responsible boundary, rely on established invariants internally, and avoid redundant checks for impossible states.
- External SAT solvers such as CaDiCaL and Kissat may be integrated as dependencies when appropriate.
- Keep documentation aligned with the actual repository. Clearly distinguish implemented behavior from plans and proposals.
- Before pushing changes, run the CI `Quality` checks locally across the PR range (for example, `MERGE_BASE=$(git merge-base origin/main HEAD) && git diff --check "$MERGE_BASE" HEAD -- . ':!LICENSES/**' ':!*.patch'` and `python3 scripts/check_cpp.py format-check`), as well as `git diff --check` for uncommitted edits.

## Project Documentation

- Follow `docs/development-principles.md` for architecture, contracts, and implementation decisions.
- Follow `docs/planning.md` when preparing implementation plans.
- Before designing or implementing solver functionality, read the overview and relevant sections of `docs/architecture.md`, including affected cross-component contracts, and the relevant module designs. Read the full architecture for system-wide or broadly mixed-theory work. Distinguish adopted decisions, proposals, and existing implementation.
- For node and NodeManager work, use `docs/node/README.md` to locate the applicable design, operator, implementation, and test documents.
- For frontend work, use `docs/frontend/README.md` and the relevant node contracts, including `docs/node/operators.md` where input forms or operators are affected.
- For test design, use `docs/testing/README.md` and the relevant module-specific testing document, such as `docs/testing/node.md` or `docs/testing/frontend.md`. If a required design is missing, follow the Test-First Development rules below.
- For build work, follow `docs/cmake.md` for the implemented scaffold and the design baseline for future solver targets. For C++ formatting, naming, documented exceptions, and developer checks, follow `docs/cpp-style.md`.
- Consult `docs/solver-architecture-research.md` when examining architectural alternatives or their evidence; it is research, not an implementation specification.

## Test-First Development

- Use GoogleTest for all project tests; CTest handles test discovery and execution. Helper scripts and external programs may support GoogleTest cases.
- Before implementing a feature or changing existing behavior, design and add the complete tests for the scoped behavior according to `docs/testing/README.md` and the relevant module-specific testing document. Cover applicable normal cases, errors, boundaries, and interactions, and derive expected results from the documented contract rather than the implementation.
- Complete coverage means addressing all applicable documented test requirements for the scoped behavior with a finite set of cases; it does not require enumerating every possible input.
- If the relevant module-specific testing design does not exist or does not cover the change, document the necessary test design before adding the tests and implementing the behavior.
- Tests may initially fail to compile or run because the required interfaces do not exist yet. Report that state accurately; do not disable tests or add fake implementations to make them pass.
- Until all project-required tests for the change pass, do not delete, disable, modify, or weaken existing or newly added test cases. Additional tests may be added.
- A demonstrably incorrect test may be corrected before the tests pass. Record the independent evidence for the error and correct only what is necessary; never change a test merely to make an implementation pass.
- After all required tests pass, review tests for genuine redundancy if useful. Preserve coverage of the required behaviors and boundaries, and rerun the required tests after any consolidation or removal. Passing tests provide evidence of correctness, not a proof.
