# Frontend Logic and Input Profiles

**Status.** The signature registry, metadata rules and definition bindings
are implemented. The [acceptance record](implementation.md) reports
verification and the earlier tested corpus slice.
This document defines the input profile
for the project scope. It records intentional compatibility extensions rather
than claiming complete conformance to each registered logic or support for
every benchmark bearing its name.

## Language, names and the compatibility policy

Use SMT-LIB 2.7, release 2026-09-09, with compatible 2.6 forms and the theory
revisions in the [node operator table](../node/operators.md). Register the
exact, case-sensitive names below. An unregistered name is unsupported; a
known label is never silently replaced by `ALL`.

Each label fixes its active theory signature before any later command. Core
is always active. The table specifies additional supported signature
components and reserves the names of any active but unsupported Real ranks.
The accepted language deliberately extends the catalog fragments in these
ways:

- Admit Bool constants under `QF_BV` and other constant-only profiles. Free
  sort and nonconstant function declarations follow the permission in the
  registry below, rather than being admitted under every label.
- Reduce decimal indexed BV literals `(_ bvN w)` modulo `2^w` at the
  frontend boundary. The typed value passed to Node fits its declared width.
- Admit every selected, well-sorted integer operation from an active Ints
  signature in assertions and value requests. Do not reject an expression
  merely because it does not have the catalog's IDL, linear or nonlinear
  assertion shape. In particular, `(< x 3)` under `QF_IDL`,
  `(* 2 (+ x 1))` under `QF_LIA`, and `(** x 2)` under `QF_NIA` are
  admitted when well sorted. This is a Dandelion compatibility extension,
  not a statement that those examples belong to the strict catalog grammar.
- In a profile with arrays, admit arrays over any admitted concrete index and
  element sorts, including nested arrays, and the selected constant-array
  form. This widens catalog `QF_ABV`, `QF_AUFBV` and `QF_AUFLIA` array
  restrictions explicitly. Array semantics remain extensional.
- Preserve `QF_UFIDL`'s restricted arithmetic *signature*, with a narrow
  model-value exception specified below. The catalog restriction that a
  binary `+` or `-` have a numeral operand is an assertion-language rule;
  the compatibility profile does not enforce it.

These extensions do not enable a theory signature missing from the registered
row. To request the full supported combination, use `ALL`. An actual Real or
String theory sort, operation or symbolic Real conversion is unsupported,
as are quantifiers, higher-order terms, recursive definitions, global sort
parameters and nonstandard recursive datatype containers. The closed rational
operand of the specified `to_fp` compatibility rank and metadata strings are
exceptions, not general Real or String support. Invalid syntax, scope or sort
is distinguished from a well-formed unsupported feature and from actual
representation, allocation or I/O failure.

Do not infer a declaration's meaning from spelling alone. The selected
profile's active theory names are reserved, including excluded Real ranks;
other names may be declared as user symbols if otherwise fresh. For example,
under `QF_UF`, a user-declared sort named `Int` is an uninterpreted sort, not
the built-in integer sort. Under `QF_UFIDL`, a freshly declared function
named `div` is a UF, not integer division, since the restricted signature
does not contain Ints `div`. Resolve and retain the declaration identity;
do not retroactively reinterpret it when another expression is seen.

For the FP concrete-rational extension, `-` and `/` are reserved only if an
active theory or unsupported active Real rank already reserves them. When a
name is otherwise free and the declaration permission allows it, a user may
declare it. Resolve each head in the candidate `to_fp` operand, including
inner heads, using lexical and user declaration identity first. A bound user
symbol makes that occurrence an ordinary term, not rational syntax; an arity
or sort error must not trigger reinterpretation as a rational. With no such
binding, the documented closed grammar decodes the operand as an exact
Rational, including when a supported arithmetic rank is active. Quoted and
unquoted forms share the same dequoted identifier. This rule neither enables
general Real terms nor promotes an inactive operator to a builtin.

