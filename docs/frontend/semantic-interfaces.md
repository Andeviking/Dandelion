# Frontend Semantic and Protocol Interfaces

**Status: implemented frontend interfaces with pending contract refinements.**
The public C++23 module `dandelion.frontend` builds independently of the
production solver and SAT backend. See the [Phase 2 test design](../testing/frontend.md#phase-2-test-design)
for pending cases. The [representation target](design.md#resources-and-representation)
uses 64-bit structural IDs, indices and counts for nodes, edges and
declarations, including arities. BV width and the FP components `e` and
`s` remain `uint32_t`, with the same checked FP total width. No
discretionary quota applies.

## Command processing and publication

Separate the one-command syntax boundary from semantic publication so a
model-dependent command can be resolved against the actual result:

```text
Session::NextSyntax() -> CommandHeader | EOF | Diagnostic
Session::Apply(const ModelOwner* = nullptr) -> TypedCommand | Diagnostic
Engine(const QueryContext&, SessionOptions) -> CheckResult | Diagnostic
Run(Session&, const Engine&, const ResponseSink&) -> RunResult
```

`Session::NextSyntax` retains the current command's flat syntax records,
source ranges and token spellings as pending session state. The public
`CommandHeader` contains `kind` and `where` and can be copied independently
of the scratch. The syntax stage validates token and command shape before
publication. A short read supplies available bytes. Only a zero-byte
successful read signals EOF. The syntax stage returns as soon as a full
command is available. `exit` is
returned after its closing delimiter without scanning a suffix.
A `Reader` callback exception becomes a `kInputIo` diagnostic at the
current input position, including during an unfinished command.
`NextSyntax` terminates the session's syntax processing, preserving the
committed prefix. `Session::Apply` consumes the pending command, including
on diagnosed semantic failure. A typed command or committed state that
survives that point owns or interns what it needs.

`Apply` resolves names, scopes, sorts and the active
[logic profile](logic-profile.md), and uses checked NodeManager factories.
It owns a command-local transaction containing pending dictionary entries,
typed roots, occurrence records and option/phase changes. Check and allocate
before publishing. Success commits the frontend-visible effects of that
command, then returns a typed descriptor. Failure restores frontend-visible
state and destroys scratch. As in the existing node contract, independent node
factory calls can consume internal IDs or retain capacity even if a later
part of the command fails; this does not publish a source declaration. A
datatype builder's own documented commit guarantee remains separate.
For ordinary builtin errors carrying an operand or parameter index, the
frontend preserves the `NodeError` and uses the offending argument's
or index token's `SourceSpan`. If no corresponding written argument or
parameter is identified, use the application span. A zero divisor in the
concrete rational rank has its own unsupported diagnostic. In
`(/ 1 (/ 0 3))`, point to `(/ 0 3)`, the expression that supplies the
outer divisor.

The driver borrows the engine and response sink for `Run`. It calls
`NextSyntax`, checks command state and runtime preconditions, calls
`Apply`, and dispatches the typed command. Eligible `get-value` commands
use the model's bindings during elaboration. For a `get-value` with no available model, syntax and
command shape are checked, then the driver reports model unavailability
before attempting model-dependent name resolution. A solver-free consumer can
apply commands with no model-bound names; when a name beginning with `@`
could require a model, report a model-dependent/inconclusive inspection
instead of declaring the full script valid or treating the name as user UF.

Use the session's committed option state for the next response. Setting
`:print-success true` enables the success response for that same command;
setting it false suppresses its own success response. `exit` emits
`success` only if the current option requires it, then terminates. EOF at a
command boundary is normal. Errors report an SMT-LIB `(error ...)` response
with an escaped string, and the driver follows the `immediate-exit` error
behavior. Unsupported query-affecting input emits `unsupported` and
terminates without silently dropping a constraint. Unsupported harmless
information/option queries may emit `unsupported` and continue. Existing
responses and the committed prefix remain observable after a later failure.
A response may be written in chunks without buffering it in full. Before
the first byte of a response is committed to the sink, a model, binding,
formatting or resource failure may instead emit one complete error response
if the sink remains usable. After any response byte is committed, **any**
failure during that response, including evaluation, allocation, binding,
formatting, a short write or flush, terminates the stream without appending
an error to the partial response. Each complete response is flushed once
when the sink provides a flush callback. A successful command with no
response emits no bytes and requires no flush. Earlier complete responses
remain valid. Model-local name publication follows the same completion
boundary.
Normal `exit` or command-boundary EOF returns process status 0; a terminal
invalid-input, unsupported-input or recoverable execution error returns
status 1. A fatal process or I/O failure may prevent a normal status or error
response. Harmless unsupported queries do not terminate the process.

For `get-option`, return actual values for supported options. For a
standard option whose setting is unsupported but whose default is specified,
return that default; for an unsupported nonstandard option return
`unsupported`. The explicit standard-option fallback values are:

| Unsupported option setting | `get-option` value |
| --- | --- |
| `:random-seed`, `:reproducible-resource-limit` | `0` |
| `:regular-output-channel`, `:diagnostic-output-channel` | `"stdout"`, `"stderr"`, respectively |
| `:verbosity` | `0`, a Dandelion default because the standard specifies none |

In the before-logic state, accept `false` for each of `:interactive-mode`,
`:produce-assertions`, `:produce-assignments`, `:produce-proofs`,
`:produce-unsat-assumptions`, and `:produce-unsat-cores`. Each is fixed at
`false`, which `get-option` reports. The successful setting produces the
usual `:print-success` response. Setting any of them to `true` is unsupported
and terminates. The corresponding inspection commands remain unsupported.
Reject malformed Boolean values and settings after the allowed state as errors;
do not silently ignore an unknown or enabling option.

The solver does not implement output redirection in this profile even though
SMT-LIB requires it for full conformance. An unsupported `set-option` emits
`unsupported` and terminates; an unsupported `get-option` is a harmless
query and may continue. This includes the reproducible resource-limit
setting: it does not establish a project quota.

`get-info` returns `(:name "Dandelion")`, the actual configured version or
`(:version "development")`, configured authors or `(:authors "")`, and
`(:error-behavior immediate-exit)`. After `unknown`, return
`(:reason-unknown r)` using the actual reason or the generic symbol
`unknown`; requesting it in another state is an error. `echo` returns its
original string token, including quotes and doubled quotes. These responses
do not enable the String theory.
[SMT-LIB 2.7 §§4.1.2, 4.1.7–4.1.8, 4.2.1 and 4.2.8](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

## Query and definition ownership

A frontend session owns one NodeManager state and a mutable query builder
until `check-sat`. Its owner stores the active profile, original ordered
assertion roots, original user-declared sort and function signature,
datatype-group owners, definition table and source metadata needed for
diagnostics and output. The solver-visible original signature includes unused
user declarations. Definitions and `:named` aliases are recorded separately
from free user declarations. Preserve the original source name and rank
syntax of each user function declaration, including sort aliases, in owned
records for model output after reader scratch or the session facade is gone.

At the only `check-sat`, seal the builder into an immutable owning
`QueryContext` held by a `std::shared_ptr<const QueryContext>`. It holds its
own `ManagerRef`, nodes, declarations, datatype groups and definition
records. The synchronous `Engine` callback receives a borrowed
`const QueryContext&` and a copy of `SessionOptions`. The driver creates a
`ModelOwner` with the *same owning context reference* and the returned
provider before exposing a model or response. A
returned state cannot escape independently while retaining only borrowed
query data. `ModelOwner::Create` consumes the provider and destroys it
exactly once on failure, while any supplied query is still alive. The
`ModelOwner` owns its model-local bindings and retains the context. The
driver and later inspection sessions created with
`Session::ForModelText` or `Session::ForModelReader` share that owner,
including the same bindings and names. In a model-bound session,
`Apply()` uses the bound owner. An explicit pointer to that same owner is
also valid. Passing a different owner is a `kModelFailure` and consumes
the pending command before any model-dependent name resolution. The
original owner remains bound even if the two owners share the sealed
query and NodeManager. Retaining a model keeps it alive beyond the
original session or source storage.

Sharing this context is a lifetime mechanism, not permission to access a
NodeManager state concurrently. The existing one-thread-at-a-time rule still
applies. The session keeps its name lookup information available for later
value requests, but assertions, declarations and definitions do not change
after the check.

Each nonrecursive function definition has this logical record:

```text
DefinedFunction {
  Decl internal_decl;              // fresh, same NodeManager state
  DeclInstance internal_instance;  // exact domain and range
  vector<Decl> formals;            // distinct kFormal identities
  Node body;                       // typed in this definition's scope
  source name and span;
}
DefinitionTable: DeclId -> DefinedFunction, in declaration order
```

The frontend obtains the declaration with `FreshFunction`, its ordinary
instance with `InstantiateDeclaration` and no type arguments, creates distinct
formals with `MakeFormal`, and constructs every call with `MakeApply`. A
zero-argument definition is a zero-argument application.
Only earlier definitions and previously visible declarations can occur in a
definition body, so the definition graph is acyclic. Caller names cannot
capture the resolved identities in the body. Formals may appear in owned
definition bodies and their local templates; they must not occur free in an
assertion root or a model-query root.

A `kApply` of an internal declaration whose ID occurs in the definition
table **means the associated definition**, never an uninterpreted function.
Consequently an independently retained Node containing such a call is only a
structural term; any solver, evaluator, printer or comparator that needs its
meaning must also retain the matching context/table. A same-state ordinary
internal symbol not in the table remains subject to its own solver contract.
Do not infer definition identity from a printable name or from the
`kInternalFunction` category alone.

A consumer can instantiate one definition by simultaneously substituting its
formals with actual arguments under the caller's environment, rebuilding only
reached nodes through checked factories and memoizing in that fixed
environment. Never descend into an actual while replacing the formal. Carry
declaration instances, indices and qualifiers through rebuilding. That one
instantiation can still contain defined calls from the body or the actuals.
A consumer that requires a DAG free of defined calls must continue resolving
those calls with the matching definition table and environments; substituting
only the outer call is not sufficient. Expansion of used compressed
definitions can be exponential. The frontend does not require it, and solver
processing may preserve defined calls as long as semantics and model recovery
are exact.

For the initial concrete representation, [cvc5's definition registration](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/smt/solver_engine.cpp#L549)
and [Bitwuzla's lambda elimination](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/preprocess/pass/elim_lambda.cpp)
are evidence for retaining definitions beyond parsing; neither proves a
Dandelion performance result.

## Frontend admission and solver fragment selection

The frontend validates each written definition body once and checks every
application's argument count and sorts. It records the call with its declaration
identity and ordered arguments; it does not enter a definition body when
constructing that call. The selected [input profile](logic-profile.md) does
not require an assertion-shape check, virtual expansion, a persistent shape
DAG or a fragment classifier in the frontend. Command-local source records
remain necessary for syntax, scope, `:named` closedness and diagnostics.

For a family `f0(x)=x` and `fn(x)=f(n-1)(f(n-1)(x))`, including under
`QF_LIA`, the frontend retains the definitions and the final call as written.
For this specific family, declaration, node and edge construction is
proportional to the written definitions, apart from ordinary allocation and
hashing costs. Do not extrapolate this result to arbitrary source input,
sort aliases, numeric literals, or later solver expansion. Resource and
representation failures remain explicit; there is no artificial work quota.

A solver consumes the `QueryContext`, including its definitions. The selected
logic supplies the active input signature and a hint, not a guarantee about
actual formula shape. A specialized solver may be selected only after a sound
check of its preconditions on the query's actual semantics, including reached
calls and arguments. If those preconditions cannot be established, use a
sound general path or report `unknown` when the applicable solving contract
permits it. The complete-decision target for the specified pure IDL/LIA
fragments remains a solver responsibility: a conservative classification must
not by itself silently discard that target. The general core, completeness
mechanism and any efficient solver-side definition summary remain subject to
the solver architecture, not prerequisites for parsing.

For example, `(* 2 (+ x 1))` under `QF_LIA` is parsed as written and can be
routed to a sound linear procedure after a justified normalization. `(* x y)`
under that label is also an accepted Ints term, but the label cannot justify
using a complete-LIA assumption. A solver may choose an NIA-capable path and
return `unknown` if it cannot decide it. The frontend does not claim the
solver can decide either input.

## Model-local values and inspection

An eligible result retains one `ModelOwner` associated with the sealed
query. For `sat` it is a joint model of the original assertions. For
`unknown`, an optional inspectable candidate is an interpretation of the
active signature but is not certified to satisfy those assertions; if no
such model exists, inspection reports model unavailability. An `unsat`
result has no model inspection. This is an explicitly narrower `unknown`
model policy than full SMT-LIB model inspection.

A model provider exposes an opaque owning `ValueRef` and its concrete Sort.
`ModelOwner::MakeValue` attaches an owned key, model identity, and optional
provider payload. `ModelProvider::Describe` supplies a typed scalar Node or
a datatype constructor and field handles. Abstract values use their keys.
`ModelOwner::MakeProxy` creates a model-owned proxy and `LookupProxy`
resolves it by declaration identity.
For any two values `a` and `b` of the **same sort in the same model**, their
model-local canonical keys satisfy the full equivalence:

```text
key(a) == key(b)  if and only if  a and b are semantically equal.
```

Keys are stable for the model's inspection lifetime; different models and
sorts have separate key spaces. A key is an equality-bearing semantic
identity, not a raw hash. A hash table may have collisions, which must be
resolved by key equality rather than merging entries. This rule uses the
sort's actual semantics: one FP NaN per format, distinct signed zeros under
Core equality, datatype values, and extensional array equality. UF and array
default interpretations and underspecified operations are total and
consistent for equal semantic arguments. If the model cannot establish the
required equality or evaluation, it reports a model failure, not a guessed
value or invented identity. This is a solver model obligation, not functionality
supplied by the current NodeManager.

Choose one spelling per semantic value in `get-value` result-value positions
throughout an inspection session. Echoed input terms and expressions inside
model definitions are not result-value spellings. Print Bool as `true`/`false`,
Int as the canonical numeral or
`(- n)` for a strictly positive numeral, and BV as a width-exact binary
literal. Print a rounding mode with its short standard name. Print FP NaN
and infinities using their standard indexed values; print every finite FP
value, including signed zero, as `(fp sign exponent significand)` using
exact-width binary fields. Construct DT values recursively from their
constructors and these canonical field values, with a result qualifier where
the constructor is ambiguous. Use a qualified abstract value for each array
or uninterpreted-sort value, including those inside DT fields. Never print a
concrete array for one request and an abstract value for an equal array in
another. Equality for array keys is extensional, not pointer or store-list
equality. This is a selected output policy; it avoids duplicate spellings
for equal values and does not limit the model's internal representation.
In the restricted QF_UFIDL input signature, a negative Int is printed as
`(- n)`. A later `get-value` may read this canonical value through the
[value-input exception](logic-profile.md#qf_ufidl-model-values), without
enabling general unary minus in declarations or assertions.

Each `ModelOwner` owns one `ModelBindings` registry, reused by all inspection
sessions for that model:

```text
(SortId, canonical model value key) -> {name, proxy Node, ValueRef}
name -> the same record
```

When printing an abstract value, reuse its published spelling or stage a
new entry for this response and emit `(as @valueN Sort)`. Repeated values
within that response reuse the same staged entry. Generate the name from
a model-local monotonic ordinal over published names. User declarations
cannot use the reserved `@` prefix. For a newly exposed
value, construct an internal zero-argument proxy with the same manager and
sort, using the current NodeManager's fresh-constant facility. The registry
owns the proxy and `ValueRef` after publication. Publish the staged entries
as a group only after the complete response has been written and any sink
flush succeeds. Prepare fallible registry work before this publication so
commit cannot fail after successful output. On response failure, discard
all staged entries and retain the earlier registry. The name is admitted
only during a `get-value` on this model, and only if it is already published.
Reject an unknown name, wrong sort or foreign model handle. A proxy is a model-bound
value, not a free variable and not a new assertion or user declaration.
Creating model-local proxies can mutate the shared NodeManager's internal
stores; it does not change the sealed query's user signature, assertions or
definitions. Manager access remains subject to the single-thread rule.
Evaluation dispatch distinguishes definition-table applications, model proxy
declarations and ordinary user declarations by their disjoint recorded
identities; only the last use a free function interpretation. Other internal
declarations follow their owning solver contract.

The textual name belongs to the current model namespace. The same spelling
could independently be generated in another model; if it exists there it
denotes that other model's value. Cross-model `ValueRef` or proxy handles
must be rejected by the owning model identity, including when both models
share a NodeManager; text alone cannot prove its
origin. This distinction is especially relevant to separate programmatic
sessions, even though a single CLI session checks only once.

For each post-check `get-value`:

1. Parse a complete command, verify the option and result state, and bind only
   the current model's exposed names while resolving and typing its terms.
2. Process terms in request order, evaluating each in the same model and
   rendering its pair. This includes terms absent from the assertions,
   defined-function calls and model-value proxies. Core and theory operations
   use the model's total, consistent interpretation. Processing may stream;
   it need not evaluate or buffer the complete response before output.
3. Render `((input-term value) ...)`. Preserve ordinary input token spelling
   with trivia normalized. Render model abstract references as
   `(as @valueN Sort)` in both term and value positions; an already qualified
   reference is printed once, not wrapped in a second `as`. Do not rewrite
   a matching character sequence inside an unrelated quoted token.
   Commit any newly printed names at the response completion boundary.
   If the response fails, even a later inspection session on the same owner
   can only resolve names from earlier completed responses.

`get-model` uses the same registry and covers all and only user-declared
function/constant symbols, including unused ones, with their original ranks.
The model provider supplies a typed export whose interpretation agrees with
the same total model used for `get-value`. The frontend does not require the
provider's internal function representation to be a finite table.

The initial concrete export form is an ordered sequence of `define-fun`
records. Each record contains the original declaration identity, ordered
typed formal parameters and a typed body DAG. The export also owns a map from
each model-only proxy declaration used in those bodies to its owning
`ValueRef`. The value belongs to this model; the declaration belongs to its
NodeManager and has the same sort. One proxy cannot denote two values. This
mapping is required even when `get-model` is the first inspection. The printer
uses the same response-local staging and publication rules as `get-value`.
Multiple proxies denoting the same semantic value share that
name; distinct values do not. Datatype fields can contain such proxies.

Bodies may use admitted theory operations, datatype symbols, formal parameters,
mapped model-bound abstract values, and user functions defined by an earlier
record. Order records by these dependencies; no forward user-function
references are emitted. Source sort aliases in original declared ranks remain
available from the owning context. For generated sort text, `SortText`
uses available, well-scoped alias spellings that resolve to the exact
concrete sort in the sealed signature. Apply this to abstract values,
datatype result qualifiers and constant-array ascriptions. Respect alias
parameter order and sort identity. In particular, a shared sort represented
compactly by an available alias must be rendered without walking or emitting
its exponentially unfolded tree. Work follows the reachable sort
representation and emitted spelling. Explicit syntax that inherently
repeats a sort can take work proportional to its output size. Allocate
printable formal and `let` names
by declaration identity, avoiding capture of referenced user, theory,
datatype and abstract-value names and of other binders in scope. Name hints
alone never determine a binding. Preserve substantial DAG sharing in printed
model bodies using capture-safe, dependency-ordered nested `let` bindings
when a tree expansion would repeat shared subterms. Each `let` binding group
has simultaneous SMT-LIB scope; dependent bindings must be nested.
The provider must justify the definition for **all** argument values in the
model, not merely observed applications. For example, `f(x)=x` is exported
with body `x`; its interpretation cannot be replaced by a finite default
table after the model is exposed.

Rendering a concrete-rational FP conversion must also preserve its resolved
meaning under the *current* output signature. A user binding for `-` or `/`
may have been declared after the original definition, so blindly reprinting
the rational source grammar could now denote a user function. If that grammar
is no longer unambiguous, bind the rounding-mode term once and emit a typed
`ite` over the five rounding-mode values, using the exactly rounded FP literal
for each branch. This expression refers to neither a Real term nor the
shadowing user symbol. Compute each branch by the exact FP conversion contract;
an unavailable computation or actual resource failure follows the model and
response failure rules, never a guessed value. This rendering does not change
the original typed node or the model's interpretation.

A provider that uses finite tuples and a default may construct a body with
nested `ite` and equality tests. Equal semantic tuples must agree, and the
default must be inhabited and correctly typed. A zero-argument symbol has a
value body. Other symbolic providers can supply a body directly. The selected
initial export does not represent cyclic dependencies between user-function
definitions; if a provider cannot export its chosen model in this form, it
must select and justify an exportable model before exposing it, or report
model capability failure. Recursive model-output forms require a separate
design, not an invented finite table.

In `QF_UFIDL`, print a negative Int *inside a model definition body* as
binary `(- 0 n)` with positive numeral `n`, which is well-sorted under `J`.
This is an expression for the value, including in tuple tests and defaults;
the canonical `get-value` result is still `(- n)` and is accepted in a later
value query. Model-definition expressions are not additional `get-value`
value spellings. Keep these rules separate from assertion admission.

Qualified array abstract values hide the array representation but the same
model must evaluate later `select` and `store` requests on them consistently.
A model may be internally partial only when it can extend that representation
consistently for future requests, including an admitted ground sort first
instantiated by a later query. This never permits changing an exposed value or
earlier function interpretation. The provider retains recovery information for
the original query and its definitions.
When model production is enabled, `sat` requires an established model of the
original query and the model/inspection capability promised by this profile.
If reasoning or model construction remains incomplete, the engine may return
`unknown` with its actual reason; it must not emit a speculative `sat`. A
returned `sat` lacking its required model is a broken provider contract and
an execution error. Actual allocation or representation failure on a later
value request is still possible; report it according to the response boundary
above without changing the already emitted result. Capability is a semantic
and interface guarantee, not a guarantee of unlimited future resources.

The standard permits abstract-value readback on the same model.
[cvc5's model-query scope](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/parser_state.cpp#L693)
is one implementation reference, not evidence that every solver supports
every abstract-value sort.

## Ownership and error examples

- In `define-fun f ((x Int)) Int (+ g x)`, the stored body retains the
  declared global `g`; a later local `let` named `g` changes the call
  argument only, not the body identity. The context keeps the body and its
  ManagerRef alive after the source buffer and session facade are destroyed.
- A model request `(+ x 2)` after QF_IDL checking uses the signature rule,
  not IDL's assertion atom grammar. If the model assigns `x=1`, its result
  is 3. The request cannot retroactively add an assertion.
- A printed `@value0` is valid only in an eligible query with a registry
  containing that name and matching sort. An ordinary assertion, a failed
  model request, or a parser-only run with no registry cannot manufacture it.
- A malformed command after `sat` produces an error after the already sent
  `sat`. A malformed suffix after `exit` is never parsed. An output
  failure leaves no valid continuation of the session.

[Frontend tests](../testing/frontend.md) exercise these interfaces with
controlled providers and independent finite oracles. A production provider
must establish its model semantics and correctness for the original query.

