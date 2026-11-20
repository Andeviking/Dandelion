# SMT-LIB Frontend Design

**Status.** Phase 2 uses fused command reading.
The [implementation record](implementation.md) describes the implementation
and its verification. The [Phase 2 test design](../testing/frontend.md#phase-2-test-design)
defines acceptance. This document specifies one user query over
quantifier-free Core, BV, FP and rounding modes, Int, extensional arrays,
UF and inductive datatypes,
including their supported combinations. General Real and String terms,
user quantifiers, higher-order
input, recursive function definitions, and user-level incremental sessions
are outside the profile. Metadata strings and the closed rational argument of
`to_fp` are in scope.

The language baseline is [SMT-LIB 2.7, 2026-09-09](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf),
with compatible 2.6 forms and `benchmark-2025` as a compatibility target.
Theories and logic revisions are pinned independently in
[node operators](../node/operators.md). The frontend uses checked nodes, exact payload storage and datatype
builders. It accepts a solver callback and a model provider interface.
Production solving and model implementation remain separate work.

The [Phase 2 design](#phase-2-fused-command-reading) specifies the active
command path. The compact syntax section records the Phase 1 representation.

## Architecture and interfaces

Parse, elaborate and execute **one command at a time**. A complete command is
checked before its effects become visible. The driver invokes the solver at
`check-sat` and emits its response without waiting for the rest of the input.
This is still a single-query interface: it does not add `push`, `pop`, or a
second `check-sat`.

The following components share the command boundary.

```text
Reader -> scanner -> grammar frames and semantic reductions -> NodeManager
                               |
                       committed TypedCommand -> Run -> solver and sink
```

A parser-only consumer uses the same reader to inspect typed commands and
the query. A later model-generated value requires the corresponding
model. The independent `SyntaxValidator` serves grammar-only consumers
through the same scanner and frames.

The reader can return available bytes, a short read, EOF, or an I/O error; it
must not wait for its entire buffer to fill before returning available input.
Consequently a complete `check-sat` is processed even when the stream remains
open. An exception from invoking a `Reader` becomes `kInputIo` at the
current input position, terminating syntax processing and preserving the
committed prefix. An `exit` terminates processing without tokenizing or
validating the remaining input. Already buffered bytes may be discarded.

This boundary follows the observed command-at-a-time behavior of
[cvc5](https://cvc5.github.io/docs/cvc5-1.4.2/examples/parser_sym_manager.html),
[Bitwuzla](https://bitwuzla.github.io/docs/cpp/classes/parser.html), and Z3.
Their acceptance of a particular input is evidence, not the project contract.

### Entry points and ownership

The C++23 module `dandelion.frontend` exposes these responsibilities.

- The solver-independent frontend session owns its reader state, name
  environments, optional source history and the typed query builder with its
  NodeManager reference. `ReadCommand()` scans and types one command,
  publishes its effects atomically and returns the committed descriptor.
  `Session::Run()` checks result-dependent prerequisites and dispatches
  responses. The independent `SyntaxValidator` shares the lexer and
  grammar frames and returns command headers.
- At `check-sat`, seal the query into an owning `QueryContext` with assertion
  roots, profile, original signature, parameterized definition templates,
  datatype groups, options and manager. The synchronous engine borrows a `const QueryContext&` for
  the call. The typed check command, session and any retained model share
  context ownership. Neither the query nor a model
  may outlive the semantic information required to interpret its nodes.
- Nullary definitions and `:named` introduce direct-body aliases.
  Parameterized definitions own a declaration, formals and body and use
  saturated calls. Only original free declarations receive `get-model`
  entries. See [binding](#environments-commands-and-binding).
- A source span has a source ID, half-open byte offsets and start/end line and
  byte-column coordinates. Successful occurrence records are session-owned
  and opt-in. Keep transient locations for diagnostics and each value
  request's normalized input rendering and span through the response.
  Reader windows and command scratch are borrowed within their lifetimes.
- A retained model owns the manager state, original-query and recovery data,
  declaration and definition interpretations, and any model values it needs.
  After a successful check, later requests can refer to terms absent from the
  assertions. Model-generated values use a typed, model-local namespace made
  available to the parser only for applicable model queries. They are not
  user declarations and cannot be used as fresh unconstrained values.

The concrete command, definition, model and lifetime contracts
are specified in [semantic interfaces](semantic-interfaces.md).

No discretionary input-size, token-size, nesting, work, numeric-intermediate
or response quota is part of these interfaces. Actual allocation failures,
checked representation limits and I/O failures remain distinct from invalid
syntax. See [resources and representation](#resources-and-representation).

### Command and execution states

The session has **before logic**, **building**, **after check**, and
**terminated** states. It permits at most one `check-sat`; a standalone
`exit` or EOF before a check terminates without a solver result. A benchmark
runner may separately require a complete single-query input.

| Form | Admission and effect |
| --- | --- |
| `set-logic L` | Once before logic. Resolve the case-sensitive name against the [registry](#logic-registry-and-conformance). `ALL` selects the supported profile. A logic-dependent command before selection or a repeated selection is an error. |
| `set-info :key [value]` | Before logic, building or after check. Validate attributes using the [metadata rules](logic-profile.md#metadata). Category accepts strings by default, including `"check"`, with a strict opt-in enumeration. Capture ordered metadata, including repetitions, only when requested. Status is advisory. |
| `set-option :print-success true/false` | Apply immediately in any live state; its new value controls this command's acknowledgement. Default false. |
| `set-option :produce-models true/false`, `:global-declarations true/false` | Supported in before-logic state, default false. With one permanent declaration level, both global-declaration settings have the same scope effect. An unsupported option setting terminates after `unsupported`. |
| Fixed-false standard options | Before logic, accept `false` for `:interactive-mode`, `:produce-assertions`, `:produce-assignments`, `:produce-proofs`, `:produce-unsat-assumptions`, and `:produce-unsat-cores`. Their value remains false; setting one to true is unsupported. |
| `get-option :key` | Return the current supported value; for an unsupported standard option return its specified default or a documented solver default when none is specified. An unsupported nonstandard option returns `unsupported`. |
| `get-info :key` | Support `:name`, `:version`, `:authors`, and `:error-behavior` from project metadata. After `unknown`, return the known reason or the generic `unknown` reason; otherwise `:reason-unknown` is unavailable. Other unsupported keys return `unsupported`. |
| `echo string` | Return the SMT-LIB string as a protocol response; this does not enable the String theory. |
| `declare-sort`, `declare-fun`, `declare-const` | Building only. Validate names, arity, rank, source sorts and profile before publication. The two zero-domain function spellings have the same effect. |
| `define-sort`, `define-fun`, `define-const` | Building only. Validate fresh names, parameters, rank, scopes and body before publishing the definition. Definitions are nonrecursive; `define-const` is the zero-parameter form. |
| `declare-datatype`, `declare-datatypes` | Building only in a DT profile. Validate and commit the group atomically. |
| `assert t` | Building only. Require a closed Bool root in the selected assertion profile and append it in input order, including duplicates. |
| `check-sat` | Building only, once. Freeze the typed query, release textual lookup if model production is disabled, and let the driver call the solver and emit its result immediately. A second check, declaration, definition or assertion after it is an invalid state. |
| `get-model`, `get-value (t...)` | After check and with model production enabled. A value request has a nonempty list of well-sorted, closed, monomorphic, quantifier-free terms in the active signature and supported project profile. A canonical negative Int in a QF_UFIDL value request uses the documented value-input exception. Same-model abstract values returned earlier may be resolved in this model-query scope. The profile excludes `:named` on value requests. |
| `exit` and EOF | `exit` with no arguments emits `success` if enabled and terminates immediately in any state; no later token is processed. EOF at a command boundary terminates normally. EOF inside a command is an input error. |
| Excluded commands | Report `check-sat-assuming`, `push`, `pop`, `reset`, `reset-assertions`, recursive definitions, global sort parameters and proof/core/assignment commands as unsupported. Malformed forms are syntax errors. |

The driver processes a valid prefix in order:

1. Apply each supported command and emit its response when required. A
   supported mutation is committed only after its own checks succeed. An
   invalid command emits an error and terminates with nonzero status. An
   unsupported query-affecting command terminates rather than silently
   dropping semantics and later returning a result. A harmless unsupported
   information/option query may respond `unsupported` and continue.
2. At `check-sat`, call the engine once with the actual query and available
   definitions. The result is `sat`, `unsat`, `unknown` with an optional
   reason and candidate model, or an execution error. Only a sound answer for
   the original assertions can produce `sat` or `unsat`. If model production
   is enabled, a `sat` response requires an owned joint model that supports
   the promised inspection contract; a missing required model is an engine
   contract failure, not a fabricated result.
3. On a later `get-value`, resolve and type the request then evaluate it in
   the retained model. All requests for that result use compatible total
   interpretations of UF, arrays, datatype values and underspecified
   operations. The model maps committed abstract-value names and sorts
   back to the same semantic values. Names introduced by either model
   response are published together after the complete response is written
   and any sink flush succeeds. An inspection session opened with
   `ForModelText` or `ForModelReader` uses the owner it was opened with.
   `ReadCommand` rejects a different owner even if it shares the same query.
   `get-model` covers the current user-declared function and
   constant signature, including unused declarations.
4. After `unsat`, model inspection is an invalid state. After `unknown`,
   an available candidate model may be inspected but is not certified to
   satisfy the original assertions; if no usable candidate is available,
   report model unavailability. This subset does not claim complete SMT-LIB
   `unknown`-model support. Earlier responses remain valid and visible when
   a later command, model access or output write fails.

### Diagnostics and authority

| Outcome | Responsible boundary and example |
| --- | --- |
| Malformed input | Scanner or command parser: incomplete quote, missing delimiter or wrong token class. Report source location when available. |
| Invalid state, name or sort | Resolver or command state: repeated logic, second check, duplicate name, BV1 assertion, wrong rank, or a datatype with no finite base. |
| Unsupported feature | Recognized but excluded command, theory, rank or logic slice. A well-formed feature must not silently disappear from the query. |
| I/O, allocation or representation failure | Reader, factory or output boundary reports an actual failure; it is not an invalid mathematical value or a justified solver answer. |
| Engine result | `unknown` is an inconclusive result for an accepted query. Parser, configuration, output and model access failures are distinct. |

Resolve in source order. A failed command publishes none of its own effects;
earlier committed commands and emitted responses are not rolled back.
After lexical and grammar checks, resolve local bindings, declared symbols
and active theory names by their identities. A resolved symbol with the wrong
arity, sort or index is invalid; never reinterpret it as another symbol. If
no declaration resolves the name, a recognized inactive or explicitly
excluded builtin in a well-formed builtin form is unsupported; an unknown
unbound name is invalid. A fresh declaration prohibited by the profile is
unsupported. Invalid indices remain invalid even for a recognized operator.
For example, `08` is not a numeral token, `-1` is a symbol and is not a
numeral index, `(_ BitVec 0)` is invalid, and the mathematically valid width
`4294967296` exceeds the selected representation. An explicitly declared UF
named `div` under QF_UFIDL is resolved before considering the inactive Ints
operator; an undeclared use of the latter is unsupported.

For an ordinary builtin factory error, map an attributed `NodeError`
`operand_index` to the half-open `SourceSpan` of that written argument and
`parameter_index` to the span of that written index. Preserve the original
`NodeError`. Use the full application span when the error identifies no
corresponding source argument or parameter, such as a call-wide arity error.

Internal invariant violations are implementation errors. Use the standard's
[command and model rules](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)
without claiming full SMT-LIB compliance beyond this profile.

## Logic registry and conformance

[Logic and input profiles](logic-profile.md) is the authoritative registry for
this frontend design. It specifies each exact accepted label, its enabled
signature, explicit compatibility extensions and Real-free projections.
Unknown labels are unsupported rather than inferred from their spelling.
`ALL` admits all project-supported first-order, quantifier-free combinations.

Catalog restrictions and local competition-label compatibility choices are
separate. The 2025 competition result pages establish which labels occur,
not that every input under those labels is accepted. In particular, general
Real and String solving remain outside scope. Supported FP ranks can accept the
specified closed rational conversion operand without admitting Real terms.

Validate metadata syntax and recognized value forms from the pending
command's token views. Render and retain owned attribute text only for source
capture. The strict category check uses the same token views.

Check signature and source-sort gates throughout elaboration, including
unused bodies and arguments. Do not expand definitions or reject supported
integer operations to enforce the declared label's narrower assertion shape.
The solver must establish the actual preconditions before selecting a
fragment-specific path. See [frontend admission and solver selection](semantic-interfaces.md#frontend-admission-and-solver-fragment-selection).

## Input and elaboration

### Scanner and command syntax

The handwritten scanner handles normal input, line comments, quoted
symbols and strings. It borrows contiguous input and uses a reusable
64 KiB window for the reader. Tokens carry kind, raw byte range and a lexeme
interval. Token references are independent of window movement.

- Use UTF-8 with checked scalar decoding, ASCII simple symbols and ASCII
  SMT-LIB whitespace. Reject an initial BOM, NUL and invalid UTF-8. Within
  quoted data use printable characters and SMT-LIB whitespace as defined
  by §3.1. In a comment consume to CR, LF or EOF. A semicolon inside either
  quoted state is data. Quoted symbols reject backslash and have no escape.
  Within strings, a doubled quote decodes to one quote and a backslash
  remains ordinary data. One-byte lookahead across a refill decides whether
  a quote closes a string. EOF after a closing quote completes the token.
- Classify whole atom runs using the §3.1 and Appendix B.1 grammar. Numerals
  have digits and no sign or multi-digit leading zero. Decimals have
  a numeral part, dot and one or more fractional digits. Radix tokens
  use lowercase prefixes and nonempty payloads. `-32` is a symbol.
  Parentheses, quoted delimiters and trivia delimit runs. Keep quoted token
  kind through grammar dispatch so `|let|` remains a symbol. Lookup and
  freshness compare dequoted names without case folding or Unicode
  normalization. Reserve solver-owned names with canonical leading `@` or
  `.` at ordinary user declaration and reference boundaries. In a model-value
  request, permit a previously emitted `@` abstract value only through the
  current model's typed namespace. In ordinary symbol slots a quoted spelling
  and its simple spelling identify the same symbol when both are legal.
  [SMT-LIB §3.1 and Appendix B.1](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)
- Use checked 64-bit byte offsets, 1-based lines and byte columns.
  CRLF counts as one line break even when split. Preserve raw newlines
  in quoted data. Keep pending CR, partial UTF-8 scalar, quote lookahead
  and unfinished token state across refills. Seek-free input adapters
  distinguish error, empty EOF and a short successful read.
- Use explicit frames with source start, grammar role, next slot and
  child-list base. Store flat indexed syntax records and ordered child
  intervals for the current command. Roles include command slots,
  named/indexed/qualified identifier, sort and term applications, each
  binder and pattern list, datatype definitions and attribute S-expressions.
  Use a generic flat S-expression record for opaque metadata and inspection
  of well-shaped excluded forms. Parser and elaborator use explicit
  work stacks even for nested attributes, alias expansion, match branches
  and failure cleanup. Grammar checking sees the complete command before
  semantic publication. After `exit`, do not tokenize or validate a tail.

#### Paged compact syntax (Phase 1)

Append token records, expression records, ordered child references and owned
reader spelling bytes to per-command paged storage. Existing pages stay in
place during normal growth. Address records by logical index and spellings by
logical byte range. Reader spellings can cross input windows and storage pages.
The text adapter can refer directly to its borrowed byte range. Keep parser
frame marks and child intervals valid across page boundaries and widening.
Render from the token sequence using exact raw spellings and normalized
inter-token spacing. Source spans use the raw offsets and byte coordinates
from the scanner.

Start with compact unsigned 32-bit page fields where possible. Widen to
unsigned 64-bit fields when a token index, expression index or offset exceeds
the compact representation. Preserve logical addresses, half-open spans and
ordered child ranges across widening. Check overflow before storing fields
and report an actual allocation or representation failure through the existing
resource boundary. The command owns this storage through parsing and apply.
Copy spellings needed by a published binding, response or source record
into their owning storage before releasing syntax pages.

Under `DANDELION_FRONTEND_TESTING`, `Session::ForceSyntaxPromotionThreshold`
accepts a `std::uint64_t` threshold and is `noexcept`. Crossing it exercises
the widening path on small input. The `std::size_t` statistics
`syntax_page_allocations`, `syntax_page_copies` and `syntax_promoted_pages`
count page allocation, full-buffer or widening copies, and widened pages,
respectively. Appending ordinary pages records zero page copies.
The paging, cache and canonicalization counters in `FrontendStats` each
start at zero.

### Environments, commands, and binding

Separate sort and term `NameTable` indexes support heterogeneous lookup
by canonical spelling. A lookup probes with a view, compares the full key
on a hash hit and never inserts a missing token. Keep entries compact with
a borrowed spelling or a stable pool reference and a tagged binding index.
The common free and parameterized declaration entries borrow
`DeclView::GetNameHint()` when it is the exact canonical key and its storage
remains stable for the table lifetime. The owning manager outlives these
views. Store alias and other ineligible spellings once in stable frontend
storage. Term entries distinguish declaration instances, datatype bindings
and direct-body alias ordinals. Creating a free declaration does not create
its application node. Store owning alias nodes in dense root storage.
Rehashing preserves binding ordinals and borrowed spelling
lifetimes. Use full structural index widths or an exact wide fallback. The scope map borrows command-local names and points to
binding records by stable index. An undo vector and frame marks restore the
previous visible binding heads. The active signature reserves its unindexed names in the
corresponding namespace. An indexed identifier has a distinct identity from
an unindexed name that shares its base spelling. Global declarations,
definitions, constructors,
selectors and inline aliases obey freshness after dequoting. Local binders
can shadow user names, including function names, subject to rank at use.
They cannot shadow active theory names. Resolve sort parameters in their
own frame. Quoted reserved tokens are usable where their canonical names
pass these same rules. Pattern heads and symbolic tester indices resolve
against the datatype signature associated with the scrutinee.

A Phase 1 command transaction owns its new entries, roots, optional occurrences and changes
to command state. Named subterms use pending term entries visible within
the current transaction. Reserve or initialize all final frontend entries
before publishing. Source capture appends only when enabled. Keep append
marks for each affected source collection, and roll back with their owning
strings and handles while rolling back the semantic changes. Use indexed
pending records and an undo journal keyed by
canonical spelling, independent of map rehashes. Record each insertion
before its next fallible action. Restore visible scope heads and logical
record counts to their frame marks. Erase inserted slots by spelling with
the command arena still live. On commit, disarm the journal. Capacity growth
and IDs consumed by independent node factories can remain in the private manager until it is released. Failure consumes
no frontend-visible declaration, label, assertion, option change or event.
The driver releases its prefix ownership when the session ends. Compile
fault-injection `Checkpoint` calls out of non-testing builds at the call
site. Testing builds keep the hooks for failure and retry coverage.
[Node guarantees and exceptions](../node/design.md#7-errors-and-factory-guarantees)

- For `let`, validate binder names and resolve each RHS from left to right
  in the outer lexical frame. Earlier valid inline names are visible in
  later RHSs. Install the resulting local binding heads together for the
  body. Substitution reuses the owning RHS nodes. Carry the used binding's scope and RHS dependency marks for closure.
- For `define-sort`, resolve the body against the prior sort environment
  plus distinct fresh parameter slots. Bind nullary aliases immediately to
  the resolved concrete `Sort` and register the canonical name as a model
  abbreviation candidate. For parameterized aliases, store a flat acyclic DAG recipe
  with bound constructor identities, constants, and one shared leaf for
  each parameter identity. Repeated uses of that parameter reference its
  leaf, and substitution reuses the same actual expression for each use.
  Eliminate an alias application by a fixed parameter map, memoizing by
  recipe record within that instantiation. Validate every argument sort
  even if the recipe discards it. Concrete and builder-local contexts
  use the same expansion rule with different leaves. Previous aliases
  already refer to resolved identities. NodeManager interns only
  instantiated concrete sorts, and a sort alias has no semantic SortId
  of its own. Memoization avoids repeating the same recipe and fixed
  substitution, but cannot avoid constructing genuinely different concrete
  sort instantiations generated by distinct substitutions.
- For `define-fun` and `define-const`, reserve the fresh canonical name
  for collision checks and exclude it from lookup in its own body. Resolve
  parameter and result sorts and elaborate the entire body under the prior
  environment and ordered distinct formals. Check declared result sort and
  validate closedness under that binding scope, including unused bodies.
  A formal can shadow a user function. Reject duplicate formals and self
  or forward references.
- A nullary definition binds its canonical name directly to an owning
  body `Node`. This global alias has no declaration, instance, template
  or application wrapper. A reference reuses the body root and checks any
  qualifier against its sort. Positive arity is invalid. Apply the existing
  identifier grammar for a zero-argument reference.
- A parameterized definition stores `DefinedFunction { Decl decl;
  std::vector<Decl> formals; Node body; }` and constructs saturated
  `kApply` calls using the resolved declaration instance. Use
  `FreshFunction`, `MakeFormal` and `MakeApply` with checked ranks.
  Query consumers interpret calls through the matching template. They
  may substitute simultaneously in the fixed argument environment
  with scoped memoization or use another exact strategy. Expansion of
  used compressed definitions can take exponential work and storage.
- For `(! t attributes...)`, require at least one attribute. Reject
  `:pattern` as unsupported. Validate ordinary attributes as syntax data
  and save their text only when source capture is enabled. Allow at most
  one `:named` per form, with a fresh symbol and a closed child of any
  admitted sort. Check collision with any pending definition name.
  Enter the alias through the same owning direct-body binding as a nullary
  definition, after its child and the annotation's checks. Later siblings,
  RHSs, branches and commands see the alias in left-to-right postorder.
  Lexical locals can shadow it. Looking up an alias or calling a definition
  never republishes a source name or annotation.

#### Frame-local operands and closedness

Keep owning operands in active frames and release completed scratch after
its parent and any pending publication have acquired the result. For source
occurrences in a lexical binder scope, track the earliest enclosing binder
region on which each completed result depends, or an empty escape mark.
Order marks by source-region entry so the minimum identifies the outermost
dependency; regions containing a use are nested. Local uses include the
binding's own region and, for a let binding, its outer-resolved RHS mark.
Combine all written application operands, including unused actuals.
Qualifiers and attributes add no dependencies. Resolved globals and prior
closed direct-body aliases introduce no lexical dependency.

At a let body boundary, drop the let's mark if no earlier mark survives.
Only used RHSs contribute through local references. At a match boundary
combine the scrutinee and the result of every written branch after clearing
that branch's local pattern dependency. Propagate an outer dependency even
from a branch omitted in first-match lowering. A `:named` operand must
reach the annotation with an empty escape mark. For example, within a scope
binding `x`, naming `x` fails even when its RHS is a literal. Naming
`(let ((unused x)) 0)` succeeds, provided the unused RHS type-checks. Check
each annotation occurrence during reduction. Node identity alone cannot
determine closure, and no secondary dependency walk is needed.

Use a node-only specialization for binder-free elaboration, with no mark
storage or propagation per operand. Switch to dependency-bearing frames for
active binder scopes, including formal bodies, let bodies and binding match
branches. Nested named annotations reuse the reduced dependency result.
All written terms remain subject to validation and any later command
failure rolls back pending aliases and captured occurrences.
[SMT-LIB §§3.6.2, 4.1.6](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

### Sorts, ranks and node construction

Use the operator document as the typed schema inventory. The frontend's
source descriptors own spelling, identifier form, overload selection, logic
gate and an explicit map to Kind or declaration category. Keep checked
node validation in NodeManager for independently callable entry points.
A single resolved factory call supplies final arity, compatibility,
index, payload and result-sort checks where applicable. Use
`CheckBuiltin` only when a caller needs a checked result sort without
constructing a node. Use `MakeWordIndexedBuiltin` for indices that fit
an unsigned word when the request needs neither a qualifier nor a rational
payload. Other requests use the general factory. Decode literals of
word-sized BV widths directly to a word for `MakeBvWordValue`. Preserve
exact wide decoding and width reduction for indexed literals. Borrow
`SortView` values from resolved owning sorts
through declaration construction. The factory validates and stores the
semantic rank, and the declaration outlives parser rank scratch. These
NodeManager entry points are the target boundary for Phase 1. Preserve
factory error category and operand/index attribution on both paths.

1. Resolve sort identifiers, including exact BV and FP parameters and the
   four FP aliases. Expand aliases and instantiate in declared argument
   order. Compare concrete sorts by checked same-state identity. Use an
   explicit result ascription as a sort constraint on the referenced
   function or constant. A qualified lexical variable reference is
   ill-sorted. Enclosing contexts such as assert, a definition
   result and argument positions check a resolved term's sort.
2. Elaborate children from left to right and filter candidates by source
   head, index form and arity. Dispatch monomorphic heads directly.
   Select Int unary or n-ary minus by arity and each `to_fp` rank by
   arity and source operand form. Map the legacy conversion aliases to
   the corresponding current kinds. The `and`/`or` zero/one-argument
   application provisions check Bool on a single child and yield the
   identity value or that child. The standard parentheses rule for
   nullary identifiers applies elsewhere. Resolve any ordinary builtin
   result qualifier against its actual sort. For `const`, require
   the explicit full Array result qualifier and pass it to the factory.
3. For a datatype constructor, match the ordered concrete child sorts
   against the domain recipes, iteratively visiting pattern/actual sort
   pairs. `Parameter` binds an ordered slot or checks its previous
   binding. `Concrete` uses sort identity. `Array` descends by index
   and element. `Apply` checks the existing constructor's identity
   and `Recursive` checks the owner group's constructor identity at
   the recipe slot, then match the ordered arguments. Include the result
   recipe only when an explicit `as` supplies a result. A remaining
   unbound slot makes the bare constructor ambiguous. In
   `(= xs nil)` with `xs : List<Int>`, require `(as nil (List Int))`.
   A `cons` with a qualified tail can have a unique result from its
   arguments alone.
   Type-check `MakeApply` using the instantiated declaration. For
   a selector and a symbolic tester, use the operand datatype's
   concrete type arguments, validate the declaration membership, and
   instantiate the corresponding declaration. The tester passes the
   resolved tester instance and operand to the factory. There is no
   numeric tester index on the resulting node. Within one constructor
   candidate, memoize successfully matched `(recipe identity, actual SortId)`
   pairs after their children and parameter constraints are checked. Recipe
   identity is scoped to its owning datatype group and manager; the actual
   sort belongs to that same manager. Parameter assignments only grow within
   this attempt. Discard its memo with the attempt or any rollback, so a
   different substitution cannot reuse it. Do not memoize by recipe alone:
   the same parameter pattern against Int and Bool must reveal a conflict.
   `SortRecipeView::GetId` supplies that group-scoped identity.
   [Node design](../node/design.md#9-implemented-query-details)
4. Assert Bool at the assertion boundary. BV1 and Bool remain distinct.
   By default preserve ordered children and high-level operator kind, including
   `fp.eq` versus Core equality, adjacent chains, n-ary `distinct`,
   and `fp` or `to_fp` even with constant bit operands. No frontend
   rewrite changes symbol meaning or substitutes a value for an underspecified
   operation. Programmatic packed FP literals use
   `numeric::FloatingPointValue::FromBits` and `MakeFpValue`. Defined calls
   and model-only abstract values use internal
   declarations through their respective identity tables, never as free UF.
   [Node operator map](../node/operators.md),
   [SMT-LIB §3.6.3 and Figure 5.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

#### Semantic declaration storage and concrete sorts

Append owning `Decl` and `SortConstructor` values to the free signature.
Keep declaration provenance and declared rank spelling in optional source
records. `HasFunction` uses the same-manager declaration identity and
tolerates gaps from templates, formals, datatype declarations and failed
commands. Store parameterized templates in publication order with an
internal declaration index for lookup. Count and ordinal access permits
block-backed storage without relocation of all templates at growth.

Cache resolved concrete sort forms such as `(_ BitVec 32)` and
`(_ FloatingPoint 8 24)` in bounded frontend storage. Include all data
required to resolve the sort in the current signature. A hit must preserve the
sort identity and keep the current occurrence's spelling and span. Keep local
parameters, unresolved recipes and failed resolutions out of the concrete
cache. Eviction permits ordinary resolution on a later miss. Under
`DANDELION_FRONTEND_TESTING`, `sort_cache_hits` counts successful reuse.

#### Commutative canonicalization

`ParseOptions::canonicalize_commutative` defaults to false for text,
reader and model inspection adapters. Enabling the option
canonicalizes typed terms before publication,
including assertions, definition bodies, named and annotated terms, and value
requests. Source order governs elaboration, diagnostics, annotation events
and rendering in both modes.

The standalone pass takes an owning manager reference and returns owning
results for the same manager, in input root order. It visits children before
parents and reuses the result for shared subgraphs. Use the same
commutative-position policy in the parser and standalone pass. Rebuild
changed ancestors with `NodeManager::RebuildNode`, preserving kind, payload,
indices, qualifier and declaration identity. This permits parent merges
even when the parent itself has no commutative positions. Keep the input
handles and their DAG valid.

```cpp
Outcome<std::vector<Node>> CanonicalizeCommutative(
    const ManagerRef& manager, std::span<const Node> roots);
Outcome<Node> CanonicalizeCommutative(const ManagerRef& manager, NodeView root);
```

Order the following positions by ascending `NodeId` using the already
canonicalized operands.

| Positions | Node kinds |
| --- | --- |
| All children at every valid arity | `kAnd`, `kOr`, `kXor`, `kEqual`, `kDistinct`, `kIntAdd`, `kIntMul`, `kBvAnd`, `kBvOr`, `kBvXor`, `kBvAdd`, `kBvMul`, `kFpEq` |
| Both children of a binary node | `kBvNand`, `kBvNor`, `kBvXnor`, `kBvComp`, `kBvUaddo`, `kBvSaddo`, `kBvUmulo`, `kBvSmulo` |
| Children `[1]` and `[2]` only | `kFpAdd`, `kFpMul`, `kFpFma` |

Preserve arity, repeated operands, nested operator boundaries and every
unlisted child position. FP rounding mode stays at `[0]` and the FMA addend
at `[3]`. `kFpMin` and `kFpMax` keep source order because the opposite-zero
result is underspecified. Other unlisted kinds keep their exact operand
order, including UF and datatype applications, `ite`, implication, ordered
comparisons, subtraction, concatenation and array operations. Under
`DANDELION_FRONTEND_TESTING`, `commutative_canonicalizations` observes actual
operand reorderings by a session.

#### Semantic rank rendering

Format model signature ranks from the declaration's ordered semantic domain
and range under the sealed sort namespace. Use canonical declared names
with necessary quoting. Keep no parse-time `sort_spellings` map or
mandatory source-rank renderings. Index nullary candidates by concrete sort
identity and parameterized candidates by recipe height when rendering starts.
Match each recipe and concrete sort pair bottom-up. Cache its unique
substitution or failure for the response. Merge child substitutions and
reject conflicting bindings for repeated parameters. Concrete constructor
identities must agree. Place the inferred arguments in parameter order and
use `Bool` for unused parameters. An inferred argument must be a strict
subsort of the rendered sort. In particular, a parameter-root identity
recipe cannot spell `S` as `(Id S)`.

Memoize each concrete sort's bottom-up spelling plan and count of sort heads
per response. Use the ordinary spelling as the initial choice. Inferred
applications must reduce that count relative to ordinary syntax. Compare
candidates by head count, then quoted head length. Prefer ordinary syntax
on an exact score tie with ordinary syntax and order tied aliases by
canonical name. Saturate head counts for comparisons without constructing
expanded candidate strings. Prune recipes taller than the concrete sort.
Once at most two heads suffice, the only competing parameterized aliases
have one atomic argument. Skip recipes whose height deficit exceeds the
maximum height of an atomically spelled strict subsort. Keep the existing
shared-recipe scale checks. Emit selected plans iteratively. A chain of
nullary aliases for successively doubled array sorts can therefore print
the top alias without unfolding the shared rank.
All spelling choices resolve to the same sort in the sealed signature.
This applies equally to declared ranks and generated sort ascriptions.

### Datatype publication and match

Process a datatype group in three phases. First canonicalize the
one-datatype spelling, validate counts, constructor/selector freshness,
and reserve all group sort names with arities. A local `par` parameter
can shadow a user sort. Field resolution checks the local parameter
before a group or previously declared constructor. Parameters are
unique within each list and must agree with declared arity. Second,
resolve field sorts and check the profile, expanding aliases before
classifying recursion. Map the result to the five existing builder recipe
categories. Prepare the full command entry, all frontend map slots,
source records and stable ordinal links before `Commit()`. A slot for
a datatype, constructor or selector names the entry's group and the
ordinals already checked against the staged syntax. Testers are linked
by constructor and have no bare `is-ctor` name. Third, call the builder
and move its returned owning group into the already initialized command
entry. Flip visibility and disarm rollback using nonthrowing operations.
No allocation, hash insertion, retaining copy or fallible enumeration
of new group records is needed between builder success and visibility.
Use the group owner and the checked ordinals to get borrowed views
on demand. A builder error preserves the previous frontend entries,
and the builder's documented no-published-ID-consumption guarantee
applies to its own transaction.

The builder checks recipe scope, the prohibition on current-group members
below a field root, and productivity. The error code `kInvalidDatatype`
covers unsupported recursive containers and invalid productivity. The
fixed-size NodeError carries one of the reasons
`kEmptyGroup`, `kNoConstructor`, `kInvalidParameterScope`,
`kNestedRecursiveSort`, or `kNonproductive`; use `kNone` for unrelated errors.
Preserve the existing `kInvalidDatatype` code for all five so existing
code-level classifications remain stable. The frontend maps
`kNestedRecursiveSort` to unsupported and the other reasons to invalid input,
without repeating builder validation.
If several faults exist, report the first actual fault in builder validation
order. With no precise field index, report the datatype command span.
For a group with a field of
`(Wrap D)` and `Wrap<X> = Array<Int,X>`, the expanded field is outside
the recursive-container profile. A group member at the field root can
take nonrecursive sort arguments, such as `(D (Array Int A))`. A
nonproductive group such as `D = loop(D)` is an invalid datatype.
Use the builder's command span for a group-level error, or attach the
offending field/name span when the diagnostic identifies it. A least
fixed point marks exactly those members with finite constructor
witnesses, by induction on the height of a witness. Concrete instance
resolution visits finite recipes when used. Even a recursive declaration
whose parameter changes along an edge requires no eager transitive
enumeration of sorts. [Node design §4](../node/design.md#4-datatype-group-transactions),
[builder validation and commit](../../src/node/datatype_builder.cc),
[SMT-LIB §4.2.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

For `match`, resolve the scrutinee to a concrete datatype instance.
A bare name that denotes one of its nullary constructors selects that
constructor. A bare name of one of its nonnullary constructors requires
pattern arguments. Other bare names bind the entire scrutinee. A
constructor pattern has precisely its declared arity and simple
variable or wildcard slots. At a field position the spelling of a
constructor can be a variable. Bare `_` and field `_` are wildcard
tokens, and `|_|` is an ordinary symbol. Reject duplicate named slots,
nested patterns and constructors of the wrong datatype in headed
patterns. Bind the locals only in their own branch, with concrete field
sorts and a shared selector application for each referenced field.
Check every branch at a common sort and complete constructor coverage
or a default. Process names and annotations in source order even if
a branch can never win. After these checks, keep the first branch for
each constructor until a default or complete coverage. Build from
right to left, using the last effective branch as fallback and preceding
constructor branches as `ite` guarded by their typed testers. A default
binds the shared scrutinee. For any scrutinee exactly its first
applicable branch can supply the result. The fallback is justified
by exhaustiveness. Selector values outside the matching constructor
are underspecified but cannot determine the selected value of a
different branch. Explicit selectors outside a match remain ordinary
well-sorted applications. [SMT-LIB §§3.6.1–3.6.3 and 5.2–5.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf),
[topic 6](topic-research.md#6-datatype-declarations-and-match)

### Exact input values

The scanner validates source digit classes. The frontend owns literal grammar,
contextual dispatch and the mapping to exact values. Independently callable
numeric and node factories validate their own arguments. Numeric helpers keep
GMP private; a ground BV/FP evaluator or general integer solver is not required
to parse source.

- Parse decimal numerals exactly using the existing Integer boundary; retain
  source `(- n)` as an Int operation. Import binary and hexadecimal digits
  exactly, preserving their full declared width. Validate independent radix
  helper inputs even when the scanner has already validated its source.
- For `(_ bvN w)`, require `w > 0` and reduce the exact unsigned numeral
  modulo `2^w` before constructing a Node value. Word-sized widths use
  unsigned accumulation and masking. Wider values use the low bits of the
  exact magnitude. Preserve exact rotate and divisibility indices. Invalid
  indices and a valid size that an existing representation cannot hold
  have different outcomes.
- Parse BV widths and FP format indices exactly before any narrowing.
  The current NodeManager uses `uint32_t` for BV widths, FP components and
  their sum, and this existing representation is the selected baseline.
  A larger mathematically valid width or format yields the documented
  `kResourceLimit`; width zero and FP components below two are invalid. This
  is an implementation boundary, not a configurable frontend quota or a
  mathematical language limit.
- For the two-argument `to_fp` form, select the concrete rational rank only
  when its second source operand has grammar
  `Q ::= numeral | decimal | (- Q) | (/ Q Q)` and none of its operator heads
  resolves to a user or local binding. Resolve identities as specified by the
  [logic profile](logic-profile.md); a wrong user rank does not fall back to
  a rational. Decode decimals exactly,
  evaluate this closed expression using exact Integer/Rational operations and
  normalize to a positive denominator. A zero divisor in this compatibility
  grammar is explicitly unsupported. Attribute the error to the expression
  in divisor position after its value has been determined. In
  `(/ 1 (/ 0 3))`, the divisor with value zero is `(/ 0 3)`, so its
  half-open span includes that subexpression's opening and closing
  delimiters. Keep an arbitrary well-typed
  rounding-mode child and the resulting high-level conversion node. General
  Real terms and `fp.to_real` remain unsupported.
- Add the required radix import, decimal fraction, sign change and Rational
  division helpers through their own checked interfaces. Preserve the
  existing `ParseDecimal` and `Rational::Make` contracts. Do not introduce
  numeric work, digit, intermediate-size or limb-count budgets. Implementations
  may avoid unnecessary materialization and check representational arithmetic;
  they must not silently truncate an exact value.

C++ allocation and checked representation failures follow the relevant
factory contract. GMP's default limb-allocation failure may terminate the
process and is not made recoverable by an expected-like return type.
[Node exact values](../node/design.md#5-exact-values),
[numeric contracts](../util/design.md#common-api-and-failure-policy),
[GMP allocator contract](https://gmplib.org/manual/Custom-Allocation).

#### Literal cache (overhaul contract)

Reuse previously decoded exact literals through a bounded cache. Include
all interpretation-relevant data in the cache key, including the literal form,
radix or width when needed. Compare full keys on collisions. Cache only
successful values and retain each use's source occurrence independently.
An oversized literal takes the ordinary exact path and bypasses admission.
On eviction, a later use takes the ordinary path. The entry and key-size
bounds govern cache admission and retention. The input language still accepts
every mathematically valid representable literal. All builds use the same
admission policy for word-sized and wider values. Test builds additionally
count successful reuse in `literal_cache_hits` and support fault injection.
Allocation failures in caching or publication follow the existing resource
diagnostic and command rollback rules. A later command may retry the same
input successfully.

### Resources and representation

There are no project-imposed defaults or configurable quotas for source or
token length, nesting, command storage, number of names or occurrences,
semantic work, numeric intermediate sizes or protocol response size. The
reader buffer is an implementation detail, not an input ceiling. Do not add
budget counters, precharges, quota arguments, quota-only errors or tests that
require such policies.

Use explicit frames and iterative traversals and cleanup where nesting can
grow with input. Release per-command scratch after the command while retaining
only state, definitions, query and source information required by the
interfaces. Preserve exact arithmetic, check actual size calculations before
overflow or narrowing, and rely on validated invariants internally. An actual
I/O, allocation, reference, container or representation failure is distinct
from invalid input; no failure justifies a guessed solver result.

Retain the implemented `uint32_t` representation of BV widths, FP exponent
and significand widths, and their sum. Thus the maximum representable width or
FP total is `2^32 - 1`; a mathematically valid larger request reports the
existing `kResourceLimit`/`kSize`. The structural representation target
uses 64-bit IDs, indices and counts for nodes, edges and declarations,
including arities. Use the structural types from the
[NodeManager interface](../../src/node/node.cppm) across the frontend
boundary. Frontend arity and count conversion must preserve their full
range up to the applicable machine and container limits.
These representation limits do not authorize separate input,
work or memory quotas, and they must be stated when describing supported
inputs rather than claiming every mathematically valid width is representable.

#### Seal cleanup and teardown

Perform fallible preparation for sealing before publishing the sealed query.
With model production disabled, batch-release the dense alias roots through
`NodeManager::ReleaseRoots` while the manager is live, then discard textual
term and sort storage, abbreviation indexes and other lookup scratch.
Prepare the release so no allocation is required after commit. With model
production enabled, share the frozen environment with the sealed context for
detached inspection. On a failed seal, retain the prior phase, roots, aliases
and source history. Independently captured records keep their own roots in
either mode.

At final teardown batch-release owners from the query's assertions and
parameterized definition bodies, the inspection environment's aliases, and
any captured roots when their respective owners are destroyed. Each slot
is a distinct owning reference. Keep an appropriate manager reference
alive for each batch. Release preserves separately retained handles and
shared children. With no surviving nodes the manager can detach the grown
unique table even if an independent manager reference survives. Destruction
is nonthrowing and may use ordinary handle destruction if batch preparation
cannot allocate.

## Phase 2 fused command reading

The [public protocol](semantic-interfaces.md#phase-2-command-processing-and-publication)
replaces pending syntax with a single typed `ReadCommand`. The scanner and
grammar share a command boundary with semantic reduction.

The selected frame and operand layout follows the command-at-a-time
reduction observed in Bitwuzla 0.8.0. Dandelion uses one contiguous operand
buffer and frame-specific uncommon state to avoid a large work record
per operand. Online coordinates preserve locations when window bytes
expire. Keeping pre-close bindings private also makes publication safe
for Dandelion's user-supplied Reader callbacks.
[Frontend architecture review](/pub/netdisk2/zhangx/dandelion-frontend-bench/qf_bv_compare/review_philosophy_frontend.md)

```text
Reader -> lexer -> grammar frames and semantic reductions -> NodeManager
                               |
                     closing delimiter and checks
                               |
                       committed TypedCommand -> Run
```

### Iterative scanner and frame storage

The lexer keeps a running checked 64-bit offset, line, and byte column. Start
at offset 0, line 1, column 1. Consuming CR advances the line and resets the
column to 1. A following LF advances only the offset, even across a refill.
A lone LF starts a new line. Other bytes each advance the byte column,
including the bytes of a multibyte scalar. Retain the scalar's start before
any refill. Invalid leading bytes, incomplete or malformed sequences and
invalid scalar encodings use the existing point diagnostic at that start
(both endpoints equal). Reader I/O failures use the current point and keep
the returned error code when supplied. The scanner carries the partial
scalar, quote lookahead, pending CR and token state across refills.

A token carries its kind, raw lexeme and begin/end marks. The text adapter
borrows source bytes. The Reader adapter borrows its current window until
refill and uses reusable scratch for a split token, including one-byte
lookahead after a closing string quote. Finish all uses of a borrowed
lexeme before the next token or refill. Decode numeric payloads and look up
canonical names in place. Own spelling only for split lexemes, pending
binders and identifiers whose semantic use crosses a refill, requested
`echo` or `get-value` text, and opt-in source records. Copy a newly introduced
permanent name directly to its stable owner or move its already-owned
pending spelling there. Identifiers resolved early travel as identities.

Use a stack of grammar frames with role, next slot or list state, start
mark and scratch bases. Roles include command, term, application head,
indexed/qualified identifier, sort, binder list, match pattern and branch,
datatype headers and fields, attribute, and opaque S-expression. Drive the
stack by consuming a token, shifting a slot or pushing a nested frame, then
reducing completed frames. Record grammar progress before a fallible
semantic action so a failure can resume at the next grammar event. Grammar
roles and checks depend on tokens and
parent slots, so the same frame machine operates with semantic actions
disabled. Fixed arities and required lists are checked at their
grammar-defined boundaries. Recognized excluded forms still validate their
grammar. The command frame consumes its closing `)` and returns without a
trailing trivia scan or lookahead.

The typed loop advances grammar and reduction state in the same frame.
`SyntaxValidator` uses the shared grammar operations with grammar-only
frames. Ordinary leaves and applications reduce directly from the token
view and operand suffix. A parallel `let` stores each pending binder's
canonical spelling in reusable stable pages. It releases the pending
binder records when the list closes and reclaims their spelling space
when leaving the scope. Indexed operators decode numerals as they arrive
and preserve the written index marks. An indexed BV literal borrows the
digits through its width and closing delimiter. The scanner copies them
to a reusable buffer if a refill or split-token scratch would otherwise
invalidate the view.

Use one reusable contiguous buffer of owning `Node` slots for completed
operands. Each active term frame remembers a base, addresses its current
operand suffix by index, and reduces it at close. Parallel buffers hold
the corresponding written start/end marks and, only in binder scopes,
dependency marks. Rare payloads use frame-specific scratch, with inline
slots for common index counts and exact wide values when needed. Indices
survive buffer reallocation and frame cleanup removes only suffixes. Retain the
completed operand or index's endpoints until its parent finishes and
map factory attribution back to the original positions. Unattributed
errors use the complete application span. An early error tied to an
open construct records its start and frame identity so close can supply
the end without retaining intervening tokens.

Elaboration computes scope marks and performs any allowed rewrites before
passing the disposable operand suffix directly to the
[consuming Node factories](../node/design.md#phase-2-consuming-construction).
The common path needs no secondary vector of `NodeView`. A miss clears the
transferred slots. On a hit or error, discard those still-owning slots
with ordinary handle destruction when the frame unwinds. A local, alias,
root or other independent owner keeps its own reference. For optional
commutative ordering, permute the owning slots and remember the written
positions. A failed factory leaves every slot owned, so the reducer can
restore the written order and use `CheckBuiltinOperands` to diagnose rank
errors. A resource error retains its original classification. Clear the
operand and location suffixes before pushing the parent result. Binder
setup, term reduction, syntax-only drain and cleanup all use bounded
native stack depth.

Ordinary application and binding-list frames keep no expression records
or child arrays. Bounded structural scratch holds command arguments,
sort ranks, and recipes for qualified identifiers, datatypes, FP
lookahead, model requests and annotations. Fixed child slots cover
small records. Only variable-length structural lists can grow an
overflow array. Opaque metadata contributes grammar depth and, when
requested, normalized text. In addition to the typed DAG and signature,
scratch is proportional to open frames, live operands, pending binders,
special-form state and requested text. A wide application or parallel
`let` can retain a linear number of immediate operands.

Clear owning elements at the command boundary and on semantic failure.
The typed frame stack retains up to 2 MiB of capacity between commands.
Other reusable vectors keep at most 256 KiB each, and structural scratch
retains at most 256 records and 64 KiB of captured text. Binder spelling
pages retain up to 256 KiB. A larger command can grow these buffers and
releases excess capacity after the command.

### Reduction of special forms

- For a parallel `let`, resolve RHSs in the outer lexical scope, while
  admitting earlier staged `:named` aliases. Keep completed RHS roots and
  pending binder names in frame scratch. Publish all lexical binders to the
  body together after the binding list closes. Restore the previous lexical
  scope when the body closes. Apply the existing occurrence-level closure and
  first-match rules to `let`, formal bodies and `match`.
- For `declare-datatypes`, collect the entire header list and index its
  canonical names and arities in the group overlay before any field sort.
  Single `declare-datatype` obtains its own header from the name and its
  constructor-list or `par` prefix before resolving fields. Resolve
  `par` parameters before group headers and committed sort constructors.
  Reduce fields to builder recipes with exact spans as read. Forward
  references use group ordinals. Accumulate constructor and selector
  freshness checks and validate group lengths and productivity before
  publication. Prepare frontend group and name slots before calling the
  builder's fallible final commit, then move the group into those slots
  and complete frontend publication without throwing.
- For the second source operand of a resolved indexed `to_fp` that can
  take the concrete-rational rank, recognize the closed rational grammar
  in parallel with ordinary term grammar. Check each unqualified `-`
  or `/` head against local, staged and committed term bindings before
  treating it as a rational head. While the candidate remains viable,
  keep only a typed numeric recipe of exact leaves, operator tags and
  source marks, with pending operators in live frames. Do not evaluate
  candidate arithmetic or report its numeric errors until the complete
  second operand and the enclosing two-operand shape select that rank.
  Fold the recipe iteratively, left to right in postorder. For an outer
  zero divisor supplied by `(/ 0 3)`, blame that complete child span.
  If shape or a resolved binding disqualifies the candidate, feed its
  reduced prefix to the ordinary term reducer from the saved marks and
  exact payloads, then continue that path from the live frame slots.
  Each recipe item is replayed at most once. Retain the ordinary numeric
  token class in the recipe for its normal admission and diagnostics.
  The third operand, if present, selects ordinary application before
  that operand is elaborated. A selected user or local binding is never
  reinterpreted after a rank error. Discard the recipe after selection.
- For eligible `get-value`, append normalized token spelling in request
  order as each term is read. Append one space between adjacent tokens
  except just after `(` or before `)`. Preserve each token's original
  bytes. A resolved abstract reference replaces exactly its captured
  token or qualified range with the canonical ascription, using output
  marks so a quoted lookalike stays untouched. Keep each completed
  request's text, owning term and span in the command descriptor. All
  requests must be admitted before emitting any value response bytes.
- For unknown metadata and uninterpreted attribute values, validate the
  token classes and S-expression frames incrementally. Without capture,
  discard accepted tokens and closed frames. With capture, feed the
  normalized text directly into pending source records. Recognized
  metadata retains its existing checking rules and ordinary `:named`
  admission remains a semantic action.

### Failure drain and sparse publication

On the first effective semantic, state or recoverable elaboration failure,
save its diagnostic, disable semantic actions and release owned semantic
scratch. Keep grammar frames, lexical state and any marks needed to
complete the diagnostic. The grammar-only run consumes exactly the same
slots without creating typed terms, resolving names, capturing source
history or performing model operations. A malformed tail or Reader failure
takes precedence. If a resource failure prevents syntax validation, return
the resource diagnostic and terminate. A well-formed failed command
consumes its closing delimiter and leaves the session available for the
next command. Driver result gating can select this grammar-only behavior
as soon as a request is known to be ineligible, with the gate result
observable only after grammar succeeds.

Keep committed storage untouched during input. Create a command-local
overlay only when adding an eagerly visible `:named` alias or datatype
header. Use namespace-specific lazy indexes over owning pending records
and keep append order for publication. Semantic lookup prefers lexical
bindings, then the overlay, then the committed environment and active
theory. Freshness includes the pending definition name without making
the definition look-up visible in its own body. Plain assertions and
declarations accumulate a root or final declaration record in local
slots and need no overlay or per-command transaction object.

Once the command closes and the remaining checks succeed, prepare all
fallible publication work with no further Reader callbacks. Use direct
reserve and nonthrowing append for a single range, and reserve plus
reverse-append preparation for a command affecting multiple ranges or
name slots. Prepare return payload and source records before publication.
Record affected sizes and installed slots before the next fallible action.
On preparation failure, erase those slots and unwind appended records in
reverse ownership order. Make a successful commit nonthrowing, including
prepared datatype group installation, phase and option updates, and
disarming rollback. Maintain committed views at the public boundary.
The only persistent frontend effects on failure are ones committed by
earlier commands. A separate response transaction governs model-local
name publication after output.

The explicit `SyntaxValidator` shares the lexer and grammar-only frame
machine. Existing grammar-without-logic tests justify this tooling path.
It needs only token and command spans. With normal source capture
disabled there is no source history in the typed session, and neither
path owns a syntax tree.

## Cross-topic decisions

- The frontend handles commands as a stream; it does not delay a check or
  require EOF before output. `exit` ends processing immediately.
- The [registry](logic-profile.md) selects explicit rules for the registered
  2025-facing labels. Local compatibility profiles and Real-free
  intersections do not claim complete coverage of their named benchmarks.
- Keep parameterized templates and their calls in the query contract. Resolve
  nullary definitions and inline names to their already typed bodies. Validate bodies and arguments without
  expanding calls to determine an assertion's catalog fragment.
- Resolve value requests after the check against the active signature and
  retained model. Model-generated values are scoped to that model; an
  `unknown` candidate is not a certificate for the original assertions.
- Keep the existing `let`, datatype publication, constructor qualification,
  match and `:named` source-scope rules below. Remove discretionary
  frontend and numeric quotas; retain checks for real invalid inputs and
  representational arithmetic.

## Integration boundaries

The [implementation record](implementation.md) reports verification of these boundaries.

| Area | Phase 1 boundary |
| --- | --- |
| Protocol | Command streaming, prefix responses, terminal EOF and `exit`, and result-dependent model commands. |
| Logic and types | Explicit signature registry, compatibility rules, and Real-free intersections. The solver checks its actual routing preconditions. |
| Definitions and Node | Direct closed aliases and saturated parameterized calls with an owning template table. |
| Datatype diagnostics | Fixed-size reasons under `kInvalidDatatype`, with atomic group publication. |
| Exact input | Checked radix and rational helpers with the existing numeric ownership and failure behavior. |
| Models | Owned model contexts, typed value resolution and capture-safe output. Production providers establish total interpretations of the original signature. |
| Representation | Retain 32-bit BV and FP width fields and manager-local identity semantics. Use the structural count and identity widths in [resources and representation](#resources-and-representation). |
| Tests and build | GoogleTest coverage and a frontend library that builds independently of the solver and SAT dependencies. |

## Integration walkthroughs

The first review follows the requirements through the normal and failing
paths. The scripts below specify typed observations for future tests.
The independent reference experiments are recorded separately.

### W1. Indexed BV and simultaneous let

```smt2
(set-logic QF_BV)
(declare-const x (_ BitVec 8))
(assert
 (let ((hi ((_ extract 7 4) x)))
  (let ((hi ((_ extract 3 0) x)) (old hi))
   (= (concat hi old) (concat ((_ extract 3 0) x) ((_ extract 7 4) x))))))
(check-sat)
```

The two `extract` ranges are high first and each yields BV4. The inner
RHS `old` sees the outer `hi`, so the two concats have the same ordered
children and share a live BV8 node. The Bool root has two child slots.
The scope heads restore outer and then global resolution. Child nodes
survive syntax destruction through the root. Changing high `7` to `8`
fails at the factory index/source mapping. Replacing the outer binding
with an unrelated name makes the inner RHS unbound. In each failure
the assertion and its occurrences are unpublished.

### W2. Int, UF, a definition and an inline name

```smt2
(set-logic QF_UFLIA)
(declare-const g Int)
(declare-fun f (Int) Int)
(define-fun add-global ((t Int)) Int (+ g t))
(assert
 (let ((g 5))
  (and (= (f (add-global g)) (f (! (add-global 5) :named a)))
       (= a (add-global g)))))
(assert (= a (add-global 5)))
(check-sat)
```

Both calls in the let retain the `add-global` definition identity and actual
5; its stored body still refers to the declared global `g`. The label `a`
denotes the same closed defined application, visible in the next conjunct
and the next command. Both assertion roots are closed; the definition table
owns the open template and no independent free symbol represents `a`.
Writing `(add-global g)` as the annotated source operand would make the
label depend on the outer lexical binder and fail even though substitution
could produce the same semantic call.
If the final conjunct uses an unbound function, discard the staged
label and every occurrence from the command. Destroying the frontend
owners releases template and label nodes unless another owner retains them.

### W3. Parameterized mutual datatypes and match

```smt2
(set-logic ALL)
(declare-datatypes ((Tree 1) (Trees 1))
 ((par (T) ((node (value T) (kids (Trees T)))))
  (par (U) ((nil) (cons (hd (Tree U)) (tl (Trees U)))))))
(declare-const xs (Trees (_ BitVec 8)))
(assert
 (let ((root (node #x2a (as nil (Trees (_ BitVec 8))))))
  (and ((_ is node) root)
       (= (value root) #x2a)
       (= (match xs (((cons h _) (value h))
                     ((cons h t) (value h))
                     (nil #x00)))
          (ite ((_ is cons) xs) (value (hd xs)) #x00)))))
(check-sat)
```

`nil` makes `Trees` productive and thereby makes `Tree` productive.
It needs its result qualifier at the term use. Both inputs of `node`
agree on BV8 for `T`, and the rank matcher crosses to the exact
`Trees` slot of the committed group. `hd` instantiates at
`Trees<BV8> -> Tree<BV8>`, `value` at `Tree<BV8> -> BV8`, and
symbolic tester indices select Bool-range declarations. The first
cons branch wins, the second still checks against BV8, and `nil`
provides the fallback. The lowered match and written ite share a
scrutinee and compatible applications. One group owner keeps all
ordinal bindings valid. Replacing the second branch value by `#b0`
fails without publishing the assertion, despite the repeat. Removing
the base constructor makes the group fail with no group names
published. A staged slot allocation failure occurs before `Commit`.

### W4. FP, symbolic rounding and concrete rational

```smt2
(set-logic QF_BVFPLRA)
(declare-const rm RoundingMode)
(assert
 (fp.eq ((_ to_fp 8 24) rm (/ (- 1.25) (/ 5 2)))
        ((_ to_fp 8 24) rm (/ 3 (- 6)))))
(check-sat)
```

The second operand syntax of each conversion selects the rational
rank. Exact division yields `(-5/4)/(5/2) = -1/2` and `3/(-6) = -1/2`.
Both conversions have the same sort, exact payload and RM child and
share a live node. The rational temporaries can die after factory
completion. The Bool root retains `kFpEq`. Replacing either rational
operand with `(/ 1 (/ 0 3))` produces an unsupported diagnostic whose
span is exactly `(/ 0 3)`. A Real declaration fails
the profile, and a mismatched RM fails ordinary typing. No solver
result arises on those paths.

### W5. Nested arrays, UF and interpreted sorts

```smt2
(set-logic ALL)
(declare-sort Key 1)
(declare-datatype Packet ((packet (bits (_ BitVec 8)))))
(declare-fun key (Packet) (Key Int))
(declare-fun cells (RoundingMode) (Array (Key Int) (Array (_ BitVec 8) Packet)))
(declare-const p Packet)
(assert
 (let ((i (key p))
       (base (cells RNE))
       (row ((as const (Array (_ BitVec 8) Packet)) p)))
  (= (select (select (store base i row) i) (bits p)) p)))
(check-sat)
```

The sort instantiation `Key<Int>` is its own concrete identity,
distinct from `Int` and `Key<Bool>`. Resolve `cells` using the
RoundingMode child, `const` using the Array qualifier and the
symbolic Packet element, and `store`/outer read using the same
`Key<Int>` key. The inner row read uses BV8 and returns Packet.
Read-over-write and the constant-array interpretation give `p`
for the selector at any BV8 index. The frontend keeps all these
operators and their concrete signature. Replacing the read key by
`(bits p)` is ill-sorted at the outer array. An array of arrays under
`QF_AUFBV` is admitted by the selected general-array extension; the same
builtin Array sort is unavailable under `QF_UFBV`. No source buffer or stack
local is needed by the retained query.

### W6. Streaming, errors, and inspection

Consider the following independently observable paths:

| Input or event | Expected behavior |
| --- | --- |
| An incomplete or ill-sorted `assert` before a check | Report the command error; the command publishes neither its assertion nor pending labels. Earlier successful commands remain committed. |
| `(check-sat)` arrives while stdin remains open | Invoke the engine and emit its result without waiting for EOF or a later command. |
| `(check-sat) (assert true)` | Emit the completed check result, then report the post-check state error. The later error cannot retract the earlier result. |
| `(check-sat) (exit) (assert` | Emit the check result, then exit without parsing the incomplete suffix. |
| `(exit)` before logic or a check | Exit without invoking the solver; EOF at a command boundary is also normal. |
| `(declare-const big (_ BitVec 4294967296))` | The selected NodeManager representation cannot hold this mathematically valid width and reports its existing representation failure. Width zero is invalid. |

With model production enabled, a controlled `sat` model accepts successive
value requests, including a term absent from assertions and a previously
emitted abstract value. Resolve the latter only in the current model's
namespace. For `unknown`, a usable candidate can be inspected without
claiming it satisfies the original assertions; an absent candidate is
reported as unavailable. `unsat` has no model inspection. A later sink or
model failure does not erase earlier responses. A controlled provider tests
the driver only; a real solver must independently satisfy model obligations.

### W7. Large/deep input, definitions, and release

Use manageable deep and wide synthetic cases, fragmented reader input, long
tokens and exact numerals. Compare typed structure and source spans with an
equivalent contiguous input, then release scratch, definitions, roots and the
model through their real owners. A late malformed command, actual reader
failure or injected project-controlled allocation failure exercises cleanup;
none is a reason to impose a fixed parser quota.

For nested nonrecursive definitions `f0(x)=bvnot(x)` and
`fN(x)=f(N-1)(f(N-1)(x))`, type every body and preserve calls as definition
applications. The retained unused definition graph grows with the written
definitions rather than constructing the fully expanded `2^N` unary chain.
For a used call, compare the meaning against independent small cases, including
formal shadowing and repeated actual arguments. No claim is made that every
used compressed definition admits a linear-cost expansion.

### W8. Definitions and integer compatibility

```smt2
(set-logic QF_LIA)
(declare-const x Int)
(define-fun f ((t Int)) Int (* 2 t))
(assert (= (f x) 2))
(assert (= (f (+ x 1)) 4))
(check-sat)
```

Both assertions are well sorted and committed. They retain the same defined
function identity with the distinct arguments `x` and `(+ x 1)`. The frontend
does not expand the body or reject the second call because of its catalog
shape. A controlled driver invokes the engine once with both roots and the
definition table. A solver may route the resulting linear meaning to a linear
procedure only after establishing its preconditions. A variant using
`(* x x)` under the same label is accepted too, but a solver cannot infer a
linear fragment from `set-logic QF_LIA` alone.

A second fixture defines `f0(t)=t` and `fn(t)=f(n-1)(f(n-1)(t))` through
`f12`. Constructing `f12(x)` retains the written definition DAG, without
creating or traversing its exponential expansion. This is a construction
invariant for this input family, not a general parsing time bound.

### W9. Model-local array readback

```smt2
(set-option :produce-models true)
(set-logic ALL)
(declare-const a (Array Int Int))
(check-sat)
(get-value (a))
(get-value ((select (as @value0 (Array Int Int)) 0)))
```

A controlled provider can return `sat` with `a` interpreted as the total
constant-zero array. If this is the first exposed abstract value, the first
request prints `((a (as @value0 (Array Int Int))))`; the second prints a
value of `0` for the requested `select`. The registry retains the array value
and proxy after each command's reader scratch is released. A different model
cannot use the first model's proxy handle, even when both share a manager.
An equal array with a different internal store history uses the same
model-local name. If writing or flushing the first response fails, the
driver terminates and the model does not publish that response's new name.
The printer retains available sort aliases in generated qualifiers so shared
alias definitions can keep output compact. See the [model output contract](semantic-interfaces.md#model-local-values-and-inspection).

## Second review and rechecks

The revised review checks both the full requirements and interactions among
streaming, definitions, model evaluation and ownership:

- A later bad command cannot erase a prior response; a second check fails
  without another solver call, and nothing after `exit` is parsed.
- Definition records and manager state outlive all consumers. Parsing checks
  actual signatures, sorts and source scopes without expanding definition
  calls merely to enforce a catalog assertion shape. Solver-side fragment
  selection must examine the real meaning and preserve the IDL/LIA targets.
- Parser-only inspection cannot resolve names introduced by a model that was
  never built. Model-bound names do not enter the assertion namespace. Array
  identity is extensional and model-local, and canonical keys agree exactly
  with semantic equality. A model export can use symbolic bodies or tables;
  it binds abstract values explicitly and prints formals without capture.
  Any failure after a response begins terminates the stream.
- Datatype publication and `:named` events remain atomic per command, and
  all source branches and binding RHSs are typed even when later unused.
  Matching shared datatype recipes avoids revisiting the same recipe/sort
  pair, using the group-scoped recipe query.
- Solve-only sealing drops alias ownership only after successful seal
  preparation. Assertions, parameterized templates and opted-in capture
  retain independent owners. With models enabled, the frozen environment
  supports lookups even after the creating session and its input are gone.
- Rendering ranks from semantic sorts preserves declaration identity and
  exact types. Per-response sort plans replace parse-time spelling
  preferences and retain compact output for shared ranks.
- Large input and exact values have no discretionary quota; checked physical
  and representation failures remain correctly classified. The selected
  32-bit BV/FP fields are an explicit, tested representation boundary.
- Reference observations are compared using complete output and process
  status. Agreement does not prove correctness or complete corpus coverage.

The [acceptance scope](#acceptance-scope) describes the verification evidence
and the remaining solver-side obligations.

## Test-first implementation

Follow the [test-first rule](../../AGENTS.md), the [frontend test inventory
requirements](../testing/frontend.md#turn-the-input-profile-into-a-coverage-inventory)
and the [node](../testing/node.md) and [numeric](../testing/util.md)
contracts. The inventory below defines planned cases. Before implementing
each slice, materialize its cases with test IDs, inputs, expected kinds,
sorts, identities, indices and payloads or diagnostic categories, and
the applicable independent evidence. An initial build failure caused
by a missing interface is recorded as such. Keep existing tests and
new tests active while implementing the slice.

### Inventory and oracles

Use a checked byte-reader fixture with arbitrary partitions and explicit
read failure. A private component fixture observes publication, allocation
checkpoints and binder identities. Public parse fixtures observe individual
commands, the query, occurrences and lifetime. A driver fixture
supplies configuration, controlled check outcomes, a model recorder and a
short-write sink. A case aborts on unexpected parse failure before
inspecting derived views. Tests name the feature row and relevant
expectation in the failure. No frontend fixture infers validity from an
input's status annotation.

| Case IDs and source | Representative cases and independent expectations |
| --- | --- |
| FE-CMD01–05, command/logic table | One positive per command and each supported Boolean option. Include wrong arity, missing/unknown/repeated logic, declarations after check, second check, EOF or exit before a check, and unsupported commands/options. A check emits its result with stdin open; later errors preserve it; bytes after exit are ignored. Verify metadata never supplies a solver result. |
| FE-INFO01–06, §4.2.9 and local metadata policy | Each of version 2.6/2.7 and rejected other version/wrong shape, source string/quoted symbol, compatible and strict category controls and wrong token classes, license, status and opaque attributes. Include `get-info`, `get-option`, `echo`, their unknown keys, option state, response grammar and `:print-success` timing. |
| FE-LEX01–08, §3.1 and Appendix B | P10 and FE-LEX-001, simple/quoted names including quoted reserved words, empty string and doubled quote, ASCII and UTF-8 text, CR/LF/CRLF, final comment and exact spans. Include BOM, NUL, bad UTF-8, quoted backslash, incomplete quote, bad radix/digits, leading-zero numeral, negative index symbol and nested attribute/list EOF. Exhaust every single split point of the small fixture and one-byte chunks. A read fault mid-token is a different outcome from EOF in the same token. |
| FE-BIND01–08, W1/W2 and §3.6 | Parallel RHS and let duplicates, nested bindings and restoration, name collisions, fresh formals, unused parameters and definition capture. Test sort aliases and wrong arity/circular name. Check definition identity and actuals, retained body and environment, transitive definitions, invalid arguments, capture-safe meaning and exclusion from free UF/model declarations. Test unused nested definitions without eager expansion and compare small used examples semantically. |
| FE-NAME01–09, W2 and §4.1.6 | Same-command postorder sibling and later-command visibility, forward reference rejection, nested annotations, non-Bool label, fresh/duplicate/collision cases, closed under binder, open outer local, inner unused and used RHS, unused macro actual still free, branch pattern local and name in a later ineffective branch. Invalid later term or C++ allocation restores prefix tables. Include ordinary metadata, excluded patterns and excluded `:named` on value requests. |
| FE-SORT01–08, node sort contracts | Separate rows for Bool, Int, RoundingMode, BV, indexed FP, each FP alias, Array, both uninterpreted arity classes, and monomorphic/parameterized DT. Substitute wrong parameter class, arity, zero/minimum/max/overflow width or format, wrong ordered sort argument, and unsupported families. Same-sort factory identity survives aliases, different constructor identities or parameter order yield different sorts. Include same name in separate namespaces. |
| FE-DT01–12, W3 and node DT contracts | Both commands, enum, parameterized lists and mutual groups with changing parameters. Include wrong arity, empty groups, par/header mismatch, duplicate canonical names, selector/tester group mismatch, overloading collision, no finite base, recursive containers via alias, legal alias dropping an unused group argument, allowed root recursion with nested nonrecursive args. Test builder and frontend stage failures followed by state inspection. See match cases below. |
| FE-MATCH01–08, W3 and §3.6.1 | Constructor/variable/pattern wildcard and quoted wildcard, symbolic tester, field named after a different constructor, wrong field count, duplicate named slot versus two wildcard slots, no coverage, repeated constructors, early default and unreachable type error. Compare one independently computed constructor case per effective branch against the lowered result, including guarded selectors. |
| FE-NUM01–09, operator profile and arithmetic | Int at zero, signed word endpoints, neighbors and `2^64`. BV widths 1, 2, 8, 32, 64, 65 and 68, width-sensitive radix equivalence, `(_ bv255 8)`/overflow, 65-bit literal versus its 64-bit variant. Exact rotate/divisibility indices past 64 bits and derived widths near the limit. All special FP categories, distinct zeros, one NaN category and finite raw bits at node boundary, all RM aliases, nonconstant RM, rational decimal and integer atoms, nested signs and divisions, normalization, unsupported inner zero vs Int `div` zero. |
| NUM-INPUT01–04, exact arithmetic | Test the four value helpers directly. Import `10000000000000000` in base 16 as `2^64`, and equivalent binary digits. Reject empty/wrong digits and unsupported radix. Decode `0.0125` to `1/80`; negate it and zero; divide `-5/4` by `5/2` for `-1/2` and reject division by zero at the numeric boundary. Exercise real representation and injected C++ allocation failures, without a work quota. |
| FE-MIX01–07, W3–W5 and architecture §7 | Arrays of datatypes and a field of Array over previously declared DT, FP and RM in UF/array/DT ranks, Bool arguments in UF, parametric UF sort applications with ordinary and mixed sorts, BV/Int conversions and arrays with BV1 indices or Bool elements. Verify one typed case and wrong sort/profile companion per combination. Check structural meaning of the architecture examples such as one-bit finite cardinality and inductive datatype cycles. Satisfiability remains a solver-layer expectation. |
| FE-RUN01–08, W6 | Check exactly one solver call, immediate response before EOF, prior output on a later error, exit without tail processing, and model/sink failures. Test sat with and without model production, unknown with and without a candidate, unsat inspection, missing required sat model, duplicate and new terms, same-model abstract-value readback, wrong/unknown names and sorts, and stable term rendering. |
| FE-SCALE01–10, W6/W7 | Wide and deep commands, long decoded names, exact numerals, nested aliases and matches, repeated defined calls, fragmented reads, derived-width checks, injected C++ allocation and retain failure, ownership teardown and iterative release. Cover actual representation boundaries; do not add a per-input quota or quota-driven cases. Keep the existing isolated GMP failure test distinct. |

FE-DT cases include explicit `(as nil (List Int))`, plain ambiguous `nil`,
a wrong qualifier, an argument-determined `cons` and the mutually recursive
constructor in W3. Test the parameter matcher on repeated slots, a mismatch
below an array argument, and argument order. Confirm that identical concrete
instantiations share the declaration identity and differently instantiated
constructors do not. Include a variable versus constructor bare-pattern case
independent of the enclosing lexical head at that spelling.

For FE-CMD logic coverage, expand the following rows to one pair per label.
Use `(check-sat)` in each complete script. All labels admit Bool constants;
free sorts and nonzero-arity free functions follow each row's C, S or U
permission in the [registry](logic-profile.md#components-and-registered-labels).
Test both declaration spellings for constants and retain nonrecursive
definitions under every permission. A negative builtin-sort example does not
first declare that spelling as a user sort. Resolved user symbols remain
distinct from inactive builtins of another profile.

| Registry rows | Positive and rejected controls |
| --- | --- |
| `QF_BV`, `QF_UFBV` | Bool/BV constants and a nonrecursive defined function in both; permit free sorts and UF over BV only in UFBV. Reject builtin Int or Array in both. |
| `QF_ABV`, `QF_AUFBV`, `QF_AX` | Admit nested arrays in both BV-array profiles and `(Array U Bool)` for free sort U in AX. Permit nonzero-arity free functions only in AUFBV; AX also permits free sorts. Reject unavailable builtin Int or BV respectively. |
| `QF_UF` | Parameterized free sorts and a Bool-argument function. Reject undeclared builtin BV. A user-declared sort named Int remains UF. |
| `QF_IDL`, `QF_UFIDL` | Accept `(< x 3)` and Int unary minus under IDL, and `(= (+ x y) 1)` under UFIDL. Accept a declared UF named `div` under UFIDL; reject builtin unary minus in a UFIDL assertion and distinguish that UF from Int division. |
| `QF_LIA`, `QF_UFLIA`, `QF_AUFLIA` | Admit `(* 2 (+ x 1))`, a symbolic product and Int `div` in each; AUFLIA also admits `(Array Bool Int)`. Permit nonzero-arity free functions only in UFLIA/AUFLIA. Reject builtin Array under the first two and builtin BV under all three. |
| `QF_NIA`, `QF_EIA` | Admit symbolic products and Int `**` in both. Reject a free sort or nonzero-arity free function, and unavailable builtin Real or BV. |
| `QF_FP`, `QF_BVFP`, `QF_FPLRA`, `QF_BVFPLRA` | FP arithmetic with RM and FP-facing bits under all four; standalone BV operators under BVFP/BVFPLRA. W4's rational conversion is valid; builtin Real declarations and `fp.to_real` are unsupported. Int/BV conversion requires active Int. |
| `QF_DT`, `QF_UFDT`, `QF_UFBVDT` | Enumeration in each and UF over datatype in the UF rows; reject nonzero-arity free functions in DT. Include FE-MIX-001 in UFBVDT. Reject an undeclared builtin BV field under DT/UFDT and builtin Int in UFBVDT. |
| `QF_ABVFP`, `QF_ABVFPLRA`, `QF_AUFBVFP` | Select FP from `(Array Bool (_ FloatingPoint 8 24))` in each; permit an FP UF only in AUFBVFP. Reject builtin Real and unavailable Int. |
| `QF_UFFP` | Apply a UF to an FP value; reject builtin Int and standalone BV operations. A user function with an inactive BV-operator spelling retains its declared UF identity. |
| `QF_ALIA`, `QF_ANIA`, `QF_AUFNIA`, `QF_UFNIA` | Use a symbolic product in all and an Int UF only in AUFNIA/UFNIA; use `(Array Bool Int)` in the array rows. Reject builtin Array in UFNIA and unavailable builtin BV in all. |
| `QF_UFDTLIA`, `QF_UFDTLIRA`, `QF_UFDTNIA` | Use Int fields, an Int-valued UF and symbolic multiplication in each. Reject builtin Array and actual Real; classification remains a solver obligation. |
| `QF_UFBVLIA`, `QF_AUFBVLIA`, `QF_AUFBVNIA` | Use UF over BV and Int and both-theory conversion; use a mixed-sort array in the two array rows. Reject builtin Array in UFBVLIA and actual Real in the others. |
| `QF_LIRA`, `QF_NIRA`, `QF_UFFPDTNIRA` | Exercise nonlinear Int including `**` in all, and FP/DT/UF terms in the last. Reject free functions in the first two, actual Real in all and builtin Array where unavailable. |
| `QF_BVLRA`, `QF_UFLRA`, `QF_UFNRA`, `QF_LRA`, `QF_NRA`, `QF_RDL` | Exercise admitted BV or Core and use a free function only in UFLRA/UFNRA. Reject actual Real and a builtin sort unavailable in the row. |
| `QF_SAT` | Assert a Bool constant; reject a nonzero-arity free function and an undeclared builtin Int or BV sort. |
| `ALL` | W3, W5, BV/Int bridges and `(** x 2)`. Actual Real, String, global sort parameters and nested current-group containers remain unsupported. |

An unregistered logic and each listed String logic produce unsupported.
Separately check Real/FP reserved names and identity-based rational dispatch,
including that a user or local binding cannot be silently reinterpreted as a
builtin. Tests inspect semantic sort and declaration identity, not spelling
alone. They distinguish the compatibility profile from strict catalog syntax.

### Operator case expansion

Use one row/case per spelling, kind, overload and admitted alias identified
in the [authoritative operator tables](../node/operators.md#2-values-and-declaration-applications).
The following gives concrete input operands and observations for generating
the cases independently of the frontend's dispatch table. Each core,
non-Boolean or value-producing case can be run as a lone `get-value`
operand with the model option enabled before `set-logic ALL`, followed by
a single check marker and parser-only inspection without model-dependent
names. Boolean cases also use an assertion. This fixture checks typed
structure without pretending a model query ran. Keep one row per distinct surface rank even when two ranks emit
the same kind. Mark each planned until it has a linked passing case.

| Test family and each individual row source | Valid operand/result fixture and negative mutation |
| --- | --- |
| FE-OP-VALUE, §2 | `true`, `false`, integer literal `0` and wide numeral, `#b00001111`, `#x0f`, `(_ bv15 8)`, each of the five explicit FP specials at `(8,24)`, each long/short RM symbol and declared nullary/fixed-rank function applications. Check exact category and payload and declaration rank. Use absent declared symbol, extra operand on a nullary value, a bad width/value, wrong FP index and unsupported operators as applicable. |
| FE-OP-CORE, §3 | `not true`, `and/or/xor/=>` with three Bool operands in visibly distinct order (declare Bool variables), `=, distinct` on three BV8 terms, `ite true #x11 #x22`. Require the listed Kind, exact child order and Bool/BV8 result, and test empty/unary `and/or` identities separately. Use BV1 in a Bool slot, BV4 in a same-BV8 slot, different-arm sort, and wrong arities. A three-child implication has right-fold meaning (e.g. false, true, false yields true). Equality on three operands conjoins two adjacent equalities. Distinct checks all three pairs. |
| FE-OP-INT, §4 | Unary minus of 3 remains an operation. Use `9 3 2` for each n-ary arithmetic family and `1 2 3` for each comparison. `mod`, `abs`, `**` get their direct ranks, `(_ divisible 3)` uses one Int and exact index 3. Result Int or Bool as specified. Change one operand to BV1, drop below minimum arity, and test zero divisibility, signed operands, left-fold `div`, and the QF_NIA compatibility extension for `**`. |
| FE-OP-BV, §5 | Use distinct BV8 children `#x80 #x02 #x01` as needed for unary, binary and n-ary rows. Include every listed comparison and overflow spelling separately and `bvcomp` with BV1 result. `concat #x1 #x02` yields BV12, `extract 3 0` over BV8 yields BV4, `repeat 2` yields BV16, each extension by 1 yields BV9 and each rotate yields BV8. Test result sort, child and index order and magnitude. Remove a required child, put Bool in a BV slot or mix BV8/BV4 at an equal-width position. Test reversed/out-of-range extract, repeat 0, exact derived-width overflow and three-child concat. |
| FE-OP-BRIDGE, §5 | `ubv_to_int`, `sbv_to_int`, `bv2nat` over `#xff`, `(_ int_to_bv 8)` and `(_ int2bv 8)` over `(- 1)`. Preserve separately signed and unsigned kinds, source legacy alias mapping, exact index and Int/BV8 results. Test wrong rank, a profile lacking either theory and excluded `bv2int`. |
| FE-OP-FP, §6 | Use FP8,24 terms `u,v,w`, RM `rm`, and FP5,11 term `f`. Instantiate each unary op, RM arithmetic, FMA, min/max/rem, chainable comparator and predicate with the ordered operands in its schema. Use `fp #b0 #x00` with a 23-bit zero fraction. Cover each `to_fp` rank individually with BV32 bits, `rm f`, `rm #x80`, `rm (/ 1 2)`, then `to_fp_unsigned` with `rm #x80`, and both FP-to-BV kinds with index 7. Assert exact FP8,24 or BV7 result and child order or rational payload, and Bool for comparisons and tests. Remove required operands without reaching another valid rank, use Bool in the value or RM slot, and exercise matching-format and bit-width constraints where specified. Preserve Core equality separately, including signed-zero operands. |
| FE-OP-ARRAY-DT, §7 | Use `a : Array<Int,BV8>`, `select a 2`, `store a 2 #xff`, and `((as const (Array Int (_ BitVec 8))) #xff)`. Check element/array sorts, order and full qualifier, and replace one index, value or qualifier with a wrong sort. For each distinct declaration category test W3 constructors, selectors and symbolic testers, observing the concrete declaration and ordered arguments. `(hd (as nil (Trees (_ BitVec 8))))` has sort `Tree<BV8>`. Test wrong argument count/sort, symbolic-vs-numeric tester index and unintended constructor membership. |

The operator tables were checked against the current
[`Kind` enum](../../src/node/node.cppm). Their union contains all 108
publishable kinds and excludes `kInvalid`. Expand every grouped
row in the test review. Distinct numeral forms, RM symbols, BV/Int legacy
aliases and the short Core compatibility ranks are extra source cases.
The type tables cover scope-selected source forms, and the internal
declaration and raw packed FP value paths remain node tests.
Use the revision-pinned definition for an expected child/result rank even
if the implementation's descriptor disagrees. Meaning checks for lowered
subterms can use a small independent evaluator over finite Bool/BV cases
or manually derive branch results from matched constructors. They need no
solver. Keep semantic checks separate from structural required observations
such as root count and equality slot multiplicity.

For supplemental bounded generation use 32 scripts per seed, maximum
term depth 6 and 128 syntax records per generated term, using seed
0 and 1. Emit sorts and expected structure from separate small signatures.
Add one deliberate error per negative case and preserve legitimate
atom boundaries in whitespace, comment and alpha-renaming transformations.
Use radix variants only at equal width. A round trip can miss shared
bugs. In an isolated copy challenge the tests with swapped extract
indices, sequential let, unsigned literal wrapping, quote/comment
confusion, and dropping the last assertion. A surviving mutation
requires scrutiny of the corresponding invariant and test.

### Test-first gates

Before implementing the affected behavior, complete the per-feature inventory
and add its GoogleTest cases under [AGENTS.md](../../AGENTS.md). The frontend
suite must exercise lexing, scoped elaboration, every accepted operator and
combination, command streaming, definitions, model-query state and the
applicable errors. Node and numeric interface changes require their own
test-first coverage and must preserve existing tests.

The solver-free component suite can use a controlled driver/model provider
to verify protocol and ownership without presenting its responses as real
solving results. Production model and solver acceptance additionally requires
a real joint model, total interpretations, definition semantics and independent
checks against the original assertions. The revised signature, definition,
option and model-value contracts must be covered by tests before implementing
those affected behaviors. Keep the BV and FP width bounds and test
structural representation changes at the corresponding Node interfaces.

## Build and acceptance commands

Use one static `dandelion_frontend` target in `src/frontend`, exporting
`dandelion.frontend` through `PUBLIC FILE_SET CXX_MODULES` and using
`dandelion_node` as a public usage requirement. Keep scanner/syntax,
elaboration, and driver implementations private. Use one private
partition if several implementation units share types. Configure with
`dandelion_configure_target` and explicit owned sources. Link only to
the node stack and normal standard library. The driver callback is a
frontend-facing boundary with no SAT/solver header or module import.
Gate any future solver and backend package discovery behind the solver
target's configure selection. Frontend-only configuration includes
the node stack regardless of whether a solver target exists.

Build `dandelion_frontend_tests` when `DANDELION_BUILD_TESTS` is on,
linking the existing GoogleTest main target and using
`gtest_discover_tests(... DISCOVERY_MODE PRE_TEST PROPERTIES LABELS frontend)`.
Use the existing source/test policy, tests-off production modules, and
static consumer sanitizer link usage. Offline configure needs only
genuine build dependencies from EXISTING mode. Reference solvers,
datasets, benchmarks and particular working directories are independent
of the normal runtime suite. Configure external/corpus test registration
only in an opt-in run. [CMake design and dependency policy](../cmake.md),
[C++ checks and toolchains](../cpp-style.md#build-and-runtime-checks)

From the project root, use the targets and commands below with fresh
build directories and the supported compiler selections. Set any local
dependency paths as documented in the build guide.

```sh
work=$(mktemp -d /tmp/dandelion-frontend-acceptance.XXXXXX)
for preset in release clang-debug gcc-asan-ubsan; do
  compiler=g++
  if [ "$preset" = clang-debug ]; then compiler=clang++; fi
  CXX="$compiler" cmake --preset "$preset" -B "$work/$preset" \
    -DDANDELION_DEPENDENCY_MODE=EXISTING \
    -DDANDELION_BUILD_BENCHMARKS=OFF \
    -DDANDELION_WARNINGS_AS_ERRORS=ON || exit 1
  cmake --build "$work/$preset" || exit 1
  ctest --test-dir "$work/$preset" -L '^frontend$' --output-on-failure --no-tests=error || exit 1
  ctest --test-dir "$work/$preset" -L '^node$' --output-on-failure --no-tests=error || exit 1
  ctest --test-dir "$work/$preset" --output-on-failure --no-tests=error || exit 1
  cmake --build "$work/$preset" || exit 1
done
CXX=g++ cmake --preset release -B "$work/no-tests" \
  -DDANDELION_DEPENDENCY_MODE=EXISTING -DDANDELION_BUILD_TESTS=OFF
cmake --build "$work/no-tests" --target dandelion_frontend
python3 scripts/check_cpp.py format-check
python3 scripts/check_cpp.py tidy --build-dir "$work/clang-debug"
```

Use the documented tooling-test option if developer-tool prerequisites are
missing and record that omission separately. Verify a final consumer of the
sanitized static target links and executes without using the library's
compile instrumentation as a substitute for sanitizer link usage.
Inspect the dependency and module graph of the solver-free configure.
The no-op rebuild must have no work. In an isolated source copy,
change one private implementation and then an interface and inspect
the expected recompilation and consumer rebuild. Retain independent BMIs
and verify release-like assertions using GoogleTest. Clang's local
sanitizer runtime is unavailable under the documented configuration.
The required sanitizer line uses GCC ASan/UBSan. Record exact versions,
flags, configuration, dependency paths/provenance, test counts and status.
Require all applicable cases discovered and passing. Record exclusions,
unavailable entries and failed prerequisites separately.

During the pre-implementation design review a fresh GCC 16.2.0 Release tree
was configured with CMake 4.5.0, EXISTING mode, GMP 6.3.0 and existing GoogleTest
1.18.0. Benchmarks and tooling tests were disabled. The present
build succeeded and `ctest -L '^node$' --no-tests=error` passed
488 cases on that baseline. See [node acceptance](../testing/node-acceptance.md)
for Node-specific evidence and the [implementation record](implementation.md)
for the later frontend build and test results.

## Bounded external evidence and corpus procedure

### Reference observations in this review

The W1–W5 scripts above, LF-terminated as printed, were run using the
pinned executables in the [topic evidence table](topic-research.md#evidence-used-throughout).
Z is Z3 5.3.3, commit `430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e`, with
`-smt2`. C is cvc5 1.4.2, commit `282d5e0c84baf49c780704c532f87ba66d8b513d`,
with `--lang=smt2 --parse-only`. Each invocation uses its own process with
a 5 s wall cap, 3 s soft/4 s hard CPU limits, 512 MiB address-space and
1 MiB output-file limits. Read complete stdout/stderr and exit disposition.
The negative controls for W1, W3, W4 and W5 replace the sole assertion's
formula `t` by `(not t)`, leaving the signature and query intact. They
check a hand-derived tautology on a common slice where the tool accepts it.

| Input (SHA-256 prefix for printed text) | Reference observation and limitation |
| --- | --- |
| W1 `f532a5a94601` | C parse-only success and Z sat, complemented by Z unsat for the negated formula. Typed extract ordering and identity still need the frontend fixture. |
| W2 `0046f3c9b072` | Z sat. C rejected a name inside a binder, exit 1. The source dependency rule for the closed operand decides the Dandelion expectation. |
| W3 `fefd4458e27b` | Z rejected the argument-determined mutually recursive `node` and then printed sat, exit 1. C rejected the 2.7 wildcard field, exit 1. Replacing the application head with `(as node (Tree (_ BitVec 8)))` gave Z sat and Z unsat on negation, both exit 0. Separately replacing `(cons h _)` with `(cons h unused)` gave C parse-only success. These controls exercise different common slices. |
| W4 `e304ad0a46e9` | C parse-only success and Z sat, with Z unsat on negation. The exact negative half, retained RM child, destination and payload follow the value and node contracts. |
| W5 `fde12d8aa778` | Z sat and Z unsat on negation. C rejected the symbolic element in the constant array, exit 1. The project extension admits any well-sorted element. Z's result gives no evidence of a Dandelion array solver. |

### Additional streaming and definition observations

A later small local comparison used cvc5 1.4.2 at commit
`282d5e0c84baf49c780704c532f87ba66d8b513d`, Bitwuzla 0.9.1 at
`8d1eb01093ae54d9b4586456b69c3bf31000a4c2`, and Z3 5.3.3 at
`430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e`; the executable versions matched
the inspected source revisions. No source was modified.

- All three emitted `sat` for a small QF_BV check before stdin was closed,
  then exited without processing a malformed suffix after `exit`.
- cvc5 and Z3 accepted `(+ x 2)` in a QF_IDL `get-value`; cvc5 also
  accepted it with strict parsing. For one UF example, cvc5 accepted a
  same-model abstract value it had printed; the tested Z3 and Bitwuzla
  invocations rejected their respective printed UF values on readback.
  These observations do not characterize their entire model interfaces.
- cvc5 stores defined symbols and definitions; Bitwuzla retains lambda
  applications and eliminates them in preprocessing; Z3's macro application
  performs substitution. See the pinned [cvc5 definition](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/smt/solver_engine.cpp#L549),
  [Bitwuzla parser](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L595),
  [Bitwuzla preprocessing](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/preprocess/pass/elim_lambda.cpp),
  and [Z3 macro application](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/cmd_context/cmd_context.cpp#L1417).
  A small synthetic nested-definition example at depths 8 and 14 returned
  `sat` with all three, for both used and unused definitions. The example is
  a semantic control, not a performance comparison.
- For `(_ bv256 8)`, cvc5 and Bitwuzla rejected the value. Z3 accepted the
  tested equality to zero. The Phase 1 profile explicitly admits indexed
  numerals with reduction to the declared width.
- A later four-level version of [FE-DT-SHARING-01](../testing/frontend.md#streaming-definitions-and-model-query-cases)
  used `Dup(T)=Array(T,T)` and an argument-determined `box` constructor.
  cvc5 1.4.2 in strict mode returned `sat`, exit 0; Z3 5.3.3 reported an
  unknown constructor application, then printed `sat`, and exited 1. The
  source rank rule and the complete output determine the test expectation,
  not the trailing result. This is a syntax control, not a timing comparison.
- A separate cvc5 1.4.2 probe with `--lang=smt2 --strict-parsing --fp-exp`
  evaluated `1/3` at FP format `(3,3)` under all five rounding modes. It
  agreed with the exact neighboring values in FE-MODEL-11 and exited 0.
  cvc5 in that mode and Z3 5.3.3 also returned `unsat`, exit 0, for the
  negation of the conversion's equality with the corresponding symbolic-RM
  `ite`. These probes used a Real-bearing reference logic with no shadowing
  UF; they check only this example, not the project's model-export implementation.

The nested-definition probe used temporary external process time and address
space bounds only to keep the experiment small. These bounds are not proposed
frontend or solver resource quotas. The results do not prove complete syntax,
model or benchmark compatibility.

### Verified local copies and evidence limits

The protected tree `/pub/netdisk1/zhangx` was neither opened nor enumerated.
The mount table showed NFSv4.2 behind autofs at `/pub/netdisk1`.
Metadata-safe reads could not be established. The project target snapshot is
unavailable for direct verification. Use the already present tracked
safe copy at `/home/zhangx/solvers/SMT/z3/fstar/benchmarks/smtlib-sample`.
No dataset retrieval was needed. A Git entry alone is insufficient to
trust the working copy. The six paths below were checked as regular files
with nonsymlink path components outside the protected tree. Each byte
count and SHA-256 matched the local manifest, and each file and the
manifest matched its Git blob at `430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e`.
The manifest blob is `afea74db5b49de50540d5eaed3c30a0f48bcc061`, manifest
SHA-256 `aa75bc101baec860810afc5aac9db976dcaa822b54e00fc1f1616420968a0e86`,
last changed by `8e8bda945172a9ccbadfd13ebf13f47553102fb7`. Its source
record is the 2025 non-incremental release
[Zenodo 16740866](https://zenodo.org/records/16740866).
The manifest's `archive_path` has a `non-incremental/` prefix. The safe
copy uses the manifest's `path` without that prefix. Evidence ends at the
recorded manifest provenance and the verified local bytes. [Dataset policy](../testing/README.md#local-benchmark-data),
[topic corpus evidence](topic-research.md#local-corpus-evidence),
[Linux NFS access-time caveat](https://man7.org/linux/man-pages/man2/open.2.html)

| Verified path under the safe copy | Bytes and SHA-256 | Features and reference evidence |
| --- | --- | --- |
| `QF_BV/challenge/integerOverflow.smt2` | 1461 / `1e96ed11af58d1bcbcc957f3ad361aa84244777076aa094e8d578353486362e0` | BV32/64, definitions, extract, three assertions. Topic 9 verified Bitwuzla-printed assertion equivalence using Z3 on each assertion, with tracked limits. |
| `QF_NIA/20220315-MathProblems/STC_0473.smt2` | 625 / `d7b9cfba2a59a6b9046746f6ee14c4514b19c45462818c4489b550a31144855b` | Int constants, n-ary multiplication and equality. C accepted only in topic 9. |
| `QF_DT/20172804-Barrett/barrett-jsat/tests/v1/v1l50090.cvc.smt2` | 1024 / `a5ff74ec32890bf3087c82e0b70d82d80ce40637de72d65f392a11b71995ef8f` | Mutual recursion, selectors, testers. C accepted only in topic 9. |
| `QF_UFDT/20210312-Bouvier/vlsat3_e80.smt2` | 11208 / `5db803cb1689a5f2a51ef9961dc1456d1671d671f3d017d5a2cae6cfbfda8c38` | DT and UF, 236 assertions. C accepted only in topic 9. |
| `QF_AUFBV/ecc/com.galois.ecc.P384ECC64.ec_full_sub3.short.smt2` | 11336 / `0b8c8f0abffd34a0d4022c2dde624b316ad3a8bd2dbafcd59daa0b539e878c02` | BV arrays and wide UF ranks. C and Bitwuzla accepted only in topic 9. |
| `QF_ABV/egt/egt-0497.smt2` | 794 / `074f8b73282a77fbdd7579bec9f598235c737d82b707cd42e267497d7cdddd6d` | BV32/BV8 array constant declaration, indexed literals, selects/concat, five assertions, multiline source and exit. The smallest QF_ABV entry by manifest size and path. In this review C accepted and Z returned unsat on a scratch copy. No recovered-formula comparison for this case. |

For future opt-in checks, re-run path, size, hash and tracked-blob verification
before staging the named inputs outside the data root. Select any additions
by observed features and bounds, initially at most eight additional
named files of at most 256 KiB each. Record the sample selection and
provenance, executable and source revisions, argv, input hash,
full stdout/stderr, exit/signal, wall/CPU/output and memory limits,
and any warnings. Compare command order and per-root structural
expectations. Where both implementations handle the same syntax and
semantics, compare each selected original root with the recovered
version under corresponding declarations by asserting their XOR.
A satisfiable comparison is a witness of difference and a clean unsat
supports equivalence for that reference. Record accepted-only, accepted
with a semantic check, mismatch, unsupported, unavailable, timed out,
crashed and inconclusive separately. Error-plus-result, ignored options
or logic, and output truncation cannot pass as clean acceptance. A
status annotation cannot validate either parsing or meaning.

The safe copy and the six probes leave missing real input evidence for
IDL/LIA/UFIDL and their profile edges, FP with symbolic RM or concrete
rational conversion, and parameterized datatype matches with non-DT
fields. W3–W5 supply synthetic design cases. A corpus-compatibility
claim for those paths requires metadata-safe originals or vetted safe
copies of the specific missing families and bounded checks once the
frontend exists. Differences between archived versions cannot be inferred
from the single verified shared hash in FE-BV's research case. The
existing sample is biased toward small and unsat-annotated queries.
It does not support quantitative coverage, typical input distribution,
or throughput claims.

### Scale and measurement plan

Keep measurement opt-in after the frontend exists. Use existing Google
Benchmark integration for bounded deterministic cases at 128, 1,024 and
4,096 flat terms or declarations, 128, 512 and 2,048 nesting levels, 64
and 4,096 digits, and 32, 128 and 512 calls with repeated versus distinct
actuals. Add long refill-crossing lexemes, repeated live keys and a
late failure. Name actual measured boundaries independently for input
acquisition, lex/parse, elaboration and node construction, and complete
parse-only. Keep required outcomes live and verify observed counts.
Use separate fresh processes and in-memory repetitions with a fresh
manager per normal run. At least three bounded process samples per
configuration support initial median and spread reporting. Record tool
versions, build flags, dataset hashes, CPU/OS,
filesystem/cache conditions and raw phases, and compare equivalent output
ownership. Measure peak process RSS separately. It includes allocator
retention, input/scratch pages, nodes, GMP and library memory. Phase
counters supplement it and cannot attribute all RSS to individual phases.
The local review and audit describe baseline measurements motivating both
changes. They establish neither Phase 1 nor Phase 2 performance.
[Frontend review](/pub/netdisk2/zhangx/dandelion-frontend-bench/qf_bv_compare/review_philosophy_frontend.md)
[Contract audit](/pub/netdisk2/zhangx/dandelion-frontend-bench/qf_bv_compare/audit_frontend_contracts.md)

## Acceptance scope

The [implementation and acceptance record](implementation.md) records
checks of the registered profiles, typed DAG construction, streaming
responses, controlled model inspection, numeric helpers, and the standalone
build. The six verified safe corpus copies and bounded reference checks
supply evidence for those scripts.

A production provider must establish semantic value identity, total
interpretations, symbolic or table export, and correctness and recovery
against original assertions. The controlled provider suite exercises the
frontend protocol and lifetime boundaries. Broad corpus compatibility
requires further evidence.
