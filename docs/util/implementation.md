# Numeric Implementation and Migration Plan

Status: **proposed baseline for review**, not an implemented change. Each
phase covers its complete scoped behavior, not merely the easy examples. The
interfaces and error mapping below are design obligations; final method
spellings can follow the project style guide without changing semantics.

## Files, modules and build boundaries

The four types must have separate substantive implementations:

```text
src/util/
  integer.cppm          integer.cc
  bitvector.cppm        bitvector.cc
  rational.cppm         rational.cc
  floating_point.cppm   floating_point.cc
  numeric_error.cppm    [small implementation file if needed]
src/node/
  node_contracts.cppm   [reference-limit implementation if needed]
  numeric.cppm          [compatibility re-export during migration]
```

Each `.cppm` owns its type and each `.cc` owns its substantive operations;
additional clearly scoped helper files are allowed. A common file may define
small error types or exact-rounding helpers, but it must not house the bulk of
all four implementations while the named files are shells. The private FP
rounding algorithm may live in a separate FP-specific implementation unit.
Proposed imports are `integer -> node_contracts` only for legacy signatures;
`bitvector -> integer`; `rational -> integer`; `floating_point -> bitvector,
rational`; and `node -> numeric compatibility + floating_point`. New
computation errors are independent of NodeManager. This is an acyclic module
graph, not a demand for four libraries or four CMake targets.

`node_contracts` is a small dependency-free historical-compatibility module
owning the existing `NodeError`, `ErrorCode`, `ResourceKind`, `Result`, and
`ReferenceLimit` declarations. The last is node reference-count machinery,
not a numeric failure. Keeping this lower module preserves the existing
`Integer::ParseDecimal` and `Rational::Make` return types without making util
depend on NodeManager. `dandelion.numeric` re-exports it and the moved Integer
and Rational modules, so existing `import dandelion.numeric` and
`import dandelion.node` consumers continue to see the same names. A separate
`NumericError`/`NumericResult` belongs to new calculation interfaces. Node
factories continue to translate their own checked errors to existing
`NodeError` values, including `kResourceLimit/kCppAllocation` and
`kResourceLimit/kSize`; the solver boundary handles actual inability to
compute and any explicitly requested cancellation according to its result
contract. Do not add fictitious
resource enumerators to today's implemented API in documentation or reuse a
node operand index for a numeric allocation error.

Named C++ module entity attachment, re-exports, test-only peers, GMP visibility
and static-link propagation need a real GCC and Clang build check. Avoid
defining the same entity in two named modules during a staged migration.
Choose one or a small number of CMake targets according to actual consumers;
module files and libraries are different concepts. Register public module
file sets and private implementation sources as appropriate, retain GMP as a
private implementation dependency, no SAT dependency, and preserve sanitized
static-library runtime link propagation. Follow [cmake.md](../cmake.md); no
CMake change is made by this proposal.

## Migration and compatibility gates

1. **Before source edits**, update or extend [the numeric test design](../testing/util.md)
   for the exact scoped behavior and add all required GoogleTest cases. Run
   them and record missing-interface compile failures or real failing tests.
   Do not remove, disable, modify or weaken existing or new cases while any
   required case fails. Correct an independently demonstrated incorrect test
   only under AGENTS.md's exception, recording the evidence.
2. Move the declarations and definitions currently in `src/node/numeric.cppm`
   and `numeric.cc` to their owning modules and compatibility re-export;
   isolate the node-only error/reference declarations. Preserve the existing
   decimal grammar, exact compare/hash/chunks, `IntegerView` and
   `RationalView` interfaces, private GMP, test-only numeric allocation
   hooks, small canonical representation, strong assignment, `noexcept` moves
   and moved-from values. Run all existing node tests unchanged. This move is
   a source/module migration, not evidence of new arithmetic.
3. Add complete scoped tests first, then Integer arithmetic and Rational
   operations. Add BV tests first, then the standalone width-bearing value
   and its checked operations. Each type's failures must be atomic; no fake
   return values or test disables while code is incomplete.
4. Add tests first, then FP format/value classification and all concrete
   operations with explicit outcome categories. No new backend is installed.
   Test format boundaries and actual representation failures, not only
   standard machine formats; do not introduce discretionary numeric quotas.
5. Add borrowed node adapters and integration tests first, then the adapter.
   Continue storing the existing payloads initially: preserve `GetInteger`
   for both Int and BV, `GetFpCategory`, `GetFpBits` only for finite bits,
   exact Rational payload, source lifetime and node hash/identity. Validate
   format or width against the associated sort before publishing an owning
   value to a node. Keep `MakeBvValue`, `MakeFpBits`, `MakeFpSpecial`, and
   their current errors. The checked adapters must not retain node references
   accidentally or expose a view into a temporary.
