# Numeric Implementation and Migration

**Status.** The standalone target and module, the numeric test suite,
and the Node, frontend, and benchmark migrations are implemented.

## Build and module layout

The C++23 static library `dandelion_util` under `src/util/` provides
one named module, `dandelion.util.numeric`. The primary interface
re-exports the five interface partitions. Each type's `.cc` unit
implements its substantive operations.

| File | Module unit and responsibility |
| --- | --- |
| `numeric.cppm` | Primary interface declaring `export module dandelion.util.numeric` |
| `error.cppm` | `:error`, containing the fixed-size error and expected alias |
| `integer.cppm`, `integer.cc` | `:integer` interface and exact signed arithmetic |
| `rational.cppm`, `rational.cc` | `:rational` interface and exact fraction arithmetic |
| `bitvector.cppm`, `bitvector.cc` | `:bitvector` interface and finite-width arithmetic |
| `floating_point.cppm`, `floating_point.cc` | `:floating_point` interface, representation, and concrete evaluation |
| `internal.cppm` | Private `:internal` implementation partition for shared backend and capacity helpers |

The primary interface imports each public partition. `:integer` imports
`:error`. `:rational` and `:bitvector` import `:integer` and any error
declarations they use. `:floating_point` imports the public partitions
whose types appear in its interface, including `:bitvector` and
`:rational`. Only implementation units import `:internal`. Use opaque
declarations attached to the numeric module for backing type names
needed in interface layouts. Where implementation units use GMP headers,
include them through a global module fragment. GMP declarations,
allocation helpers, and test fault controls stay out of the visible
production interface.

Register the primary and interface partitions in a public
`CXX_MODULES` file set and `:internal` in a private file set.
Register implementation units as private ordinary sources. Apply
the project's owned-source checks, verified `import std` support,
and static-library sanitizer policy. Verify the layouts on the
supported GCC and Clang 64-bit configurations. The size requirements
are implementation contracts on those targets and carry no external
binary compatibility promise. [CMake standards](../cmake.md#4-c-modules-and-import-std),
[C++ style](../cpp-style.md)

Link GMP 6.3 or newer with
`target_link_libraries(dandelion_util PRIVATE GMP::GMP)`. Util owns the
direct GMP dependency for both the numeric and node libraries. The final
executable may receive GMP through CMake's link-only propagation for
static libraries. Consumer compilation does not require GMP headers
or its compile options. Node links to util with the module usage
requirements it exposes. Standalone util consumers need no node,
frontend, SAT, or MPFR target. Existing build-time dependency validation
belongs to the GMP provider. MPFR can be linked privately into an
optional reference test executable. [CMake dependency policy](../cmake.md#51-dependency-sources-and-network-policy)

## Node and frontend entry points

Node accepts checked numeric views. On a miss, it copies the canonical
payload into the node block. On a hit, it retains the existing node.
Integer, bit-vector, floating-point and rational views support borrowed
read-only storage, including the inline payload of a live node. A view
can make an independent owning copy. Util adapts borrowed integer
magnitudes to GMP through a read-only stack descriptor. The supported ABI
uses a native `uint64_t` GMP limb with 64 usable bits. Node owns its
diagnostics and translates failures from util at the factory boundary.
[Typed Node values](../node/design.md#10-phase-1c-numeric-boundary-and-typed-literals)

The frontend constructs numeric nodes through typed Node factories and
calls `numeric::RoundRationalToFp` directly when rendering concrete
rational-to-FP conversions. The frontend associates source ranges with
diagnostics. Interpretation of concrete FP outcomes across a query belongs
to the solver and model layers.
[Frontend semantic interfaces](../frontend/semantic-interfaces.md)

## Implementation order and acceptance

1. Add the applicable [numeric GoogleTest cases](../testing/util.md)
   before implementing the scoped behavior. Record missing-interface
   compile failures and real failing cases. Apply the test preservation
   rule and its independent-evidence exception from [AGENTS.md](../../AGENTS.md).
2. Establish the target, public error and value interfaces, capacity
   checks, and narrow private fault controls. Compile positive and
   negative dependency probes with GCC and Clang. Implement Integer
   ownership and arithmetic, then Rational and BitVector. Run the
   complete applicable test matrix at each step, including transitions
   between inline and wide storage.
3. Implement FP format, construction, identity, and outcome checks,
   then the exact rounding kernel and all FP operations. Use the tiny
   enumerators and targeted wide-format checks. Use the same util FP
   kernel in standalone callers and frontend rendering.
4. Complete Node and frontend migration with typed API tests. Keep the
   direct GMP link in util. The util tests check the GMP runtime version,
   exact arithmetic, and fatal OOM. Build the fatal OOM subprocess
   without sanitizer instrumentation so its address-space
   bound measures the intended GMP allocation.
5. Accept only after standalone import/link checks, the complete
   numeric suite, affected node and frontend suites, and the
   supported compiler, sanitizer, and release checks in the test
   design. Record configuration-specific results, including any
   unavailable optional reference oracle. Rewriters and symbolic
   theory/model consumers have separate correctness and acceptance
   obligations. [Numeric acceptance](../testing/util.md#build-and-acceptance)

## Design review

The requirements pass traced the selected Int, BV, and FP inventory
to concrete operators and identified which shape checks, fold rules,
literal diagnostics, and interpretation choices belong to callers.
The interaction pass verified the dependency direction for three
representative paths. Borrowed node bits enter a checked standalone
BV construction and the owning result can outlive the node.
Rational-to-FP rendering uses the util kernel and the frontend's source
diagnostics. Opposite-zero min/max and out-of-range FP-to-BV
pass allowed outcomes to the model without committing to an
interpretation. A failed preparation on either side leaves prior
values and published nodes intact.

The representation review identified the need for an explicit zero
denominator tag separate from the Rational pointer storage and for
GMP capacity checks beyond the public index type width. The semantic
review identified integral rounding overflow in tiny FP formats
whose top binade includes fractions. The implementation gates cover
these cases and the evidence for the selected algorithms. Runtime
performance remains to be measured. [Numeric research](research.md)
