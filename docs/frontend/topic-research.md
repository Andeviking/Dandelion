# SMT-LIB frontend topic research

Status: **historical topic research**. Its observations retain the stated
versions and experimental conditions. Recommendations for whole-submission
preflight, eager definition expansion, strict assertion-shape enforcement,
free-symbol admission, rational-name resolution, model export/readback, and
frontend or numeric budgets are superseded by the
[frontend design](design.md); their original
wording below is retained as research history, not an implementation contract.
Dandelion has an implemented
typed NodeManager, exact value storage, and a build and test scaffold. The
[input profile](../architecture.md#proposed-input-profile) and
[frontend tests](../testing/frontend.md) are proposals. Topics 2–9 below follow
the [research assignment](research-plan.md). Topic 1 and the final integration
review are recorded separately in the frontend design.

## Evidence used throughout

- **Language.** [SMT-LIB 2.7, 2026-09-09](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)
  is the project baseline. [2.6, 2024-09-20](https://smt-lib.org/papers/smt-lib-reference-v2.6-r2024-09-20.pdf)
  is the compatibility comparison. Language, theory, and logic revisions
  are independent. The node profile pins [Core](https://smt-lib.org/theories-Core.shtml),
  [ArraysEx](https://smt-lib.org/theories-ArraysEx.shtml), and
  [FloatingPoint](https://smt-lib.org/theories-FloatingPoint.shtml) at
  2024-07-21, [FixedSizeBitVectors](https://smt-lib.org/theories-FixedSizeBitVectors.shtml)
  at 2025-09-09, its QF_BV extensions at 2025-02-25, and
  [Ints](https://smt-lib.org/theories-Ints.shtml) at 2026-01-16.
  The catalog separates `:smt-lib-release` from `:last-updated`.
  In particular QF_EIA has release 2026-03-13 and last update 2026-03-18.
  [Logic declarations](https://smt-lib.org/logics-all.shtml)
- **Local source and executable provenance.** All paths in the table are
  relative to `/home/zhangx/solvers/SMT` unless absolute. Short labels refer to
  the executable and mode, while a source comparison refers to the pinned
  checkout. No solver source was edited.

| Source checkout and revision | Executable and reported version | Probe mode |
| --- | --- | --- |
| [cvc5 cvc5-1.4.2, 282d5e0c84baf49c780704c532f87ba66d8b513d](https://github.com/cvc5/cvc5/tree/282d5e0c84baf49c780704c532f87ba66d8b513d) | `cvc5/build-1.4.2-static/bin/cvc5`, 1.4.2, git 282d5e0c84 | C = `--lang=smt2 --parse-only`; C-strict adds `--strict-parsing`; C-run omits `--parse-only` for selected controls |
| [Z3 z3-5.3.3, 430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e](https://github.com/Z3Prover/z3/tree/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e) | `z3/build-5.3.3-static/z3`, 5.3.3, matching build hash | Z = `-smt2`, including execution |
| [Bitwuzla 0.9.1, 8d1eb01093ae54d9b4586456b69c3bf31000a4c2](https://github.com/bitwuzla/bitwuzla/tree/8d1eb01093ae54d9b4586456b69c3bf31000a4c2) | `/home/zhangx/.local/bin/bitwuzla`, 0.7.0 | B = `--parse-only -v`, including its command and warning log |
| [STP 2.4.1, 295953fc1f7424cc4822a02f5c7964e0aafbdf10](https://github.com/stp/stp/tree/295953fc1f7424cc4822a02f5c7964e0aafbdf10) | `solvers_bin/stp`, 2.3.4, build SHA `c4b1febb8bbd3451dcf45f5e73472d66e24a1370` | S = `--SMTLIB2`, including execution |

The STP checkout had local changes outside the inspected grammar files. The
difference between the Bitwuzla and STP source and executable revisions limits
attribution of observed behavior to particular source lines.
[Probe inputs, modes, and outcomes](#appendix-bounded-reference-probes) and
[corpus evidence](#local-corpus-evidence) are listed below. The experiments
support individual observations about those versions and inputs.

## 2. Input profile and command semantics

### Logic admission and conformance

Use two checks. First resolve the exact, case-sensitive label to a registered
profile with an explicit signature, extensions and restrictions. Then check
the script against both that profile and Dandelion's expression limits. Do
not broaden the profile based on the expressions encountered. For named
logics that admit an excluded feature, a strict allowlist could reject the
whole logic and be simple to explain, but would lose compatible benchmark
inputs. Treating the label only as a hint would admit inputs outside its
definition. Prefer the intersection for explicitly registered mixed labels.
Publish a qualified claim such as the supported BV/FP subset of
`QF_BVFPLRA`; symbolic Real remains unsupported even if cvc5 accepts it.
`ALL` denotes Dandelion's supported expression profile and gives mixed
inputs a documented route when no registered named logic fits. The language
permits solver-specific logic names under §4.2.1.
[SMT-LIB 2.7 §§3.9, 4.2.1](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

| Candidate logic entries | Restrictions to record and check |
| --- | --- |
| `QF_BV`, `QF_UFBV`, `QF_ABV`, `QF_AUFBV` | Use each catalog declaration's free-symbol permissions. Pure `QF_BV` has BV constants and no arbitrary new UF signature. Both array labels restrict arrays to BV index and BV element. BV-to-Int and Int-to-BV remain conditional on the presence of Ints in the chosen signature. |
| `QF_UF`, `QF_AX` | Core with free sorts and functions for UF. ArraysEx with free sorts and constants for AX. Boolean theory symbols and declared terms must resolve even where no arithmetic theory is present. Constant arrays have an explicitly documented extension gate. |
| `QF_IDL`, `QF_UFIDL`, `QF_LIA`, `QF_UFLIA`, `QF_AUFLIA` | Encode individual catalog rules. IDL recognizes listed difference atoms over free Int constants and literal bounds, including unary-minus bounds. LIA forbids `div`, `mod`, `abs`, `/`, `**` and restricts multiplication to the declared concrete-coefficient forms. The literal wording names a free constant, so `(* 2 (+ x 1))` needs an explicit policy if admitted. Check the array-sort restriction for AUFLIA as well. |
| `QF_NIA`, `QF_EIA` | Both use Ints and have free constants. Admit symbolic multiplication in both and exponentiation in EIA. `QF_NIA` explicitly excludes `**`. |
| `QF_FP`, `QF_BVFP`, `QF_DT`, `QF_UFDT`, `QF_UFBVDT` | Explicit benchmark-profile entries are recommended. Describe the signature and restrictions of each, including where UF is allowed and the BV use in FP bit construction. These labels have competition evidence but lack individual definitions in the inspected public catalog. |
| `QF_BVFPLRA`, `QF_FPLRA` | Register as benchmark compatibility entries if the eventual registry adopts the intersection policy. Accept supported FP/BV uses with required BV bit operands, plus §7's concrete conversion forms. Reject a declaration of `Real`, an FP-to-Real operator, or symbolic Real arithmetic. Add other competition labels only with a written entry and checks. |
| `ALL` | Supported theory families and combinations from the [node operator inventory](../node/operators.md). No inference from label concatenation. Quantifiers, general Real and String, maps and global type parameters remain outside the expression profile. |

The [catalog](https://smt-lib.org/logics-all.shtml) and
[SMT-COMP submission registry](https://smt-comp.github.io/2026/solver_submission/schema.html)
serve different purposes. [SMT-COMP 2025 QF_BVFPLRA](https://smt-comp.github.io/2025/results/qf_bvfplra-single-query/)
confirms actual single-query use of that label. The test examples using
`QF_FP` and `QF_UFBVDT` need explicit registry entries or `ALL` as the
label when testing just typing. The absence of a catalog entry is not evidence
that a competition input is malformed.

Apply the logic's language restriction to the resolved expression after
transparent definitions and `let` expansion as prescribed in §3.9. Enforce
signature and project boundaries throughout declarations, definitions and
terms, including unused syntax. Use a small structural check per restricted
logic and memoize context-independent predicates on live expanded DAG nodes.
Do this before simplification erases an illegal operator or a syntactic
coefficient distinction. For example `(let ((c 2)) (* c x))` expands to the
listed LIA form, while aliasing `x` to `(+ y 1)` in that same position
does not acquire free-constant status. If broader affine input is wanted,
specify it as an extension with its own normal form and safe complexity.
[SMT-LIB 2.7 §3.9](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

In the references, Bitwuzla 0.9.1 consumes a sequence of theory-name fragments
in `Parser::is_supported_logic`. Bitwuzla 0.7.0 accepted the BV/FP probe under
`QF_BVFPLRA`, and warned on a repeated `set-logic` in verbose parse-only mode.
cvc5 1.4.2 supports logic-dependent parser state and stricter startup checks
but C-strict accepted the nonlinear LIA probe at its parse boundary. Z
diagnosed the nonlinear term and printed `sat` for the remaining script.
Those modes have different command boundaries. A recognized label or clean
parse-only exit establishes little about conformance to a named fragment.
[Bitwuzla logic dispatch](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L3132),
[cvc5 parser state](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_state.cpp),
[Z3 logic checker](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/solver/check_logic.cpp)

### Proposed command inventory

Track three pre-exit states for static validation: before logic, building
the query, and after the single query marker. The executor additionally
tracks the real result and any model it can inspect. Validate fixed arity and
token categories before profile checks when possible. A parse-only consumer
may inspect ordered command descriptors and their typed terms, including the
query marker. Executing them requires the applicable engine capabilities.
These are state requirements, and the later design owns the public command API.

| Form | Proposed admission and state effect |
| --- | --- |
| `(set-logic L)` | Once, before declarations, definitions, assertions and the query. Repeated selection is a state error even if identical. Unknown or explicitly excluded label is unsupported. Require it for a complete script by default. A compatibility entry point could choose `ALL` once on the first logic-dependent command and record that choice. Never retroactively reinterpret prior commands. |
| `(set-info :key value)` | Accept standard metadata with key-specific shapes and preserve or explicitly acknowledge uninterpreted user metadata. Check `:status` as `sat`, `unsat`, or `unknown` metadata, `:source` as the selected attribute-value forms including a quoted symbol or string, and version declarations 2.6 or 2.7. Permit a bare keyword for an admitted metadata attribute whose value is optional. Preserve positions and order, including repeats. Use the selected frontend policy for features. The version attribute records provenance and does not by itself switch lexer modes. A future explicit version may be classified unsupported instead of implying support. |
| `(set-option :print-success true)`, `(set-option :produce-models true)` and corresponding `false` forms | Initial option table should give each key its type, allowed states and observable effect. `:print-success` defaults false in the pinned revisions and should affect acknowledgements where there is an executor, including the setting command itself under the new value. `:produce-models` defaults false and is set in start mode. If no model path exists, record the requested capability and report unavailability on execution. Unsupported options such as incremental settings, proof/core production and output-channel changes need an explicit response. Never pretend a side-effecting option took effect. `:global-declarations` needs a deliberate compatibility decision even though there is only one assertion level. |
| `declare-sort`, `declare-const`, `declare-fun` | Sort-constructor arity is an exact numeral within representation limits. Function domains may be empty, all signature sorts must be resolved, and the logic must allow the declaration. Commit each name after validation of the complete command. |
| `define-sort`, `define-fun`, `define-const` | Non-recursive, check parameters and body in the old environment plus the corresponding locals. `define-const` is the 2.7 spelling for a zero-argument definition. The older `define-fun` form offers the compatible 2.6 path. Definitions affect reference resolution without adding unconstrained solver symbols. |
| `declare-datatype`, `declare-datatypes` | Admit the 2.6 declaration forms including local `par` with the checks in topic 6. Publish each group atomically to the language environment. |
| `(assert t)` | During query building. Require a closed Bool term in the profile. Retain assertion order and each occurrence even for identical roots. |
| `(check-sat)` | Exactly one for a full query submission, possibly with no assertions. Capture the current context and freeze it for the rest of that submission. A fragment API may accept declarations or terms without a query. `check-sat-assuming`, another `check-sat`, `push`, `pop`, `reset`, `reset-assertions`, recursive definitions and global sort parameters are recognized exclusions. |
| `(get-model)`, `(get-value (t ...))` | Recognize after the query marker with `:produce-models true`. Values require a nonempty list of closed, monomorphic, quantifier-free terms in request order. Full execution also needs an eligible result and model. Recommend model inspection after `sat` with a consistent retained model. The standard's sat mode also includes `unknown` and permits inspection without a satisfiability guarantee. If adopting the narrower policy, document it and diagnose unavailable models after `unknown` separately from `unsat`. Prior model-generated values in later `get-value` calls require the same-model runtime namespace or an explicit exclusion. |
| `(exit)` and EOF | `exit` is a recognized termination command even before logic in the standard. Recommend stopping command execution at its closing delimiter. A full-query call ending this way without a query reports missing query, while a stream API can report normal termination. For a submitted file, prefer requiring trivia only after `exit` to detect omitted commands and malformed tails. Stream termination may leave the remainder unread, including any bytes already buffered. EOF after a complete query is normal with or without `exit`. |
| Other recognized commands | Give sound syntax diagnostics for admitted forms and explicit unsupported responses for excluded forms such as `get-assignment`, proof/core commands and solver-specific commands. An explicit command registry can recognize extensions without silently dropping them. |

Use distinct outcomes for malformed syntax, well-shaped but invalid terms
or state transitions, recognized input outside the selected profile, I/O
failure, diagnosed resource failure, and normal termination. Only an
executed, accepted query may produce solver `unknown`. An invalid trailing
command must remain visible even if streaming execution already produced a
result for the query prefix. A full-file preflight can instead reject the
script before execution, with runtime model references deferred as required.
[SMT-LIB 2.7 §§4.1–4.2](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

**Verification cases.** `set-info :source "a; (b)"` before `set-logic` is
metadata, while `(assert (= "a" "a"))` uses the excluded String theory.
Use `(* (- 2) x)` and `(* x x)` as positive and negative LIA controls,
then move the latter to NIA and check its Int result sort. Pair `(< (- x y) 3)`
in IDL with `(< (+ x y) 3)`. Pair `(** 2 x)` under EIA with the same
operator under NIA. Pair nested arrays under `ALL` with a BV array whose
element is an array under `QF_AUFBV`. Test the model option before and after
logic, empty value requests, two valid inspections against a single model,
and a subsequent assertion or second check. The trace must retain option
and model-request ordering even in a solver-free test. Probes P01–P09 and
P35–P36 cover absent, unknown and repeated logic, a malformed trailing
command, and the selected 2.7 constant-definition spelling.

**Decisions needed later.** Freeze the registered logic table with actual
signature rules, including the competition profiles, the exact strict versus
compatibility boundary, and rules for applying project extensions under
named logics. Keep static model-query checks even if the driver initially
lacks model execution. Settle input-wide preflight versus streaming execution,
post-`exit` tail handling, `unknown` and abstract model values as part of
the command/result and ownership design.

## 3. Lexing, syntax, and input processing

The SMT-LIB token grammar separates byte/character recognition from term
meaning. A quoted symbol is distinct lexically from a bare keyword or
reserved word. Symbol resolution later equates `x` with `|x|`. Treat
quoted spellings of reserved words as symbols, preserving the token class
to distinguish `|let|` from the binder marker. Numerals have no sign or
multidigit leading zero. Decimals have an integer part with the standard
decimal grammar and a nonempty fractional part. `-32` is a symbol and
`(- 32)` is a term. Binary and hex payloads require at least one valid
digit after `#b` or `#x`. The token class and raw range should survive
until the syntax and value layers have checked it.
[SMT-LIB 2.7 §§3.1–3.6](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

### Alternatives and baseline

| Approach | Useful property and cost |
| --- | --- |
| Handwritten scanner and explicit parser frames | Gives direct control over limits, lexeme spans, byte I/O, and ordered name events. Requires explicit grammar-state tests for every frame and EOF position. |
| Generated scanner and LR grammar | A grammar file centralizes admissible forms and a generator handles parser states. STP's Flex/Bison grammar carries AST values in semantic actions. Still needs length and stack limits, explicit ownership and cleanup on errors, and deliberate decisions about when actions publish names. Adds a build dependency or generated artifact policy. |
| Whole script of generic S-expressions | Easy to capture source and use as an independent debug reader. Retains input and syntax even when no later consumer needs them. Generic lists defer contextual errors and can duplicate traversal and allocation. |
| Direct construction from token stream | Can release most transient data early and is useful where sorts and bindings are known at each reduction. Deferred types, postorder label visibility, and reusable source provenance require pending state or a later pass even on this path. |

Recommend a handwritten scanner and explicit grammar-aware frames, with
flat transient records for one command or subexpression where later typing
needs them. A frame records its form, source start, next expected slot and
the base of a shared child list. Complete a form on its closing delimiter,
then feed its index or resolved value to the parent. Use distinct frame
states for a term head, arguments, sort head and arguments, each binder
group, the scrutinee and pattern/branch pairs, a command's argument slots,
and generic attribute S-expressions. The latter can contain reserved words,
strings and lists. Store ordered ranges and child intervals in arenas or
index vectors. Implement iterative validation, elaboration and cleanup
for deep sorts, terms, annotations, and errors. A token stream alone cannot
distinguish `(_ is cons)`, `(_ extract 3 0)`, `(as nil (List Int))` and
ordinary application without looking at the form and argument positions.
Never flatten generic lists into command tokens or publish partial commands.

cvc5 1.4.2 separates lexer, command parser, term parser and semantic state.
Its term parser uses a parse stack and explicit states. Z3 5.3.3 has
expression frames, but `push_match_frame` calls `parse_expr` for the
scrutinee and branches. An explicit frame loop alone does not prove every
input form is recursion bounded. Bitwuzla 0.9.1 uses work and control stacks
and `close_term_letbind`/`close_term_parletbind`; these are examples of
ordered reductions. STP's grammar raises `YYMAXDEPTH` to 104857600, a
different resource policy from a byte/depth budget. The suitable lesson is
to measure per-frame cost and bound every nested path, including semantic actions.
[cvc5 lexer](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_lexer.cpp),
[cvc5 term parser](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_term_parser.cpp#L96),
[Z3 match parser](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/parsers/smt2/smt2parser.cpp#L1403),
[Bitwuzla term work stack](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L1504),
[STP grammar](https://github.com/stp/stp/blob/295953fc1f7424cc4822a02f5c7964e0aafbdf10/lib/Parser/smt2.y#L298),
[Flex manual](https://westes.github.io/flex/manual-single.html)

### Scanner and ownership

- Use distinct modes for ordinary scanning, a line comment, a quoted symbol,
  and a string. Outside quoted modes semicolon starts a comment with CR or LF
  ending the line and EOF ending a final comment. Parentheses, semicolons and
  double quotes inside `|...|` are ordinary data, while backslash is invalid.
  In a string, two adjacent double quotes decode to one quote and backslash
  is ordinary data. At a candidate closing quote, look one character ahead
  across a refill before committing. EOF after a candidate closing quote
  completes that token. EOF while still inside quoted data reports
  truncation. A pending CR and a partially read
  multibyte character must also survive refill. Classify bytes independently
  of process locale. For byte sources, choose validated UTF-8 as the concrete
  encoding, enforce the revision's allowed characters in each context, and
  keep simple symbol syntax ASCII. Document BOM behavior and add a fixture
  for it before claiming support.
- Prefer a borrowed immutable contiguous buffer for callers that already own
  one and a reusable chunk buffer for files and streams. Tokens reference the
  current window until refill. Copy only bytes needed across a refill or kept
  past command consumption. Use chunk append or amortized scratch growth for
  a long lexeme, with a length limit before extending it. Capture canonical
  symbol text in an owning intern table and decode metadata on demand into
  owned storage. A command may pin its own slices or store copied lexemes
  needed for typing. A source occurrence stores a source identifier and
  checked half-open byte range, not a view into the refill buffer. If
  excerpts must survive, retain a bounded owned excerpt or an explicitly
  owned source. Keep canonical text independent of original spelling.
- Track absolute byte offsets and line beginnings. Define a diagnostic
  coordinate convention, for example one-based lines and byte columns with
  CRLF counted once, independent of chunk partition and host locale. A
  running line/column plus command-local line starts avoids keeping a
  whole-file index when legacy clients only need the first error. Record the
  opening range for unclosed forms and the EOF position. Distinguish a short
  successful read, EOF, and read error. On I/O failure mid-token report the
  I/O error even if the prefix could separately be malformed. Never accept a
  truncated script by treating read failure as EOF.
- Stop on the first fatal lexical, syntax or input failure in a submission.
  Do not search for a later top-level command after an unclosed string.
  A future recovery API can resynchronize only with lexical state and
  command rollback, and must explicitly report every failure. Scan cost is
  linear in input bytes, with memory for the buffer, simultaneously needed
  lexemes, parser depth, current command records and retained occurrences.
  Construction and type work require separate bounds.

**Verification cases.** Use P10 and P11 below and the existing
[FE-LEX-001](../testing/frontend.md#fe-lex-001-quoted-names-and-metadata).
With a mock reader split between each pair of bytes in a short script,
compare decoded text, token types and ranges to contiguous input. Include
splits inside `""`, CRLF, UTF-8, `#x0F`, and comment terminators. Check
`#b`, `#b102`, `#xg`, an illegal quoted backslash, embedded NUL,
`(assert true` at EOF, and a valid comment at EOF. Use separate assertions
for a malformed numeral shape, such as a width token spelled `08`, and a
well-shaped zero width. Simulate I/O failure one byte before a string closes.
The lexical and command observations should agree for every chunk split
that reaches normal EOF. P34 probes 2048 nested `not` forms in the
reference modes. Future local tests should also nest sort applications,
matches, alias substitution, and malformed cleanup at the configured limit.
An input over a configured depth limit returns a resource diagnostic.

**Dependencies.** The command consumer decides when command records can be
released and what occurrences or original spelling it retains. Topic 4 must
receive binding and annotation events in source order, even when type
resolution defers an enclosing application. Topic 9 owns buffer sizing and
resource accounting policy. Decide the file-tail rule with topic 2 before
testing post-`exit` reads.

## 4. Symbols, scopes, definitions, and annotations

### Environment and bindings

Use owning canonical symbol keys with distinct sort and term namespaces.
Preserve spelling and source range on occurrences. Reject duplicate
user-declared or defined names within the appropriate command namespace,
including constructors and selectors in the same datatype group and
aliases introduced by `:named`. The global namespace of declarations
is separate from lexical bindings even when they resolve to the same
printed name. Apply the active signature's prohibition on shadowing theory
symbols to local term and sort binders. Keep syntactically reserved tokens
distinct from theory names, which come from the selected signature. For
example a UF name should not be rejected merely because its spelling is
a builtin in a different theory. Check same-group duplicates after
dequoting. Node declaration IDs identify declarations independently of
their name hints.
[SMT-LIB 2.7 §§3.1, 3.6.2, 4.1.5–4.1.6, 4.2.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf),
[Dandelion identity and declaration contracts](../node/design.md#3-sorts-and-declarations)

A stack of small maps is easy to implement and useful for command staging, but
every reference can pay for a deep search. Prefer a hash map of currently
visible local binding heads keyed by interned names, with a frame marker
and an undo stack of replaced head values. Lookup checks locals, the
command overlay and the committed term environment. This gives expected
constant-time access after interning and linear work in the number of
bindings on entry and exit. Account for symbol bytes during interning and
use full equality after hashing. Values own the relevant identities,
sort recipes or nodes. Store stable indices or handles across growth.

For `let`, parse and resolve RHS expressions left to right in the old
lexical frame, allowing earlier legal `:named` aliases to affect later
source positions. Reserve the group, check its names, then install all
`let` bindings simultaneously for the body. Pop them as a group at the
body's end. Share each resolved RHS node at each use. Keep a source binding
ID and link to its RHS even when the typed output is already substituted.
Definitions introduce fresh formal IDs in a body frame. Do not publish
the defined function in its own body environment. Bind formals only where
the grammar gives them scope and check the resolved body sort against the
declared result. A sort alias uses a flat acyclic recipe with local slot
IDs and resolved constructor identities, and simultaneous sort substitution.
A local parameter may shadow a user sort. No alias introduces another
interned semantic sort identity.

### Definition instantiation

Compare three choices. Rebuilding an unshared source tree on every call
duplicates work and complicates capture avoidance. Keeping macro application
nodes or delayed substitutions can save expansion, but requires a new
representation or changes what later components accept. Use the current
contract's checked DAG substitution with fresh formals as the initial path:

1. Keep an owning typed body, ordered formal declaration identities, and
   the signature outside `ManagerState`. Resolve and check arguments once
   at the application. Body construction has already resolved its global
   names to identities.
2. Make a fixed map from formal declaration instances to owning actual
   nodes. Walk the template's DAG postorder with a memo from template
   node ID to an owning instantiated node. A formal leaf returns its actual
   without walking into that actual. Reuse an unchanged node and rebuild a
   changed parent with its original kind, indices, payload and new ordered
   children through checked factories. Skip components known to contain no
   replaced formals when that summary is cheap enough to retain.
3. Discard the substitution map and memo at call completion. Cost is one
   visit per reachable template node and edge in that call plus rebuilding,
   hashing and payload costs. The same subgraph is shared within a call and
   identical live results can share through NodeManager. Independent calls
   with different arguments can still have a large total expansion cost.
   A whole-call cache keyed by definition identity and ordered actual node
   identities might save repeated identical calls. Initially use per-call
   memoization and measure repeated-call cost before retaining a bounded
   shared cache with clear lifetimes.
4. A template may contain its own formals. A call inside another definition
   may yield that definition's formals through the caller's actuals. Enforce
   that each closed assertion and solver-bound root has no reachable
   `kFormal`, using the construction invariant and a boundary check if the
   interface allows otherwise unrestricted node input. A failed body or
   instantiation propagates a diagnostic and releases staged owners.

Z3 represents bound variables by indices and shifts indices when needed, an
effective mechanism when its expression language can retain quantifiers.
Bitwuzla's 0.9.1 parser stores a non-nullary definition as a lambda term
and builds application terms at uses. Beta reduction is available in the
function solver. That parser has no parameterized
`define-sort` in the inspected source. cvc5 distinguishes parser scope from
command invocation and defines `:named` inline to make later uses in the
same command visible. Its explicit error for a name inside a binder rejects
P17, a closed literal inside a let, along with P18, a reference to an
outer let variable. P15 also shows that acceptance of duplicate binders
varies. Fresh identities and local snapshots fit Dandelion's first-order
DAG without requiring a new bound-variable encoding.
[Z3 parser scope and definition code](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/parsers/smt2/smt2parser.cpp#L1828),
[Bitwuzla definitions](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L595),
[Bitwuzla beta reduction](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/solver/fun/fun_solver.cpp#L214),
[cvc5 inline naming](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_state.cpp#L1921)

### Inline names, closure and rollback

Treat `:named` as a zero-argument definition for the annotated subterm,
including a non-Boolean subterm. Check the key's value as a symbol and
check freshness in the signature and the command overlay. Record the
binding event when the annotated term completes in depth-first,
left-to-right postorder. A later sibling and a later command may reference
it, while an earlier sibling may not. Nested lexical bindings retain
normal shadowing. Ordinary nonsemantic attributes use an explicit
preservation policy on the source occurrence, with chosen attribute-value
shapes. Exclude `:pattern` explicitly under the quantifier-free profile.
Keep each occurrence's node owner, range, and attributes outside the
interned node, according to the current frontend boundary.

Compute closure for the annotated subterm before erasing lexical
information. Declared constants and already validated named aliases are
closed. A reference to a variable bound outside the annotated subterm is
free even when the surrounding `let` RHS is a literal. A local `let` binding
inside the annotated subterm contributes its RHS dependencies only when
the body depends on the binding. Implement this with an iterative
free-dependency walk on resolved source identities. In a walk for one
annotation, inspect the body of a `let` and follow a binding's RHS when
the binding is used, using the RHS's resolved outer environment. Use a
per-check visited set. Treat a branch pattern as introducing identities
only in that branch. Collect closure separately from syntax, scope and
type checking, which still visits unused RHSs and unreachable branches.
Budget total dependency work across multiple annotations. A single
bottom-up minimum over all lexical references would reject names whose
only outer reference occurs in an unused RHS. This is a detail to record
in the [node frontend boundary](../node/implementation.md#8-frontend-and-model-boundaries)
and future tests when the design is updated.
[SMT-LIB 2.7 §§3.6.2, 4.1.6 and Figure 5.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

Keep newly declared names, labels, root additions and occurrences in a
command-local overlay. Reserve or construct the final entries and commit
only after the delimiter and all semantic checks. On failure discard the
overlay and unwind lexical scopes to the command mark. This preserves the
observable signature and roots. Ordinary factories can already have
interned sorts or consumed declaration and formal IDs in the manager.
The [node ownership and error contracts](../node/design.md#7-errors-and-factory-guarantees)
provide no general rollback of those stores. If an API permits recovery,
budget those records and avoid promising numeric ID continuity. Fresh
query state is a straightforward cleanup path for a terminal submission
error. A datatype builder's stronger transactional guarantee and the
frontend's name publication need coordinated staging as described in §6.

**Verification cases.** Check P12–P18 and the project
[parallel let example](../testing/frontend.md#fe-bind-001-parallel-let-and-shadowing).
For `(declare-const x Int) (define-fun f ((y Int)) Int (+ x y))`,
`(let ((x 5)) (f x))` refers to the declared global `x` and uses `5` for
the formal. Exercise the converse capture case by passing a caller's formal
as the actual inside a second definition. A self-reference through an
unbound function name fails. For one closedness check inside an outer
`(let ((x 1)) ...)`, name `(let ((unused x)) 0)` and compare with naming
`(let ((used x)) used)`. The second subterm depends on the outer variable.
An annotation around a closed literal in a match branch is legal if its
name is fresh. Use a repeated label, a collision with an existing
declaration, and a malformed command after a successfully processed label.
In the last case a state fixture should show no published label. Check
later references by binding identity, including when distinct source
occurrences share one node.

**Dependencies.** Topic 5 must resolve each annotated child's type before
uses need a concrete sort, or keep a single pending type slot tied to the
binding event. Source events remain ordered even if type work is deferred.
Decide whether names on result-dependent read commands such as `get-value`
are supported. Standard inline definitions apply broadly, whereas such
commands need the checked model's signature and may contain runtime-only
values. A conservative profile can explicitly exclude this intersection
until its lifetime and state effects are defined.

## 5. Sorts, type resolution, and operators

### Resolve ranks before construction

The [NodeManager contract](../node/design.md#3-sorts-and-declarations) has
concrete published sorts. A sort's source syntax resolves in the sort
namespace, checks constructor arity and indices, expands aliases, and
interns the concrete result. `Bool`, `Int`, `RoundingMode`, indexed BV
and FP, the FP format aliases, `Array`, fresh uninterpreted sort constructors
and committed datatype constructors use the identities in that contract.
Sort variables exist in local recipes for aliases or datatypes. A rank's
parameter substitution maps only the current recipe's parameter slots.
No unresolved slot is published as a sort.

For common monomorphic heads, resolve directly. Check shape and child
sorts using an ordered argument descriptor. For overloaded and
parameterized heads, consider three ways to resolve:

| Approach | Consequence |
| --- | --- |
| Bottom-up argument sorts, explicit result qualification | Matches the standard's rule for ambiguous heads, reduces candidate search, and gives local diagnostics. Requires explicit `as` at intrinsically ambiguous applications. |
| Bidirectional check and synthesis | An explicit qualifier or known argument type simplifies constraint propagation and type diagnostics. Propagating expected result types can also support contextual inference as a separately specified extension. |
| General finite constraint solving over unresolved occurrences | Can support combinations of nested overloads and expected sorts, but increases retained syntax, ambiguity reporting and worst-case search. It is unnecessary for most monomorphic terms and for purely argument-determined parametric ranks. |

Recommend a fast bottom-up path plus a small rank-matching worklist
for parameterized and qualified heads. Resolve a source head into a
bounded set of candidate schemas by namespace, arity and index shape.
Bind parameters by structurally matching argument sorts to domain
patterns. Check equal slots by interning identity once concrete. If a
template pattern contains a sort parameter, record the first binding and
check subsequent occurrences. Eliminate inconsistent candidates. An
explicit `(as id Sort)` unifies the result with that sort. Without it,
check whether the argument tuple itself determines a unique rank and
result. A contextual expected type can check the resolved result or be
used when an explicit compatibility policy permits it. It does not
introduce an implicit numeric, Bool/BV or signedness conversion.
Retain inferable cases such as `cons` with a qualified tail. A cross-sort
`ite`, bad qualifier, or failure to construct a valid rank is ill-sorted.
For diagnostic and resource accounting use count of candidate checks and
sort-pattern edges visited. Memoize repeated comparisons on shared sort
identities where needed. Bound search before materializing candidate
nodes if an optional extension introduces combinatorial choices.

**Contract finding.** The [frontend boundary](../node/implementation.md#8-frontend-and-model-boundaries)
allows sufficient context or a qualifier for an ambiguous polymorphic
constructor. SMT-LIB 2.7 §3.6.3 and Figure 5.3 require a qualifier for an
intrinsically ambiguous function symbol with the given argument sorts,
except in a match pattern. `(= xs nil)` with `xs : List<Int>` is the
smallest relevant example. The admitted strict form is
`(= xs (as nil (List Int)))`. In P25–P26 cvc5 required the qualifier and Z
accepted both. Recommend explicit qualification as the baseline and a
separately labeled contextual-inference extension only if worthwhile.
The affected contracts are the frontend-boundary wording and the later
profile and tests. The concrete node representation already supports
either admission rule.
[SMT-LIB 2.7 §§3.6.3, 5.2 and Figure 5.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

cvc5's `ParseOp` applies result ascription and resolves special overloads
before using its term manager. Z3's `cmd_context::mk_app` tries builtin,
declaration, macro and parametric constructors through a richer application
interface. The parametric-path implementation matches known argument sorts
to templates, and its parser can accept a contextual constructor occurrence
as in P26. Bitwuzla demonstrates specialization for projected BV/FP
theories, including signature-based `to_fp` variants. These are examples
of where to put resolution, with different admitted languages.
[cvc5 ascription and application](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_state.cpp#L1125),
[Z3 application dispatch](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/cmd_context/cmd_context.cpp#L1437),
[Bitwuzla indexed term parsing](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L1769)

### Coverage map to checked node construction

Use the [operator inventory](../node/operators.md) as the full list of
source schemas and shapes. The table gives the frontend action for each
section and exceptions that need an explicit dispatch path. Each schema
is gated by topic 2's profile and uses ordered children throughout.

| Authoritative rows | Resolved construction and frontend additions |
| --- | --- |
| [§2 values and applications](../node/operators.md#2-values-and-declaration-applications) | Dedicated factories produce `kBoolValue`, `kIntValue`, `kBvValue`, `kFpValue` and `kRoundingModeValue` with exact payloads and the appropriate concrete sort. `MakeApply` carries a resolved `DeclInstance` and domain-ordered children, empty for a constant. An alias or substituted parameter returns the owned value. A source declaration with no arguments is distinct from a fresh internal symbol. |
| [§3 Core](../node/operators.md#3-core) | Map the listed families to their corresponding builtin kind, with no index or extra payload. `kEqual` and `kDistinct` compare same-sort operands and return Bool. Keep them n-ary. `kImplies` is right-associative, comparisons of adjacent equals are chainable, and distinct is pairwise in meaning. Recognize the explicit zero/one-argument compatibility cases for `and` and `or` before calling the minimum-arity factory. |
| [§4 Ints](../node/operators.md#4-integer-arithmetic) | Unary and n-ary minus select `kIntNeg` and `kIntSub`. Other arithmetic and predicates use the exact arity, operand and result schema. `kIntDivisible` carries exact positive `n` as its index. Retain `div`, `mod` and `**` as operations with their specified semantics. Apply logic restrictions before general rewriting. |
| [§5 BV](../node/operators.md#5-fixed-size-bit-vectors) | All operators use the listed `kBv*` kinds, except the listed Int conversions. Use exact integer indices for extract high then low, repeat, extends, rotates and `int_to_bv`. Qualify widths only where syntax calls for them. Retain order for concat, subtract, shifts, divides and signed operations. `bvcomp` returns BV1. Comparisons and overflow predicates return Bool. Resolve legacy conversion aliases explicitly. Reject `bv2int` under this profile and limit conversion use by signature. |
| [§6 FP and rounding modes](../node/operators.md#6-floating-point) | Map short and long RM values and FP sort aliases to canonical sorts and values. Use `kFpConstruct` for `fp` and retain three children with sign, exponent and trailing significand widths. Use the exact listed arithmetic, predicates and comparisons. For `to_fp`, one BV child selects `kFpFromBits`. In two-argument source forms the second term can select `kFpConvert` or `kFpFromSbv`. The restricted rational grammar selects `kFpFromRational` with indices, one RoundingMode child and an exact rational payload. `to_fp_unsigned` selects `kFpFromUbv`; FP-to-BV variants carry the target width. |
| [§7 arrays and DT](../node/operators.md#7-arrays-and-datatype-applications) | `select` and `store` use concrete array index and element sorts. `kArrayConst` has one child of the element sort and a full Array result qualifier. `kApply` for constructors, selectors and testers uses the instantiated declaration and ordered children. `(_ is ctor)` resolves a symbolic index to a tester and passes no numeric index to the factory. `match` lowers using §6. |

No source spelling should request `kInvalid`, `kInternalFunction`, a
raw `kFormal` declaration, or an arbitrary `DeclInstanceId`. Formals are
frontend-created during definition checking. Raw `MakeFpBits` value
construction is programmatic, while source `fp` and `to_fp` forms retain
operation nodes even with constant children. General `fp.to_real`,
`fp.to_ieee_bv`, higher-order application, and String operators have no
mapping in this profile. Annotated, bound and defined source terms do not
imply extra node kinds.

Keep logic admission, resolution of source shape/overload, and
source-specific coercion or extension policy in the frontend. Delegating
the instantiated builtin contract to `MakeBuiltin` avoids keeping a
second table of all final arity, width and payload rules. Where overload
resolution needs shape predicates or a result sort before it has concrete
children, reuse the node rule descriptor or a pure type query if a later
design exposes one. All public checked node calls must still validate their
own arguments. Map error operand/parameter indices to source spans. Use
`MakeApply` to validate the concrete signature, and retain the meaning of
wrong-constructor selectors as defined. A factory failure is never evidence
of a solver result.

**Verification cases.** `(assert (bvcomp #b0 #b0))` is ill-sorted, while
`(assert (= (bvcomp #b0 #b0) #b1))` has a Bool root with a BV1 child.
For BV8 extract test `(3,0)`, `(0,0)`, `(8,0)` and `(3,4)`
as high/low pairs. Check that the two valid result widths are four
and one. Unary resize with width zero and a mathematically valid width
past the representation limit have different outcomes. Compare
`(= (_ +zero 8 24) (_ -zero 8 24))` and
`(fp.eq (_ +zero 8 24) (_ -zero 8 24))` as distinct typed operators. The
FP theory gives different truth values, but the frontend output need
only retain the operator and operands. Compare a signed-BV `to_fp` with
the unsigned spelling and test an Int/BV array qualifier such as
`((as const (Array Int (_ BitVec 8))) #x00)` with a matching and a mismatched
index and element. Pair a nonlinear product with its rejection under LIA
(P05), and check a constructor's wrong explicit qualifier separately
from missing qualification.

**Dependencies.** Freeze the semantic descriptor available to the frontend
with NodeManager owners. Specify where to check logic restrictions in
relation to alias and match lowering. Reconcile whether indexed standards
and extensions include any tags beyond the operator document. In particular
Z, C and B accepted three-argument `concat` in P22. The selected BV
declaration and node inventory give binary `concat`; use binary in the
baseline and require explicit evidence and a profile amendment to add an
n-ary source form.

## 6. Datatype declarations and match

### Declaration groups and concrete instances

SMT-LIB 2.6 already has inductive datatypes, parametric declarations and
the match construct. SMT-LIB 2.7 adds the reserved wildcard spelling
`_` to patterns. Constructor and selector names introduced by the
standard commands are fresh function symbols. A datatype may refer to
previous sorts and to group members at the top of a field sort, including
mutually recursive references. The restriction is applied to declared
field types with aliases expanded. The selected node contract implements
group staging, parameter-scoped sort recipes and productivity checks.
[SMT-LIB 2.6 §4.2.3 and 2.7 §§3.6.2, 4.2.3, 5.2–5.3](https://smt-lib.org/language.shtml),
[Dandelion datatype builder](../node/design.md#4-datatype-group-transactions)

1. Desugar the one-datatype command to a group. Prebind all fresh sort
   names as group slots with arities. Verify a positive group size,
   equal header/body counts, `par` length and unique parameters, and
   nonempty constructor lists. Validate sort and function namespaces
   and all group names, including dequoted spellings. Collect constructors
   and field selectors in source order. Treat testers as semantic links
   associated with constructors rather than declaring unrelated bare
   names such as `is-cons`.
2. Resolve each field with the corresponding local `par` parameters,
   staged group slots and previous sort declarations. Expand aliases
   before classifying recursion. Translate to the builder's
   `Concrete`, `Parameter`, `Array`, `Apply` and `Recursive`
   recipes. A direct field `(D (Array Int A))` in parametric `D`
   can be valid. A nested field `(Array Int (D A))` is excluded,
   as is `(D (D A))`. Test alias expansion in both directions. A
   parameter `A` can later be instantiated with a concrete datatype
   without changing the syntactic recursion check on the declaration.
3. Validate productivity via the node builder. The least fixed point starts
   with constructors that require no group fields. Mark a datatype when
   any constructor can be built using productive field types. By induction
   on constructor-tree height every marked member has a finite
   witness, and a remaining member has no finite constructor term
   under the declared restriction. Productivity is independent of
   expanding an infinite set of concrete instance sorts. A linear worklist
   with unmet dependency counts per constructor and reverse adjacency
   is an optimization option if large groups matter. The current
   implementation iterates to a fixed point, with worst-case work
   proportional to the group size times the recipe and field work
   for the relevant passes. Delegate the semantic check instead of
   duplicating it in the frontend.
4. Pre-stage name entries and all allocations needed to publish them.
   Call builder `Commit()` only after syntax, scope and staging checks.
   After it succeeds, fill and publish the staged frontend entries through
   operations whose failure behavior is established. The builder guarantees
   a failed commit consumes no group IDs, but does not install frontend
   names. A fallible frontend insert after commit could leave committed
   records with no names. A terminal submission can release its state on
   unrecoverable failures. If reusable command recovery is required, verify
   that post-commit publication can actually be made nonthrowing or define
   the exact retained state, including sort/declaration ID consumption.
5. Constructors, selectors and testers use concrete `DeclInstance`
   domain and range under the relevant parameter substitution. Resolve
   constructor arguments and explicit ascriptions through §5. Resolve a
   selector or symbolic tester index from the scrutinee's concrete datatype
   instance. Validate that the constructor belongs to that datatype and
   use the instantiated field types. Never recurse through the definition
   of a datatype just to resolve an instance.

Eager monomorphization cannot enumerate formats beyond the actual
dependencies, especially when recursion changes the type arguments.
Deferred group recipes and touched instances fit the current manager.
Bare name and arity checks alone lose declaration identity at a mixed UF/DT
boundary. Constructor-name overloading for convenience can be found in
reference implementations. It adds collision and typing rules and should
not be inferred from node declaration name hints. Dandelion's builder is in
`src/node/datatype_builder.cc`.
[cvc5 datatype declaration parsing](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_term_parser.cpp#L1097)

### Pattern typing and lowering

A scrutinee must have a concrete datatype instance. In its constructor
namespace a bare nullary constructor denotes that constructor, while
a bare variable pattern binds the scrutinee as a default. Check the
arity of a constructor pattern. In `(c x1 ... xn)` the field positions
are variable bindings, even when the spelling of an already declared
constructor appears there. Reject nested patterns and duplicate names
after dequoting, except for repeated wildcard tokens. A bare pattern
spelling of a known non-nullary constructor should report missing
pattern arguments, as in the inspected cvc5 and Z3 readers. Record
this as a profile rule for the collision between the variable and
constructor interpretations of a bare symbol. For `_`, introduce no
visible binding. It stands
for an unused fresh variable at each occurrence. A quoted `|_|` is a
symbol, so it can bind a variable or refer to an applicable declared
constructor. Bind each branch only within that branch, use its concrete
field sorts, and type-check every branch at a common sort. Errors in
a branch after a default are still errors. Track constructor coverage
with a bitset or marks keyed by constructor ordinal. Require complete
coverage or a default. Repeated branches are legal and matching order is
first applicable branch, including an early variable or wildcard.
[SMT-LIB 2.7 §§3.6.2, 5.2 Figure 5.3, 5.3 Definition 11](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

After all branches and annotation effects are checked, lower from the
right to the left to typed testers and `ite`s. For each constructor
branch bind its used variables to selector applications on a shared
scrutinee. A named default variable binds that same scrutinee. Use the
last branch as the fallback because of checked exhaustiveness. A preceding
default replaces the accumulated suffix, preserving the semantics that
later branches never win after that default. Use one selector result per
used field on that scrutinee. For each earlier applicable constructor
branch, build an `ite` guarded by its tester. The guard protects use of
the associated selectors, whose values outside their constructor are
underspecified. The skipped test on the last branch is justified because
every scrutinee reaches some matching branch and reaching the last entails
its pattern when there is no earlier default. Preserve source branch
occurrences as needed for diagnostics or APIs. Extra work beyond branch
typing is linear in pattern slots, encountered constructors, and
constructed tester/selector/ite edges, plus any per-branch substitution
walk required by the typed representation. Node sharing avoids duplicating
the scrutinee.

Z3 5.3.3 elaborates matches using constructor bindings, substitution and
right-to-left `ite` construction. cvc5 builds typed match-case terms and
a match node. Retaining such nodes would be useful for a source-oriented
API or solver-specific rewriting, with an expansion and processing
contract for later consumers. It adds a term representation Dandelion's
node inventory currently does not use. C and Z accepted the repeated
branch probe P28, with Z's grounded result confirming first-branch
selection for that input. C and C-strict rejected the 2.7 field wildcards
in P27, while Z accepted them. C and Z accepted P31, the nested-array
recursive declaration outside the chosen profile. These acceptance
results do not establish a particular nested-datatype semantics or
completeness. Hakak et al. show how arrays can hide cycles in a naive
combination, with a datatype value reappearing by selecting from its
own field. Their separate theory and reduction explain why supporting
nested recursion would require more than widening the parser.
[Z3 compile_patterns](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/parsers/smt2/smt2parser.cpp#L1464),
[cvc5 match-pattern parsing](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_term_parser.cpp#L1390),
[Hakak et al., Automated Reasoning with Nested Datatypes, v2](https://arxiv.org/abs/2606.30888v2)

**Verification cases.** Use P25–P33. Extend P25 with
`((_ is cons) xs)` and `(hd xs)`, checking tester range Bool and
selector range Int even when `xs` is `nil`. An unconstrained test
`(= nil nil)` requires two qualifications in the strict grammar.
For mutual recursion use `T = node(L)`, `L = nil | cons(T,L)`.
Removing `nil` removes every finite witness. Rename one selector to an
existing name, mismatch a header arity against `par`, or make an alias
`Wrap<X> = Array<Int,X>` and use `(Wrap D)` while defining `D`. Verify
no group names become visible on failure. For ordinary mixed terms,
declare a datatype with an array of already-declared datatype values or
a BV field, use its selector in a UF rank, and mutate one field or UF
argument to a mismatched sort. For patterns, compare a bare branch
variable with the same spelling as an unrelated outer name, repeated
wildcards with a duplicate named field, and repeated constructor branches
with a nonexhaustive set. A branch after `_` is type-checked, even if it
cannot win. All outcomes follow signature rules, coverage and the
ordering semantics above.

**Dependencies.** Qualification policy applies to term uses, not to
patterns resolved using the scrutinee sort. Verify diagnostic ownership
for builder errors and scope/annotation ordering from §4. The scope, type
substitution and productivity checks establish frontend validity. Solver
model construction for arrays of datatypes, wrong-constructor selectors
and changing datatype parameters belongs to datatype and theory
combination work.

## 7. Numeric literals and value boundaries

### Exact decoding and construction

Use the revision's grammar to recognize source numbers, then make a
checked exact conversion. `Integer::ParseDecimal` currently accepts
digits with an optional minus, including leading zeroes and `-0`.
It uses a signed 64-bit fast path and a GMP-backed fallback.
It is a numeric API, so its accepted strings are broader than SMT-LIB
numerals. A negative numeral is the source term `(- n)`; when the
resolved operator is Int unary minus applied directly to a numeral,
it may be represented by a canonical negative Int literal. Retain
the source shape when needed for logic checks, such as a LIA coefficient.
Keep other unary minus applications as operations. No unary plus or
C-style signed numeral token is implied by the numeric API. The integer
implementation is in `src/util/integer.cc`.
[Integer contract](../util/integer.md),
[SMT-LIB 2.7 §3.1 and Ints](https://smt-lib.org/theories-Ints.shtml)

| Input | Construction and invariant |
| --- | --- |
| Unbounded Int numeral | Use `Integer::ParseDecimal` on digits that passed the lexer. Preserve the exact signed value when using the canonical negative-literal path. No host-word limit on the value. |
| `#b...`, `#x...` | Width is respectively the digit count or four times the count, with leading zeroes included in width. Decode the exact magnitude and pass an owning temporary's view to `MakeBvValue` with the resolved sort. Hex digits are case-insensitive and the `#x` prefix is case-sensitive. |
| `(_ bvN w)` | Recognize the `bv` symbol prefix and a numeral payload `N` in that symbol. Require positive width and `bit_length(N) <= w`. Distinguish validity from width or allocation limits. No modulo reduction. On the other hand `((_ int_to_bv w) n)` is an operation with modulo semantics, and the parser preserves it. |
| Operator indices | Parse exact digits, or use a checked bounded accumulator that gives the same result and overflow classification when the field has a fixed limit. Fully check lexical shape. Compare before narrowing or computing checked derived sizes. Rotate and divisibility indices have arbitrary precision in the node key. Tester indices are symbols. |
| FP format, specials and RM | Check `e,s > 1` and exact representability of `e+s`. Special `(_ NaN e s)`, `(_ +oo e s)`, `(_ -oo e s)`, `(_ +zero e s)` and `(_ -zero e s)` use `MakeFpSpecial`. Long and short names map to five RM values. An RM variable or compound RM term is a typed expression and may remain symbolic. |
| FP bit operands | Retain `fp` with BV1, BVe, BV(s−1) children and retain unary `(_ to_fp e s)` applied to BV(e+s). Exact BV parsing suffices. Core signed-zero identity and abstract NaN categories apply to values. Avoid folding an operation to a value during input decoding. |

For widths, extensions, concat and repeat, compare exact input values
against the implementation and policy limits before multiplying or
adding. A valid width above the NodeManager ceiling of `2^32−1`, or
a derived result crossing that ceiling, is a resource outcome.
Zero width, reversed extract indices, a negative numeric index in a
position requiring a numeral, and an overflowing literal at a valid
width are invalid inputs by their respective rules. If a configured
budget prevents the required checks, report resource exhaustion instead
of asserting that the spelling would be semantically valid or invalid.
Do not allocate a vector of size equal to the declared width just to
decode zero or compare magnitude bits. Compare the exact bit length
to the width. Separate declared width limits from digit and limb
allocation limits.

Two implementations are viable for wide radix values. Converting through
decimal text can wrap the existing API, but entails an unrelated radix
conversion and another large temporary. A checked decoder can pack digits
into a machine word when small, or pack wide digits in order into bytes
or limbs and import them once using the private GMP boundary. Prefer
the latter with a checked base-2/base-16 import in the numeric layer,
preserving canonical signed/unsigned small handling. Reuse project
numeric views, keeping GMP types private and lexing outside numeric
storage. The import's expected digit work is linear for fixed-radix
packing plus import, copying and interning costs. A repeated
multiply-and-add of a growing Integer per digit can do quadratic
limb work. The existing decimal implementation's actual large-value
cost depends on the GMP conversion and buffer copies.
[Dandelion exact values](../node/design.md#5-exact-values),
[FixedSizeBitVectors and QF_BV extensions](https://smt-lib.org/logics-all.shtml),
[GMP integer import](https://gmplib.org/manual/Integer-Import-and-Export)

### Closed rational argument to FP

Use the grammar `Q ::= numeral | decimal | (- Q) | (/ Q Q)` only
for the compatible two-argument source form `((_ to_fp e s) rm Q)`.
Check the `rm` expression in the normal term grammar. There is no
global Real grammar for open expressions. When the second argument is
not a candidate for the closed rational form, resolve its ordinary term
rank to an FP or BV operand, or diagnose the excluded Real form. A
numeral in this specific slot selects the concrete rational path.
An Int variable is not implicitly promoted to Real or converted to FP.
A malformed application retained by general syntax still needs an
appropriate arity or syntax diagnostic. Well-shaped Real syntax outside
the admitted rational grammar is unsupported. This disambiguation and
logic admission belong to topics 2 and 5.
[Dandelion operator contract](../node/operators.md#6-floating-point),
[FloatingPoint theory and formal semantics](https://smt-lib.org/theories-FloatingPoint.shtml)

Process each concrete rational subterm in postorder. Decode a decimal
with `k` fractional digits as the concatenated numerator digits divided
by `10^k`. Unary minus changes the numerator sign. For division
of `a/b` by `c/d`, require `c != 0` and form `(a*d)/(b*c)`;
cross-cancel before large products if helpful. Every nested division
must pass the zero test even if an outer numerator is zero.
`Rational::Make` copies and normalizes exact integers with positive
denominator and gcd one. It reports `kInvalidParameter` on a zero
denominator and provides no evaluation of decimal fractions or division
between rationals. At the frontend grammar boundary a zero denominator
is specifically `kUnsupportedFeature` because it falls outside the
compatibility form. Represent accepted conversion as `kFpFromRational`
with a canonical exact payload, destination indices and the RM child.
A rational zero is `0/1`. Rounding exact zero to FP, when performed
later, produces positive zero.

The minimal numeric gap is wide binary/hex decoding and exact operations
for decimal scale, sign change and rational division. Prefer a checked
decimal-to-rational helper and checked rational negation/division, or an
equivalent one-shot evaluator with private GMP temporaries called through
a value interface. Keep the choice of AST, parsing of `/` and mapping of
profile errors in the frontend. Using canonical temporary Rationals
for each division is easy to test but can repeatedly allocate and reduce
intermediates. A single numeric evaluator can avoid some copies, provided
it preserves per-operation zero checks, exact bit budgets, and the source
range for diagnostic purposes. No general Integer arithmetic, MPFR,
FP evaluation or Real sort is a prerequisite for this grammar. Check
bounds for the digit count, scale and intermediate numerator/denominator
before allocation, charge exact arithmetic work separately, and use
iterative stacks. A skewed tree of rational divisions can repeatedly
visit growing operands even when the final result is small.
[Dandelion rational API and proposed arithmetic](../util/rational.md),
[Numeric module layout](../util/implementation.md#build-and-module-layout)

cvc5 and Z accepted P23, which has nested signed rationals and symbolic
rounding. B rejected it at unary minus. For P24, C and Z accepted the
source division by zero under their Real semantics. B terminated with
signal 8 in the bounded run, with no valid outcome. That outcome does
not identify a root cause. P20 exercised out-of-range BV numeral
handling: Z accepted it with modulo-like meaning for the tested equality,
while C, B and S all diagnosed overflow or exited with an error. P21
shows host-index limits in the references. The pinned language and
Dandelion's profile decide the expected outcomes.
[Bitwuzla indexed and numeric parsing](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/parser/smt2/parser.cpp#L1769),
[cvc5 numeral parsing](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_term_parser.cpp#L1037),
[Z3 SMT-LIB scanner](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/parsers/smt2/smt2scanner.cpp)

**Verification cases.** Compare `#b00001111`, `#x0f` and `(_ bv15 8)`
for BV8 and value 15. `#xf` has width 4. Use `(_ bv255 8)`,
`(_ bv256 8)`, `(_ bv0 0)`, `(_ bv18446744073709551616 65)` and
the same numeral at width 64. Exercise `#x10000000000000000`, which
has width 68 and value `2^64`. Preserve `18446744073709551616`
as Int and test `(- 9223372036854775808)` and its neighbor as signed
values. Check rotate index `18446744073709551617` exactly, even
where its operation is semantically a rotation by one. Use
`((_ zero_extend 4294967295) #b0)` for result-width overflow after
a valid index, and an FP format with individually fitting indices
whose sum overflows. At FP(2,3), use `#b00000` and `#b10000` for
signed zeros, `#b01100` for positive infinity, `#b01101` and
`#b11111` for the single NaN value, and `#b00001` for the least
positive subnormal. Check these bit-pattern interpretations on the
numeric/node value boundary and the retained type and bits at the
frontend operation boundary. Test P23's `(/ (- 1.25) (/ 5 2))`
as `-1/2`, `(/ 3 (- 6))` as the same payload, and
`(/ 0 (/ 1 0))` as unsupported. Distinguish `(div 1 0)`, still
a well-sorted Int operation under `ALL`, and `(/ 1 0)` outside the
rational compatibility rule.

**Dependencies.** Use topic 3's delimiter and lexeme contracts before
decoding. Choose checked resource reporting consistent with §9.
[GMP's allocator contract](https://gmplib.org/manual/Custom-Allocation)
and [node error policy](../node/design.md#7-errors-and-factory-guarantees)
do not permit promising recoverability from arbitrary GMP limb OOM.
The proposed numeric layer should continue to expose checked size and
catchable C++ allocation failures, while a process isolation decision
belongs to the caller. Evaluation and eventual model output require
separate FP and Int contracts.

## 8. Correctness and test design

### Coverage and independent oracles

The [frontend test design](../testing/frontend.md) specifies solver-independent
observations, a feature inventory, independent expectations, and intentionally
bounded corpus checks. The tests in `tests/node/frontend_boundary_test.cc`
can check factory behavior and selected lowered shapes. They exercise no
text parser. Use GoogleTest with CTest and extend the scoped test design and
cases before implementing each feature. Every row below is planned research
coverage. Reference observations in the appendix establish a candidate
expectation or an oracle limitation, not a passing Dandelion test.

Maintain a reviewable feature-to-case inventory and make a row for every
distinct command, admitted option, sort schema, source operator schema
and accepted alias. A row records the revision or project extension,
logic gate, input, expected typed or error observations, independent
reason, linked tests, and their measured state. Use the tables below and
in §§2 and 5 to derive those rows, including separate rows where one
operator spelling has several schemas. Add exclusions explicitly. This
gives auditability without taking the Cartesian product of all types and
operators.

| Case group | Required individual rows and observations | Boundary, negative or interaction anchors |
| --- | --- | --- |
| FE-CMD | Use one row for every command form in §2, each meaningful option and each key with a validation rule. Inspect committed command order, declaration references, assertion occurrences and the sole query marker. | Test missing/unknown/repeated logic, order, second check, unsupported command, empty value request, EOF/exit and tail behavior. Supply controlled `sat`, `unsat`, `unknown` and unavailable-model states to exercise execution-dependent cases. |
| FE-LEX | Cover trivia, every atom category, quoted data, lexical errors, all list and identifier forms, annotations and input failure. Check raw spans, decoded data, and precedence when the input fails partway through a token. | Every chunk split of a short script, empty/truncated token, bad digit, BOM/UTF-8 policy, CRLF split. Test unsupported source forms as well as malformed ones. |
| FE-BIND | Separate cases for declarations, parallel `let`, formal scope and substitution, sort aliases and both syntaxes of nullary definition, name annotation visibility and lifetime. | Let RHS in outer scope, duplicates after dequoting, theory-shadowing error, captured global versus substituted actual, unused-binding closure, incomplete command rollback, declaration IDs before and after failed staging. |
| FE-SORT | One positive per source sort constructor and alias form, including parameterized uninterpreted sorts, arrays and DT instances. Verify sort identity, order of type args and format parameters. | Wrong number/sort of arguments, width zero and oversized derived width, symbolic index as numeral, two same-named symbols in different namespaces, Bool versus BV1, FP aliases versus nonmatching formats. |
| FE-OP | One positive per operator inventory row constituent and overload, including n-ary semantics, legacy conversion names, constants and array qualifier. Observe resolved kind or declaration, result, ordered children, indices and payload. | One wrong-arity, wrong-sort or bad-index case where applicable, with schema-specific boundaries. Check intentional exclusions and logic restrictions such as EIA versus NIA. Use separate checks for shape and result meaning. |
| FE-DT | Single/group, parameterized/mutual declarations, field recipes, constructors/selectors/testers, match branch binding and lowered meaning. | No finite base, nested recursion through alias, wrong selector sort, ambiguous head, duplicate field binder versus repeated wildcard, nonexhaustive patterns, repeated constructor order and unreachable ill-typed branch. |
| FE-NUM | Exact signs, decimal/radix norms, BV width and magnitude, FP special category, rational grammar and target indices. Inspect values in addition to types. | Leading zeros and widths, values beyond host words, rational normalization with negative denominator, nested zero division versus ordinary Int division, FP value canonicalization versus retained operations. |
| FE-MIX | Representative array/UF/BV, DT/UF/BV, parameterized DT/Int, FP/RM/BV and Int/BV conversion terms in appropriate profiles. Verify both signature and occurrence relationships. | Pair each with a single ill-sorted change and a syntactically valid but out-of-profile version. Add arrays over datatypes and arrays inside datatype fields if both shapes are admitted. |
| FE-LIMIT | Read and allocation failures, depth and work counters, diagnostics, cleanup, source and manager lifetimes. | Check one below, at, and one above configured limits without allocating huge inputs. Use failed input refill, a term with many sibling arguments, a command with many aliases, and repeated shared definition calls. Separate ordinary C++ failure checks from an optional isolated fatal-GMP probe. |

Use small fixtures with an in-memory reader, controlled chunk/failure source,
private manager, declared profile and a sink for completed commands and
observable diagnostics. For assertions inspect roots with the public node
views and required source occurrence information. Check direct declaration
references and exact values. For lowered forms check the promised semantic
observations, such as a resolved reference and actual value, without
requiring incidental node order or a particular memo layout. Use an
ordered command recorder to detect dropped or duplicated assertions.
A model-command fixture uses a supplied execution state with visible
instructions, requests and result behavior. It has no authority to claim
an actual solver result. A command-level recovery test can inspect a
failed overlay even when the full submission API stops there.

Hand-computed cases are the primary oracle. For example FE-BV-001 checks
`extract 3 0` produces BV4, not only that the input was accepted.
FE-BIND-001 observes the outer `x` in the parallel RHS even though a
reference solver may return only `unsat` for that particular formula.
Use the section-specific examples to cover cases that a common reference
cannot parse. For associative/chainable forms, construct hand-derived
examples with differing adjacent terms and tie-breaking cases. Limit the
expectation to the contract. Recognizing FP comparison yields the correct
FP comparison node without requiring frontend FP evaluation.
[Existing independent cases](../testing/frontend.md#small-examples-with-explicit-expectations)

A small typed generator may build, for example, 32 terms per seed and
profile with depth at most 6 and at most 128 syntax nodes per term.
Start from a separate signature description reviewed against the language
and theory records. Emit the chosen parameters and expected sort along
with the text, and generate sealed environments or complete scripts. Use
a deliberate one-change negative generator, such as swapping extract
bounds known to differ or a field argument of the wrong sort. A random
byte mutation can be valid, so robustness fuzzing must check deterministic
processing, termination and consistency without inventing a rejection
oracle. Add paired whitespace/comment transformations only between tokens,
fresh alpha-renaming that avoids capture, and radix variants preserving
both width and value. Compare the contract's semantic properties rather
than serialized text. Round-tripping is a useful additional check with
a shared-bug limitation. As a quality control, the tests should fail
under a few isolated mutations that capture plausible faults, such as
sequential `let`, accepting a wrapped BV numeral, swapped extract
indices, treating comment markers inside quotes as comments, and silently
dropping the last assert. Semantic Fusion provides independent evidence
for using transformations with a justified semantic relation. Its oracle
is satisfiability preservation for solving, so a fusion result alone
cannot check source-level identities, types or command order.
[Winterer, Zhang and Su, PLDI 2020](https://doi.org/10.1145/3385412.3385985)

### Reference harness and reports

Choose a reference mode per question and run it only on the shared
language slice. A parser API exposing typed assertions can test structure.
A solver CLI can test small semantic examples or execution order.
A textual result from a solver CLI cannot establish parser shape,
and a parse-only exit cannot establish command execution. The cvc5 CLI separates
`AssertCommand` creation from its call to `assertFormula`. P07 and P08
demonstrate that this affects both parameter and assertion checks.
Z3 recovered after an error and printed a subsequent `sat` in P07.
STP's CLI exited zero after textual errors in P13 and P22.
[Command parser](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/smt2/smt2_cmd_parser.cpp#L128),
[AssertCommand invocation](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/parser/commands.cpp#L274),
[Z3 parser recovery](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/parsers/smt2/smt2parser.cpp#L3305)

Capture complete stdout and stderr with version, argv, file hash, return
code or signal, wall time, limits, and warnings. Parse response forms rather
than searching for a `sat` substring or accepting the last line.
Classify error-plus-result, `unsupported`, ignored logic/options,
time limits, missing references, crashes, and output-limit truncation
explicitly. Permit reference extensions only as separately recorded
evidence. A tool's silence is evidence only about the boundary it ran.
For selected well-sorted cases in the common fragment, compare the original
and recovered formula with a Boolean XOR under a common signature and
corresponding declarations. `sat` witnesses a difference, and `unsat`
supports equivalence under that reference's semantics. Compare per-root
order and counts separately, since conjunction equivalence alone cannot
show duplicate assertion retention or matching source locations.
No check is successful if `unknown`, a tool error, or a truncation
prevents a conclusion.

**Candidate procedure.** Run solver-free GoogleTest unit and component
cases in GCC, Clang and working sanitizer configurations with explicit
test discovery and zero-test failure. Keep externally comparable and
corpus checks opt-in. Record each case as expected-outcome verified,
mismatch, unsupported by the frontend profile, unavailable, timed out,
crashed, or inconclusive. For corpus acceptance distinguish accepted-only
from semantically checked, stating which component and which observations
were checked. The §9 sample illustrates those distinctions.
[Testing strategy](../testing/README.md),
[GoogleTest parameterized tests](https://google.github.io/googletest/advanced.html)

**Dependencies.** Topics 2 and 5 supply the exact logic gates and resolved
forms for inventory rows. The overall design must provide enough access to
supported command state, source ranges, and declaration identity for those
rows to assert their outcomes. If the public result objects omit ephemeral
inspection data, use a component fixture or a recorded semantic event.
Finalize scoped expectations and fixtures under the test-first rule before
implementing the affected behavior. Corpus rows for unavailable inputs keep
that status until actual verification.

## 9. Performance, resources, build, and corpus validation

### Work and bounded resource choices

| Cost center | Initial implementation and measurement focus |
| --- | --- |
| Input | Use a contiguous view for already-owned text and a reusable 64 KiB starting chunk for file/stream input. Byte scanning is linear. Preserve only cross-refill lexemes and data retained by a later stage. Compare to a whole-input buffered read for small files. Mapping a regular file can save an explicit copy, with page-fault, file-lifetime and mutation concerns, and does not cover streams. |
| Syntax and source ranges | Use a contiguous frame vector and transient index-based records at command granularity. Accumulate ordered child slots once. Keep compact offsets and an owning source representation only when an API needs it. Per-occurrence records can dominate when many textual occurrences intern to the same node. Measure owning occurrence retention separately from span-only debug views and scoped scratch storage. |
| Names and types | Intern name bytes once per new canonical key and look up through scope heads. Count forward lookups, bytes hashed/compared, scope updates, rank candidates, sort recipe work and concrete intern operations. An expected-time hash table can degrade under collisions, treat full equality and bounded storage/work as part of resource behavior. |
| Definitions and match | Count template nodes and edges visited, substitutions, expanded alias recipe work and newly built match nodes. Per-instantiation memoization preserves within-call sharing, and NodeManager can return existing live nodes. It does not erase traversal work across multiple identical calls. Avoid recursive tree copies and compare bounded call caching only when repeated argument tuples justify it. |
| Numeric and nodes | Track digit bytes, intermediate exact bit sizes and node factory work by input family. Hashing and comparing a node cost at least the examined child slots and payload, with wide numeric payload costs depending on limbs. Node storage and lifetime depend on distinct live keys, argument slots, allocation, declaration stores and owned references, not only on input byte count. |

Provisional baseline inputs for later calibration are a 64 KiB reuse
buffer, a checked 64-bit byte offset, a per-token 1 MiB limit and
65,536 live syntax frames. These are starting values for evaluating
the design, with configurable smaller limits for tests. Maintain
separate configurable caps for total bytes read, current-command
storage, declarations, source occurrences, node/edge work, type
resolution and substitution visits, exact-value input digits and
intermediate sizes, and retained outputs. For a provisional small
test configuration, use 4 MiB input, 8 MiB staged storage, 4,096 live
frames, 16,384 occurrences and 100,000 work visits; lower one limit
at a time in boundary checks. A production default based on just
maximum nesting or file size misses wide terms, large declaration
tables and expensive small numeric inputs. Calibrate total memory and
work limits against deployment budgets and verified samples before
publishing production defaults. That includes very large flat or
shared inputs in later authorized work.

Charge before allocation, integer narrowing, pushing a frame, and
expanding a recipe or substitution. Use checked addition for aggregate
bytes and checked multiplication for item count times element size.
Report limit category and responsible range. Standalone counters are
an estimate unless all underlying allocation is accounted for.
Charge scratch buffers and retained objects separately so that clearing
a command releases the appropriate budget. Treat C++ allocation and
input failures as terminal for a submission and release owned state
iteratively. There is no general rollback or recoverable-limb-OOM
guarantee for NodeManager/GMP; any hard service-wide memory limit
or isolation policy belongs outside the parsing counters. Topic 1
must decide retained source and query ownership, and topic 3 must
expose refill/EOF/error behavior and accounting hooks.
[Node error policy](../node/design.md#7-errors-and-factory-guarantees),
[Node storage and measurement](../node/implementation.md#10-measurement-and-research-evidence)

### Build and measurement baseline

The current repository provides `dandelion_node`, private GMP use,
GoogleTest/CTest tooling, and opt-in Node workload benchmarks. A future
solver-free frontend target can import the public node module and use
the existing `dandelion_configure_target()` and owned source-list policy.
Keep solver, SAT dependencies and discovery out of frontend-only
configuration. This is a configure-time property, so selecting just
the frontend build target after requiring a SAT package does not help.
Let final test executables receive the needed static dependencies and
sanitizer link flags. Use separate Ninja build trees/BMIs for GCC 16.2.0,
Clang 23.1.5 with the verified libstdc++, and GCC ASan/UBSan. Local
CMake is 4.5.0 and Ninja is 1.13.2. Current Clang sanitizer availability
is limited by the local runtime configuration. The frontend target has
yet to be implemented or built.
[Build standards and presets](../cmake.md),
[Node acceptance and toolchain evidence](../testing/node-acceptance.md),
[CMake 4.5 C++ modules](https://cmake.org/cmake/help/v4.5/manual/cmake-cxxmodules.7.html)

For the future tests use existing GoogleTest integration and
`gtest_discover_tests(... DISCOVERY_MODE PRE_TEST ...)` with a frontend label
and `--no-tests=error`. Start configurations with locally available
dependencies and networking disabled for dependency acquisition. Check clean,
no-op and one-file incremental builds, distinguish implementation versus
module-interface rebuild, and verify standalone frontend-only configure and
sanitizer consumer links. Record actual compiler, standard library, linker,
Ninja and CMake versions, flags, jobs, wall/CPU time and build memory method.
Internal parser choices do not require a generator dependency initially.
Avoid default benchmark tooling or corpus dependencies in ordinary developer
builds.

Use opt-in Google Benchmark once the frontend can be timed. Keep workload
definitions deterministic. A bounded initial series can use 128, 1,024 and
4,096 declarations/flat arguments, 128, 512 and 2,048 nested terms, 64 and
4,096 digits, and 32, 128 and 512 macro calls with fixed versus changing
arguments. Run separate inputs with more unique terms, heavier sharing,
lexemes crossing a chunk, exact rational products, and failed input near
the end. For known-small inputs use the corpus sample below as named
additional workloads. Record bytes, token counts, typed edges and
occurrences as well as calls, and cap each benchmark invocation. The
synthetic parameters expose scaling mechanisms without estimating their
frequency in corpus input.
[Google Benchmark user guide](https://google.github.io/benchmark/user_guide.html),
[Existing Node workloads](../benchmarks/README.md)

Use separate timing boundaries for file acquisition, token scan, syntax,
semantic resolution with node construction, and total parse-only
acquisition-to-typed-query. If instrumenting factory time or repeating
resolved node requests outside parsing, report the instrumentation overhead
or replay boundary. Keep necessary outputs live and consume observable
counts to prevent optimization. For a cold process include initialization,
require a fresh manager and report filesystem/cache conditions. Do not
claim cold filesystem cache merely because the process is new. Use a
separate in-memory repeated run with a fresh state per repetition, and a
deliberate warmed lookup/replay scenario only when measuring that effect.
Record executable identity, checkouts, release flags, CPU and OS,
input hashes, limits, iteration counts, independent process samples and
median/spread. Start with three independent processes and bounded
repetitions per workload. Report wall time and CPU time separately.
Compare equivalent contracts, output ownership, and work across variants.

Measure per-process peak RSS in a fresh process and note platform and units
(for example Linux maximum RSS in KiB). It includes touched input, code and
shared libraries, stack, allocator-retained storage, transient numeric
temporaries, manager state and output. It is a lifetime high-water mark,
not a sum of individually timed phases, and an RSS delta from a separate
baseline cannot reliably attribute memory to the parser or nodes. Pair it
with phase counters for input and scratch capacity, owned declarations and
occurrences, GMP/numeric backing where observable, node live and peak
counts, and allocator statistics as appropriate. For concurrent builds,
use process-tree or cgroup evidence and record shared memory/sample limits.
The nested-`not` run P34 is a bounded functionality observation, with
no controlled timing or RSS comparison. No speedup or full-solver
performance conclusion is available.
[Project performance and build evidence requirements](../cmake.md#8-measurement-and-acceptance)

**Verification cases.** With a configurable limit of eight live syntax
frames, exercise inputs requiring seven, eight and nine frames, counting
from the same documented command entry state. The first two can complete,
and the third must report the depth limit even if it is well typed. A
shallow application with many siblings instead exercises the staged-byte
and edge limits. Run a quoted token crossing a read boundary with both
normal EOF and an injected read failure, and check the different completion
outcomes. For measurement controls, compare identical scripts split across
input chunks and compare fresh-state repetitions with identical inputs.
Their typed observations and workload counts should agree. A deliberately
warmed replay can change allocation and interning counts, with unchanged
typed results. Treat a benchmark with zero completed iterations, a missing
phase boundary or failed input as unavailable for throughput comparison.

### Local corpus evidence

Mount table inspection for `/pub/netdisk1` showed NFSv4.2 behind autofs.
Linux documents that `O_NOATIME` may be ineffective on NFS, where the
server maintains access time. The protected dataset root
`/pub/netdisk1/zhangx` was neither opened nor enumerated. No authoritative
read of that snapshot or new dataset retrieval was performed.
[Project dataset policy](../testing/README.md#local-benchmark-data),
[Linux open(2), O_NOATIME](https://man7.org/linux/man-pages/man2/open.2.html)

Five files were selected from an already present Git-tracked safe copy at
`/home/zhangx/solvers/SMT/z3/fstar/benchmarks/smtlib-sample`. Its
`manifest.json` was changed in commit
`8e8bda945172a9ccbadfd13ebf13f47553102fb7`, is in the inspected Z3
checkout at `430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e` with Git blob
`afea74db5b49de50540d5eaed3c30a0f48bcc061`, and identifies
[Zenodo record 16740866](https://zenodo.org/records/16740866) as its source
(2025 non-incremental release, version 2025.08.04).
The manifest reports a selection of unsat-annotated inputs biased toward smaller
cases and selected families. Files below were checked against manifest
size and SHA-256 and byte-compared with the tracked blobs at HEAD. The
GrammaTech file also matches the independent project record in
[frontend testing](../testing/frontend.md#benchmark-regression-checks).
This verifies the local copy contents and the recorded provenance chain
up to the manifest. It does not establish identity between all copies
in the two snapshots or verify upstream archive contents. For the probe
only these named files were staged outside the dataset.

| Path relative to `non-incremental/` in the manifest, bytes and SHA-256 | Observed content and admission evidence |
| --- | --- |
| `QF_BV/challenge/integerOverflow.smt2` · 1,461 · `1e96ed11af58d1bcbcc957f3ad361aa84244777076aa094e8d578353486362e0` | 2.6, two BV32 constants, 17 nullary definitions, three assertions, signed division, 64-bit extensions and a width-33 extract. C accepted. B accepted and emitted three assertions whose positions and meanings were checked as detailed below. Candidate for the strict BV profile. |
| `QF_NIA/20220315-MathProblems/STC_0473.smt2` · 625 · `d7b9cfba2a59a6b9046746f6ee14c4514b19c45462818c4489b550a31144855b` | 2.6, three Int constants, one assertion with three n-ary repeated-operand products and equality to 473. C accepted only. Candidate for the NIA profile. |
| `QF_DT/20172804-Barrett/barrett-jsat/tests/v1/v1l50090.cvc.smt2` · 1,024 · `a5ff74ec32890bf3087c82e0b70d82d80ce40637de72d65f392a11b71995ef8f` | 2.6, three datatype sorts in one group, recursive and mutually recursive fields, three constants, selectors and symbolic testers in one assertion. C accepted only. Requires the candidate competition logic entry. |
| `QF_UFDT/20210312-Bouvier/vlsat3_e80.smt2` · 11,208 · `5db803cb1689a5f2a51ef9961dc1456d1671d671f3d017d5a2cae6cfbfda8c38` | 2.6, two datatype declarations, `u : Place -> Unit`, 236 assertions. C accepted only. Requires the candidate competition logic entry. |
| `QF_AUFBV/ecc/com.galois.ecc.P384ECC64.ec_full_sub3.short.smt2` · 11,336 · `0b8c8f0abffd34a0d4022c2dde624b316ad3a8bd2dbafcd59daa0b539e878c02` | 2.6, UF with 1152- and 768-bit arguments, BV arrays and selects, 121 assertions. C and B accepted only. Candidate for the QF_AUFBV profile. |

For GrammaTech only, B additionally ran `--parse-only --print-formula` on
the safe copy and returned 0 (printed SHA-256
`1970bb9de2d02fd1441d2fcffb49d29e6f52738ff28d7297691ce9a05b7e8337`).
The two declaration widths, three output assertions and the indices
`63,31` were inspected. For each position `i` in 1–3, preserve the
source logic and all source declarations and definitions. Replace the
three original assertion commands by a single
`(assert (xor original_i printed_i))` followed by `(check-sat)`, with
the two expressions taken from the respective one-line assertion forms.
Z returned `unsat`, exit 0, on each comparison with a 5 s wall cap,
3 s CPU cap and 512 MiB address-space cap. This checks the three
Bitwuzla-printed assertion meanings against the original using common
declared BV constants and Z3's semantics. It neither checks original
command metadata on reprint nor exposes typed Dandelion assertions.
The other four inputs remain reference accepted-only. `:status unsat`
was not used as a parsing or equivalence oracle.

**Follow-up selection rule.** Once safe copies and exact profiles are
available, use a named manifest and choose a few additional cases by
observed features: IDL/LIA syntax, FP with symbolic RM, BV/FP bridges,
concrete FP rationals, typed array values, and DT parameterization or
`match` combined with another theory. Set byte/file/family bounds
before selecting, for example eight additional named inputs of at most
256 KiB each in an explicitly authorized small validation run. Keep
input checksum, source family, actual operators, line/command/term
counts, profile, tool/version and mode, time/memory cap, and outcome
for each. Check structure on a selected subset and preserve a small
standalone reproducer for confirmed discrepancies. Report the protected
tree as unavailable for direct verification. FP, IDL/LIA, parameterized
DT with `match`, and further mixed-theory real coverage remain gaps in
this run. The sampled input distributions are deliberately biased and
do not support workload-frequency claims.

**Dependencies.** Timing requires a later frontend entry point with an
observable completion boundary and explicit retained output lifetime.
The selected profiles from §§2 and 5 determine which corpus cases can
be checked at full fidelity. The final ownership and budget policy
decides which counters can be accurate and which need estimates.

## Appendix. Bounded reference probes

The 36 invented scripts and five safe corpus copies were run in
`/tmp/dandelion-frontend-topic-research`. The primary matrix contains
115 invocations from the modes in the [evidence table](#evidence-used-throughout).
Each ran in a fresh process with a 5 s wall timeout, 3 s soft and 4 s hard CPU
limits, 512 MiB address-space limit, and 1 MiB regular-file output limit.
Stdout and stderr were captured separately. The largest synthetic script
was 12,332 bytes. All primary invocations exited or signaled before wall
timeout. Three supplemental C-run controls and the B print control used
a 5 s wall timeout. The three reference equivalence checks and their
limits are recorded with the corpus case. No performance comparison was
conducted. The scratch folder contains `probe.py`, the exact inputs and
the primary raw `results.json`. The inputs and interpretation below
allow reproduction without that folder.

For each table row create a file with an LF after the logic line
`(set-logic LOGIC)` when present, then the exact body in one line, LF,
then `(check-sat)` and LF. P09 has a replacement final line. Expand
`[List]` to the following text, including a separating space after the
second declaration, on the same line as the subsequent assertion.

```smt2
(declare-datatype List (par (T) ((nil) (cons (hd T) (tl (List T)))))) (declare-const xs (List Int))
```

In the table, **parse** means exit zero with no error or unsupported
response at the stated parse boundary. A reported warning is noted when
it materially affects the observation. **Error** means an observed
diagnostic, normally exit 1 unless specified. `sat` and `unsat` in
Z/S/C-run columns reflect execution of that script or a surviving
prefix. B used verbose mode and warned about missing `exit` for successfully
parsed synthetic inputs. No B parse-only invocation supplies a solver
result. All omitted modes were out of that probe's selected matrix.

| Probe | Logic and exact body | Observed outcomes |
| --- | --- | --- |
| P01 | No logic line. `(assert true)` | C parsed with an all-theories fallback warning, C-strict errored, Z `sat`, B parsed with missing-logic warning. |
| P02 | `QF_BVFPLRA`. `(declare-const x (_ FloatingPoint 8 24)) (declare-const bits (_ BitVec 32)) (assert (= x (_ +zero 8 24))) (assert (= ((_ to_fp 8 24) bits) x))` | C and C-strict parsed, Z `sat` with no diagnostic on this input, B parsed and logged the logic. |
| P03 | `QF_BVFPLRA`. `(declare-const r Real) (assert (= r 0.0))` | C and C-strict parsed. |
| P04 | `QF_BV`. `(set-logic QF_BV) (assert true)` | C rejected repeat; Z emitted an error followed by `sat`, exit 1; B parsed with repeat warning. |
| P05 | `QF_LIA`. `(declare-const x Int) (assert (= (* x x) 2))` | C and C-strict parsed, C-run rejected nonlinear arithmetic, Z emitted a nonlinear error then `sat`, exit 1. |
| P06 | `QF_LIA`. `(declare-const x Int) (assert (= (* 2 (+ x 1)) 4))` | C parsed, Z `sat`. |
| P07 | `QF_BV`. `(assert #b1) (assert true)` | C parsed, C-run rejected non-Bool, Z emitted a non-Bool error then `sat`, exit 1, B rejected the non-Bool assertion. |
| P08 | `QF_BV`. `(set-option :dandelion-example true) (assert true)` | C parsed; C-run emitted `unsupported` then `sat`, exit 0; Z emitted unknown parameter error then `sat`, exit 1. |
| P09 | `QF_BV`. `(assert true)`. Final line `(check-sat) (exit) (assert` | C parsed its selected prefix, Z `sat`, exit 0. No trailing error reported. |
| P10 | `QF_BV`. `(set-info :source "quoted ""word"" ; (metadata)") (declare-const \|x;y(z)\| (_ BitVec 8)) (assert (= \|x;y(z)\| #x00))` | C and B parsed, Z and S `sat`. |
| P11 | `QF_BV`. `(declare-const \|x;z (_ BitVec 8))` | C and Z diagnosed an error, with Z identifying EOF in the quote. |
| P12 | `QF_BV`. `(declare-const x (_ BitVec 8)) (assert (= \|x\| #x00))` | C and B parsed, Z `sat`. |
| P13 | `QF_BV`. `(declare-const x (_ BitVec 8)) (declare-const \|x\| (_ BitVec 8))` | C, C-strict and B rejected duplicate, Z emitted duplicate error then `sat`, exit 1; S emitted a syntax error, exit 0. |
| P14 | `QF_BV`. `(assert (let ((x #x01)) (let ((x #x02) (y x)) (= x y))))` | C and B parsed, Z and S `unsat`. |
| P15 | `QF_BV`. `(assert (let ((x #x01) (\|x\| #x02)) (= x #x02)))` | C parsed, Z `sat`, B terminated with signal 11. |
| P16 | `QF_BV`. `(assert (and (= (! #b1 :named n) n) true))` | C and B parsed, Z `sat`. |
| P17 | `QF_BV`. `(assert (let ((x #b0)) (= (! #b1 :named n) #b1))) (assert (= n #b1))` | C rejected inline naming in a binder, Z `sat`. |
| P18 | `QF_BV`. `(assert (let ((x #b1)) (= (! x :named n) #b1)))` | C rejected inline naming in a binder, Z `sat`. |
| P19 | `QF_BV`. `(assert (and))` | C, C-strict and B rejected, Z errored on missing arguments then emitted `sat`, exit 1. |
| P20 | `QF_BV`. `(assert (= (_ bv256 8) #x00))` | C, C-strict and B rejected overflow, S emitted fatal overflow error, exit 255, Z `sat`. |
| P21 | `QF_BV`. `(assert (= ((_ rotate_left 18446744073709551617) #x01) #x02))` | C and B rejected numeral/index size; Z emitted an index error then `sat`, exit 1. |
| P22 | `QF_BV`. `(assert (= (concat #b0 #b0 #b1) #b001))` | C and B parsed, Z `sat`, S emitted syntax error, exit 0. |
| P23 | `ALL`. `(declare-const rm RoundingMode) (assert (= ((_ to_fp 8 24) rm (/ (- 1.25) (/ 5 2))) ((_ to_fp 8 24) RNE (- 0.5))))` | C parsed; Z `sat`; B errored at unary minus. |
| P24 | `ALL`. `(assert (= ((_ to_fp 8 24) RNE (/ 1 0)) (_ +zero 8 24)))` | C parsed, Z `sat`, B terminated with signal 8. |
| P25 | `ALL`. `[List] (assert (= xs (as nil (List Int))))` | C parsed, Z `sat`. |
| P26 | `ALL`. `[List] (assert (= xs nil))` | C and C-strict rejected the constructor result type, Z `sat`. |
| P27 | `ALL`. `[List] (assert (= (match xs ((nil 0) ((cons _ _) 1))) 0))` | C and C-strict errored at the first wildcard in a field position, Z `sat`. |
| P28 | `ALL`. `[List] (assert (= xs (as nil (List Int)))) (assert (= (match xs ((nil 0) (nil 1) ((cons h t) h))) 1))` | C and C-strict parsed, Z `unsat`. |
| P29 | `ALL`. `[List] (assert (= (match xs ((whole 0) (nil #x01))) 0))` | C rejected mixed branch sorts, Z emitted that error then `sat`, exit 1. |
| P30 | `ALL`. `[List] (assert (= (match xs ((nil 0))) 0))` | C reported nonexhaustiveness, Z emitted that error then `sat`, exit 1. |
| P31 | `ALL`. `(declare-datatype D ((base) (node (part (Array Int D)))))` | C parsed, Z `sat` on the otherwise empty query. |
| P32 | `ALL`. `(declare-datatype D ((loop (next D))))` | C rejected nonproductive datatype, Z emitted that error then `sat`, exit 1. |
| P33 | `ALL`. `(declare-datatype D (par (A) ((base) (step (next (D (Array Int A))))))) (declare-const x (D Int)) (assert ((_ is step) x))` | C parsed, Z `sat`. |
| P34 | `QF_BV`. Concatenate `(assert `, 2,048 copies of `(not `, `true`, and 2,049 copies of `)`. | C and B parsed, Z and S `sat`. |
| P35 | `QF_DANDELION_BOGUS`. `(assert true)` | C, C-strict and B rejected the logic. Z emitted `unsupported` and `sat`, exit 0, with an ignored-logic diagnostic on stderr. |
| P36 | `QF_BV`. `(define-const x (_ BitVec 8) #x0f) (assert (= x #b00001111))` | C and C-strict parsed, Z `sat`, B rejected the command in this version. |
