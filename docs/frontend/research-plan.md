# Frontend Research Plan

Status: **historical research task breakdown**. The frontend is not
implemented, and this document is not an implementation-ready frontend
design. The independent [topic research](topic-research.md) records the
assigned investigation; the revised [frontend design](design.md) contains
the current directions and remaining integration decisions. In particular,
the former resource-budget recommendations do not override the agreed
no-quota policy.

## Scope and starting point

The work covers a quantifier-free SMT-LIB frontend for BV, FP, integer
arithmetic, extensional arrays, UF, datatypes, and their supported combinations.
The user-facing target is one query. General Real and String theories,
quantifiers, user `push`/`pop`, and multiple `check-sat` sessions are outside
the project scope. SMT-LIB metadata strings are still in scope.

Start with the [project architecture and proposed input profile](../architecture.md#proposed-input-profile),
the implemented [node and NodeManager contracts](../node/README.md), especially
the [operator profile](../node/operators.md) and
[frontend boundary](../node/implementation.md#8-frontend-and-model-boundaries),
and the [frontend testing design](../testing/frontend.md). The current operator
profile includes constant arrays and a restricted concrete-rational-to-FP
conversion; it does not add a general Real sort. The node implementation can
represent the specified terms, but it does not implement a parser or a
definition-substitution engine.

Treat established node contracts as the starting point and distinguish them
from proposed frontend behavior. If research identifies a conflicting or
unsuitable contract, explain the evidence and the required change to the
affected design, implementation, and tests. Do not silently change the
accepted project scope or assume that a represented node is already solvable.
Follow [development principles](../development-principles.md) and the
[planning standard](../planning.md).

## Assignment and research organization

One researcher is assigned **topics 2 through 9**. Topic 1, overall frontend
design, cross-topic convergence, the integration review, and the final
implementation-ready decision belong to a later, separate effort. The
researcher must investigate each assigned topic in depth and report its
conclusions separately. The numbering is for organizing research, not a
requirement for C++ classes, directories, build targets, or a development
sequence.

Topic-specific proposals may describe algorithms, interfaces, data structures,
and expected responsibilities where necessary to answer that topic. Mark
assumptions about another topic or the overall architecture explicitly. Record
cross-topic dependencies and contradictions for later resolution; do not turn
the reports into a single overall architecture, settle the final public API,
or declare the complete frontend design ready. Do not implement the parser,
tests, benchmarks, numeric functionality, or CMake integration as part of
this assignment. Proposed changes to existing contracts are findings for
later consideration, not authorization to modify those contracts.
Small throwaway prototypes and scripts outside the project are allowed when
needed for bounded research; they are not frontend implementation.

Write the project-authored research findings in English in
`docs/frontend/topic-research.md`, with a separate section for each assigned
topic. An individual section may link to supplementary evidence when needed;
it must remain possible to review each topic on its own. The final response to
the user can be in Chinese. Do not revise the existing architecture, node,
testing, build, or project-instruction documents in this assignment.

For each assigned topic, provide:

- The precise questions, applicable input and scope, current project behavior
  and contracts, and the relevant standard or documented extensions.
- A comparison of materially different applicable approaches, using the
  standard and representative existing systems. Prefer primary sources and
  explain their assumptions, costs, limitations, and implications for
  Dandelion. Distinguish a documented fact from an inference, an unmeasured
  hypothesis, or an actual measurement.
- A concrete topic-local recommendation or decision rule, with the relevant
  invariants, algorithm, interface needs, ownership, failure behavior, and
  complexity. Explain why it fits the requirements and which conclusions
  depend on choices reserved for the overall design. Do not add abstractions
  or defensive checks without a concrete purpose.
- Independently justified representative valid, invalid, unsupported,
  boundary, and interaction cases, or an explanation where a category does
  not apply. These are research and test-design evidence, not implemented
  GoogleTest cases.
- Source links and precise standard revisions or solver tags and commits.
  Report any bounded experiment with its exact inputs, versions, outcome, and
  limitations. Reference agreement is evidence, not a definition of semantics.
- Dependencies and tensions with other topics, proposed changes to existing
  contracts, and remaining questions. For each unresolved question, explain
  its impact, what information or decision is needed, and a justified fallback
  if one exists. Resolve questions that can be answered locally; do not hide
  global decisions behind an unsupported local assumption.

Existing solvers are sources of ideas and evidence, not prescribed designs or
automatic correctness or performance oracles. Do not assume that a design is
inefficient merely because it is mature or complex. Small, bounded experiments
are allowed; do not run large experiments or unbounded corpus scans as part of
this research. Consider cvc5, Z3, Bitwuzla, and other relevant systems where
they support the feature under investigation; do not require a comparison from
a solver outside its scope. Record the exact source revision separately from
the version of any executable tested. Preserve existing source checkouts and
user changes; conduct any experimental instrumentation on isolated copies.

Follow the [dataset policy](../testing/README.md#local-benchmark-data): the
protected dataset tree, including its metadata, must remain unchanged, and
datasets must not be downloaded without explicit permission. Do not read or
enumerate it unless metadata-safe access is established; use an existing
verified safe copy where available. Report unavailable inputs and provenance
limits rather than treating them as successful validation. Synthetic examples
and small tests are useful evidence but are not a real-corpus distribution.

## 1. Overall architecture and interfaces — reserved

This topic is **not assigned** to the topic researcher. It describes the
subsequent work that will use the topic reports. The researcher may identify
local architectural requirements and compare existing systems, but must not
perform the overall design or reconcile all cross-topic choices.

Determine the frontend's inputs, outputs, and relationship to the expression
system, command execution, and future solver. Compare command-at-a-time and
whole-script approaches, including whether and where an intermediate syntax
representation is needed. Specify how callers obtain parsed commands and
typed terms, what state must survive between commands, and how a solver-free
parse-only consumer can inspect the result.

Define ownership and lifetimes for the parser, input, symbols, declarations,
nodes, source occurrences, and any query or command objects. Explain how
command parsing relates to execution, including commands whose validity or
output depends on a solver result. Define dependency direction and identify
the boundaries that must remain independent of SAT and theory solving.

Deliver a coherent architecture, interface shapes, state and lifetime
contracts, and end-to-end data flow. Reconcile its choices with the other
topics before declaring the design complete.

## 2. Input profile and command semantics

Investigate the proposed language and theory revisions, compatible 2.6 forms,
logic names and their actual restrictions, and accepted extensions. A logic
name alone does not establish that every construct in an input is supported.
Compare policies for a recognized logic that permits excluded theories when a
particular script uses only supported constructs, and explain the corresponding
capability claims.

Analyze supported commands, options, metadata, ordering, declarations and
definitions, one query, result-dependent commands, `exit`, and handling of a
missing or repeated `set-logic`. Classify malformed or ill-sorted input,
unsupported input, input or resource failure, and normal end of input. Keep
parsing a query distinct from obtaining a solving result; metadata such as
`:status` is not a solver answer.

Deliver a proposed input and command capability inventory, including valid forms,
profile conditions, exclusions, errors, and standard or extension evidence.
The inventory should account for `set-info`, `set-option`, sort and function
declarations, non-recursive definitions, assertions, one `check-sat`, model or
value queries, and `exit`. Distinguish recognized syntax from an executable
command or a supported solving feature. Include absent, unknown, or repeated
logic declarations and subsequent commands in the analysis.

Check concrete profile boundaries such as metadata strings versus String
terms, `QF_LIA` syntax versus general integer terms, and `QF_BVFPLRA` inputs
with and without symbolic Real terms. Compare compatible 2.6 and selected 2.7
forms rather than deriving language features from the corpus year. Propose a
topic-local policy and mark command state and result-dependent execution as
dependencies on the later overall design.

## 3. Lexing, syntax, and input processing

Compare hand-written and generated approaches and recommend an initial lexer
and parser design. Analyze input buffering, token and source-location lifetimes,
quoted symbols, metadata strings, comments, numerals, BV literals, parentheses,
indexed identifiers, annotations, and incomplete input. Clarify which
syntax or data must be retained for later stages.

Describe how term, sort, binding, and command syntax is processed, including
deep nesting and the use of an explicit stack or another bounded approach.
Specify the behavior after a syntax or input error and how large inputs avoid
unnecessary copying. Record token ownership, diagnostics, and command-boundary
assumptions involving topics 1 and 4 for later integration.

Examine buffer-boundary behavior, token and string ownership, exact source
locations, termination at end of input, and whether an error permits continued
use. Compare the costs of preserving a syntax representation with constructing
semantic objects as input is consumed. Include examples where comments or
parentheses occur inside quoted symbols or strings, not just between tokens.

Deliver a topic-local recommendation for parsing algorithms, state transitions,
input and memory model, and tests for valid, malformed, and deeply nested
inputs. Record command-boundary and ownership assumptions for later integration.

## 4. Symbols, scopes, definitions, and annotations

Specify the symbol environment, declaration identity, duplicate-name rules,
quoted and unquoted names, scope entry and exit, and shadowing. Define
simultaneous `let` bindings and non-recursive `define-fun` and `define-sort`
handling. Describe capture-safe substitution, its memoization and sharing, and
the lifetime of formal parameters and definition bodies.

Specify the handling of `:named` and other accepted annotations, including
source-level closedness, visibility to later expressions, source occurrences,
and state restoration when a command fails. Explain how no formal introduced
for a definition remains free in a solver root.

Deliver concrete symbol and scope algorithms, definition instantiation and
rollback rules, and independently justified binding and identity cases. Include
parallel rather than sequential `let` bindings, nested shadowing, duplicates,
quoted-name identity, and references to named subterms. Explain the cost and
sharing of repeated definition instantiations and how an incomplete command
affects published names. Record dependencies on datatype patterns in topic 6
and source ownership in the reserved overall design.

## 5. Sorts, type resolution, and operators

Specify how sorts are resolved and how type constraints and contextual
information disambiguate builtins and declarations. Cover qualified
identifiers, `(as ... Sort)`, indexed operators, arity, overloaded operators,
parameterized declarations, and logic-specific restrictions.

Define what is checked by the frontend and what is delegated to NodeManager,
without duplicating semantic rules unnecessarily. Map each accepted source
operator schema to its resolved kind or declaration, sort, children, indices,
and payload; identify intentionally internal node kinds separately. Reuse the
existing operator specification as the starting point and identify any
necessary corrections rather than maintaining conflicting rule tables.

Deliver a topic-local type-resolution algorithm, proposed frontend-to-node
requirements, an operator coverage map, and applicable valid, invalid,
ambiguous, and combined cases. As concrete checks, distinguish Bool from BV1,
core `=` from `fp.eq`, and a well-sorted integer multiplication from a term
permitted by a declared linear logic. Include index ordering, exact bounds,
contextual type information, and cases that need an explicit sort qualifier.

Cover the selected operator schemas by reference to the existing authoritative
inventory rather than copying it without adding information. Record missing or
inconsistent mappings and distinguish local recommendations from the final
cross-component interface. Note datatype-specific constraints from topic 6
and exact numeric payloads from topic 7.

## 6. Datatype declarations and `match`

Specify ordinary, parameterized, recursive, and mutually recursive datatype
declarations, including sort aliases, field types, declaration identity,
well-foundedness, and the project's accepted recursive-type restrictions.
Map declarations to the existing datatype builder and define how constructors,
selectors, testers, and their concrete instances participate in name and type
resolution.

Specify pattern variables, wildcards, constructor patterns, branch typing,
exhaustiveness, repeated patterns, and first-match order. Describe a
semantics-preserving lowering to the selected node representation, including
sharing, substitution, and failure behavior.

Deliver declaration and `match` algorithms and their interfaces to topics 4
and 5, with tests for recursive, parameterized, malformed, and mixed-theory
cases. Include ambiguous polymorphic constructors, symbolic tester indices,
selector types, unreachable branches that still require type checking,
exhaustiveness, and repeated or wildcard patterns. Analyze the accepted
datatype-builder contract and the difference between permitted field nesting
and out-of-profile recursive containers. Do not treat datatype parsing as a
datatype decision procedure.

## 7. Numeric literals and value boundaries

Specify exact parsing and construction of integer, BV, rounding-mode, and FP
literals, including signs, binary and hexadecimal forms, widths, indices, and
special FP values. Describe the selected concrete-rational-to-FP input grammar,
normalization, and zero-denominator handling without introducing general Real
expressions.

Determine the minimal additional exact numeric operations or conversion APIs
needed by the frontend and where they belong. Define error and resource
behavior before narrowing any source number to a machine type. Distinguish
literal construction from general BV arithmetic, FP evaluation, and solving;
the frontend does not require those complete implementations merely to parse.

Deliver numeric input and conversion contracts, any proposed changes to the
existing numeric API, and exact independently derived boundary cases.
Check the meaning of leading zeroes in BV literals, values and widths beyond
host-word sizes, invalid BV widths and values, FP special values, and the
selected zero-denominator policy. Analyze the currently exposed
`Integer::ParseDecimal` and `Rational::Make` APIs rather than assuming that
general numeric arithmetic is already implemented. Record dependencies on
accepted syntax in topics 2 and 3 and payloads in topic 5.

## 8. Correctness and test design

Investigate how to turn the proposed profile into the coverage inventory
required by the [frontend testing design](../testing/frontend.md). Identify
the obligations for commands, sorts, operator schemas, binding forms, and
applicable interactions, with independently justified positive, negative, and
boundary cases. Record known gaps and exclusions rather than inferring coverage
from individual tests. This is research for a later complete test design, not
the final cross-topic acceptance review.

Propose GoogleTest unit and component tests that inspect typed structure,
declaration identity, exact values, bindings, and command behavior without a
solver. Combine hand-derived expectations, bounded generated and metamorphic
cases, differential checks, and selected semantic comparisons where suitable.
Reference tools may ignore logics, accept extensions, report an error and
continue, or disagree; define how the harness detects and reports those cases.

Deliver a proposed test inventory, oracles, test interfaces and fixtures,
and candidate acceptance procedures. Real corpus checks must distinguish
accepted-only inputs from semantically checked inputs and report unavailable,
unsupported, failed, and inconclusive cases separately. Consider a reference
process's complete output and exit status, including an error followed by a
later result.
The normal unit and component suite must not require reference solvers, a
dataset, network access, or a particular working directory. Preserve the
project's test-first rules when proposing how tests will later be added.

## 9. Performance, resources, build, and corpus validation

Identify expected work and memory costs for input handling, parsing, symbols,
source occurrences, type resolution, definitions, and Node construction.
Propose concrete initial resource and data-structure choices, including how
large inputs and failures are handled. Inspect potential repeated work and
unnecessary copies without claiming unmeasured performance gains.

Investigate frontend-only CMake and GoogleTest integration, GCC and Clang
builds, and applicable sanitizer checks. Describe bounded Google Benchmark
workloads and measurement boundaries for input, parsing, node construction,
elapsed time, and memory; do not equate component measurements with solver
performance.
Select real inputs by documented provenance and actual features, within the
dataset policy, and distinguish missing cross-theory corpus evidence from a
passed test.

Deliver a proposed build and resource baseline, a reproducible measurement and
corpus validation plan, and requirements for topics 1 and 3. Explain what
process peak RSS includes and why it cannot by itself be attributed solely to
nodes or parser allocations. Consider cold and repeated parsing, input
acquisition, node construction, and bounded deep or large shapes separately.
Report the scope of any actual measurement; do not infer full-solver performance
from a microbenchmark. A lack of measurements does not by itself prevent
selecting a correct initial design; it does limit performance claims.

## Integration and review — reserved

This section and the readiness conditions below are for the later overall
design effort. They are **not part of the topic research assignment**.

The overall design owner reconciles the topic results into one coherent
design and identifies a single responsible component for each invariant and
check. In particular, resolve these shared boundaries:

| Boundary | Topics to reconcile |
| --- | --- |
| Input profile, command state, parsing, and execution | 1, 2, 3 |
| Token, name, node, source-occurrence, and query ownership | 1, 3, 4, 9 |
| Definitions, substitution, datatype patterns, and sharing | 4, 5, 6 |
| Logic restrictions, type resolution, numeric payloads, and NodeManager | 2, 5, 6, 7 |
| Feature coverage, error categories, reference tools, and corpus evidence | 2, 3, 4, 5, 6, 7, 8, 9 |

Review the resulting design against the complete scope and the existing
contracts, then challenge its choices, costs, and interactions from a second
perspective. Trace at least these flows across component boundaries and object
lifetimes, including failure where applicable:

1. BV input with indexed operators, shared terms, and nested simultaneous
   `let` bindings.
2. Integer and UF declarations, a non-recursive definition, and a `:named`
   subterm or other accepted annotation.
3. Parameterized or mutually recursive datatypes with constructors, selectors,
   testers, `match`, and fields of another supported theory.
4. FP terms with rounding modes and a permitted concrete rational conversion.
5. Arrays and uninterpreted functions combining multiple supported sorts.
6. Malformed, ill-sorted, unsupported, and resource-limited inputs, including
   command failure and the resulting state.
7. A large or deeply nested input, including input buffers, sharing, source
   information, and cleanup.

Record material findings, their resolutions, any necessary contract changes,
and remaining limitations. Revisit affected interfaces and tests after a
material change rather than adding a local workaround.

## Conditions for an implementation-ready design

The integrated design is ready for the agreed frontend scope only when:

- Every accepted input family and relevant combination has a defined semantic
  path, responsible components, output representation, and error behavior.
- Correctness-relevant choices, interfaces, state, ownership, algorithms, and
  their interaction are resolved; any genuinely unavailable information and
  the work it blocks are explicit. No required feature is silently postponed.
- The selected initial data structures and resource behavior have a reasoned
  baseline. Unmeasured performance questions may remain open when they do not
  leave a correctness or interface decision unresolved.
- A reviewable test inventory covers the scope under the
  [frontend testing design](../testing/frontend.md), with independent
  expectations and clear procedures for unavailable or inconclusive evidence.
- Build integration and solver-independent testing are specified. Corpus
  compatibility is claimed only for inputs actually checked with adequate
  provenance; unavailable corpus evidence is not a successful corpus run.
- Both architecture review perspectives and the representative flows above
  have been completed and material findings addressed.

After the design is ready, add the required tests for the scoped behavior
before implementing it, following the test-first rules in
[AGENTS.md](../../AGENTS.md). Passing tests support the design and
implementation but do not prove correctness or rule out new findings.
