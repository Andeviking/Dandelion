# Dandelion Solver Architecture

Status: **full-scope architecture proposal for review**. This document designs
the solver as a whole. The [frontend](frontend/README.md) and expression
layer are implemented. Solving procedures remain design choices. The individual algorithms and data structures marked for evaluation
remain choices rather than established performance results.

Dandelion targets quantifier-free formulas over fixed-size bit-vectors (BV),
floating-point arithmetic (FP), integer arithmetic (IDL, LIA, and NIA),
extensional arrays, uninterpreted functions and sorts (UF), algebraic datatypes
(DT), and their combinations. The authoritative scope is in [AGENTS.md](../AGENTS.md).
General Real, String, and quantified formulas are outside the scope.

The external solving interface in this design handles **one query at a time**.
There is no user-facing `push`/`pop` or multiple-`check-sat` session in this
architecture. Conflict-driven search and theory refinement within one query
still have their own internal state and may make multiple SAT calls.

## Reading guide

For focused work, start with the scope and input profile below, the
[architectural principles](#1-architectural-principles), and the
[system overview](#2-system-overview). Then read the sections relevant to the
change, including any affected cross-component contracts:

- [Frontend and expression representation](#3-frontend-and-expression-representation)
  and [rewriting and preprocessing](#4-rewriting-and-preprocessing).
- [Boolean search and theory coordination](#5-boolean-search-and-theory-coordination)
  and [theory procedures](#6-theory-procedures).
- [Theory combination](#7-theory-combination) and [models and results](#8-models-and-results).
- [Correctness and performance evaluation](#9-correctness-and-performance-evaluation)
  and [decisions requiring further evaluation](#10-decisions-requiring-further-evaluation).

Read the complete document for system-wide architecture or broadly mixed-theory
work. Module-specific documents contain additional contracts and requirements.

## Proposed input profile

The [frontend profile](frontend/logic-profile.md) implements the selected
portions of SMT-LIB below. The capability targets describe future solving.

- Use the first-order, quantifier-free portions of [SMT-LIB 2.7, release
  2026-09-09](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf),
  including compatible 2.6 input within the supported profile. Higher-order
  maps, user quantifiers, general Real, and String are outside the scope.
- Accept supported sorts and operators of BV, FP, Int, extensional arrays, UF,
  and inductive datatypes, including well-sorted nested combinations. The
  selected logic and theory declarations determine their syntax and semantics.
- The initial expression profile includes constant arrays, closed concrete
  rational-to-FP conversions, and the selected Ints `**` operator. It excludes
  general Real terms, global sort parameters, and nonstandard recursive
  datatype containers. See [node operators](node/operators.md) for exact
  forms and logic restrictions. Their node representation and typing are
  checked by NodeManager and the active frontend signature.
- Support declaration and non-recursive definition commands, `let`, indexed
  operators, datatype declarations and `match`, `assert`, and one `check-sat`.
  Process commands as a stream and emit the check result without waiting for
  EOF. The Phase 2 protocol flushes the result before the next command read.
  A complete `exit` stops at its closing delimiter, including when suffix
  bytes are already buffered. The interface also includes
  `get-info`, `get-option`, `echo`, and model queries against the retained
  result. See the [frontend design](frontend/design.md).
- Report unsupported commands, options, sorts, or operators according to the
  applicable interface; report malformed or ill-sorted input as an error.
  An accepted query that cannot be decided may return `unknown`. A second
  `check-sat`, `check-sat-assuming`, `push`/`pop`, and recursive function
  definitions are not part of this single-query profile.
- Do not introduce discretionary frontend or numeric quotas. Existing
  machine representations and physical allocation limits remain real
  boundaries, and must not be misreported as invalid mathematical input.
  Retain the current `uint32_t` BV width and FP component/sum bounds; they
  are implementation limits, not SMT-LIB limits.

The proposed capability targets below describe the complete project, not
implemented support. Completeness claims apply to the specified pure fragments
only and assume adequate resources; combinations need their own arguments.

| Input fragment | Acceptance target | Decision target |
| --- | --- | --- |
| Boolean and pure BV | Quantifier-free Core and fixed-width BV operations in the selected revisions. | Complete for the supported finite-width fragment. |
| Pure FP | FP and rounding-mode operations in the Real-free profile, with the specified concrete-rational compatibility form; general FP/Real conversions are excluded. | Complete for the supported finite FP fragment. |
| Pure IDL and LIA | Supported integer difference and linear fragments, determined from the actual formula and definitions. | Complete decision procedures. |
| NIA | Quantifier-free nonlinear integer arithmetic in the selected theory profile. | Sound results; no procedure can guarantee a terminating SAT/UNSAT decision for every input, so `unknown` is allowed. |
| Arrays, UF, and DT | Extensional arrays, uninterpreted symbols, and well-founded inductive datatypes, including supported nested sorts. | Completeness must be established for each supported pure or combined fragment. |
| Mixed theories | Well-sorted combinations of the target theories, including accepted interpreted conversions. | Sound results; completeness or `unknown` is determined per combination. |

Theory revisions must be recorded separately from the language version. For
example, [FixedSizeBitVectors](https://smt-lib.org/theories-FixedSizeBitVectors.shtml)
was updated on 2025-09-09 and [Ints](https://smt-lib.org/theories-Ints.shtml)
on 2026-01-16. The latter includes exponentiation (`**`), whereas the current
[QF_NIA definition](https://smt-lib.org/logics-all.shtml) excludes it. The
planned frontend deliberately admits this supported operator under QF_NIA as
a compatibility extension. Actual support and completeness must be published
per input fragment, without inferring them from a logic name alone.

## 1. Architectural principles

- All supported theories use a common typed expression representation and can
  occur in the same input; theory combination is part of the architecture.
- Preserve high-level BV, FP, integer, array, UF, and datatype structure until
  the selected solver or rewriting step has a reason to lower it.
- Use SAT as the Boolean search engine, with specialized theory reasoning and
  a coordinator that checks their interactions. A BV-heavy formula can take
  an efficient bit-blasting path without making integers into BV values.
- Use a CDCL(T)-style design as the main candidate for mixed-theory search,
  while allowing simpler specialized paths where they handle the whole query.
  The common semantic and model contracts apply to every path.
- A `sat` answer requires a consistent interpretation of the original input;
  an `unsat` answer requires a sound refutation. Incomplete reasoning, limits,
  or unsupported input must never be turned into a guessed result.
- Build a small number of components with clear responsibilities. The boxes
  below are logical boundaries, not a mandate for a plugin framework, one C++
  class per box, or a separate build target for every theory.

NIA satisfiability is undecidable in general. Its support means sound solving
with practical techniques and an honest `unknown` when no answer is established,
not a guarantee that every query terminates with `sat` or `unsat`.

## 2. System overview

```text
                SMT-LIB frontend / programmatic interface
                                  |
                       typed terms, sorts, symbols
                                  |
                         rewriting / preprocessing
                                  |
                          single-query solver
                        /                     \
               SAT interface          theory coordination
                     |                /       |       \
                SAT backend    BV + FP   Int   arrays / UF / DT
                        \                 /
                         consistent model
                                  |
                    result, model, diagnostics
```

The diagram shows responsibilities and communication, not a fixed sequence
or a claim that every query exercises every theory. SAT and theory reasoning
may exchange information repeatedly during the same query. BV and FP are
separate semantic theories; the proposed FP path lowers FP operations to BV,
which can then be encoded to SAT.

Specialized handling may use the same engine with different settings or a
different algorithm; this is not a requirement for multiple solver instances
or a general dispatch framework. A specialized path must establish that it can
handle the actual sorts, operators, and their combinations in the whole query.
A logic name alone is insufficient. Separately satisfiable theory projections
cannot be combined without establishing the shared interpretation. Solving
subproblems independently requires a sound decomposition and a way to compose
their results and shared interpretations. If an incomplete path cannot
establish a result, another applicable path may be tried or the result is
`unknown`; switching paths does not turn an incomplete result into `sat` or
`unsat`.

| Component | Responsibility |
| --- | --- |
| Frontend | Validate commands and publish an owning typed query. Resolve bindings, check sorts, and diagnose unsupported or invalid input. |
| Expression layer | Store typed terms, sorts, declarations, constants, and their shared subexpressions. |
| Rewriter / preprocessor | Simplify formulas with sound transformations while preserving information needed for results. |
| SAT interface | Translate Boolean structure and BV encodings into SAT, invoke a backend, and expose assignments and results. |
| Theory coordination | Connect SAT assignments, equality, theory checks, refinements, and joint model construction. |
| Theory procedures | Implement the semantics of BV, FP, integers, arrays, UF, and datatypes. |
| Model and output | Recover values for the original input and produce the result and requested model information. |

## 3. Frontend and expression representation

The SMT-LIB frontend and any programmatic interface construct the same internal
sorts and terms. The frontend handles declarations, definitions, `let`,
indexed operators, datatype declarations, and type checking. Its accepted
SMT-LIB version, logic names, and commands must be documented precisely; a
logic label is not evidence that every operator appearing in a script is
supported. Unsupported constructs are reported clearly instead of being
silently discarded.

In the active Phase 1 frontend contract, nullary `define-fun`,
`define-const` and closed `:named` occurrences bind directly to owning
typed bodies during elaboration. Parameterized nonrecursive definitions retain
typed templates and saturated applications. A consumer interpreting such
an application retains the matching definition environment. The frontend
checks active signatures and types, and the solver justifies fragment-specific
reasoning from actual formula semantics.

The [Phase 2 command protocol](frontend/semantic-interfaces.md#phase-2-command-processing-and-publication)
specifies one grammar-aware read and elaboration operation per command.
The frontend reduces typed subterms as input arrives and publishes the
command only after its closing delimiter and all checks. A reader callback
can observe only the committed prefix. If semantic or result-state checking
fails early, grammar validation continues so a malformed tail is reported
first. A separate syntax validator serves tooling without a logic or
signature. Whole-command syntax storage is unnecessary for either path.

The sealed query owns typed roots in assertion order, the original typed
signature, datatype groups, options and parameterized templates. Optional
source records belong to the session. For model-enabled queries, keep the
sealed textual environment available for inspection. With model production
disabled, release textual lookup and alias root ownership at seal.
The [frontend interfaces](frontend/semantic-interfaces.md) specify those
lifetimes and the [input profiles](frontend/logic-profile.md) specify
admission. The [implementation record](frontend/implementation.md) describes
baseline behavior and verification.

The expression layer represents at least:

- `Bool`, `Int`, `(_ BitVec w)`, FP and rounding-mode sorts, arrays,
  uninterpreted sorts, and algebraic datatypes, including nested sorts.
- Constants, function declarations and applications, datatype constructors,
  selectors and testers, and the operators of the supported theories.
- An operator's result sort, ordered operands, indexed parameters, and
  declaration or literal payload where applicable.

Use immutable typed terms in a DAG, with structural sharing where beneficial.
A shared node represents the same syntax; it does not replace reasoning about
equalities implied by assertions. Symbol identity must distinguish separate
declarations even if their printed names coincide. A structural key must
distinguish relevant operator and declaration identities, sorts, ordered
children, indexed parameters, and literal payloads. Name resolution must
respect binders and shadowing; recursive datatype references must use declared
sort identities rather than infinitely expanding sort definitions.

Term, sort, query, and model handles have explicit ownership and lifetime
rules. A model that can outlive the solver must own or retain everything it
uses; otherwise its API must prohibit that lifetime. A query-owned manager is a
candidate, not a measured choice. Allocation layout, handles, interning, and
reclamation are implementation decisions rather than reasons to weaken these
identity and lifetime invariants.

The [Phase 2 Node contract](node/design.md#phase-2-consuming-construction)
allows checked factories to transfer disposable owning operands into a new
parent after all fallible preparation. A factory retains the canonical
parent on a hit and preserves the caller's operand slots on hits and
recoverable errors. Each stored edge owns one reference, including repeated
edges to the same child.

Numerals and BV constants require exact arithmetic and must not be silently
limited to a host machine word. Internal exact rational numbers can be used by
integer algorithms or FP literal conversion without exposing a general
SMT-LIB `Real` sort.

## 4. Rewriting and preprocessing

Rewriting simplifies individual typed terms; preprocessing can transform whole
assertions, introduce auxiliary terms, or eliminate variables. Both must obey
the semantics of the relevant theories, including corner cases. A local
rewrite should preserve the term's meaning; an equisatisfiable transformation
must also record enough information to recover a model of the original query.

Keep the original assertions or an equivalent model-validation path. Do not
lower every expression eagerly merely because a BV encoding exists: integer
and mixed-theory procedures need access to their own operations. Word-level
BV simplification, arithmetic normalization, constant folding, and common
subexpression sharing are candidates, with their cost and benefit determined
by measurement.

Each transformation that eliminates, substitutes, or lowers a value must
provide the information or model converter needed to extend a model of its
output to one of its input. Converters compose in reverse dependency order.
Their contract also covers auxiliary symbols and underspecified operations;
merely recording a rewritten formula is insufficient if original values
cannot be reconstructed. Transformations that preserve logical equivalence
without changing model interpretations need no unnecessary recovery record.

## 5. Boolean search and theory coordination

The solver engine owns one query and connects a SAT backend to the active
theory procedures. The Boolean layer represents propositional structure and
theory atoms. BV expressions may be lowered to Boolean gates and clauses;
theory-specific terms that cannot be reduced directly need corresponding
abstract terms and constraints.

The proposed logical solving contract is:

1. Encode the Boolean skeleton and any selected exact BV/FP reductions. Keep
   a mapping between SAT literals and the typed terms they represent.
2. Search with SAT and the relevant theory procedures. Theory checks may
   propagate consequences or detect conflicts on partial assignments when the
   chosen integration supports it. An UNSAT answer for a sound
   over-approximation of the original query refutes the original query.
3. Check a complete candidate with the relevant theory procedures and
   coordinate equalities, values, and constraints shared across theories.
4. If a theory or combination check detects an inconsistency, add a justified
   conflict, lemma, or sound refinement and continue. If a procedure cannot
   establish consistency or a useful refinement, preserve the possibility of
   `unknown`.
5. Return `sat` only when the solver establishes a joint model for all active
   theories and can relate it to the original assertions. Independently
   satisfiable theory fragments or a Boolean SAT assignment alone are not
   sufficient.

Candidate-only checking and search-time theory propagation are different
integration choices under this contract; a design may also combine them. The
latter requires suitable backend callbacks, explanations, and search-level
backtracking. A final theory and combination check is required in either case
unless an exact reduction has already discharged those obligations. The choice
of checks, their frequency, and the SAT backend require evaluation; they are
not implied by a generic SAT library. These forms solve a single user query.

Theory-valid lemmas may refine the Boolean search. Some reductions introduce
fresh symbols, for example an index witnessing that two arrays differ; those
transformations require a sound model-extension argument as well. They are not
automatically valid for every arbitrary assignment to a new symbol.

A theory check receives its assigned theory literals and the current shared
arrangement. Its logical result has one of these meanings:

| Result | Required evidence or obligation |
| --- | --- |
| Conflict | An explanation showing that the relevant assigned constraints cannot hold together in the theory. |
| Propagation | A theory literal entailed by the current assignment, with an explanation usable by the Boolean search. |
| Refinement | A theory-valid consequence, a justified split, or a transformation with a sound model-extension argument. |
| Consistent | A local model satisfying the current arrangement or a constructive guarantee that it can be extended. |
| Incomplete | No justified conflict, refinement, or consistency guarantee is available. |

Finding no conflict is not a `Consistent` result by itself, and local
`Consistent` results do not establish a joint model. An incomplete theory need
not force an immediate `unknown` if another sound check can still refute the
query; it does prevent claiming `sat` without a joint-model guarantee.

The coordinator registers shared terms and relevant equality choices,
distinguishes logical consequences from choices made only by a candidate model,
and reschedules affected checks when a new term, split, or refinement changes
their inputs. For example, `x > 0`, `y > 0`, and `f(x) != f(y)` is satisfiable
for integer `x` and `y` and an uninterpreted `f : Int -> Int`. An arithmetic
candidate that happens to choose
`x = y = 1` is incompatible with UF, but does not refute the query. A justified
equality split or another sound refinement may permit a different candidate.

Explanations used by SAT must be sound consequences of the theory and the
active query. If theory propagation occurs during SAT search, its temporary
decision assignments must be retracted when SAT backtracks within that query;
this does not introduce a user-facing session.

The SAT adapter exposes the capabilities actually needed by the chosen
algorithms: variables, clauses, solving, assignments, and resource limits,
with additional propagation or proof interfaces only where supported. A
backend must either permit the refinement loop to add constraints and solve
again, or the accumulated problem must be rebuilt correctly. CaDiCaL and
Kissat remain candidates; exact versions and integration modes are undecided.

Theory-visible literals have stable external identities even if the backend
preprocesses or eliminates its internal variables. Candidate checking must
receive appropriate reconstructed values and check their theory consistency;
SAT model reconstruction alone does not establish theory consistency. The
adapter must support subsequent references to such variables through a
documented restoration or protection policy. Search-time propagation also
requires version-specific observation, explanation, and backtracking contracts.
Do not assume all backends expose these capabilities or require freezing every
variable: both depend on the actual integration mode. Compare the
[MathSAT5 discussion of SAT preprocessing](https://es-static.fbk.eu/people/griggio/papers/tacas13.pdf)
and the [CaDiCaL 3.0.1 interface](https://github.com/arminbiere/cadical/blob/rel-3.0.1/src/cadical.hpp).

## 6. Theory procedures

### 6.1 Fixed-size bit-vectors

The BV procedure owns exact fixed-width semantics. Its main proposed path is
word-level simplification followed by bit-blasting to Boolean constraints and
SAT. Gates and subexpressions can be shared; the choice between an AIG and
another Boolean representation is an implementation decision.

Support arithmetic, comparisons, bitwise operations, shifts, concatenation,
extraction, extensions, and conversions according to the selected SMT-LIB
theory revision. Widths, signed interpretation, wraparound, oversized shifts,
overflow predicates, and division by zero must follow SMT-LIB rather than C++
machine arithmetic. Alternative word-level reasoning or abstraction may be
used only when its relationship to the exact BV semantics is defined.

Each shared BV term must have a consistent correspondence between its typed
value and the Boolean encoding observed by SAT and other theories. If an
operation is abstracted, the procedure must check the operation against a
candidate and either establish its semantics or provide a sound refinement.
Model reconstruction must recover the BV values of the original terms.

### 6.2 Floating point

The proposed FP procedure uses **word-blasting**: translate FP constraints
into semantically equivalent BV constraints, then use the BV/SAT path. An
existing FP encoding library is a candidate dependency, subject to correctness,
licensing, and performance evaluation.

The encoding and model must account for rounding modes, NaN, infinities,
subnormals, and signed zeros. SMT-LIB core equality and `fp.eq` are different:
for example, `fp.eq` treats positive and negative zero as equal and returns
false for NaN. Underspecified conversions must retain consistent SMT-LIB
semantics; they must not be replaced with a value that changes satisfiability.
Multiple IEEE bit patterns represent NaN, while SMT-LIB uses an abstract NaN
value for each FP sort. The encoding must preserve FP equality and model
semantics rather than equating FP identity blindly with raw bit-pattern
identity.

Only the five standard rounding modes are valid values. Any finite encoding
must rule out invalid codes, and FP values shared with UF, arrays, or
datatypes must carry their semantic identity across the interface. The FP
model must agree with the chosen BV representation and its underspecified
conversions.

General FP/Real conversions, including `fp.to_real` and conversion from a
symbolic Real expression, are outside the Real-free scope. The planned profile
accepts the restricted concrete rational-to-FP compatibility form in
[node operators](node/operators.md#6-floating-point), including a symbolic
rounding mode, without introducing general Real solving. An undefined rational
constant expression is outside that compatibility form.

### 6.3 Integer arithmetic

The integer procedure preserves unbounded mathematical `Int` semantics and
provides reasoning for:

- **IDL:** difference constraints such as `x - y <= c`, for which a graph-based
  procedure is a candidate.
- **LIA:** linear equations and inequalities, with exact arithmetic and
  integer-aware reasoning such as branching or cuts.
- **NIA:** nonlinear integer terms, handled by sound practical procedures
  such as constraint reasoning, abstraction/refinement, or a model-based
  approach. The precise algorithm is an evaluation decision.

`div`, `mod`, negative operands, and zero divisors follow the selected SMT-LIB
integer semantics, not assumptions borrowed from C++. Internal rational
relaxations are allowed even though user-level Real formulas are excluded.
Artificially bounding an unbounded NIA query does not justify `unsat` unless
the bounds themselves are consequences of the original query.

Classify each integer constraint according to the operations it actually uses,
including the applicability of `div` and `mod`; a logic label alone does not
justify routing an unsupported operation to an IDL or LIA procedure. Pure
The supported pure IDL and LIA fragments are targets for complete decision
procedures, subject to available resources. A `QF_IDL` or `QF_LIA` label can
also admit broader Int formulas under the compatibility profile and does not
by itself establish those completeness conditions. Completeness of a
particular algorithm and of mixed-theory combinations needs its own argument.
If a complete decision procedure is used
as a fallback, its scheduling must not indefinitely postpone its progress;
reliable individual checks alone do not establish termination. A rational
relaxation's satisfiable model is not automatically an integer model:
`2*x = 1` is satisfiable over rationals but unsatisfiable over integers.

An integer check must supply justified explanations or refinements and share
relevant equalities and values with the coordinator. A reported NIA `sat`
requires an integer model satisfying the original nonlinear constraints;
`unsat` requires a sound argument for the original unbounded query.

### 6.4 Extensional arrays

The array procedure handles typed `select` and `store`, including nested
arrays and supported combinations of index and element sorts. It enforces
read-over-write and extensionality, potentially using lemmas on demand.

An array disequality requires some index at which their elements differ. A
fresh witness can express this fact, provided the transformation is a sound
extension and its value is consistent with the other theories. Array models
must represent a total array interpretation, including relevant reads and a
consistent default or equivalent representation.

The procedure registers witness terms, exchanges index equalities and
disequalities, and checks that read values agree with their element theories.
Consistency of finitely observed reads alone does not guarantee that nested or
extensional arrays have a compatible total model.

The ArraysEx theory specifies `select` and `store`; the planned input profile
also accepts the constant-array extension specified in
[node operators](node/operators.md#7-arrays-and-datatype-applications). Its
interaction with extensionality and finite index domains must be covered by
the array procedure's correctness argument.

### 6.5 Uninterpreted functions and sorts

The UF procedure enforces congruence: applications of the same function to
equal arguments have equal results. A congruence-closure equality engine or
dynamic Ackermann-style constraints are candidate mechanisms. Declarations and
applications retain their argument and result sorts; functions may interact
with BV, FP, integers, arrays, and datatypes.

The resulting model assigns a consistent interpretation to each user-declared
function and uninterpreted sort. It must respect equalities established by
other theories, not just syntactic equality of arguments. This includes Boolean
arguments and nonempty uninterpreted domains. A finite set of observed
applications needs a consistent total extension, including a default or an
equivalent interpretation for other arguments.

### 6.6 Algebraic datatypes

The DT procedure handles inductive datatypes through constructors, selectors,
and testers. It enforces constructor exhaustiveness, disjointness,
injectivity, and acyclicity of inductive values. A known tester must agree
with the value's constructor and selectors. Cycle checks must also account for
equalities that connect constructor terms indirectly. Selector use on a value
of the wrong constructor follows the SMT-LIB semantics rather than an invented
default.

The representation must accommodate recursive and mutually recursive datatype
declarations, datatype parameters, and fields with supported theory sorts.
The frontend checks the applicable declaration well-foundedness conditions.
Datatype model completion constructs well-formed finite constructor values
whose fields agree with their theories. Finite datatype cardinality can also
affect consistency with other theories. For example, if `Color` has only
`red` and `blue` constructors, a value cannot make both corresponding testers
false. See the [datatype procedure](https://cvc4.github.io/publications/2015/RB15.pdf)
and the [SMT-LIB language reference](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf).

Standard combinations can include arrays of datatype values and datatype
fields of array sort. SMT-LIB 2.7 restricts a datatype being declared from
appearing below the top symbol of its constructor-field sort. In particular,
a recursive field `(Array Int Tree)` in the declaration of `Tree` is outside
that standard declaration profile. A separate nested-datatype extension with
recursion through a container would require an explicit syntax and semantics,
including how to detect cycles through array elements. Whether to provide such
an extension is outside the planned profile; ordinary supported array/DT
combinations are not excluded.
See the [nested-datatypes study](https://arxiv.org/abs/2606.30888).

## 7. Theory combination

The coordinator tracks **shared terms**: terms whose values or equalities are
relevant to more than one theory. It communicates required equalities,
disequalities, value constraints, and justified splits or refinements between
the Boolean search and theory procedures. The concrete equality engine and
combination algorithm must state which fragments they handle and why their
answers are sound. For each shared sort, identify the procedure responsible
for its value domain, how relevant equalities are decided or communicated, and
what establishes model compatibility. An explicit shared equality view is
recommended; whether equality reasoning itself is centralized or distributed
remains an implementation choice.

In particular, the coordinator must address:

- **Finite sorts:** respect the two Boolean values, the five rounding modes,
  fixed-width BV and FP domains, finite datatypes, and composite finite sorts.
  A 1-bit BV has only two values, so three pairwise-distinct 1-bit terms are
  inconsistent. Likewise, `(Array Bool Bool)` has at most four distinct values,
  so five pairwise-distinct arrays of that sort are inconsistent. Exact
  encodings, theory reasoning, or justified splits may discharge these
  obligations; a separate general cardinality solver is not required by this
  architecture.
- **UF with interpreted arguments or results:** if BV terms `x` and `y` are
  equal, `f(x)` and `f(y)` must agree even when the result is an integer,
  array, FP value, or datatype.
- **Arrays with other theories:** index equality, read values, nested arrays,
  and extensionality witnesses must agree with the procedures for their sorts.
- **Datatype fields:** constructor constraints and field values must remain
  consistent with the theory of each field, including arrays, BV, and Int.
- **BV/Int conversions:** the bridge enforces the interpretation and width of
  each conversion, not just equality between unrelated abstract values.

For a `w`-bit term `b`, its unsigned interpretation is in `[0, 2^w)`. For
integer `i`, the unsigned value of `((_ int_to_bv w) i)` is `i mod 2^w`;
signed BV-to-Int interpretation has a different range. These constraints are
semantic obligations when those input operators are accepted, not a complete
combination procedure. BV and Int may also interact through UF, arrays, or
datatypes without an explicit conversion. Translating BV problems to integer
arithmetic, or bounded integer problems to BV, is a separate optional solving
strategy whose correctness and performance require their own evaluation. See
the [SMT-LIB BV theory](https://smt-lib.org/theories-FixedSizeBitVectors.shtml).

Classic Nelson–Oppen combination has prerequisites that cannot be assumed for
finite BV sorts. A proposed combination procedure needs its own correctness
argument for the relevant sorts, shared terms, and conversion operators;
neither a method's name nor a general reference to politeness substitutes for
its actual conditions. See the [analysis of theory-combination properties](https://theory.stanford.edu/~barrett/pubs/TZN25.pdf).
When a supported algorithm is incomplete and cannot establish a joint result,
the solver returns `unknown`. A single global SAT answer is not justified
merely because each theory separately found a local candidate.

The following examples are semantic requirements for the combined engine;
they are not claims that Dandelion has implemented them:

| Constraints | Expected result | Reason |
| --- | --- | --- |
| `x = y` and `f(x) != f(y)`, where `x` and `y` are BV terms and `f` returns an integer | `unsat` | UF congruence must respect BV equality. |
| `i = -1` and `((_ int_to_bv 8) i) != #xff` | `unsat` | Conversion takes the integer residue modulo `256`. |
| Arrays indexed by one-bit BV are distinct but their reads agree at both `#b0` and `#b1` | `unsat` | Finite index cardinality and array extensionality must agree. |
| `l = cons(0, l)` for an inductive list datatype | `unsat` | An inductive datatype value cannot contain itself. |
| `fp.eq(+0, -0)` and core `distinct(+0, -0)` | `sat` | IEEE comparison and core equality have different FP semantics. |
| `x > 0`, `y > 0`, and `f(x) != f(y)` for integer `x`, `y` | `sat` | An accidental `x = y` in one local candidate is not an implied equality. |
| Five pairwise-distinct arrays of sort `(Array Bool Bool)` | `unsat` | Extensional arrays with a two-value index and two-value element sort admit at most four values. |
| A `Color` value has neither the `red` nor the `blue` tester, where those are its only constructors | `unsat` | Datatype constructors are exhaustive. |

## 8. Models and results

The result interface distinguishes `sat`, `unsat`, `unknown`, and input or
configuration errors. A `sat` answer requires a joint interpretation of the
**original** query, established by a model or a sound model-extension
argument, not merely rewritten terms or SAT variables. A full model need not
be materialized if it is not requested and the result's correctness is already
established. If the interface promises model queries after `sat`, it must
retain enough information to construct the required model. The model component
combines theory values and reverses or accounts for preprocessing
transformations.

Model construction covers Boolean and BV values, integers, FP values and
rounding modes, function and uninterpreted-sort interpretations, arrays, and
datatype values. Values shared by multiple theories must agree. When model
output or value queries are requested after `sat`, the emitted representation
must follow the supported SMT-LIB interface and denote a model of the
original assertions. Model export covers every original free declaration,
including unused symbols, using its canonical name with necessary quoting
and the semantic domain and range. The printer uses well-scoped aliases to
render exact sorts compactly. Elaboration-time aliases and parameterized
definitions introduce no independent model entries.

After a check, value requests arrive as subsequent commands and may contain
well-sorted terms absent from the assertions. Same-model abstract values
returned in an earlier response can be resolved in later value requests;
their identities and interpretations belong to that model, not to the user
declaration namespace. A model accompanying `unknown`, if available, is a
candidate and is not certified to satisfy the original assertions. If none is
available, model inspection reports that fact rather than fabricating one.

Phase 2 admits all written terms of a value request before emitting the
first response byte. Evaluation and rendering can stream after admission.
The model publishes newly rendered abstract names only after the full
response is written and flushed, independently of command publication.

The model component composes transformation-specific recovery operations in
reverse dependency order and coordinates completion of total functions and
arrays. For an operation underspecified by the standard, such as integer
division by zero or a selector applied to the wrong constructor, the
interpretation remains consistent for equal arguments; separate occurrences
cannot be assigned independently merely because the value is underspecified.
If the solver cannot establish a required joint model or reconstruct one under
its result contract, it must not emit an invalid model or an unjustified `sat`.

An independent evaluator should check supported ground assertions against a
constructed model. It must respect underspecified operations and consistent
function interpretations. Array equality requires the total interpretations to
be compared correctly; agreement on observed reads alone does not establish
extensional equality. Proofs, unsat cores, and interpolation have distinct
requirements; they are not assumed to follow automatically from model support.

## 9. Correctness and performance evaluation

Correctness validation spans the whole architecture:

- Test parsing and typing, including indexed operators, declarations, datatype
  structure, and clear handling of unsupported input.
- Exhaustively compare small-width BV operations and encodings with an
  independent evaluator; test FP rounding and special values, integer
  arithmetic boundaries, array extensionality, UF congruence, and DT cycles.
- Test combinations explicitly: finite BV cardinality, UF over BV/Int, arrays
  with interpreted indices and values, datatype fields, and BV/Int conversions.
- Include Boolean arguments to UF, finite array cardinality, distinct NaN
  encodings, rounding-mode domain constraints, datatype tester exhaustiveness,
  and compatible global models despite conflicting local candidate choices.
- Exercise SAT preprocessing and theory-visible variable restoration or
  protection, plus the composition of model-recovery transformations.
- Differential-test against independent solvers where available; investigate
  disagreements instead of treating a majority vote as a proof.
- Validate satisfying models against the original assertions. Test that
  incomplete NIA reasoning and resource limits can return `unknown` without
  generating an unjustified answer.

Performance work measures parsing, rewriting, SAT encoding/search, theory
checks, refinement counts, model construction, end-to-end runtime, and peak
memory as appropriate. Group results by benchmark source family and actual
formula features where known, and retain `unknown`, timeout, unsupported, and
incorrect outcomes separately. Record workload, versions, configuration, and
resource limits. Use only authorized local datasets as read-only inputs; keep
generated output elsewhere. No performance advantage is claimed by this
proposal.

## 10. Decisions requiring further evaluation

The overall responsibilities above are proposed for the full scope. Remaining
**semantic and interface decisions** include:

1. Validating broader workloads against the [frontend profile](frontend/logic-profile.md).
   The [current evidence](frontend/implementation.md) covers a bounded selection.
2. A sound combination procedure and completeness conditions for each
   supported mixture, including shared finite sorts and interpreted
   conversions.
3. IDL, LIA, and NIA algorithms, their explanation interfaces, and the
   completeness of each supported fragment.
4. Model ownership details and the contracts of model-conversion operations;
   node and state ownership are specified in [node design](node/design.md).
Remaining **implementation and performance choices** include the SAT backend
and integration level, equality-engine organization, FP encoding dependency,
future term allocation and sharing optimizations, rewriting policies, and
circuit representation. The implemented first-version node system is described in
[the node plan](node/implementation.md), without a performance claim.
Optional proof, unsat-core, and interpolation interfaces require separate
design; measurements do not substitute for the semantic obligations above.

The supporting [solver architecture research](solver-architecture-research.md)
records the published designs and evidence used to form this proposal. The
build-system baseline is [docs/cmake.md](cmake.md).
