# Development Principles

This document defines principles for Dandelion's architecture, module design,
algorithms, and implementation. It complements [AGENTS.md](../AGENTS.md), the
[architecture proposal](architecture.md), the [C++ style guide](cpp-style.md),
and the [testing strategy](testing/README.md). It does not select a particular
architecture, algorithm, API, or data structure.

## Design from first principles

Start with the agreed solver scope, its semantics, and its correctness and
performance requirements. Derive component responsibilities, interactions,
and invariants from those requirements. Consider the full agreed scope and its
theory combinations even when a particular component is not yet implemented.
Do not add frameworks or extension points merely for hypothetical needs.

Choose module boundaries that make responsibilities, dependency direction,
ownership, and object lifetimes understandable. Keep implementation decisions
local where that reduces unnecessary coupling. An abstraction, indirection,
configuration option, or layer should have a concrete purpose and justify its
complexity. Shared concepts should have a coherent owner rather than being
reimplemented inconsistently across components.

Study existing solvers and learn from their ideas and implementations, while
making independent decisions for Dandelion. Favor solutions that address the
underlying problem directly. Conciseness means removing unnecessary complexity;
it does not mean minimizing line count or avoiding the complexity inherent in
the problem.

For a nontrivial algorithm or data structure, identify its assumptions and
invariants, the results it must produce, and the relevant correctness,
termination, and complexity conditions. Choose the mechanism for those
obligations and the expected workload, rather than adopting a pattern solely
because another project uses it.

## Establish and rely on invariants

For each relevant interface, establish what it accepts, what it guarantees,
and which caller or component is responsible for maintaining those guarantees.
Use types and representations to express and enforce invariants when they do
so clearly and effectively. Document non-obvious preconditions, ownership,
and lifetime requirements where callers need them.

Validate potentially invalid input at the boundary responsible for accepting
it. Independently callable entry points, such as a parser and a public API,
must each uphold their contracts; they may share the same validation or
construction mechanism. Once an invariant is established, internal operations
should rely on it instead of repeatedly checking for states that all valid
paths exclude. Do not add defensive checks, exceptions, assertions, or fallback
branches for impossible states merely to make every function self-contained.

For example, suppose every path that creates a valid BV sort rejects width zero
and preserves that invariant. Internal operations accepting such a sort need
not check for zero width again. This guarantee does not establish that two
operands have compatible widths, that a computed width fits its representation,
or that resource limits cannot be reached. Handle such conditions where the
operation requires it and they can actually occur.

An invariant can be relied upon only while its construction, ownership,
lifetime, and mutation rules preserve it. A property of the current CLI alone
does not establish a contract for a separately callable API. When an invariant
changes, update its establishing paths and dependent contracts together.

## Assertions and impossible paths

Use an assertion when it checks a meaningful internal contract and provides
diagnostic value. Do not add assertions mechanically for conditions already
guaranteed by the representation or contract. Assertions can be disabled, so
required validation, correctness, and side effects must not depend on them.
Invalid or unsupported input must be handled according to the relevant
interface contract in all build configurations.

An optimizer assumption or `std::unreachable()` is not a check and does not
establish an invariant. Calling `std::unreachable()` has undefined behavior;
use it only when the applicable paths and invariants guarantee that execution
cannot reach the call. Similarly, use an optimizer assumption only when its
condition is guaranteed. Never use either in place of handling a reachable
invalid input or failure.

## Fix causes and preserve contracts

When a defect or unsuitable design is found, investigate the actual failure
and the invariant or responsibility involved. Correct the cause at the
appropriate boundary, interface, representation, or algorithm. Update affected
callers and design documents when needed. A local fix is appropriate when it
addresses the cause; a broader change is appropriate when the contract or
design itself is wrong. Do not accumulate input-specific workarounds that leave
the underlying inconsistency intact.

Some cases are genuinely distinct under the specification and need explicit
handling. Likewise, validation at a real boundary is a necessary fix when
invalid input can reach it. The goal is to handle the real cases coherently,
not to prohibit conditionals or require a broad redesign for every defect.