6. Introduce rewriter, symbolic FP encoding, theory coordination and model
   callers only under their own designs and tests. They must preserve
   underspecified function congruence and original-query model recovery.
   A complete numeric layer does not establish those solver obligations.

The new common outcome type and optional view/factory overloads expand the
API but do not change existing Node observable semantics. Moving the existing
module entities and `RoundingMode` can break source imports or C++ module
attachment if done carelessly: the compatibility re-exports are mandatory,
and any unavoidable incompatibility requires an explicit reviewed contract
change. If a later change replaces BV/FP node payload storage, then update
`docs/node/design.md`, `operators.md`, `implementation.md`,
`docs/testing/node.md` and inventory, source adapters, and affected callers
in the same tested change; preserve literal accessors and interning unless a
new contract is explicitly approved. This proposal does not make that change.

## Acceptance and open choices

The gate is complete applicable [numeric tests](../testing/util.md), existing
node tests unchanged, GCC and Clang builds, GCC ASan/UBSan, release behavior,
and documented failure/unsupported/unknown results. Run CTest discovery with
`--no-tests=error`; record unavailable configurations separately. The node
test suite alone checks typing, value ownership and structural identity, not
all new numeric or solver semantics. Performance needs representative future
measurements with source versions, compilers, configurations, seed and workload;
no speedup is asserted here.

Review before implementation: exact method names and error enum layout;
the cost of `Integer`-backed BV64; concrete FP
algorithm optimizations and any future MPFR or symbolic SymFPU dependency;
the eventual symbolic encoding and model-choice interfaces. None of these
may change the defined values, required operator semantics, or existing node
contracts by default. The existing 32-bit BV width and FP format bounds are
retained; they are implementation boundaries, not general SMT-LIB limits or
discretionary computation budgets.

| Unresolved decision and reason | Impact, decision gate, and fallback |
| --- | --- |
| The exact result/error API spelling and module re-export mechanics need supported-toolchain compilation, particularly existing `Result` and test-peer visibility. | Affects numeric callers and node source compatibility, not mathematical values. Before moving code, add compatibility tests and compile an isolated GCC/Clang module probe; preserve the current module until a compatible migration passes. Any proposed source contract break requires explicit review. |
| The exact FP baseline still needs implementation-level correctness arguments for each special case, midpoint, subnormal, overflow and sqrt scaling; shortcuts need separate proofs. No complete evaluator exists to validate. | FP concrete arithmetic must not be called implementation-ready until those arguments and the complete test-first cases are reviewed. Use the exact Integer/Rational baseline with resource failure; disable no tests and retain terms or return `unknown` until an operation is established. |
| Symbolic FP encoding and the representation of model choices require a separate theory/model design and dependency evaluation. | All FP symbolic operators and equal-argument congruence are affected. Decide before claiming symbolic FP support; an exact encoding or another sound procedure must establish the result, otherwise return `unknown`. The concrete value layer alone is not that procedure. |
| Inline unsigned BV storage or MPFR may improve particular costs, but no Dandelion workload measurements exist. | These do not block the selected Integer-backed BV and exact FP baselines. Reconsider only with versioned correctness, ownership and representative performance evidence; any replacement must preserve the same contracts. |

## Architecture review record

Two design passes were made for this proposal. The requirements pass traced
all 95 numeric/RM node kinds to concrete operations, checked the present
`uint32_t` sort limits and exact indices, and separated existing literal
representation from planned evaluation. The challenge pass compared ownership,
failure and oracle dependencies: in particular, a new inline BV representation
would not automatically provide the existing borrowed `IntegerView`, so the
selected bridge keeps the current payload; and two SymFPU-based solvers do
not provide independent evidence. These decisions and the test matrix were
updated together.

Representative flows were traced: an arbitrary integer index is validated
before width narrowing, an Int-to-BV conversion computes a residue while a
literal is range-checked, and a borrowed BV node view dies with its owner;
a closed Rational payload with symbolic RM stays an operation node until a
concrete RM is known; an opposite-zero min or invalid FP-to-BV result reaches
the model as a choice, preserving equal-argument congruence. At every flow a
checked size or allocation failure leaves existing values and nodes intact; GMP
limb OOM remains a separate fatal limitation. This review is design evidence,
not an implementation proof. The decision gates in the table remain open.
