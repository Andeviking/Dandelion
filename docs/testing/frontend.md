# Frontend Testing

**Status.** Phase 1 and Phase 2 suites are implemented, with historical verified runs recorded in the implementation record. The review did not independently verify the current tree or complete coverage of each required subcase; the cases below remain acceptance requirements.
The [acceptance record](../frontend/implementation.md) identifies the
configurations and corpus slice exercised. This document applies the
[testing strategy](README.md) to SMT-LIB input, name resolution, typing,
and construction of the terms passed to the solver.

## Contract and input scope

The frontend must correctly represent each accepted input before any solving
result can be trusted. Tests distinguish parsing a command from executing it:
recognizing `check-sat` does not establish `sat` or `unsat`, and input status
metadata is not a substitute for running a solver.

The target is the supported quantifier-free profile described in the
[architecture](../architecture.md), with real compatibility checks against
the local `benchmark-2025` snapshot. The selected profile must state the
language and theory revisions and accepted extensions. Test compatible 2.6
inputs as well as accepted 2.7 syntax; do not infer either exact syntax or
feature support from the dataset's year or a logic name alone. The general
Real and String theories, user quantifiers, and a multi-query incremental
session are outside the current scope.

The [frontend design](../frontend/design.md), [logic profiles](../frontend/logic-profile.md)
and [semantic interfaces](../frontend/semantic-interfaces.md) select
command-at-a-time execution, direct aliases, parameterized templates, signature-based compatibility
and model-bound value queries. Production model obligations require their
own solver-side tests in addition to a controlled frontend provider.

An SMT-LIB metadata string is not an assertion in the String theory. Test
quoted symbols and strings used by supported commands even when general String
formulas are unsupported. The profile accepts constant arrays and a
restricted concrete-rational-to-FP conversion; test their exact limits in the
[operator specification](../node/operators.md).

Frontend tests can be repeated as regression tests without a Dandelion solver:
their expected results concern tokens, commands, names, types, and represented
meaning. They must not depend on an invented `sat` or `unsat` response. Where
command validity depends on a solver result, a test may supply controlled
execution state through a test interface; it must not present that state as an
actual solver result.

## Turn the input profile into a coverage inventory

Before treating a frontend feature as covered, identify the applicable language
and theory revision and the project's accepted input form. Maintain a reviewable
inventory alongside the tests; it can be a table and need not be a new test
framework or code generator. Each row should record:

- A stable feature identifier, its standard or documented extension, and the
  supported logic/profile conditions.
- Valid input forms, including relevant arities, sorts, indices, and bindings.
- Expected observable information and independently justified examples.
- Relevant invalid, unsupported, boundary, and combined cases, or why a kind
  of case does not apply.
- Test identifiers and status: covered and passing, missing, planned or
  unsupported, or inconclusive. Do not mark an unimplemented feature covered
  because a test was skipped.