The [Phase 2 reader](design.md#reduction-of-special-forms) carries
this identity check through every nested candidate head as it reads the
operand. Only the complete two-operand indexed form with an eligible
closed expression selects exact rational evaluation. Other shapes use
ordinary term admission, including bound `-` and `/`. A selected
rational divisor that evaluates to zero is explicitly unsupported, with
the half-open span of the entire divisor expression.

## Metadata

Every `set-info` validates standard attribute syntax with one keyword and
at most one attribute value. The recognized keys have these value rules.

| Key | Admitted value |
| --- | --- |
| `:smt-lib-version` | The exact decimal tokens `2.6` and `2.7`. Another well-formed decimal is unsupported. A missing value or a different token class is invalid. |
| `:source` | A string or quoted symbol. |
| `:category` | Any string, including `"check"`, by default. With `ParseOptions::strict_metadata` enabled, only `"crafted"`, `"random"` and `"industrial"` are admitted. |
| `:license` | A string. |
| `:status` | The unquoted symbol `sat`, `unsat` or `unknown`. |

Missing or wrong value forms for the recognized keys are invalid. Other
keys accept opaque well-formed attribute data. The category compatibility
extension applies independently of source capture. Strict metadata leaves
logic and signature admission as specified in this document. Status is
advisory and never determines an engine result or its validity. Only
`capture_source_records` requests owned metadata text and history.
The Phase 2 reader validates opaque S-expressions in place and discards
their text when capture is disabled. Lexical and grammar checks apply in
both capture modes, including after a recognized value fails admission.

## Components and registered labels

| Component | Active signature and admission |
| --- | --- |
| `B` | Selected fixed-size BV sorts, literals and operators. BV/Int conversions require both `B` and `I`. |
| `F` | FP, rounding modes, FP ranks, FP aliases and the concrete-rational conversion. BV sorts and the `#b`, `#x` and `(_ bvN w)` literals are admitted; standalone BV operators require `B`. The Real sort and `fp.to_real` are reserved but unsupported. |
| `I` | Int sort, numerals and selected Ints operators, including `**`, without an assertion-shape gate. |
| `J` | QF_UFIDL Int subsignature: numerals and binary `+ - < <= > >=`, plus Core. Its numeral-operand assertion restriction is not enforced. |
| `D` | The selected inductive datatype declarations, constructors, selectors, testers and `match`. Field sorts must be admitted. |
| `A` | Extensional arrays over admitted concrete index and element sorts, recursively, including the selected constant-array extension. |
| `R` | Reserve the Real sort and the Real ranks of `- + * / < <= > >=`. Their use is unsupported, except for the `F` concrete-rational operand. |
| `RI` | Reserve the Reals_Ints conversions `to_real`, `to_int` and `is_int`; those interpreted ranks are unsupported. |

If no `A` component is active, the builtin Array constructor and its
operations are not enabled. A user-declared free sort constructor with the
same spelling, when that spelling is not otherwise reserved and the profile
permits the declaration, remains a UF sort, not an array. The same rule
applies to other inactive builtin names.
Array, datatype and FP support here describe input representation, not a
claim that the combined solving procedures are complete.

The declaration permission applies to *free* user declarations over admitted
sorts. `C` permits constants, including Bool, but no free sort constructors
or nonzero-arity free functions. `S` additionally permits free sort
constructors. `U` permits both free sort constructors and first-order free
functions and constants. `declare-const` and zero-domain `declare-fun` have
the same permission. Nonrecursive definitions, sort aliases, datatype
constructors and `:named` aliases are not free declarations; their own
profile and freshness rules still apply. `C` under `QF_BV` deliberately admits
Bool constants as a compatibility extension. A declaration rejected by its
permission is unsupported, not silently turned into a different symbol.

| Exact label | Active components | Declarations | Origin or interpretation |
| --- | --- | --- | --- |
| `QF_SAT` | — | C | Local Boolean/Core label. |
| `QF_BV` | B | C | Catalog BV with Bool constants. |
| `QF_UFBV` | B | U | Catalog BV/UF profile. |
| `QF_ABV` | A, B | C | Catalog label with general-array extension. |
| `QF_AUFBV` | A, B | U | Catalog label with general-array extension. |
| `QF_AX` | A | S | Catalog free sorts/constants plus constant arrays. |
| `QF_UF` | — | U | Catalog Core and free symbols. |
| `QF_IDL` | I | C | Catalog label with Int assertion-shape extension. |
| `QF_UFIDL` | J | U | Restricted subsignature with shape and model-value extensions. |
| `QF_LIA` | I | C | Catalog label with Int assertion-shape extension. |
| `QF_UFLIA` | I | U | Catalog label with Int assertion-shape extension. |
| `QF_AUFLIA` | A, I | U | Catalog label with array and Int assertion-shape extensions. |
| `QF_NIA`, `QF_EIA` | I | C | NIA also admits the selected Ints `**` operator. |
| `QF_DT` | D | C | Local datatype profile. |
| `QF_UFDT` | D | U | Local datatype/UF profile. |
| `QF_UFBVDT` | B, D | U | Local BV/datatype/UF profile. |
| `QF_FP` | F | C | Local Real-free FP profile. |
| `QF_UFFP` | F | U | Local Real-free FP/UF profile. |
| `QF_FPLRA` | F, R | C | Local Real-free FP profile. |
| `QF_BVFP` | B, F | C | Local BV/FP profile. |
| `QF_BVFPLRA` | B, F, R | C | Local Real-free BV/FP profile. |
| `QF_ABVFP` | A, B, F | C | Local general-array BV/FP profile. |
| `QF_AUFBVFP` | A, B, F | U | Local general-array BV/FP/UF profile. |
| `QF_ABVFPLRA` | A, B, F, R | C | Local Real-free general-array BV/FP profile. |
| `QF_ALIA`, `QF_ANIA` | A, I | C | Local general-array integer profiles. |
| `QF_AUFNIA` | A, I | U | Local general-array integer/UF profile. |
| `QF_UFNIA` | I | U | Local integer/UF profile. |
| `QF_UFDTLIA`, `QF_UFDTNIA` | D, I | U | Local datatype/integer/UF profiles. |
| `QF_UFDTLIRA` | D, I, R, RI | U | Local Real-free datatype/integer/UF profile. |
| `QF_UFBVLIA` | B, I | U | Local BV/integer/UF profile. |
| `QF_AUFBVLIA`, `QF_AUFBVNIA` | A, B, I | U | Local general-array BV/integer/UF profiles. |
| `QF_LIRA`, `QF_NIRA` | I, R, RI | C | Local Real-free integer profiles. |
| `QF_UFFPDTNIRA` | D, F, I, R, RI | U | Local Real-free FP/datatype/integer/UF profile. |
| `QF_BVLRA` | B, R | C | Local Real-free BV profile. |
| `QF_UFLRA`, `QF_UFNRA` | R | U | Local Real-free Core/UF profiles. |
| `QF_LRA`, `QF_NRA`, `QF_RDL` | R | C | Local Real-free Core profiles. |
| `ALL` | A, B, D, F, I | U | All project-supported first-order, quantifier-free combinations. |

Real-bearing `I, R, RI` rows are local Real-free profiles, not strict
intersections with the catalog: like other `I` rows they also admit Ints
`**`. The `RI` conversion names are reserved only where `RI` is active. In
`ALL`, `to_real` is not an interpreted conversion and a user function with
that name retains its declared identity; the Real theory remains unsupported.

String logic names, including `QF_S`, `QF_SLIA` and `QF_SNIA`, are not
registered. Unknown labels and unsupported query-affecting forms are
reported and terminate the single-query driver. A recognized Real-bearing
label does not make a Real declaration or expression supported. The
[SMT-COMP 2025 results](https://smt-comp.github.io/2025/results/) establish
use of labels, not the actual contents or coverage of the local snapshot.

## Definition use and solving classification

The frontend checks syntax, source scope, symbol and sort identity, operation
rank, and the active signature. It validates every written definition body,
argument, let RHS and match branch, even when unused. Nullary
`define-fun` and `define-const` bind closed typed bodies directly.
Parameterized definitions retain typed templates and saturated calls.
The frontend checks these terms against the active signature without
expanding applications or performing IDL/LIA/NIA shape checks.

The declared logic is a hint and an input-signature selection, not a proof
that the query belongs to a solver's IDL, LIA or NIA algorithm. A consumer
must inspect the actual typed terms, sorts and reached definition meanings or
use a sound transformation before selecting a specialized procedure. A label
alone never authorizes an incomplete solver to report `unsat` or assume a
linear fragment. Mixed or nonlinear inputs follow an appropriate supported
path or return `unknown` when the chosen procedure cannot decide them. The
architecture retains its complete-decision target for the specified pure
IDL/LIA fragments; a conservative classifier must not silently discard that
obligation. Its full solving-side algorithm and completeness conditions are
separate from frontend acceptance.

Retaining definitions does not guarantee polynomial work for every possible
later transformation. It does ensure that the frontend does not expand an
application just to choose a logic profile. The input with `f0(x)=x` and
`fn(x)=f(n-1)(f(n-1)(x))` stays as its written definition DAG during frontend
construction, including under `QF_LIA`.

## QF_UFIDL model values

Within `QF_UFIDL`, user input in declarations, definitions and assertions
uses `J`: unary minus is not a general operator. For a post-check `get-value`
only, additionally accept the canonical negative integer value `(- n)`,
where `n` is a positive numeral, at any Int-valued operand position. Construct
its exact negative Int value directly; do not enable `(- x)` or arbitrary
unary minus. This deliberate value-input extension permits reading back a
negative value printed by the model. The same printed form is not thereby
admitted in an earlier assertion. The unqualified quoted identifier `|-|`
has the same identity as `-` and is accepted in this value form. `(- 0)`, a
qualified unary head such as `((as - Int) 1)`, and `(- x)` are not covered by
the exception. A `get-model` definition instead uses the binary expression
`(- 0 n)` for a negative integer; see [model export](semantic-interfaces.md#model-local-values-and-inspection).

The Phase 2 command boundary preserves this value-only admission and
does not enable `:named` in `get-value`. For execution, result-dependent
requests use the actual retained answer and owner after grammar
validation. The solver-free typed reader can classify ordinary requests
without a result, and resolution of a model-local name requires its
published same-owner binding.
[Phase 2 protocol](semantic-interfaces.md#phase-2-command-processing-and-publication)

## Evidence and test obligations

The selected compatibility policy is a project decision. The
[SMT-LIB catalog](https://smt-lib.org/logics-all.shtml) supplies strict
reference definitions, not a complete description of these extensions.
On 2026-11-18, small synthetic stdin probes with cvc5 1.4.2
([revision](https://github.com/cvc5/cvc5/tree/282d5e0c84baf49c780704c532f87ba66d8b513d),
`--lang=smt2 --strict-parsing`) and Z3 5.3.3
([revision](https://github.com/Z3Prover/z3/tree/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e),
`-in`) both returned `sat` and exited 0 for a Bool declaration under QF_BV,
`(< x 3)` under QF_IDL, `(* 2 (+ x 1))` under QF_LIA, and explicit false
proof/core options. Both also accepted a later `get-value` of `(- 1)` under
QF_UFIDL. In contrast, both rejected the tested `**` expression under
QF_NIA; Z3 also printed a subsequent `sat` after its error and exited 1.
Dandelion's `**` rule is a deliberate extension, not a reference consensus.
A separate QF_BV input declaring `f : BV8 -> BV8` was rejected by both;
Z3 printed a later `sat` after errors and exited 1. Both accepted the Bool
constant control. The restricted and unrestricted declaration permissions
are project choices informed by these observations. cvc5's strict mode also
accepted a QF_UFFP Bool-argument UF named `|/|` used as a BV operand of a
Float32 conversion; its negative-equality control returned `unsat`. That
observation motivates identity-based dispatch, not a general FP performance
or compatibility claim.
A four-second process timeout kept each synthetic probe bounded. These
observations do not measure Dandelion or establish the distribution of the
local 2025 corpus.

The [frontend test design](../testing/frontend.md)
requires positive and negative controls for each registered label and each
applicable signature gate, the examples above, reserved-name collisions,
source declaration identity, unsupported Real and String features, and
definition-graph preservation. Validate solving classification separately
against the actual expanded meaning; parser acceptance alone proves neither
fragment completeness nor a solver result.