Follow the test-first and test-change rules in [AGENTS.md](../AGENTS.md) and
the relevant testing documents. Derive regression expectations from the
contract, and correct a failing test only under the documented exception for a
demonstrably incorrect test. A passing test suite is evidence, not a proof.

## Write clear, effective modern C++

Use C++23 and prefer modern language and library facilities when they express
the design clearly and work with the project's supported GCC and Clang
toolchains. Correctness, performance, and clear module boundaries take
precedence over using a newer feature for its own sake. Follow the specific
conventions and toolchain exceptions in the [C++ style guide](cpp-style.md).

Express ownership and resource management clearly, using types and RAII where
appropriate. Non-owning views and handles require a valid lifetime and do not
by themselves guarantee the validity of the referenced data. Choose error
representations according to the interface and its established policy; a
particular C++ facility does not by itself define that policy.

Each piece of code should serve an actual semantic, algorithmic, diagnostic,
or maintainability purpose. Keep implementation and comments direct and
informative; do not add machinery solely to anticipate impossible states or
unsupported requirements.

## Design for performance and verify claims

Consider algorithmic complexity, allocation, data layout, locality, and the
cost of component interaction during design. Avoid unnecessary work on hot
paths while retaining checks needed by actual contracts. An unnecessary check
may be removed for clarity even when its runtime impact has not been measured;
do not claim that it improved performance without evidence. Compilers can
eliminate some checks, and assertions can have different costs across builds.

Distinguish performance hypotheses from measured results. When making a
performance claim, measure an appropriate workload and configuration and
record enough context for the result to be reproduced. Small, focused
experiments are useful, but their results do not automatically predict
end-to-end solver behavior. Performance priorities do not justify changing the
meaning of accepted inputs or reporting an unjustified solver result.

## Applying these principles

When evaluating a design or change, ask:

- Which requirement or invariant does each component, abstraction, and check
  serve, and which paths establish that invariant?
- Are responsibilities, ownership, dependencies, and failure behavior clear
  across the actual interfaces and supported theory combinations?
- Does a fix address the responsible cause and preserve the relevant
  contracts and tests?
- Does the chosen C++ express the design well on the supported toolchains, and
  are performance claims supported by suitable evidence?

## Research context

The principles above are Dandelion's decisions, informed by these sources;
they do not adopt every rule in the sources:

- [Parnas on module decomposition](https://doi.org/10.1145/361598.361623)
  describes information hiding as a criterion for localizing design decisions.
  It does not establish that adding module boundaries automatically improves
  runtime performance.
- [Google's code review guidance](https://google.github.io/eng-practices/review/reviewer/looking-for.html)
  examines design and complexity at every level. Dandelion's agreed full scope
  is a real design requirement; speculative generality is not.
- The [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
  emphasizes reader clarity and allows necessary performance optimizations.
  Dandelion's C++23 and modules choices are documented project exceptions.
- The [C++ Core Guidelines on interfaces and preconditions](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-pre)
  explain explicit contracts and invariants. Dandelion does not require a
  particular contracts library or a check at every internal call.
- [LLVM's assertion guidance](https://llvm.org/docs/CodingStandards.html#assert-liberally)
  distinguishes internal bugs from invalid user input and explains that
  assertions may be disabled. LLVM recommends broad assertion use; Dandelion
  instead chooses assertions for their concrete diagnostic value. The
  [C++23 draft](https://timsong-cpp.github.io/cppwp/n4950/utility.unreachable) specifies the
  undefined behavior of calling `std::unreachable()`.
- [Google SRE's postmortem guidance](https://sre.google/sre-book/postmortem-culture/)
  emphasizes understanding contributing causes and preventing recurrence.
  This informs root-cause fixes without requiring a formal postmortem for
  routine defects.
- [Google's performance hints](https://abseil.io/fast/hints.html) recommend
  considering performance in design, and [Abseil's microbenchmark guidance](https://abseil.io/fast/39)
  explains why isolated measurements may not predict real workloads.
- [Abseil's guidance on non-owning handles](https://abseil.io/tips/180)
  explains why views and handles require attention to the lifetime of their
  underlying objects.