For example, a BV-extract row identifies the operand width and the conditions
`0 <= low <= high < width`. It links a valid case, an out-of-range index, a
reversed index, and checks of both the result sort and the two indices. The
[BV theory](https://smt-lib.org/theories-FixedSizeBitVectors.shtml) provides
the independent rule. Record the revision actually used by the input profile.

For each accepted command, sort, and operator schema, design at least one
representative well-formed case and, when meaningful, cases for incorrect
arity, sorts, or index parameters. For declarations and binding constructs,
also test the relevant state or scope. Cover named boundary values and a
small set of interactions; do not attempt a Cartesian product of all sorts,
operators, widths, and nesting depths. If a combination is important to the
profile but is not yet checked, record it as a gap rather than assuming the
individual tests cover it.

An inventory measures the behaviors we deliberately checked, not all possible
inputs. Line or branch coverage, when available, can reveal unexercised code;
neither metric nor an all-green test run proves correctness.

## Behaviors to cover

Create focused positive and negative cases for:

- **Lexing and syntax:** whitespace, comments, quoted symbols, metadata
  strings, numerals, binary and hexadecimal BV literals, parentheses, indexed
  identifiers, annotations, incomplete input, and accurate error locations
  when location reporting is part of the interface.
- **Declarations and commands:** sorts, constants, functions, non-recursive
  definitions, datatype declarations, assertions, supported options and info,
  one `check-sat`, and applicable model or value queries. Check command order,
  duplicates, undeclared names, malformed commands, and explicit handling of
  unsupported commands and options.
- **Binding and identity:** symbol resolution, `let` bindings and shadowing,
  function parameters, datatype patterns, and simultaneous bindings. Preserve
  declaration identity even when names or term structures are similar.
- **Sorts and operators:** exact arity, operand and result sorts, indexed
  operator parameters, BV widths, FP formats and rounding modes, unbounded
  integer numerals, array index and element sorts, and UF signatures. Test
  valid overload resolution and meaningful sort or width mismatches.
- **Datatypes and combinations:** constructors, selectors, testers, `match`,
  well-founded recursive and mutually recursive declarations, parameterized
  sorts, type qualification where needed, and supported fields of other
  theories. Do not silently accept an out-of-profile recursive container as a
  standard declaration.
- **Scale and robustness:** bounded examples with many declarations or
  assertions, nested terms, long symbols, and large literals. Check that errors
  are handled without truncating values, crashes, or silently dropping input.

Terms and values may be stored using different representations. Tests should
assert their semantic and type contracts, not pointer identities, allocation
strategies, or incidental pretty-printing unless those are explicitly part of
the relevant API.

Use the following matrix when deriving concrete cases. The listed inputs are
representatives, not a claim that these cases exhaust any group.

| Group | Valid and boundary coverage | Invalid or unsupported coverage |
| --- | --- | --- |
| Tokens | Comments and whitespace between tokens; quoted symbols and strings containing punctuation; `#b` and `#x` literals; very large numerals. | Unterminated quoted symbols or strings, invalid BV digits, and incomplete tokens; do not interpret C-style escapes as SMT-LIB escapes. |
| Commands | Supported declarations and definitions, multiple assertions, metadata, one query, and allowed commands after it. | Undeclared names, duplicate declarations where prohibited, malformed or unsupported commands, unsupported options, and a second query or user scopes. |
| Bindings | Nested and shadowed `let`, simultaneous bindings, function parameters, and datatype pattern variables. | Duplicate simultaneous bindings and references that are unbound in the enclosing scope. |
| Core and UF | Boolean terms, equality and `ite`, user sorts and functions, and Bool-valued or multi-argument functions. | Wrong arity, incompatible operand or branch sorts, and a function used with the wrong argument sorts. |
| BV | Widths 1, 2, 8, 32, 64, and 65 where applicable; indexed operators at valid endpoints; equivalent binary and hexadecimal values. | Zero width, mismatched widths, missing operands, reversed or out-of-range extract indices. A resource limit is distinct from a mathematical sort error. |
| Int | Zero and a numeral larger than a host word; signed terms and supported linear and nonlinear operators; `div` and `mod` as typed terms. | Incorrect operand sort or arity; operations outside the selected profile must be identified as unsupported rather than silently discarded. |
| FP | Accepted FP format parameters, rounding-mode constants, FP literals and operators; distinguish core equality from `fp.eq` in the representation. | Invalid format parameters, missing rounding modes, or a BV term where an FP term is required. |
| Arrays | `select`, `store`, and accepted constant arrays with interpreted index and element sorts; nested arrays and accepted element types. | A store value or select index of the wrong sort, or an ill-sorted constant-array qualifier/value. |
| Datatypes | Enumeration and parameterized datatypes, constructors, selectors, testers, `match`, and accepted recursive declarations. | Ill-founded declarations, wrong constructor or field sorts, and invalid or out-of-profile recursive container forms. |
| Cross-theory terms | A datatype with a BV field used by a UF; array indices and values from different supported sorts; FP and rounding-mode terms in accepted combinations. | An otherwise similar term with one mismatched sort or an unsupported combination; do not infer support from `set-logic` alone. |

Choose a small, independently justified set for each accepted operator family,
including operations discovered in selected real inputs. For an indexed or
polymorphic operator, verify the resolved parameters and result type as well as
acceptance. Tests of the frontend need not evaluate the operator's result; tests
of a later evaluator or solver are separate obligations.

## Streaming, definitions, and model-query cases

Design the following cases before implementation; a controlled engine or
model tests the driver boundary without claiming that Dandelion solved a
formula. Use the standard and manually derived expectations as primary
oracles, and reference implementations only on their common supported slice.

- **FE-STREAM-01:** feed `(set-logic QF_BV) (assert true) (check-sat)`
  through a reader whose input remains open. A controlled solver is called
  exactly once and its result is emitted before EOF. Vary chunk boundaries,
  including a split immediately before the closing parenthesis. Verify
  `:print-success` timing and metadata/`echo` responses independently.
- **FE-STREAM-02:** after a controlled check result, send an invalid command
  or a second `check-sat`. The prior result remains visible, the error is
  reported, and there is no second solver call. `exit` in any state ends
  parsing immediately, including when a malformed suffix is already buffered;
  EOF at a command boundary before any check causes no solver call.
- **FE-STREAM-03:** `(set-option :print-success true)` followed by `(exit)`
  emits two `success` responses and does not inspect a malformed suffix.
  Verify that setting it back to false suppresses that command's own success.
  Before logic, set each fixed-false standard option to `false`, check its
  success response and `get-option` value, and continue to a controlled
  check. Setting any of these options to `true` is unsupported and terminates;
  test malformed values and invalid later state separately. An unknown
  `get-option` returns `unsupported` and permits a later check; an unknown
  `set-option` returns `unsupported`, terminates with status 1 and does not
  invoke the solver. Check `echo` preserves its source string token, including
  doubled quotes. Verify `reset-assertions` terminates as unsupported before
  a check that would otherwise change outcome, and normal `exit` has status 0.
- **FE-STREAM-04:** inject a model evaluation or binding failure before the
  first response byte, then verify one complete error if the sink is usable.
  Separately fail evaluation after the first value in a multi-value response,
  and inject short write and flush failures. Each stops the stream without
  appending an error to a partial response; previously completed responses
  remain visible. Test missing required model on `sat` before its response,
  model-production-disabled `sat`, `unsat`, and `unknown` with and without a
  candidate. Inject an actual representation failure for a later huge exact
  value without constructing it; it does not change the earlier `sat`.
- **FE-DT-ERROR-01:** after sort-alias expansion, a recursive container
  field yields `kInvalidDatatype` with reason `kNestedRecursiveSort` and
  a frontend unsupported diagnostic. A nonproductive group yields the same
  code with `kNonproductive` and an invalid-input diagnostic. Test empty
  groups, constructor sets and wrong parameter scope at the builder boundary,
  preserve no-publication on failure, and verify existing code-level error
  categories.
- **FE-DEF-01:** test a definition applied with a shadowed caller name, nested
  definitions, repeated and unused parameters, nullary definitions, incorrect
  arity/sorts and nonrecursive name resolution. Inspect the owning definition
  environment and saturated-call identity for parameterized definitions and
  direct body identity for nullary definitions, then compare small expansions against
  independent expected terms. The defined call is never an unconstrained UF or
  a separately user-declared model symbol.
- **FE-DEF-02:** create a finite family of definitions with
  `f0(x)=bvnot(x)` and `fn(x)=f(n-1)(f(n-1)(x))`. Verify an unused family
  remains a graph of written definitions and calls, rather than constructing
  the exponentially expanded chain. Count body visits and constructed calls
  for both unused and used BV families. Check small used cases for semantic
  equivalence, and verify lifetime after temporary source storage is released.
  In addition, under `QF_LIA` use `f0(x)=x` and the same composition through
  `f12`. Both unused and used families are accepted without expanding the
  calls. For a fixture with one final `f12(x)` application, inspect its 13
  definitions, 24 calls in definition bodies and one final call. Instrument
  frontend body visits and node construction: work for this family follows
  its written graph, not the expanded chain. This is a deterministic operation
  count, not a timing threshold or a general linear bound.
- **FE-DEF-03:** under `QF_LIA`, define `f(t) = (* 2 t)`. The assertion
  `(= (f x) 2)` and `(= (f (+ x 1)) 4)` are both accepted for declared `x`.
  Check the same definition identity and different ordered actuals, in both
  source orders, without instantiating the body. With a controlled model
  `x=1`, their call values are 2 and 4. Check nested definitions, simultaneous
  `let` environments and named aliases. A well-sorted nonlinear unused
  argument is accepted; an ill-sorted unused argument is still rejected.
  Separately let `f(a,b)=a-b` and `g(t)=f(t,f(t,1))`; after destroying the
  reader, session and driver, a retained model must evaluate `g(5)=1` using
  the owned query and definition table. A cache keyed only by body node must
  not conflate the two `f` environments.
- **FE-DEF-04:** under `QF_UFDTLIA`, declare an enum-like datatype with
  `box(Int)` and `empty`; in a `match`, `(* 2 t)` for the `box` field `t`
  is accepted. A repeated, unreachable `box` branch containing `(* t t)` is
  also accepted. Replace its body with `(+ t true)` and verify rejection even
  though first-match lowering omits the branch. Destroy source scratch before
  a later call through the stored definition or alias and check node ownership.
- **FE-LOGIC-01:** add positive and negative cases for each registered 2025
  profile, including `QF_ABVFP`, `QF_ALIA`, and `QF_UFDTLIA`. Under a
  Real-bearing label test both a supported Real-free input and an actual
  Real declaration, which remains unsupported. In QF_BV accept Bool
  declarations; under QF_IDL accept `(< x 3)`; under QF_LIA accept both
  `(* 2 (+ x 1))` and `(* x x)`. Inspect their Int node kinds and operands,
  not a claimed solving result. Under QF_NIA, accept `(** x 2)` as the
  documented extension; do not treat rejection by a strict reference as a
  frontend failure. The solver may classify accepted terms differently.
- **FE-LOGIC-02:** under QF_UFIDL accept `(= (+ x y) 1)`; reject
  `(= x (- 1))` in an assertion and general `(- x)` in a value request.
  After a model is available accept canonical `(- 1)` as a value-request
  operand. Under QF_UFLIA, Int `div` is accepted in assertions as well as
  value queries. Under QF_UFIDL, explicitly declare `div` as a UF and verify
  its application carries that user declaration, not Int division. Reject a
  declaration of `div` that collides with the active builtin in QF_UFLIA.
- **FE-LOGIC-03:** under QF_UF, a user-declared sort named `Int` remains an
  uninterpreted sort; without that declaration it is not promoted to the
  built-in Int sort. Under FP, the builtin `Real` sort and `fp.to_real` remain
  reserved and unsupported. Under QF_UF, a declared free sort constructor
  named `Array` stays a free sort rather than enabling arrays. Under QF_UFFP,
  a declared UF with an inactive BV-operator name stays that UF; under QF_BV,
  reject a declaration that collides with the active BV builtin. Inspect
  declaration and sort identities in each case, not just printed names.
- **FE-LOGIC-04:** under QF_BV, accept Bool and BV constants and a nonzero-
  parameter `define-fun`, but report a free sort or nonzero-arity `declare-fun`
  as unsupported. Under QF_UFBV, accept the same free declarations and retain
  their identities. Under QF_AX, accept a free sort and an array constant but
  reject a free nonzero-arity function. QF_SAT accepts Bool constants but not
  free sorts or functions. For every registry row check its C, S or U rule.
  Under QF_LIRA accept `(** x 2)` but report the active Real conversion
  `to_real` unsupported; in ALL, an explicitly declared Int-valued UF with
  that name remains the declared UF. Never route by label alone.
- **FE-LOGIC-05:** under QF_UFFP, declare `|/| : Bool × Bool -> BV1` and
  convert its application to FP, resolving it as a UF and the BV conversion
  rank. Without such a declaration, `(/ 1 2)` in the rational operand uses
  the rational rank. Under ALL declare `/ : Int × Int -> BV1`; `(/ 1 2)`
  must denote that UF, while a wrong argument sort is invalid and never
  falls back to rational syntax. Pair `/` and `|/|`, nested rational signs
  and divisions, a locally shadowing binding, symbolic rounding mode, zero
  denominator, and reserved Real names. Verify `#b1`, `#x0` and `(_ bv1 1)`
  literal admission in FP-only profiles without enabling standalone BV ops.
- **FE-LOGIC-06:** treat bad numeral token `08` as malformed, the valid symbol
  `-1` in a numeral index slot as invalid, width zero as invalid, and a valid
  width beyond the 32-bit representation as a representation failure.
  An undeclared QF_UFIDL `div`, undeclared ALL `to_real`, and builtin String
  without a declaration are unsupported; an unrelated unknown symbol is
  invalid. Where a profile permits it, an explicitly declared free symbol
  with one of those spellings keeps its declared identity and wrong ranks are
  invalid rather than reinterpreted.
- **FE-LOGIC-07:** under QF_AUFBV declare
  `a : (Array (_ BitVec 1) (Array (_ BitVec 1) (_ BitVec 1)))`, then type
  `(select (store a #b0 (select a #b0)) #b0)` and check that its sort is the
  inner array sort. The same builtin nested array declaration is unsupported
  under QF_UFBV, which does not enable arrays. This is a frontend type and
  profile test; array satisfiability is tested by the solver separately.
- **FE-INFO-01:** test `:category` with each documented string and reject an
  non-string category in both modes and an unknown category in strict mode; accept string `:license`, string or quoted
  `:source`, and `sat`/`unsat`/`unknown` status. Test exact `2.6` and `2.7`,
  unsupported well-formed other versions and invalid token classes. Captured metadata
  preserves its order. Metadata never supplies the solver result.
- **FE-DT-SHARING-01:** define `Dup(T)=Array(T,T)`, `A0(T)=T` and
  `A(n+1)(T)=Dup(A(n)(T))` as shared sort recipes, then match a `Box<T>`
  constructor field against `A12(Int)`. Count matched pairs and
  confirm the number follows the 13 distinct recipe/actual pairs, rather
  than the fully expanded tree. In a second case use the same parameter
  recipe against Int and Bool and reject the conflicting substitution. Cover
  same recipe identity reached through multiple arguments, distinct groups,
  an explicit `(as nil (List Int))`, bare ambiguous `nil`, a wrong qualifier
  and a constructor determined by its arguments.
- **FE-NAMED-SHARING-01:** use nested closed `:named` annotations and count
  visits to already verified inner occurrences. Pair a closed unused-let RHS
  with a used outer variable and with structurally shared nodes in different
  lexical scopes. Check source-order alias publication and rollback on an
  invalid inner annotation independently of closure caching.
- **FE-MODEL-01:** under `QF_IDL`, type a post-check `get-value ((+ x 2))`.
  A controlled model
  assigning `x = 1` must evaluate it as `3`; a different valid model may
  return a different value. Request a second term absent from the assertions
  and verify it uses the same total interpretation.
- **FE-MODEL-02:** make a controlled model return a qualified abstract value,
  then request that value and a term containing it. Check model and sort
  identity, unknown names, wrong sorts, cross-model misuse, and destruction
  of borrowed input buffers. Names from a model must not become user
  declarations or independent unconstrained values. Parser-only mode must not
  claim to resolve a value without the corresponding model.
- **FE-MODEL-04:** return an array value and request it again as the argument
  of `select`; an extensionally equal array obtained by a different store
  history must use the same abstract value name. Two distinct arrays must not
  share a name. For datatype values containing arrays or UF values, verify
  recursive canonical printing and readback. Exercise the unique NaN, both
  signed zeros, Bool, an exact Int and a width-sensitive BV; equal semantic
  values retain the same output representation across requests. Force equal
  hashes for two distinct semantic keys and ensure the registry does not merge
  them. Independently test a provider's key equality in both directions for
  equal and unequal model values; an unverified equality reports failure.
  Use an independent two-index truth table: the all-false array equals an
  all-true array updated to false at both indices, but differs from an array
  true only at the `true` index, even before that index was read. The frontend
  registry test supplies correct keys and exercises collisions. Separately
  test the provider's semantic-key obligation; the registry cannot detect a
  provider that supplies one opaque key for two unequal semantic values.
- **FE-MODEL-05:** with two model owners, reject a foreign `ValueRef` or
  proxy handle and an unregistered name. If both models independently expose
  the same textual `@value0`, that spelling denotes each model's own value;
  it does not by itself prove foreign provenance. Check conflicting sort
  qualifiers, multiple requests, source buffer destruction and sink failure.
- **FE-MODEL-06:** a controlled total function with one explicit tuple and a
  default is printed by `get-model` as a `define-fun` with an `ite` and
  correctly typed canonical values. Check observed and unobserved tuples,
  all original user declarations including unused ones, and omission of
  defined aliases from the free signature. If equal tuples are given
  conflicting results, the provider contract fails; this is a provider test,
  not something a frontend given only opaque values can prove. Check no
  forward user references, and reject a missing model before emitting a
  promised `sat`.
- **FE-MODEL-07:** a controlled `f : Int -> Int` model uses the symbolic body
  `f(x)=x`. Inspect the exported `define-fun` and the binding of its formal,
  then check `get-value` at zero, a positive and a negative argument in the
  same model. Do not treat agreement on those three examples as proof that a
  finite constant-default table represents identity over all integers. In a
  separate fixture, use an acyclic dependency on another user function and
  verify output order, semantic rank aliases and lifetime after the input buffer
  is destroyed. A provider unable to export a selected model must report
  capability failure, not replace it with a different function after inspection
  has begun.
- **FE-MODEL-08:** use a controlled QF_UFIDL model where `x=-1`. Verify
  `get-value (x)` prints `(- 1)`, then `get-value ((- 1))` returns the same
  integer, including `(+ (- 1) 2)` in a later request. In a separate session,
  make `get-model` the first inspection: it must express negative values as
  binary `(- 0 1)`, including tuple tests and defaults. Check `INT64_MIN` and
  adjacent exact values. In separate
  commands reject `(- x)`, `(- 0)` and `((as - Int) 1)` as value inputs and
  reject `(- 1)` in an assertion; accept `(|-| 1)` as the unqualified quoted
  form. Do not broaden the underlying UFIDL operator subsignature.
- **FE-MODEL-09:** make `get-model` the first inspection for `f : U -> U`
  with independent values `u0 != u1`, `f(u0)=u1` and default `u0`. Supply
  body proxies and their `ValueRef` bindings, then verify the emitted names,
  tuple branch and default with a hand-computed model and later readback.
  Include an array or datatype field containing an abstract value, the empty
  model, unused user declarations and omission of definitions/`:named` aliases.
  In another export, set global `g=7` and use body `f(y)=g+y` with a formal
  name hint `g`; output must bind the formal distinctly and yield `f(2)=9`.
  Check dependency order, a legal abbreviation for the semantic sort, and no forward references.
- **FE-MODEL-10:** provide a shared body DAG `t0=x`, `t(n+1)=(+ t(n) t(n))`
  through `t12`. Verify capture-safe nested `let` output retains sharing and
  independently evaluates to `4096*x`; simultaneous bindings must not refer
  to siblings in their own group. Reopen the same model inspection session and
  confirm previously exposed abstract names are preserved. Under ALL, declare
  `U` with arity one and first mention `(Array (U Bool) Int)` in a post-check
  value request; lazy model extension must keep earlier interpretations intact.
- **FE-MODEL-11:** under ALL, define an FP conversion of `1/3` with symbolic
  rounding mode before later declaring `/ : Int × Int -> BV1`. Export a
  user function whose model body uses that retained conversion. The printed
  body must retain the rational conversion's meaning under the later binding,
  without treating `/` as that UF; evaluate it for all five rounding modes.
  For format `(3,3)`, independent exact rounding gives `(fp #b0 #b001 #b10)`
  for RTP and `(fp #b0 #b001 #b01)` for RNE, RNA, RTZ and RTN. Check the
  emitted `ite` uses the RM term consistently, preserves bindings and avoids
  introducing a Real term. Test conversion failure before and after response
  output begins according to the stream contract.
- **FE-MODEL-03:** test `sat` with model production enabled and disabled,
  `unsat`, and `unknown` with and without an inspectable candidate model.
  An unknown candidate is not certified to satisfy assertions. Check output
  and model-access failures after an already emitted result.
- **FE-SCALE-01:** use finite, manageable deep/wide input, long tokens and
  exact numerals to exercise streaming, cleanup and allocation-failure
  paths. Test mathematical invalidity separately from actual representation
  or allocation failure. Include a lexical token longer than the formerly
  proposed 1 MiB cap, using generated input outside any dataset. Test sizes
  are fixture choices, not product limits; no byte, depth, expansion-work or
  output quota is part of the API. Finite tests do not prove unbounded capacity.
- **FE-SORT-LIMIT-01:** test `(_ BitVec 4294967295)` as a representable sort,
  `(_ BitVec 4294967296)` as mathematically valid but beyond the retained
  representation, and width zero as invalid. For FP, distinguish a valid
  `e+s` at `UINT32_MAX` from a valid larger sum and invalid components.
  These cases check sort metadata and error classification without requiring
  allocation of width-sized values or execution of a solver.

Test a declared logic separately from the constructs actually used. For example,
a `QF_BVFPLRA` script may contain only BV/FP terms, while another script under
the same label may use symbolic Real arithmetic. Specify whether the first
script is accepted by the chosen profile; classify the latter according to the
project's exclusion of general Real solving. A concrete rational converted to
FP is accepted only under the selected compatibility grammar. Do not assume that
the logic label alone settles these cases, or silently accept an unsupported
operation because a reference parser does.

### Provider signature checks

A controlled provider can attach to the query manager and construct Nodes
with valid ranks that still refer to declarations absent from the sealed
signature. Exercise an uninterpreted sort with the same name as an original
sort and a different constructor identity, including through an abstract
proxy and Boolean result. Also exercise a standalone BV operation in an FP
profile. The model-export and programmatic-evaluation boundaries report a
model failure. Prior check output remains visible and the provider's
evaluation callback is reached only for terms admitted by the signature.

## Small examples with explicit expectations

The examples below specify observable behavior without prescribing concrete
C++ test APIs. Add the corresponding scoped GoogleTest cases before the
frontend behavior is implemented; they may initially fail to compile if the
interfaces do not yet exist. Each case should identify its profile,
input, expected outcome, expected semantic observations, and independent
basis. Names here are suggested stable case identifiers.

### FE-BV-001: indexed operation and width mismatch

This example requires no solver:

```smt2
(set-logic QF_BV)
(declare-const x (_ BitVec 8))
(assert (= ((_ extract 3 0) x) #b1111))
(check-sat)
```

Verify that `x` has sort `(_ BitVec 8)`; the extract indexes are `3` and `0`
and its result has sort `(_ BitVec 4)`; the literal has width 4 and value 15;
and the asserted equality has sort `Bool`. The reference to `x` resolves to
its declaration. The command is recognized without claiming a solver result.
Replacing `#b1111` with `#x0f` makes the equality ill-sorted: the literal has
width 8, while the extract has width 4.

For an 8-bit `x`, add valid `((_ extract 0 0) x)` with a 1-bit result, and
invalid `((_ extract 8 0) x)` and `((_ extract 3 4) x)`. Check the indices
and result sort, not merely the acceptance result.

### FE-BIND-001: parallel `let` and shadowing

`let` introduces bindings in parallel. In this expression, the inner binding
of `y` refers to the **outer** `x`, not the `x` being introduced beside it:

```smt2
(let ((x #x01))
  (let ((x #x02) (y x))
    (= x y)))
```

Verify the bindings and sorts according to the chosen term representation.
For example, a semantics-preserving expansion compares `#x02` with `#x01`.
The test must not require the frontend to use expansion or any particular node
layout. See the [SMT-LIB 2.7 reference](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf).

As a negative companion, use `(let ((x #x01) (y x)) (= x y))` where no `x`
is declared in the enclosing scope: the reference in `y` is unbound. A parser
that incorrectly treats bindings as sequential could accept it. A separate case
should reject duplicate names in one simultaneous binding list.

### FE-LEX-001: quoted names and metadata

```smt2
(set-logic QF_BV)
(set-info :source "quoted ""word"" ; (still metadata)")
(declare-const |x;y(z)| (_ BitVec 8))
(assert (= |x;y(z)| #x00))
(check-sat)
```

The semicolons and parentheses inside the string and quoted symbol are data,
not comment or command delimiters. The two occurrences of `|x;y(z)|` resolve
to the same declaration and the assertion is retained. With source capture enabled, check the doubled quote in the captured
payload. With capture disabled, check the null source view and equivalent
typed assertions. Pair this with an unterminated quoted symbol, which must
be reported as malformed input.

### FE-INT-001: exact large numeral

```smt2
(set-logic QF_LIA)
(declare-const n Int)
(assert (= n 18446744073709551616))
(check-sat)
```

Check that `n` and the numeral have sort `Int` and that the numeral's exact
value is preserved; it is larger than an unsigned 64-bit value. A companion
case such as `(div n true)` must be rejected as ill-sorted, since the second
operand is Bool, not Int. This is a frontend type check, not an evaluation of
division; both operand sorts are available in this profile.

### FE-FP-001: FP operator and rounding-mode types

```smt2
(set-logic QF_FP)
(declare-const f (_ FloatingPoint 8 24))
(assert (fp.eq (fp.add RNE f (_ +zero 8 24)) f))
(check-sat)
```

Check the FP format, the rounding-mode value, the operand/result sorts of
`fp.add`, and the Boolean result of `fp.eq`. Replacing the second FP operand
of `fp.add` with `#x00` is an ill-sorted case. An additional test must preserve
the distinction between `fp.eq` and core `=` without assuming that either
operation has been evaluated by the frontend.

### FE-MIX-001: datatype, UF, and BV sorts

```smt2
(set-logic QF_UFBVDT)
(declare-datatype Packet ((packet (bits (_ BitVec 8)))))
(declare-fun tag (Packet) (_ BitVec 8))
(declare-const p Packet)
(assert (= (tag p) (bits p)))
(check-sat)
```

For a profile that accepts this logic and declaration syntax, check that
`packet` constructs a `Packet`, `bits` selects a BV8 field, `tag` maps a
`Packet` to BV8, and the equality is Boolean. Replacing `(bits p)` by
`((_ zero_extend 1) (bits p))` makes the equality ill-sorted because that
term is BV9. A reference parser that ignores this logic name may still provide
limited type evidence, but has not validated the complete input profile.

## Phase 1 test design

Add these GoogleTest cases before implementation. Run representative inputs
through text and fragmented Reader adapters. Run binding, rollback and
metadata cases with capture off and on. Run the applicable existing binding,
syntax, model, datatype, canonicalization and benchmark suites as well.
Derive each check from the written scope, exact sorts, declaration
identities and provider fixture. Use test-only counters or fault hooks for
storage and publication invariants, and preserve existing checked cases
through the test-first transition.

| Case | Fixture and expected result |
| --- | --- |
| FE-P1-ALIAS | Under QF_LIA declare `g : Int`. Establish a root for `(+ g 1)`, then define `(define-fun a () Int (+ g 1))`, `(define-const b Int a)` and a further chain using both forms. Uses of `a`, `b`, `(as b Int)` and the final alias reuse the original body node. In a `let` that shadows `g`, the body still references the global declaration. A local `b` shadows only the written use in its scope. Compare Node storage before and after the definitions with the body already live. There are no new declarations, instances or wrapper nodes, no semantic definition entries and no free function entries for these names. Repeat with an alias to a saturated parameterized call and confirm only the existing call identity is reused. |
| FE-P1-ALIAS-ERROR | For both nullary forms reject an ill-sorted unused body, mismatched result sort, self-reference, forward or unbound reference, duplicate or active reserved name, and collision with a free function or a prior `:named` alias after dequoting. Reject positive-arity use and a mismatched `as` qualifier. Also reject a body containing an ill-sorted unused let RHS. A body with valid local binding, such as `(let ((x 1)) (+ x 2))`, is closed. Check the blamed token or body span from the source. After each body or rank failure for a fresh name, a legal command can bind that name. After a freshness error, the prior binding still resolves. |
| FE-P1-QUERY | Statically check the three semantic spans, count and ordinal signatures, absent source getters, absent raw-ID overload and absent `TypedCommand::annotations`. Build two free declarations separated by parameterized templates, formals, a failed command, and a datatype group. Assert the same Bool formula twice. Verify the ordered duplicate roots, original free declaration and sort identities (including an unused declaration), datatype owners, logic, options and manager. `HasFunction` returns false for null, a foreign lookalike, an internal template, a formal and a datatype member. `FindDefinition` returns the exact committed ordinal record for each template and null for those non-template identities, original free functions, null and a foreign lookalike. Preserve ownership after input and session destruction. |
| FE-P1-TEMPLATE | Store `f(a,b)=a-b` and `g(x)=f(x,f(x,1))`, with a nullary alias in between, and verify two templates in order, distinct ordered formals and saturated calls from one to the other. Check `f(5,2)=3`, `f(2,5)=-3` and `g(5)=1` with the independent small evaluator. Include one captured global and a caller-side shadow. Reject wrong arity, wrong argument or result sort, repeated formal and self-reference, including when the template or actual is unused. Retain the compressed call-count and scale checks of FE-DEF-02. |
| FE-P1-NAME | Check `(! (! 1 :named inner) :named outer)` followed by sibling uses. Both aliases name the Int literal and publish in inner-then-outer order. Test a later parallel let RHS referring to a name from an earlier RHS, and reverse the order to fail. Check a name in a definition body is available after that definition, and repeated alias references and parameterized calls publish no names or annotations. Reject duplicate `:named` on one form, collisions after dequoting including a pending definition name, non-symbol names and `:named` in value requests. Check ordinary attributes and unsupported `:pattern`. |
| FE-P1-ESCAPE | Swap the order of `(! 1 :named closed)` and an annotation on a local bound to that same literal across commands. Closedness follows the written occurrence. Reject a formal reference, an enclosing pattern variable and a reference appearing only in an unused actual of a parameterized call. Within a let binding `x`, naming `(let ((unused x)) 0)` succeeds, and naming `(let ((used x)) used)` fails. Reject an outer dependency retained by a first-match-unreachable branch. Accept naming the whole match when dependencies are confined to its own branch pattern scopes. Keep unused RHS and branch sort errors. Compare large binder-free and nested binder fixtures using test instrumentation to verify no per-operand escape data on the binder-free path and one reduction per written occurrence, including nested annotations. |
| FE-P1-ROLLBACK | Begin with committed declarations, aliases and captured occurrences. Stage a new name in an early child and cause a late unbound name, open annotation or result-sort error. Repeat with malformed command syntax, which prevents application. For well-formed failures retry with the same name in a corrected command. Sweep applicable allocation checkpoints for name insertion, definition publication, source spelling and history storage, and sealing. Reacquire views after failure and compare all semantic ranges, the seven captured histories, options, state and old alias resolution to their committed values. A failed seal leaves aliases usable. A retry publishes exactly one effect. Drive a rejected constraint followed by a check and verify no solver invocation. |
| FE-P1-SOURCE | Use one script with a quoted sort and free declaration, compound written ranks, both nullary commands, a parameterized definition, two identical assertions, nested named and ordinary annotations, and ordered repeated metadata before and after check. Capture off yields null at every phase. Capture on starts empty and records exact counts, order, names, ordinal mappings, shared typed identities, token renderings and byte spans across short reads. Check unchanged semantic observations across modes. Copy captured records explicitly, destroy the backing input and session, then verify the copies' spellings and owning handles. The query or owner alone keeps no session history. |
| FE-P1-METADATA | Cross both capture values with both strict values. Accept each standard category and require default acceptance of `"check"` and an arbitrary string with doubled quotes. In strict mode those two yield invalid input at the value. Reject bare category symbols, a missing recognized value, extra values and an unclosed S-expression. Admit both exact versions and reject another well-formed decimal as unsupported and a quoted or integer version as invalid. Check all three status symbols, malformed status forms, both source forms, string license and opaque attributes with and without a value. Verify ordered captured repetitions and rollback. Use deliberately misleading `:status unsat` with a controlled `sat` result and check one engine call and its emitted answer. Pair a well-sorted nonlinear QF_LIA input with strict metadata enabled to exercise unchanged signature admission. |
| FE-P1-SEAL | With models and capture off, build an unused, nonliteral alias body and another alias body reachable from repeated assertion roots and a retained parameterized template. After successful check, observe reclamation of the unused alias-only graph and survival of all expected query roots and signatures through Node inspection. Use a test-only batch observation to count distinct owning slots including repeated aliases of one node. Repeat with independent retained root/child handles, and with capture on so the independent source records keep their roots. Destroy the session, then drop query and remaining handles and check reclamation and table detachment with a separate ManagerRef. Repeat model-enabled with no captured history and prove the unused body still resolves in detached inspection. |
| FE-P1-MODEL | Enable model production. Declare an unused function plus `|plain|` and `|needs space|`, and use nullary and parameterized sort aliases with a shared nested array rank. Include a nullary term alias, a `:named` term and a parameterized definition. Destroy the original input and session before calling either `ForModelText` or `ForModelReader`. Check full free export coverage, necessary quoting only, dependency ordering and ordered semantic domain and range. Reparse rank text under the sealed signature and compare concrete sort identities. Use a deep shared sort chain to require compact alias output, two equivalent aliases to check deterministic selection, and a repeated parameter recipe to distinguish equal from unequal actual positions. Verify unused alias and unseen sort resolution, qualified array abstract readback and same-owner rejection of a foreign model. Exercise missing, duplicate and foreign export entries with the existing response and flush boundary tests. |
| FE-P1-INSPECT | Exercise the legacy and explicit default model overloads and all three option fields in both model adapters. Capture starts fresh after detachment despite original metadata or declarations. Accepted post-check metadata publishes only to the new session, and a strict rejection rolls back. Verify exact typed request input and span with or without capture, including both bare and qualified spellings of an exposed abstract value, ordinary quoted token spelling and a direct alias operand. An ordered-then-canonicalized request confirms option forwarding without changing printed input. |
| FE-P1-NODE | Compare ordinary indexed parsing with checked Node construction for extract, extension and rotation. Include an exact index beyond one word where the selected rank allows it, and out-of-order or unrepresentable indices with exact diagnostic attribution. At widths 1, 64 and 65 compare binary, hexadecimal where applicable, and indexed decimal literals, including width reduction of an oversized indexed numeral and distinct widths sharing low bits. Verify declared ranks after destroying borrowed rank scratch. Use FE-OVERHAUL-09 and FE-OVERHAUL-10 for shared commutative policy and compare rebuilt parents that carry instance, index, qualifier or rational payloads. Verify original roots and failure diagnostics. |
| FE-P1-NAMES | With long names, optional quoting, forced table growth and distinct colliding keys, resolve old free declarations, parameterized calls and aliases to their exact identities. Exercise rollback and retry after growth, absent-name probes and local shadows. Test-only observations verify manager-backed canonical keys for eligible declarations, stable pooled keys for aliases and no insertion on failed lookups. Inspect the built non-testing frontend for absence of `Checkpoint` calls and failure-hook state. The rollback suite exercises the testing build. |
| FE-P1-BENCH | Parse a small script with both nullary forms, one parameterized definition, a named annotation, repeated metadata and one check. Timed parsing keeps capture off and counts three definition commands. Inspect captures source records, counts one semantic template plus the two nullary bodies in the graph walk, and retains schema version 1 command and occurrence counts. Verify assertion, signature and root counts by hand. Repeat with a failed final command and inspect only committed history. |

For FE-P1-BENCH, use the following fixture.

```smt2
(set-info :status unknown)
(set-info :status sat)
(set-logic QF_LIA)
(define-fun a () Int 1)
(define-const b Int a)
(define-fun id ((x Int)) Int x)
(assert (= (id (! b :named n)) n))
(check-sat)
```

Expect eight successful commands, one assertion, one check, three term
definition commands and one semantic template. Inspection has two metadata
records, one name, one annotation, six root occurrences and zero free user
functions and sorts. The body of each nullary definition, the named term
and the annotation share the literal `1`.

For the model rank fixture, a nullary alias chain can use
`S0 = Int` and `S(n+1) = Array(Sn, Sn)`. A short top alias must be selected
over the expanded rank. Declare a free function with the top sort written in constructor form and
verify its export selects the top alias, with identical output in both
capture modes. Add a parameterized `Pair(T) = Array(T,T)` and a control
`Array(Int,Bool)` that it cannot spell. For standalone canonicalization,
place reorderable children under each payload-carrying parent and compare
the result with independently constructed checked nodes of the expected order
and rank. Check that textual diagnostics retain the written argument order.
These tests measure structural work and ownership through explicit
observations. They impose no timing or RSS thresholds.

## Phase 2 test design

These GoogleTest cases specify the
[fused protocol](../frontend/semantic-interfaces.md#phase-2-command-processing-and-publication)
and its acceptance requirements. Use the public
`ReadCommand`, `Run` and `SyntaxValidator` paths, instrumented Reader
and sink callbacks, and controlled engines and model providers. Confirm
exact source IDs, offsets, lines, byte columns, diagnostic categories and
structured errors when applicable. Distinguish an emitted engine response
from a solver-free check descriptor. Retain the earlier acceptance cases
and follow the project test-change rule during migration.

| Case | Fixture and independent expectation |
| --- | --- |
| FE-P2-SPLIT | Run a fixture with nonzero source ID through text, a Reader split once at every boundary, and a Reader returning exactly one byte per call. Include simple and quoted names, an indexed extract, exact BV and decimal tokens, an echo with doubled quotes and a multibyte scalar, a comment containing UTF-8, and LF, CR and split CRLF. Use a Reader that overwrites the refill window. Verify full command spans, exact echo bytes, sort and declaration identities, and exact BV values from the written widths and digits. End the final command at a chunk boundary and require no EOF read until the next call. Exercise an identical fixture with capture enabled and compare captured token renderings and occurrence spans. |
| FE-P2-COORD | With one-byte reads, place a BV width mismatch on the second argument in `(bvadd #x01 #b1)`, and an invalid significand index in `((_ to_fp 5 1) RNE #x00)`. Put the blamed operands or indices before a split CRLF, comment and multiple later tokens so the diagnostic outlives their windows. Check the structured operand or parameter index and exact token span. For an unattributed arity error in `(bvsub #x01)` require the full application span. Test invalid leading bytes, an overlong encoding, a surrogate, a bad continuation and EOF mid-scalar in quoted data or a comment. Check the point at the first byte, with a preceding valid multibyte scalar to distinguish byte and scalar columns. Use the coordinate fixture below as a fixed oracle. |
| FE-P2-DEPTH | Generate 16,384 nested unary `not` applications over a declared Bool, and independently verify one child per level and the original leaf. Repeat inside an unterminated command after an early unbound operand and require terminal syntax error, no root publication and iterative cleanup. Drive an equally deep opaque metadata value valid and one delimiter short, with and without capture. With capture disabled test scratch high-water on a deep value and a shallow value containing many sibling atoms. Completed siblings require no retained metadata text or syntax records. |
| FE-P2-WIDTH | Use 8,192 ordered operands in `and`, with distinguishable Bool leaves and repeated occurrences. Check ordered edges and exact multiplicity using checked Node identity. Add 4,096 distinct parallel `let` binders, with an outer `x` distinct from the new `x` RHS. In one later RHS use `x` and the name staged by an earlier RHS; in the body use the new `x`. Check the three references resolve to the outer term, staged alias and new local term respectively. Reverse the named uses to fail. An invalid unused RHS still fails. Compare binder-free and scoped frame observations. Verify an owning operand span goes to construction without materializing a second array of views, including with optional canonicalization on. |
| FE-P2-FLAT | Combine nested shadowing, ordinary and indexed applications, wide sibling lists and a long indexed BV numeral in one-byte Reader mode. Check the typed edges and exact modulo value. For both capture settings, the term depth and width must not grow the retained syntax fragment count. Compare a late index error with the same command containing an extra command argument. The first case blames the written index and the second terminates with a syntax error. Split a quoted 65-bit BV numeral within the name, after the complete name and after the width. Verify the same exact value at each split and then read another indexed numeral in the same command. An extra index on a BV literal must report the index-count diagnostic for the full literal and leave the next assertion readable. |
| FE-P2-DATATYPE | Use the mutual Tree/Forest group below with one-byte reads. Check the forward field of `node` resolves to `Forest Int` and the recursive `cons` fields resolve to `Tree Int` and `Forest Int`. Check that `nil` and `leaf` are finite witnesses, and type a qualified constructor use and a match. Inspect the previous group and sort ranges in a Reader callback between headers and fields. Check group publication after close, and rollback for a header/body count mismatch, an unknown late field sort and a nonproductive group. Retry the fresh failed names in well-formed cases. |
| FE-P2-FP | With fragmented input under `QF_FP`, convert `(/ (- 1.5) (/ 3 2))` with `((_ to_fp 8 24) r ...)` and a declared symbolic RM `r`. Expect a Float32 rational conversion with exact reduced payload `-1/1` and only the RM child. For `(/ 1 (/ 0 3))` check the unsupported diagnostic spanning the entire `(/ 0 3)`, across CRLF and comments. Under `QF_UFFP`, in separate fixtures declare `|/|` or `|-|` with domain `(Bool Bool)` and BV1 range. Require signed-BV conversion on a valid bound call and an ordinary rank or type diagnostic on a bad call. Include `(/ (- 1) 2)` with only the inner head bound, and `(let ((/ #b1)) ((_ to_fp 8 24) r (/ 1 2)))` for a lexical binding. Use an unbound control to select the rational rank. Under `ALL`, use `(- (- 4) 1)` as the second operand. The outer binary minus disqualifies the candidate, and the Int result fails the FP/BV rank at that operand. Compare `((_ to_fp 8 24) r (- 1) #b1)` for wrong arity. With `(/ 1 (/ 0 3))` as the second operand and a third operand present, require an ordinary division diagnostic without the rational zero-divisor detail. |
| FE-P2-PRECEDENCE | After a valid prefix, drive an early unbound-name error followed by an extra `assert` slot, an invalid token, a missing close or an I/O failure. Expect syntax or I/O ahead of unbound-name failure, no further semantic reductions after the error, and no reads after terminal failure. Contrast a well-formed command with two unbound occurrences: blame the first in reduction order and let the next call commit a legal assertion. Include a semantic error on an open head with a span closed only during drain, and a well-formed unsupported command with a malformed version of the same command. |
| FE-P2-STAGING | Establish an assertion, a declaration and a captured named occurrence. In the next command, pause refill after reducing `(! true :named n)` and before finishing the surrounding `and`. The Reader inspects `GetPrefix()`, `GetOptions()`, `GetPhase()` and `GetSourceRecords()` on every callback. Compare all committed ranges and history counts to the snapshots from before the call. A subsequent operand `n` resolves within that command. On success observe exactly one assertion and named occurrence after return. With a late ill-sorted operand, observe the old prefix and successfully retry with the same name. With a malformed tail, observe the old prefix and terminal syntax. Repeat with capture off. Assert that plain declarations and assertions create no staging overlay in either capture mode. |
| FE-P2-REENTRANCY | During a refill, observe the committed prefix through all four Session getters. Attempt nested `ReadCommand` and `Run` on the active Session, and separately nested `SyntaxValidator::ReadNext` on the active validator. Require `kInvalidState` for each nested call, then verify the outer read completes with its exact expected result and intact state. |
| FE-P2-RESOURCE | Sweep frontend and applicable Node allocation checkpoints across lexical scratch growth, live grammar and operand growth, overlay insertion, pending text and source histories, descriptor preparation, staged sort/function publication, datatype recipes and commit, and sealing. Inject each failure after a successful prefix. Assert exact resource classification, no new visible effects, and surviving original handles. Recover and retry the same spelling after a well-formed failure if grammar reached the boundary. Pair one recoverable semantic allocation fault with a malformed tail and require grammar to win. When frame growth prevents completion, require termination and zero later Reader calls. Compare capture-on and capture-off state and require one effect on successful retry. |
| FE-P2-INPUT-IO | From a prior committed alias and assertion, return an explicit I/O error, throw, or return a byte count beyond window capacity during the next command with pending aliases. Check `kInputIo` at the current point, preserved explicit error code, rollback and terminal no-read behavior. Compare a zero-byte successful read at the same truncated offset, which is terminal syntax. A zero-byte success between commands ends normally. |
| FE-P2-RUN-GATE | With model production enabled and a controlled `unknown` result without a candidate, run `(get-value (missing))` and require `kModelUnavailable` ahead of unbound-name failure. Compare an empty list, missing close and extra request slot, which yield syntax. Pair valid and malformed `get-model` after `unsat` and `:reason-unknown` after `sat`; valid forms fail state, malformed ones fail grammar. Test a pre-check request and a disabled-model request as phase/option controls. In every rejected request check one prior engine invocation where a check succeeded, no evaluation or model rendering, and no response bytes except earlier responses and one diagnosed error. After `unknown`, verify reason output uses the actual reason even without a candidate. |
| FE-P2-CHECK-EXIT | Have the Reader return through the `check-sat` closing byte, then require engine count one and a complete flushed result on the next callback, before yielding either EOF or a suffix. Use a buffered bad suffix with write/flush and diagnostic event logging, and verify the result precedes the suffix error. A truncated check never calls the engine. For `exit`, offer a suffix that would throw if fetched separately and a buffered suffix containing illegal UTF-8 and delimiters. Check one committed exit descriptor, any enabled success response, no suffix lexing and no extra Reader call. A malformed exit yields an error. |
| FE-P2-RESPONSES | Exercise fixed and dynamic response tags with allocation observation at descriptor construction. Verify exact text for the supported fixed info and option defaults, unsupported harmless queries and the actual reason. Check that toggling `:print-success` governs its own acknowledgement. For `echo`, span refills inside doubled quotes, CRLF and UTF-8 and compare the exact raw string token. With no response tag check no write and no flush. Destroy the source/session before reading the returned echo payload. |
| FE-P2-VALUES | With a controlled owner and capture both off and on, read a multi-term request containing comments, quoted spellings, a direct alias, a bare exposed abstract name and its qualified form. Check normalized token spacing, exactly one qualifier per abstract reference, untouched quoted lookalikes, exact request spans and typed identities, and retained text after source destruction. Compare a late ill-sorted, malformed or unsupported request against the same valid first term. No evaluation or value-response byte occurs in any failing variant. Cover the QF_UFIDL negative-value exception, an unknown abstract name, a solver-free request that needs an unavailable model binding, same-owner detached resolution and a foreign owner sharing the query. |
| FE-P2-VALUE-OUTPUT | Prepublish one abstract name with a complete response. In a later two-value response, stage a distinct value and fail a write after its spelling has partly or wholly escaped. Separately fail flush after all bytes, fail evaluation of the second term after earlier bytes, and inject allocation failure preparing a new registry entry. Assert failure leaves the stream terminal with no appended error after any committed byte, no later command processed, and no new visible binding. In a detached session on the returned owner, resolve the earlier name, reject the uncommitted name, then retry and publish the new name using the next committed ordinal. Include a control that fails before the first byte and can emit one complete error if the sink is usable. |
| FE-P2-SYNTAX-ONLY | Read an undeclared application and an assertion before logic using `SyntaxValidator` and require headers with exact spans; the typed reader diagnoses missing logic or bindings as applicable. Check well-formed excluded forms, malformed nested binders, split metadata and syntax-only exit on buffered malformed bytes. Confirm the grammar path can run without a semantic session, factory or owning whole-command syntax records. |
| FE-P2-BENCH | Run the Phase 1 benchmark fixture above through all fused adapters. Expect eight committed commands, three term definitions, one template, one assertion and one check. Stop on a well-shaped failing suffix and verify the schema 2 counts and committed-prefix offset. Check read timing and teardown fields and report integration without timing or RSS thresholds. |

For FE-P2-COORD, the bytes `(echo "é")\r\n(echo "` followed by `E2 82`
and EOF put the incomplete scalar at offset 20, line 2, column 8.
Its span is empty at that point. For delayed operand and index attribution,
compute endpoints from the fixture's explicitly recorded byte positions
as the fixture is built, using literal line fragments and hard-coded
column expectations.

For FE-P2-DATATYPE use this group under `ALL`.

```smt2
(declare-datatypes ((Tree 1) (Forest 1))
  ((par (T) ((leaf (value T)) (node (children (Forest T)))))
   (par (U) ((nil) (cons (first (Tree U)) (rest (Forest U)))))))
```

For resource and lifetime observations, use testing-only checkpoints
and counters at the responsible operation. Preserve the existing
allocation and I/O diagnostic policies and check progress only through
observable boundaries. Run the applicable frontend and Node cases under
GCC and Clang and the working sanitizer configuration in the
[acceptance procedure](../frontend/design.md#build-and-acceptance-commands).
Record unavailable configurations and the first missing-interface build
accurately. The structural cases use bounded sizes and require no corpus
or solver backend.

## Case-level traceability

The table links the documented requirements to existing cases. The
[acceptance record](../frontend/implementation.md#build-and-test-evidence)
records the baseline and earlier suite results. Existing case names and
baseline observations are traceability evidence. The listed files are under
`tests/frontend/`. The Phase 1 cases above govern the revised API and
output expectations.

| Requirement | Existing cases and observations |
| --- | --- |
| FE-LEX-001 and FE-STREAM-02. Byte spans, token spelling, command grammar and immediate exit. | `ExactSpansAndLexemesAtEverySplitPoint`, `MalformedTokensAndTruncatedCommands`, `ChecksTermAndCommandGrammarBeforeSemanticState` and `StopsOnExitWithUnreadOrAlreadyBufferedSuffix`. `syntax_test.cc` |
| FE-STREAM-01 and FE-STREAM-04. Input is processed while the reader is open. Explicit read errors differ from EOF. | `ReportsReadFailureSeparatelyFromEof` and `CheckWritesAndFlushesBeforeReadingAnOpenStream`. `syntax_test.cc`, `driver_test.cc` |
| FE-INFO-01 and FE-STREAM-03. Metadata order, option visibility, command state and owned query. | `PrefixStateMetadataAndOwnedCheckContext`, `StatusLogicOptionAndDeclarationErrorsPreservePrefix`, `FixedFalseOptionsGettersAndHarmlessUnsupportedQueries` and `SyntaxPendingAndRecoveringAfterFailedApply`. `command_test.cc` |
| FE-LOGIC-01–07. Profile components, C/S/U permissions and inactive symbol identities. | `Registered/LogicProfile.SignatureAndDeclarationPermissions` checks profile admission, the initial equality and defined call, and Boolean root sorts. `InactiveSpellingCanHaveUserIdentity` and `ArrayCombinationPreservesInnerSortAndMixedBridges` check declaration identity and cross-theory ranks. `profile_test.cc` |
| FE-OP-CORE/INT/BV/BRIDGE/FP/ARRAY-DT. Ordinary builtin ranks and indexed spellings. | `CheckedRanks/OperatorSpelling.TypedKindOrderedOperandsAndBadRank` checks typed kind, ordered children, result sort and indices, with zero operands where illegal and one wrong first-operand sort. `CompatibilityFormsAndResultQualifiers` and `IndicesWidthsAndFormatsKeepDistinctFailureCategories` cover short Core ranks, qualifiers and selected index, width and format errors. Datatype ranks are also tested by `ParameterizedMutualConstructorsSelectorsAndTesters`. `operator_test.cc`, `sort_datatype_test.cc` |
| FE-BV-001, FE-INT-001, FE-FP-001 and FE-LOGIC-05. Exact literals and restricted rational operands. | `ExactBvAndIntValuesAndFpCategories` checks width-sensitive BV identity and exact Int and FP payloads. `RoundingModesFormatsAndRationalIdentityPrecedence` checks rounding symbols, normalized rational payload and identity-based rank selection. `operator_test.cc` |
| FE-BIND-001 and FE-DEF-01–03. Parallel bindings, retained bodies and actuals, and scaling with written definitions. | `SimultaneousLetAndLexicalRestoration`, `DefinitionIdentityRetainsOriginalBodyAndOrderedArguments` and `WrittenDefinitionGraphDoesNotExpandCalls`. `binding_test.cc` |
| FE-NAMED-SHARING-01. Publication order, source closure and rollback. | `PostorderPublicationClosureAndMetadata`, `ClosureReusesCheckedOccurrencesAndAliasesRollBack` and `RetainFailuresPreservePublishedPrefixAndAllowRetry`. `binding_test.cc` |
| FE-SORT-LIMIT-01, FE-DT-ERROR-01, FE-DT-SHARING-01, FE-MIX-001 and FE-DEF-04. Alias ranks, datatype ownership, guarded match lowering and mixed sorts. | `AliasParametersOrderedRanksAndFpAliases`, `RepresentationLimitsAndExactlySizedSorts`, `ParameterizedMutualConstructorsSelectorsAndTesters`, `ConstructorRecipeMatchingSharesPairsAndRejectsConflicts`, `InvalidGroupsAndUnsupportedContainersAreAtomic`, `FirstMatchChecksEveryBranchAndLowersGuardedSelectors` and `DatatypeArrayFpAndUfRanksAndBoundaries`. `sort_datatype_test.cc` |
| FE-SCALE-01. Iterative handling of long and nested input. | `DeepOpaqueAttributesAndLongUtf8Symbol`, `DeepTermsWideDistinctAndLargeExactNumeral` and `DeepShorthandSortsAndNestedMatches`. `syntax_test.cc`, `binding_test.cc`, `sort_datatype_test.cc` |
| FE-STREAM-02–04 and FE-MODEL-03. Exact check timing, result eligibility, committed response prefix and sink failure. | `BordersOfTheCommittedPrefixAndImmediateExit`, `ResultPrerequisitesAndUnknownCandidates`, `ErrorsBeforeAndAfterResponseBytesAndSinkFailures` and `NoResultOnEngineOrInputFailureAndMalformedModelSyntaxFirst`. `driver_test.cc` |
| FE-MODEL-02, FE-MODEL-04 and FE-MODEL-05. Canonical values, same-model readback, array equality and provenance. | `CanonicalValuesArrayKeyCollisionsAndSameModelReadback`, `ProxyAndValueHandlesRejectAnotherModelOnSameManager`, `ScalarCanonicalOutputAndFiniteness`, `StoreOnExposedArrayPreservesExtensionalKey` and `ParameterizedDatatypeValuesUseNecessaryQualifiers`. `model_test.cc` |
| FE-MODEL-01 and FE-MODEL-06–11. Query lifetime, total inspection, dependent export and exact FP rendering. | `RetainedQueryAndDefinitionsSurviveInputAndDriver`, `ExportBeforeValueSharesTotalInterpretationAndOriginalRanks`, `SharedExportUsesCaptureSafeDependencyOrderedLetBindings`, `UfidlNegativeValuesReadBackAndExportAsBinarySubtraction`, `LaterInspectionCanReachNewConcreteSorts` and `RationalConversionExportSurvivesLaterSlashDeclaration`. `model_test.cc` |
| Provider signature and model response boundary. | `RejectsIncompleteOrOutOfOrderExportsAndForeignValues`, `ValidNodeRanksCannotSmuggleSortsOutsideTheSignature` and `FpRoundingFailureRespectsModelResponseBoundary`. `model_test.cc` |
| Benchmark regression and oracle disposition. | `ClassifyTest` checks process and protocol records. `RunnerVerdictTest` checks the report and exit status with synthetic process outcomes, including zero sample XOR evidence. `benchmark_regression_test.py` |

## Baseline refinement cases

The GoogleTest suite implements these baseline refinements. The
[verified frontend baseline run](../frontend/implementation.md#verified-frontend-baseline-run)
records their verification before the post-fix cleanup. The
[operator schemas](../node/operators.md), [logic profiles](../frontend/logic-profile.md)
and [frontend semantic interfaces](../frontend/semantic-interfaces.md)
provide the oracles for exact success results and failure diagnostics.

### Typing, binding and datatype cases

| ID and target | Fixture and expected observations |
| --- | --- |
| FE-RANK-02, `operator_test.cc` | Strengthen the per-spelling negatives using the rank of each case. For fixed ranks, remove one operand where the remaining form cannot select another accepted rank, and append one otherwise well-sorted operand. For n-ary ranks, go below the declared minimum and keep an accepted larger instance. Test incorrect sorts at the positions with distinct restrictions, including FP rounding mode and later FP value operands, `ite` condition and branch agreement, BV width agreement, and array index and element ranks. For indexed operators vary the index count, token class or range where meaningful. Keep overload transitions and Core `and`/`or` short ranks as positive controls. |
| FE-RANK-SPAN-02, `operator_test.cc` | In an otherwise valid multiline request, use `(bvadd #x00 #b0)`. Expect `kInvalidInput`, retained `NodeError` with `operand_index=1` and the exact `SourceSpan` of `#b0`. Exercise the shared application diagnostic mapping with a `NodeError` attributed to the first written `to_fp` index. The frontend produces this error during minimum-bound validation. Direct Node construction with a negative index also produces an attributed error, but the indexed SMT-LIB form accepts only numerals for these operators. Verify the full application span on a builtin error with neither index. Check source ID, offsets and line/byte columns. For `((_ to_fp 5 11) RNE (/ 1 (/ 0 3)))`, expect `kUnsupported` and the exact span of `(/ 0 3)`. |
| FE-PROFILE-SHAPE-02, `sort_datatype_test.cc` and `operator_test.cc` | Under QF_SAT, `(Array Real)` is invalid (`kInvalidInput`), and a `declare-datatypes` group reports its first deferred `kUnsupported` diagnostic under QF_SAT and `kInvalidInput` for unequal header and body counts under QF_DT (with grammar failure winning when unclosed). Well-shaped Array or datatype declarations under QF_SAT stay unsupported. `((_ fp.to_real 0) true)` is invalid because `fp.to_real` is unindexed. A plain `fp.to_real` application or an excluded regex operator with the expected numeral indices remains unsupported. Reject wrong regex index counts and symbolic indices. |
| FE-FP-02, `operator_test.cc` | Convert a known FP(8,24) operand using `((_ to_fp 5 11) rm f)`. Check `kFpConvert`, ordered children `rm,f`, exact indices 5 and 11, and FP result components 5 and 11 in that order. A BV16 packed operand selects `kFpFromBits` and its BV15 companion is invalid. Parse Core `=` and `fp.eq` with the same ordered `(_ +zero 5 11)` and `(_ -zero 5 11)` operands. Check distinct zero identities, Bool result and `kEqual` versus `kFpEq`. Core equality distinguishes the signs and `fp.eq` equates them, so losing either kind or zero sign changes the meaning. |
| FE-ARRAY-RANK-02, `operator_test.cc` | For `a : (Array Bool Int)`, check `(select a true)`, `(store a true 8)` and `((as const (Array Bool Int)) 8)`. Check exact index and element identity on the returned array sort and the ordered operands. Independently change the select index to Int, the store index to Int, the store element to Bool and the constant-array value to Bool. Each fails its own rank. Add a positive control over `(Array Int Bool)` to check the inverse ordering of the two sorts. |
| FE-PROFILE-02, `profile_test.cc` | For every registry row, assert the complete ordered root list and exact count. Derive the expected sequence from the written fixture: base `kEqual`, BV `kEqual` with `kBvNot`, Int `kEqual` with `kIntPow` over `kIntMul` and a `kIntAdd` right operand, array `kArraySelect`, datatype tester `kApply`, FP `kFpEq`, restricted Int `kIntLe`, and free function `kApply` for a U-permission row. Select only the applicable entries and check tester versus free declaration identity. Add standalone QF_UFLIA `(assert (= (div x 2) 3))` and typed value `(div x 2)` with declared Int `x`. Require `kIntDiv`, ordered Int operands and Int result in both contexts. Reject a colliding `div` declaration and preserve explicit UF identity when that spelling is declared under QF_UFIDL. |
| FE-PROFILE-RI-02, `profile_test.cc` | Under an RI label, exercise undeclared `to_int` and `is_int` and require unsupported interpreted ranks. Under the U-permission RI label QF_UFDTLIRA, reject declarations of both reserved spellings. Under ALL, undeclared calls are unsupported and explicit user declarations of both names retain their declaration identities and ordinary ranks. Use `(declare-fun to_int (Bool) Int)` and `(declare-fun is_int (Int) Bool)` with well-sorted and wrong-sort controls. |
| FE-LET-NAME-02, `binding_test.cc` | Use `(let ((x (! 1 :named n)) (y n)) (= x y))` to verify the inline name is published in source order and visible to the next RHS in the outer lexical frame. In a fresh session, swap RHS order to exercise the forward reference. Keep a closed named value in a let body, such as `(! 1 :named closed)`, and reject `(let ((x 1)) (! x :named open))` even though both occurrences can yield the same interned Int node. Exercise closed-first and open-first in one session with separate commands. Check only closed names enter the prefix and closedness follows source scope. |
| FE-DEF-ORDER-02, `binding_test.cc` | Run `(= (f x) 2)` and `(= (f (+ x 1)) 4)` in both orders under QF_LIA with `f(t)=(* 2 t)`. Check exactly two assertions, the same retained definition identity in both calls, and exact actuals `x` and `(+ x 1)` with literal 1. Pair each call with its expected right-hand literal. With `x=1` the two calls evaluate to 2 and 4 under the hand-derived substitution. Check the body remains a multiplication over the formal. |
| FE-MATCH-ORDER-02, `sort_datatype_test.cc` | Use `(declare-datatype Choice ((a) (b (field Int)) (c)))` and match a `Choice` under `(let ((a 99) (b false)) ...)` with branches `((c 3) ((b a) a) (a 1))`. Check outer tester `c`, next tester `b`, guarded `field` selector for field binder `a`, and fallback 1. The hand-derived results for `a`, `(b 7)` and `c` are 1, 7 and 3. A branch with a wrong result sort is invalid even after a default has made it unreachable. Verify pattern lookup uses the scrutinee constructor identities and the branch binder shadows only in that branch. |

### Rollback, stream and model cases

| ID and target | Fixture and expected observations |
| --- | --- |
| FE-ROLLBACK-02, `command_test.cc` | For each of option update, metadata append, logic selection, query sealing and free sort/function publication, supply two identical commands in the stream. Inject a deterministic allocation failure in the first `ReadCommand`, inspect after it is consumed, then read the duplicate successfully. Test option fields and absence of a success descriptor, exact ordered metadata, before-logic phase and empty logic, building phase with the same assertions and metadata before successful sealing, and exact prior declaration counts/identities with the failed spelling reusable. After retries require exactly one published effect per command. Sweep the applicable publication checkpoints at these boundaries, with a clean prefix as control. |
| FE-ROLLBACK-NAME-02, `binding_test.cc` | In a command with an initial valid `:named staged` annotation, trigger an error inside a later nested annotation with an open lexical reference or a conflicting name. Check that no assertion, source annotation or new alias from the failed command is published. Retry a corrected command using the same fresh spellings. Check that the successful names and annotations occur in source order and the earlier committed prefix is retained. |
| FE-DRIVER-02, `driver_test.cc` | Enable `:print-success`, set logic, assert `false`, disable `:print-success`, request an unknown option, and check with a controlled engine returning `unsat`. Expect exactly `success\nsuccess\nsuccess\nunsupported\nunsat\n` and five flushes. An invalid post-check command adds one complete error and one flush. In a separate script enabling success then exiting, expect `success\nsuccess\n`, two flushes and no suffix processing. Compare `(set-logic QF_SAT)(assert false)(check-sat)` with the same script inserting `(reset-assertions)` before check. The control engine returns `unsat` only with the single retained false assertion and `sat` if it is missing. The reset script must have exactly `unsupported\n`, one flush, status 1, zero engine calls and its committed false assertion intact. |
| FE-READER-02, `syntax_test.cc` and `driver_test.cc` | Return complete logic and assertion commands and a partial next command from the reader, then throw during that command. Check `kInputIo` at the known boundary with exact source coordinates, `kTerminated`, preservation of the complete prefix and no pending partial command. A second `ReadCommand` reads no bytes; verify the corresponding terminal behavior with `SyntaxValidator::ReadNext`. For driver execution, require one complete error and its flush, no suffix processing and no check invocation for an incomplete check. Use EOF and an explicit `ReadResult` I/O error at that boundary as separate controls. |
| FE-MODEL-CREATE-02, `model_test.cc` | Move the sole provider and a query into `ModelOwner::Create` with an ineligible answer or model-disabled query. Check `kModelFailure`, no owner, and one provider destruction while any supplied query remains live. Use a destructor counter and a weak query sentinel without an independent owning query in the failure fixture. Exercise an injected construction allocation failure if available at that boundary, with the corresponding resource diagnostic. A successful construction keeps the provider alive through owner release. |
| FE-MODEL-COMMIT-02, `model_test.cc` | Publish an existing abstract name in a successful response. In a later multi-value request, write a pair containing a fresh abstract name, then fail the second evaluation. Separately fail the sink with a short write and with a flush error after all bytes. Retain the owner outside the failing driver, reopen inspection and verify only the earlier name resolves. In a successful retry, repeated equal values share one new name and that name is readable after flush. Apply the same check to `get-model` with an early record containing a staged proxy followed by an invalid record. Check exact output prefix, flush counts, and absence of a second diagnostic after response bytes. |
| FE-READER-EXCEPTIONS-02, `syntax_test.cc` | Deliver a partial command after a complete committed prefix, then throw `std::bad_alloc` or `std::runtime_error` from the reader. Require `kInputIo` at the exact read boundary, an unchanged committed prefix, and immediate termination with no subsequent read. |
| FE-MODEL-RESERVE-02, `model_test.cc` | Publish `@value0`, then arm the allocation fault once the sink accepts the last value pair or function record containing two fresh names. Fail each capacity allocation in turn, with and without a flush callback. Require a C++ allocation diagnostic, exact output ending at the final body record, no flush, and lookup of `@value0` alone. Retry and verify that the new names are published and resolve with the same ordinals. |
| FE-MODEL-BOUND-02, `model_test.cc` | Create two owners from the very same sealed query and manager. Expose `@value0` in both for different controlled values. For both `ForModelText` and `ForModelReader`, pass the second owner to `ReadCommand` of a session bound to the first and require `kModelFailure`, consumption of the failed command and no lookup or evaluation in the other owner. A following identical request with the default binding and one with the original explicit owner both obtain the original value. |
| FE-MODEL-SORT-TEXT-02, `model_test.cc` | Under ALL, define `A0(T)=T` and `A(n+1)(T)=Array(A(n)(T),A(n)(T))` through depth 12 or greater. Return an abstract value of a concrete application such as `(A12 Bool)` and export a matching qualified term. Exercise the shared sort in a generated abstract-value ascription and in a generated constructor or constant-array qualifier as applicable. Verify the compact alias keeps rendered bytes bounded by the written spelling, resolves to the exact same sort in the sealed signature, and survives release of input text. Use a finite deterministic size/work assertion that an unfolded exponential sort would exceed. Keep a small alias-free sort control and check parameter order with an asymmetric alias. |

The post-fix scale fixture extends FE-MODEL-SORT-TEXT-02 through depth 64
and inspects the nested array and its element in one response. It checks
exact compact output and bounds the number of recipe matches by a linear
function of the alias depth using a test-only session counter.
A separate case lets an alias use a nullary spelling for a compound
argument and verifies that the shorter alias still wins the spelling tie.

For **FE-MODEL-FAULTS-02** in `model_test.cc`, drive `Describe` and
`ExportModel` with individually malformed results from a controlled
provider. Use one valid fixture and mutate one part per case.

| Boundary | Faults and expected outcomes |
| --- | --- |
| Scalar description | Missing description, null node, a nonliteral term of the correct sort, a literal of a different sort, and a literal from a different manager. Require `kModelFailure`. |
| Datatype description | Missing or null constructor instance, selector or tester supplied as constructor, wrong constructor instance/sort, wrong field count, wrong field sort, same-sort foreign field value, and a cycle through a repeated active semantic key. Require `kModelFailure`. A correctly typed finite value with a repeated sibling key is a valid control. |
| Export signature and bodies | Missing, duplicate, foreign or extra declarations, forward reference, wrong result sort, wrong number of formals, repeated or foreign formal, wrong formal sort or kind, unbound formal, and operation or sort outside the active signature. Require `kModelFailure`. A correct dependency-ordered export keeps the exact semantic domain and range. |
| Export proxies | Missing proxy mapping, repeated mapping for one declaration, a foreign value or proxy, and a proxy with a wrong sort where constructible at the tested boundary. Require rejection. Two distinct valid proxies of one semantic value must share its printed name. |
| Callback and response boundary | A provider error or exception before the first model response byte yields one complete diagnostic if the sink is usable. A description or export validation fault in a later record leaves exactly the written prefix with no appended error. In each case keep earlier check output, assert exact flush counts, verify no trailing command is applied and inspect name visibility as in FE-MODEL-COMMIT-02. |

### Probe CLI and XOR controls

The deterministic GoogleTest subprocess fixtures for
`tests/frontend/parse_probe.cc` use project-authored scripts.

- **FE-PROBE-CLI-02:** exercise the no-argument and `--compare` modes.
  Type a complete single-query input and check exit 0, empty stdout in
  typed mode, and the exact summary on stderr. Require an SMT-LIB
  comparison script on stdout in compare mode. Unknown flags, extra
  arguments, missing check and malformed input return 1. An excluded
  command returns 2. A typed script whose comparison cannot be recovered
  (for example, a used non-nullary definition) returns 3 in compare mode.
  Missing model-local names are also inconclusive when the probe cannot
  establish them. Check diagnostics and absence of stray solver responses.
  Follow `exit` with malformed bytes and verify the command count and
  comparison exclude that suffix.
- **FE-PROBE-XOR-02:** inspect emitted signature commands and exact
  per-assertion XOR pairs in source order. Use Boolean assertions `p` and
  `(not p)`, including a duplicate in one variant. Verify one pair per
  written root, no original assertion as a separate constraint, and one
  final `check-sat`. A hand-derived two-row truth table gives false for
  the disjunction of correct pairs. Swap two recovered roots or perturb only
  a final recovered root and require the disjunction can be true even
  though the original roots together are inconsistent. This exposes
  conjunction-level comparison, extra assumptions and dropped pairs.
  Cover zero assertions with `(assert false)`, and exercise a nullary
  definition whose recovered name would collide with a user spelling.
  Verify every recovered use binds to its fresh definition. The deterministic
  test oracle needs only the small Boolean or BV fragment in these fixtures.

## Frontend overhaul test design

`tests/frontend/overhaul_test.cc` is registered with the frontend CTest suite.
It exercises the [overhaul contracts](../frontend/design.md) through the
public session API and the test-only frontend and Node inspection hooks.
Use source capture when checking successful provenance. Use the semantic
context and exact diagnostics with capture disabled.

| Case | Fixtures and independent expectations |
| --- | --- |
| FE-OVERHAUL-01 | Read a script through the text adapter, through a forced split at every interior byte, and in 1-byte, irregular, 4 KiB and 64 KiB chunks. Exact command, metadata, declaration, assertion and value-request spans come from known source substrings and a byte-coordinate oracle. CRLF is one newline, UTF-8 columns count bytes, and normalized rendering preserves quoted spelling and doubled quotes. |
| FE-OVERHAUL-02 | Shift CRLF, UTF-8 and doubled quotes across the 64 KiB boundary and render a quoted token longer than two reader windows. Inject a reader error at each byte boundary of a command after a committed prefix. Check exact failure position, error code, committed metadata and termination. Compare an incomplete UTF-8 token across splits. Deliver a separate unread malformed suffix after `exit` and assert the reader is never called for it, including when some suffix bytes were already buffered. |
| FE-OVERHAUL-03 | Compare wide and deep value lists on text and reader paths, with and without forced widening. Verify exact rendering and spans, every ordered child of a wide node, and the full deep chain. Normal growth allocates multiple pages and reports zero copies and promotions. A low threshold reports promoted pages while preserving all observed results. |
| FE-OVERHAUL-04 | A named `let` with an unused RHS referencing an outer binder is closed. Check shadowing and simultaneous outer resolution on used RHSs, and reject an unused but ill-typed RHS. An open reference still fails when it resolves to the same node as a closed occurrence. A first-match-unreachable branch still produces valid annotation events and still fails if it has an external dependency. Failed commands publish no names, annotations or assertions, and a later command can reuse the spellings. A nested named chain publishes innermost first and visits one operand occurrence per annotation. |
| FE-OVERHAUL-05 | Exercise free membership across declaration gaps and exact BV structural identity as in FE-P1-QUERY and FE-P1-NODE. Check quoted spellings, ordered rank text and independent record copies with FE-P1-SOURCE. |
| FE-OVERHAUL-06 | Observe a cold miss, small-key hit, oversized bypass and eviction with exact integer readback. Concrete BV and FP sort occurrences reuse resolved sorts while differing keys keep their correct widths or formats. Access each of 8,192 distinct short keys twice in succession and verify one hit per key. Replay every key once after the same warm-up in a fresh session and require some replay misses. This observes eviction without picking which keys should survive. Repeat a 16,384-digit integer as the oversized-key fixture. |
| FE-OVERHAUL-07 | Sweep apply-time allocation checkpoints in a declaration with repeated sorts and an assertion with repeated literals. Preserve the original function, assertion and name records on every injected resource failure. Apply a repeated command in the same session and check its typed and rendered contents and published counts. |
| FE-OVERHAUL-08 | Use FE-P1-SEAL to check alias reclamation and captured owners, with duplicate root handles, independently retained children and final table detachment. |
| FE-OVERHAUL-09 | Verify every permitted operator family at its applicable arity and ordered controls in both enabled parsing and standalone normalization. Compare child identities against explicit source operands or independently sorted eligible positions. Keep FP rounding and FMA addend positions fixed, and preserve the order of signed zeros in `fp.min` and `fp.max`. Unlisted controls include ordered comparisons, subtraction, concatenation, implication, ordered UF arguments, `ite`, select and store. |
| FE-OVERHAUL-10 | Legacy and explicit default options preserve ordered sums. Enabled parsing and the standalone batch and single-root overloads merge equivalent `ite`, `and` and UF parents after a child is reordered. Verify original standalone roots, repeated operands, nested operator boundaries, query publication, and original source rendering. Attribute a type error to its written operand with normalization enabled. A 512-operand permutation checks exact sorted child IDs and stable node IDs across independent text and reader sessions. |

The cache pressure and oversized-key sizes are finite fixture choices for
observing the bounded paths. The tests do not use timing or allocator
residency as evidence. Hash collision injection uses the existing Node
test peer. Frontend spelling checks use distinct exact keys and dequoted
duplicates through the public API.

## Oracles and differential checks

For unit tests, inspect the frontend's observable typed structure or other
stable semantic interface against manually specified expectations. A test
that merely accepts an input or prints something is not enough to establish
that widths, declarations, and bindings were preserved.

For each case, make the check actionable:

1. Give the case a descriptive identifier and supply a complete script, or a
   term with the declarations and environment required by its test interface.
2. State whether the case should be accepted, rejected as malformed or
   ill-sorted, rejected as unsupported, or reported as an actual
   representation/allocation failure under the selected profile. Do not
   invent a frontend quota. An implementation may expose error details through
   its own API; exact human-readable wording need not be frozen.
3. For accepted input, check the relevant observable structure: command count
   and order where applicable, declaration references, sort, operator,
   indices, arguments, and exact values. Check only information that the
   frontend's documented contract promises to expose or preserve.
4. For rejected input, check that it is not silently accepted, that the error
   is attributed to an appropriate category when available, and that malformed
   input does not crash the process. Check source locations if the API promises
   them. Distinguish an unsupported feature from a malformed one in the test
   report even if the external protocol uses a common error response.
5. Record why the expectation is correct: a specific standard rule, a small
   hand-derived example, or a reference implementation's exact version and
   limitations. Reference agreement is additional evidence, not the definition
   of the language.

Use GoogleTest assertions and parameterized cases to share setup where that
clarifies a family. Include the case identifier and input or a safe path in
failure diagnostics. Keep cases independent and deterministic. The normal
suite must not need a reference solver, a dataset, network access, or a
particular working directory.

Use paired cases for syntax variants with the same meaning, such as `#b1111`
and `#xf`, and cases where a small difference changes the type or validity.
Whitespace/comment changes and capture-avoiding renaming can provide further
metamorphic checks. A parse/print/parse check is useful for discovering loss of
information, but both sides may share a bug; retain independently derived
expectations.

Reference frontends provide another comparison on a shared supported subset.
[Bitwuzla's parse-only mode](https://bitwuzla.github.io/docs/binary.html) can
be used for supported BV, FP, array, and UF inputs without solving them. Other
reference parsers may cover integers or datatypes; record the exact version,
mode, warnings, and supported input profile. Do not treat an ignored logic,
extension, warning, or successful exit as evidence that a reference checked
all commands or the entire intended semantics.

For stronger selected comparisons, recover a formula from the parsed
representation and check its equivalence to the original with an independent
solver where both are supported. Both versions must use corresponding
declarations, sorts, and theory semantics. A type mismatch, changed signature,
unsupported theory, or `unknown` is not an equivalence proof. This check
verifies the comparison under the reference solver's semantics; it does not
establish that either implementation is universally correct.

## Bounded generation and tests of the tests

Handwritten cases should anchor the expected meaning. A small generator can
supplement them by constructing bounded terms from an independently specified
set of well-sorted operator signatures. Record its seed, depth, size, and
selected profile; assert the expected sorts and relationships produced by the
generator rather than accepting any parse result. Do not use the frontend's own
type checker as the only source of expected types.

For negative generated cases, make one deliberate change whose invalidity is
known, such as a reversed extract index, one wrong argument sort, or a missing
closing parenthesis. An arbitrary byte edit may yield another valid input or
an unsupported extension, so its expected result must not be guessed. Check
that generated terms remain within the selected logic's restrictions.

Use semantics-preserving changes only under their actual preconditions. For
example, add whitespace or comments **between** tokens, not inside a symbol or
string; alpha-renaming must avoid capture; equivalent literal spellings must
have the same width as well as value. Compare the promised semantic properties,
not object addresses or raw serialized text. A failing generated case should
be reproducible and, if minimized, the smaller case must retain the same
relevant feature and failure.

Occasionally test the tests with a few targeted mutations in an isolated copy,
not an exhaustive mutation campaign. For example, a suite should fail if the
implementation treats `;` inside `|x;y|` as a comment, makes `let` bindings
sequential, swaps extract indices, or silently drops an assertion. A surviving
mutation signals a coverage gap; it is not a proof that the original code is
correct. Keep generated and mutation runs bounded and within the project's
large-experiment policy.

## Benchmark regression checks

Choose a small, explicit set of local benchmark-2025 examples spanning the
accepted theories and combinations, source families, metadata, declarations,
and term shapes. Record path, hash, profile, expected input classification,
reference tools, and applicable warnings. Increase coverage only through
deliberate selections; do not make an unbounded dataset scan a default test.

For example, the following previously inspected file is one candidate case;
the path is relative to `benchmark-2025/non-incremental` under the dataset
root, not a path that the test code should hardcode:

| Field | Recorded value |
| --- | --- |
| Relative path | `QF_BV/challenge/integerOverflow.smt2` |
| SHA-256 | `1e96ed11af58d1bcbcc957f3ad361aa84244777076aa094e8d578353486362e0` |
| Declared input | SMT-LIB 2.6, `QF_BV`, `:status unsat` |
| Reference evidence | Bitwuzla 0.7.0 accepted a safe copy in parse-only mode; Z3 4.8.7's parse API returned three assertions. |

The status is solving metadata and is not an expected parser output. These
reference observations do not validate the complete represented formula or
prove Dandelion accepts the file. Check the hash before using the record; a
different snapshot, missing file, ignored logic, or missing reference is a
separately reported condition. Add cases from other theories and source
families according to their actual features and the supported profile; this
one case is not a representative sample of the whole corpus.

Classify an input as accepted, malformed or ill-sorted, unsupported by the
declared profile, or inconclusive because of a reference limitation or resource
limit. A disagreement must be investigated rather than automatically assigned
to Dandelion or to the reference implementation. Expected results in
`:status` concern solving and are not an oracle for parsing.

Track **accepted and semantically checked** separately from **accepted only**:
successful parsing of a real file does not prove its assertions or symbols
were retained correctly. Record reference warnings, timeout, crash, skipped or
missing prerequisites, and mismatches separately. A reference that accepts a
solver-specific extension does not make it part of Dandelion's profile; an
older reference that rejects or ignores an operator cannot prove the input is
invalid.

Follow the [dataset policy](README.md#local-benchmark-data) for all reads and
copies. The normal small unit suite should run without a local corpus or
reference solver; an explicitly requested corpus test must report missing
inputs or tools clearly rather than appearing to pass.

### Python runner checks

Run `python3 -m unittest tests/frontend/benchmark_regression_test.py` from
the project root. These tests exercise the opt-in Python helper with in-memory
process records and a synthetic manifest in a temporary output directory.

- A successful parse-only reference invocation has exit 0 and silent output.
  A typed probe must additionally report the exact expected summary on stderr
  and leave stdout empty. Test errors on stdout even with exit 0, unexpected
  solving output, an incorrect summary and a diagnostic on stderr.
- A compare probe with positive assertion count must emit one original and
  recovered term pair per assertion under the disjunction. Treat empty or
  structurally incorrect comparisons as inconclusive and skip reference XOR
  runs for that sample. Keep `(assert false)` for zero assertions.
- Each reference XOR check expects exactly one `sat` or `unsat` token,
  allowing surrounding SMT-LIB whitespace. Test matching and opposite
  answers, `unknown`, no answer, extra result tokens, and an error before or
  after an apparently correct result. Exercise nonzero exits, signals,
  timeouts and unavailable executables independently of their output.
- Verify the written `report.json` contains counts of `passed`, `failed`,
  `unsupported`, `unavailable`, `timed_out`, `crashed` and `inconclusive`
  checks across the typed probe, recovery, applicable references and oracle
  controls. Profile skips are outside those counts. A diagnostic from a
  reference on a valid sample is inconclusive evidence. A comparison skipped
  because of recovery failure is also inconclusive.
- Verify exit 0 with complete evidence, exit 1 for a failed check, and exit 2
  when the evidence is incomplete without a failed check. Cover unavailable
  references including a run with none configured, timeouts, signals,
  unsupported input, inconclusive recovery, reference parse diagnostics,
  and failed or inconclusive controls. Require actual sample XOR evidence
  even when every control passes.

## Readiness review

Use the inventory, not a single coverage percentage, to assess the frontend:

- Every feature claimed as supported has linked passing representative cases,
  with relevant boundary and negative cases or a documented reason they do not
  apply. Each case has a justified expected outcome and checks the promised
  typed or semantic information.
- Representative combinations and selected real inputs have been checked;
  supported-feature gaps, unsupported inputs, missing references, and
  inconclusive results remain visible in the report.
- The unit and component suites can run repeatedly without the solving engine,
  and failures name the case and the relevant expectation. Tests cannot pass
  merely because no cases were discovered or prerequisites were absent.
- Run the applicable tests in the project's GCC and Clang configurations and
  under a working sanitizer configuration. Report toolchain limitations
  separately instead of calling an unexecuted configuration a pass.
- Preserve a focused regression after a confirmed defect, and record its origin
  and expected behavior. Keep the normal suite bounded; ask before a large
  experiment.

This review supports a defensible frontend capability statement. It is not a
formal proof of the parser or a claim that every benchmark in the local snapshot
has been checked.
