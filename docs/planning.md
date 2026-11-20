# Planning Standard

This document defines how to prepare implementation-ready plans for
Dandelion. It applies to plans for modules, features, infrastructure, and
changes that affect existing behavior. It complements
[AGENTS.md](../AGENTS.md), the [development principles](development-principles.md),
the [architecture](architecture.md), and the [testing strategy](testing/README.md).
Follow the applicable project instructions and module contracts; this document
does not replace them.

A plan may consist of several linked documents. Scale its form and detail to
the work: a substantial module needs a detailed design and implementation
plan, while a straightforward local change may need only a concise description.
The applicable decisions and acceptance conditions must still be clear. An
exploratory research report or architecture proposal may compare unresolved
alternatives, but it must not be presented as an implementation-ready plan.

## Purpose

An implementation-ready plan gives a capable contributor enough information
to implement and verify the agreed scope without inventing important
semantics, interfaces, responsibilities, algorithms, or error policies during
implementation. It selects a concrete initial design and explains why that
design satisfies the requirements. It need not prescribe every local variable
or prevent an equivalent implementation detail that does not affect a stated
contract or selected algorithm.

The plan must be complete for its stated scope and compatible with the
project's full agreed scope. A sequence of implementation tasks does not narrow
the design to the first task. Do not silently postpone a required feature or
cross-component obligation merely because it is difficult. Distinguish the
design of a complete scope from the order in which its parts are implemented.

## Architecture and first principles

Start from first principles: identify the problem, the agreed requirements,
the relevant semantics, constraints, and expected use. Derive the necessary
responsibilities, invariants, interfaces, and mechanisms from those facts.
When changing existing behavior, identify the underlying cause and the
boundary that owns it. Validate inputs at the responsible boundary and rely
on invariants established by valid construction paths.

Give architecture priority in planning. For a substantial module or
cross-component change, design the relevant architecture before detailing
implementation tasks. Identify the decisions that would be expensive to
reverse, including component responsibilities, dependency direction, shared
representations, interfaces, ownership, and lifetimes. Work through their
implications for the complete agreed scope and relevant interactions, rather
than relying on the current implementation or assuming that individual
components will fit together later. Even for a local change, check whether it
affects an existing architectural contract.

Choose a direct solution with clear ownership, dependency direction, and
module boundaries. Each abstraction, layer, configuration option, and extension
point must serve a concrete requirement; do not introduce a framework for
speculative flexibility. Simplicity means removing unnecessary complexity,
not omitting required cases or minimizing the number of lines. Keep
implementation details local where this reduces coupling without hiding
cross-component obligations. Make stable semantic obligations explicit and
choose concrete initial internals so they can be revised locally without
unnecessarily changing those obligations.

Consider performance as part of the design. Where relevant, identify expected
workloads and hot operations, algorithmic complexity, allocation and lifetime
patterns, data layout, memory use, and communication or build costs. Select a
reasonable concrete baseline without claiming it is optimal. Distinguish
assumptions and expected benefits from measured results. Use focused,
reproducible measurements when needed to support a performance claim; a
microbenchmark alone does not establish end-to-end solver performance. Follow
the testing strategy's limits on experiments and datasets.

## Research and evidence

Before selecting the architecture of a substantial module or a significant
algorithm, research the problem broadly. Examine relevant standards and
theoretical results, published research, and existing systems and their
implementations. Compare representative approaches with materially different
designs, including useful ideas from other domains when applicable. Investigate
the problems they solve, their assumptions, invariants, costs, failure cases,
and applicability to Dandelion. Look for evidence against the initial
preference as well as evidence supporting it.

Use research to inform independent choices, not to follow one existing system
by default. Explain which ideas are adopted, adapted, or rejected and why the
selected design fits the requirements. Record sources and relevant versions;
prefer primary sources for technical claims. Distinguish documented facts,
inferences, unmeasured hypotheses, and results measured in a specific system
or workload. Another system's design or results do not prove that a choice
will work best here. Where evidence is insufficient, state the limitation and
select a justified baseline under the rules below.

Scale the depth of research to the decision. A small local change can rely on
focused investigation and established project contracts; it does not require a
survey of unrelated systems. Do not substitute a long list of sources for an
analysis of the decisions they inform. Follow the testing strategy's limits on
experiments and datasets.

## Required content

Provide the following information where applicable. Organize it around the
work rather than creating empty sections or duplicating existing authoritative
contracts. Link to those contracts and identify any proposed change to them.

1. **Objective and scope.** State the problem, intended behavior, supported
   cases and combinations, explicit exclusions, constraints, and relevant
   assumptions. Distinguish the project target, the scope of this particular
   plan, and what is already implemented. Do not infer implementation or
   completeness from a design or from a supported input representation.
2. **Architecture and interfaces.** Identify components, their owners and
   dependencies, the inputs and outputs at their boundaries, and the
   responsibilities of callers and callees. Specify the relevant API shape,
   state, ownership, lifetime, identity, equality, and mutation rules.
   Describe interactions with existing components and supported combinations,
   and explain the reasoning behind consequential boundaries and choices.
3. **Behavior and invariants.** Specify accepted inputs, results and
   observable effects, meaningful invalid or unsupported cases, and resource
   or other failures. State where each invariant is established and how it is
   preserved. Separate externally required behavior from replaceable internal
   representations.
4. **Concrete implementation.** Select the initial data structures and
   algorithms, including the rules needed for construction, updates,
   lookup, removal, cleanup, and failure recovery where relevant. Give
   pseudocode or precise steps for nontrivial transitions and edge cases.
   Identify affected source areas, build integration, and external
   dependencies when they are part of the work.
5. **Correctness and limits.** State algorithmic assumptions, invariants,
   correctness arguments, and relevant termination, completeness, and
   complexity conditions. Explain how transformations preserve the required
   semantics and how results or models are justified when applicable. State
   actual limitations and incomplete cases instead of promising an impossible
   guarantee.
6. **Research and decisions.** Identify the relevant approaches considered,
   the evidence and assumptions behind them, and the reasons for the selected
   architecture and other significant choices. Cite sources and versions as
   appropriate; state evidence limitations and unmeasured expectations.
7. **Tests and acceptance.** Specify the required test design, independently
   justified expectations, normal cases, errors, boundaries, and relevant
   interactions. Identify the checks and commands or existing documented
   procedures needed to verify completion, including appropriate build
   configurations and tools. State what each check establishes and report
   unavailable, skipped, failed, or inconclusive checks accurately.
8. **Execution and status.** Identify necessary dependencies and a feasible
   order of work without replacing the full design with a roadmap. Mark
   planned, implemented, and verified behavior accurately, and provide
   checkable completion criteria. Keep records of implementation and
   verification separate from unexecuted plans.

Follow the test-first rules in [AGENTS.md](../AGENTS.md): before implementing a
feature or changing behavior, complete the applicable test design and add the
required tests. Use the [testing strategy](testing/README.md) and relevant
module-specific test design for coverage, or document the missing test design
first. Tests may initially fail to compile or run; report that state rather
than introducing fake behavior or disabling checks. Preserve tests and apply
the documented exception for demonstrably incorrect tests as specified in
AGENTS.md.

## Decisions and unavoidable unknowns

Resolve decisions that affect correctness, public behavior, component
boundaries, data representation, or the selected initial algorithms before
calling a plan implementation-ready. Specify a single initial choice, or an
explicit decision rule when different cases require different choices.
Explain the rationale and any assumptions on which the choice depends.
Listing alternatives without selecting one is not a decision.

Lack of performance measurements alone is not a reason to defer a necessary
design choice. Choose a correct, practical baseline and describe how it can be
evaluated and replaced later while preserving its contracts. For example, a
unique-table plan should select its initial key, collision handling, growth,
deletion, and lifetime rules; saying only that a hash table will be chosen
during implementation is insufficient.

Some questions cannot responsibly be settled in advance, for example when a
required external constraint is unavailable or a correctness argument has not
been established. For each such question, record:

- The specific decision and why it cannot yet be resolved.
- The affected scope, contracts, risks, and dependent work.
- The evidence or information needed, and the point or condition for deciding.
- A justified default or fallback if one exists; otherwise, the work that must
  remain blocked until the question is resolved.

Do not use an unknown as permission to silently reduce the agreed scope,
assume an unproven correctness property, or pass the decision to the implementer
without guidance. Continue independent work when possible. If essential
information only the user can provide is missing, report the specific need
instead of inventing an answer. A plan with an unresolved correctness or
interface decision is not implementation-ready for the affected work.

## Architecture review before submission

Before submitting a plan, review its architecture deliberately and repeatedly.
Perform at least two passes from distinct perspectives: check the design
against requirements and first principles, then challenge its choices and
their interactions. After a material revision, review the affected architecture
and contracts again. For substantial work, trace representative end-to-end
flows across component boundaries and object lifetimes, including relevant
errors, resource failures, and interactions. Check the design against the
full agreed scope and the research, not only against the proposed
implementation steps.

Use the review to find contradictions, missing obligations, unnecessary
coupling, and assumptions that do not hold. Reconsider the design itself when
the issue is architectural; do not merely add an implementation workaround.
Update affected decisions, interfaces, algorithms, and tests together, then
repeat the relevant checks. Record material findings and how they were
resolved, along with any remaining limitations or blocked work. Keep the
record concise and place supporting detail in linked material when useful. If
no material issues are found, report the review performed without inventing
findings. Unresolved correctness or interface decisions keep the affected work
from being implementation-ready.

At minimum, review whether the plan answers these questions:

- Does it cover the agreed scope, including relevant interactions, without
  conflating a staged implementation with a smaller design?
- Can the contributor identify the responsible component and determine the
  required behavior, invariants, initial algorithms, and failure handling?
- Are important choices made and justified, and are any unavoidable unknowns
  explicit with their impact and resolution conditions?
- Were relevant alternative designs and contrary evidence investigated, and
  does the chosen architecture address their applicable tradeoffs?
- Are the testing obligations and completion criteria independently
  checkable, and consistent with the project's test-first rules?
- Is the design as direct and modular as the requirements allow, and are
  performance expectations distinguished from evidence?

## Maintenance

Plans are working design records, not immutable promises or stable binary
interfaces by default. If implementation, testing, new evidence, or a changed
requirement exposes an error or warrants a different design, address the
underlying cause and update the affected decisions, contracts, tests, and
status together. Explain material deviations and their evidence. Do not claim
that a planned behavior or validation has been completed until it actually has.

For an example of a substantial plan split by responsibility, see the
[node documentation](node/README.md), including its semantic contracts,
operator rules, implementation decisions, and testing documents. Its specific
data structures are choices for that component, not requirements for every
future plan.
