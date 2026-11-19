# Foundation Numeric Testing

Status: **test design for review**, not evidence that these tests or numeric
operations already exist. Applies [the common strategy](README.md),
[AGENTS.md](../../AGENTS.md), [numeric design](../util/README.md), and the
existing [node testing contract](node.md). Each implementation change first
updates this design as necessary and adds the complete applicable GoogleTest
cases. Record expected initial compile failures when an interface is absent.
Do not disable, delete, modify or weaken existing or new tests until all
project-required tests for the change pass; correct a demonstrably incorrect
test only with independent evidence and the documented exception. The
following IDs are required coverage, not existing test names.

## Inventory and coverage matrix

Maintain a reviewable test inventory for every operation in the four type
documents and every numerical Kind in [node operators](../node/operators.md).
For each row identify valid results, invalid inputs/preconditions, width or
format boundaries, resource and ownership behavior, test IDs, oracle and
pass/fail/missing/unsupported status. A parameterized suite must include each
actual operation; a test of one signed predicate does not satisfy all signed
predicates. Mark nonapplicable fields explicitly. Do not count node typing
coverage as execution of a numeric operation.

| Domain | Required partitions |
| --- | --- |
| Integer | zero, both signs, small/big boundaries, exact arithmetic and normalization back to small, all signs of divisor, zero divisor rejection, huge exponents, narrow/parse/hash/move/failure. |
| BV | each listed bitwise, arithmetic, division/remainder, shift, comparison, overflow, structural and Int conversion operation; widths 1, 2, 8, 32, 63, 64, 65 and >64; zero, all ones, sign bit and signed extrema; invalid and huge indices. |
| Rational | reduction, denominator signs, zero, comparison, exact operations, rounding to integer, zero divisor, exact decimal/fraction input, two small/large transitions and ownership. |
| FP and RM | all listed arithmetic, fma, sqrt, rem, integral rounding, min/max, comparisons, predicates and conversions; all five RMs; NaN, infinities, signed zeros, normal/subnormal, ties, overflow, underflow, arbitrary valid formats and choice results. |
| Boundaries | independent checked factories, input vs computed sizes, actual representation and C++ allocation failures, explicitly requested cancellation if supported, no partial publication, borrowed lifetime, compatible node views, conversion edges and mixed-theory callers. No discretionary numeric quota is required. |

The following mapping names all 95 numeric/RM kinds in the current inventory.
Each actual kind in a row needs its own parameterized case, including the
respective oracle and applicable failures; the IDs identify the minimum
test family and do not claim a current pass.

| Kinds | Required test families |
| --- | --- |
| `kIntValue`, `kBvValue`, `kFpValue`, `kRoundingModeValue` | I-01, B-01, F-01/F-05, X-01/X-02, all five modes |
| `kIntNeg`, `kIntAdd`, `kIntSub`, `kIntMul`, `kIntAbs` | I-01 and bounded exact arithmetic, n-ary left-fold cases |
| `kIntDiv`, `kIntMod`, `kIntDivisible`, `kIntPow` | I-02/I-03, X-04 |
| `kIntLt`, `kIntLe`, `kIntGt`, `kIntGe` | I-01 comparison grid and n-ary adjacent chains |
| `kBvNot`, `kBvNeg`, `kBvAnd`, `kBvOr`, `kBvXor`, `kBvNand`, `kBvNor`, `kBvXnor` | B-02 and unary/pair exhaustive checks; n-ary left folds where specified |
| `kBvAdd`, `kBvSub`, `kBvMul` | B-02 and exhaustive arithmetic, including n-ary left folds where specified |
| `kBvUdiv`, `kBvUrem`, `kBvSdiv`, `kBvSrem`, `kBvSmod` | B-03 and exhaustive zero/nonzero sign cases |
| `kBvShl`, `kBvLshr`, `kBvAshr` | B-04 and exhaustive shifts |
| `kBvComp`, `kBvUlt`, `kBvUle`, `kBvUgt`, `kBvUge`, `kBvSlt`, `kBvSle`, `kBvSgt`, `kBvSge` | B-05, exhaustive comparison pairs and BV1 vs Bool result |
| `kBvNego`, `kBvUaddo`, `kBvSaddo`, `kBvUmulo`, `kBvSmulo`, `kBvUsubo`, `kBvSsubo`, `kBvSdivo` | B-02/B-03 and independent exact range tests |
| `kBvConcat`, `kBvExtract`, `kBvRepeat`, `kBvZeroExtend`, `kBvSignExtend`, `kBvRotateLeft`, `kBvRotateRight` | B-05 and all legal tiny indices/checked width failures |
| `kUbvToInt`, `kSbvToInt`, `kIntToBv` | B-01, X-01 and all small-width residues |
| `kFpConstruct`, `kFpFromBits`, `kFpConvert`, `kFpFromSbv`, `kFpFromUbv`, `kFpFromRational`, `kFpToSbv`, `kFpToUbv` | F-01/F-02/F-05, Q-03, X-01/X-04 and five modes as applicable |
| `kFpAbs`, `kFpNeg`, `kFpAdd`, `kFpSub`, `kFpMul`, `kFpDiv` | F-01/F-02/F-04 and tiny exhaustive special/finite cases |
| `kFpFma`, `kFpSqrt`, `kFpRoundToIntegral`, `kFpRem` | F-03/F-04 and tiny exhaustive cases |
| `kFpMin`, `kFpMax` | F-06 and all tiny pairs including NaNs and signed zeros |
| `kFpEq`, `kFpLt`, `kFpLeq`, `kFpGt`, `kFpGeq` | tiny-pair comparison, NaN/zero cases and n-ary chains |
| `kFpIsNormal`, `kFpIsSubnormal`, `kFpIsZero`, `kFpIsInfinite`, `kFpIsNaN`, `kFpIsNegative`, `kFpIsPositive` | all tiny bit patterns and special categories |

## Integer cases

- **I-01**: parse and compare `INT64_MIN`, `INT64_MAX`, each adjacent value,
  `0`, `-000`, `2^64`, `-2^128`; reject empty, lone minus, plus, spaces and
  nondigits. Check decimal round-trip, `TryInt64/Uint64/Uint32`, magnitude
  chunks and `Hash` consistency. `abs(INT64_MIN)=2^63`; negating that result
  returns `INT64_MIN`, which is small again. Exercise `max+1`, `min-1`,
  `min*(-1)`, and cancellation of large equal values without signed UB.
- **I-02**: `DivMod(7,3)=(2,1)`, `(-7,3)=(-3,2)`, `(7,-3)=(-2,1)`,
  `(-7,-3)=(3,2)`; include exact divisions, `0`, `INT64_MIN/-1`, and large
  coprime operands. For a bounded grid `a=-40..40`, `b=-9..9, b!=0`, check
  `a=b*q+r` and `0<=r<abs(b)` using independent host-wide arithmetic. A
  checked numeric zero divisor reports invalid parameter. Nodes representing
  `div` and `mod` by zero remain valid, not a concrete numeric result.
- **I-03**: `gcd(-12,18)=6`, `gcd(0,0)=0`, `lcm(-12,18)=36`,
  `lcm(0,18)=0`, divisibility by large positive indices, and exact quotient
  success/failure. Check `0^0=1`, `0^-3=0`, `(-1)^-3=-1`, `(-2)^-3=0`,
  `(-2)^3=-8`; exponents larger than 64 bits for bases 0, 1 and -1 must use
  their exact parity. `2^1000` is exactly `1` followed by 1000 binary zeroes.
  Separately exercise an actual checked representation or injected C++
  allocation failure without treating it as a numeric value or solver result.
  Check the `ALL`, `QF_EIA` and `QF_NIA` compatibility profiles in the
  frontend suite, and distinguish the latter from the strict catalog grammar.

## BitVector cases

- **B-01**: checked BV8 literal accepts `0..255` but rejects `-1` and `256`;
  `int_to_bv(8,-1)=#xff`, `int_to_bv(8,256)=#x00`. BV65 `2^64` is accepted,
  BV64 `2^64` is not. BV1 `1` differs from BV8 `1` and their hashes obey
  equality. A width of zero is invalid; `2^32` is mathematically valid but
  exceeds the present representation. A tiny value at width `UINT32_MAX`
  need not allocate a width-sized bitstring merely to validate it.
- **B-02**: BV8 `#xff + #x01 = #x00`, `#x80 * #x02 = #x00`,
  `-#x80 = #x80`; unsigned add/multiply overflow predicates are true in the
  first two cases, signed negation overflow is true in the last. Check each
  signed and unsigned add/sub/multiply overflow both ways using exact signed
  and unsigned range tests, including `#x7f+#x01`, `#x00-#x01` and
  `#x80-#x01`. For BV8, signed `#x80 / #xff = #x80`, `sdivo=true`.
- **B-03**: BV8 unsigned `#x35 / 0 = #xff`, `urem(#x35,0)=#x35`;
  `sdiv(#x80,0)=#x01`, `sdiv(#x7f,0)=#xff`, and `srem`/`smod` by zero
  return the dividend. `srem(-5,3)=-2`, `smod(-5,3)=1`,
  `smod(5,-3)=-1`, expressed as BV8 residues. Include zero dividend and all
  four operand sign combinations.
- **B-04**: BV8 `#x80 >>logical 8 = 0`, `>>arithmetic 8 = #xff`,
  `#x7f >>arithmetic 255 = 0`, `#x01 << 8 = 0`; test `w-1`, `w`, `w+1`
  and a BV65 shift amount `2^64`. The operand is a full unsigned value, not
  a machine shift count. Run width-64 masks and shifts under UBSan.
- **B-05**: `concat(#xa,#x3)=#xa3`, extract `(7,4)=#xa`,
  `repeat(3,#b10)=#b101010`, zero/sign extension of `#b10` by 2 gives
  `#b0010`/`#b1110`, rotate-left BV8 `#x81` by 1 gives `#x03`, and
  rotate-right by 1 gives `#xc0`. Rotate by `18446744073709551617` equals
  rotate by 1 on BV8 while node index payloads remain different. Check
  invalid extract order/range, zero repeat, valid zero extension, and exact
  overflowing sums/products before narrowing. `bvcomp(#x12,#x12)=#b1`.

For every width 1..6, exhaustively enumerate every operand and pair for all
binary bitwise, arithmetic, signed/unsigned division/remainder, shift,
comparison and overflow operations, plus each unary operation. Use an
independent mathematical oracle based on host integers wide enough for
`2^(2w)` and on explicit signed interpretation; implement signed quotient
truncation and zero rules independently. Exhaust all legal extracts and small
rotations/extensions/repeats that fit the test bound. This is finite evidence,
not coverage of large-width allocation or a proof. Add deterministic seeded
large cases at 63/64/65 and multiple limbs, recording seed and comparing with
an independently implemented arbitrary-integer oracle. Do not derive expected
values by calling the production BitVector implementation.

## Rational cases

- **Q-01**: `(-2)/(-4)=1/2`, `2/(-4)=-1/2`, `0/(-9)=0/1`; denominator zero
  is invalid. Reduction of large common factors returns small components;
  check `1/2 == 2/4`, identical hash and canonical output, copy independence,
  self-copy/self-move, and moved-from `0/1`.
- **Q-02**: `1/6+1/3=1/2`, `1/6-1/3=-1/6`,
  `(-2/3)*(9/4)=-3/2`, `(2/3)/(-4/5)=-5/6`; division by zero is rejected.
  Compare `-7/3` with `-2`; floor `-7/3=-3`, ceiling and truncation are `-2`.
  Exact Integer conversion succeeds for `6/3` and fails for `1/2`.
- **Q-03**: parse the compatible nested expression `(/ (- 1.5) (/ 3 2))`
  as `-1`; every nested zero denominator, e.g. `(/ 1 (/ 0 3))`, is
  `kUnsupportedFeature` at the frontend boundary. Test symbolic rounding
  mode as a child and a normalized rational payload in the node. Reject
  general symbolic Real terms. These are frontend tests when that component
  exists, not claims that the current node tests parse the grammar.

### Frontend input and rendering bridge

- **NUM-INPUT01–04** (`NumericInput`): import unsigned base-2 and base-16
  digits, including equivalent encodings of `2^64`, leading zeroes and
  mixed hex case. Reject signs, whitespace, prefixes, empty strings, bad
  digits and other radices. The decimal-fraction helper takes two nonempty
  decimal digit sequences separated by one dot and normalizes the result.
  Test normalization to zero, exact reduction and a denominator beyond a
  machine word. Negate zero, signed-word extrema and multi-limb rationals.
  Divide with both signs, reduction to small values and zero divisor
  rejection. Verify C++ allocation failure and independence of the inputs.
- **NUM-FP-RENDER01–03** (`NumericRounding`): independently enumerate the
  nonnegative finite values of format (2,3) in units of 1/4. Compare a grid
  of exact rationals to both neighbors under each rounding mode, both
  signs, signed zero, subnormal transition and overflow. Use the virtual
  midpoint to the next binade for nearest overflow. Verify format and
  mode validation, actual total-width overflow, and round a small input
  at exponent width 65 to exercise bias encoding beyond a host word.
  Inject C++ allocation failure for a wide field. The bridge computes
  rational conversions for model rendering. The wider FP evaluator has
  its own implementation and acceptance scope.

## FP cases and independent oracle

For F(2,3), the bias is 1, minimum positive subnormal is `1/4`, minimum
positive normal is `1`, and maximum finite is `7/2`. Enumerate all 32 bit
patterns: both signs, exponent `00/01/10/11`, fraction `00..11`; exponent
`11` with nonzero fraction is the single NaN. Both zeros have distinct core
identity and compare equal under `fp.eq`; NaN equals itself under core
equality, while `fp.eq(NaN,NaN)` and all ordered comparisons with NaN are
false. Check both positive/negative predicates on NaN are false. Retain
existing `EveryTinyFpPatternHasTheHandDerivedCategory` node evidence.

- **F-01 (rounding)**: convert exact `9/8` to F(2,3): RNE, RTN and RTZ give
  `1`; RNA and RTP give `5/4`. For `-9/8`, RNE, RTP and RTZ give `-1`;
  RNA and RTN give `-5/4`. Convert `1/8`: RNE, RTN, RTZ give `+0`; RNA and
  RTP give `1/4`. Convert `-1/8`: RNE, RTP, RTZ give `-0`; RNA and RTN
  give `-1/4`. These are exact halfway decisions, not host floats.
- **F-02 (overflow)**: convert `15/4` to F(2,3): RNE, RNA and RTP give
  `+infinity`; RTN and RTZ give `7/2`. Repeat with negative sign and swapped
  RTP/RTN directions; also test just below and above the halfway threshold
  using exact rationals. Include exact maximum finite and smallest subnormal.
- **F-03 (one rounding)**: in F(2,3), `fma(RNE,3/2,3/2,-2)=1/4`, while
  rounding the product first gives `2`, then adding `-2` gives `+0`.
  `sqrt(RNE,5/2)=3/2` since `5/2 < (13/8)^2`; `sqrt(-0)=-0`,
  `sqrt(-1)=NaN`. `rem(7/2,1)=-1/2` because the halfway quotient rounds to
  even integer 4. Also check exact cancellation and zero signs in all RMs.
  Local Z3 4.13.1 disagreed on the FMA example, whereas Bitwuzla 0.7.0 and
  experimental cvc5 1.4.2 agreed; see the
  [recorded reproduction](../util/research.md#small-local-differential-check)
  and use the exact arithmetic as the expectation, not a solver vote.
- **F-04 (special arithmetic)**: check all signs for zero/infinity in add,
  subtract, multiply and divide: `0/0`, `infinity/infinity`, `0*infinity`,
  and `infinity + -infinity` produce NaN; `1/+0=+infinity`,
  `1/-0=-infinity`; finite/infinity is signed zero. Include fma invalid
  special combinations, finite round-to-integral `+/-1/2` under every RM,
  and signed zero from remainder and integral rounding.
- **F-05 (conversions)**: F(2,3) `-1/4` converted to unsigned BV2 under
  RTZ gives `#b00`; under RTN the rounded integer is `-1` and the result is
  unspecified. At BV2 signed/unsigned range endpoints and one beyond, test
  every RM; NaN and both infinities produce unspecified outcomes. Convert
  BV8 `#xff` as signed `-1` versus unsigned `255` to a format large enough
  for exact results; compare FP-to-FP narrowing at halfway, zero signs,
  infinity, and NaN. Convert all finite tiny FP values to exact rationals,
  separately retaining zero's sign; NaN and infinity have no rational value.
  Reinterpret every tiny bit pattern and compare with field construction.
- **F-06 (choices)**: both argument orders of `min` and `max` on opposite
  zeros report the allowed set `{+0,-0}`, not one mandatory result. Single
  NaN returns the other operand, both NaNs return NaN. Distinguish an
  unspecified BV result from resource failure. Different equal NaN encodings
  and equal RM values form the same semantic argument tuple for model
  congruence. Once solver/model support exists, the formula requiring
  `min(+0,-0) != +0` must remain satisfiable, while asserting distinct
  results for the same FP-to-BV symbol, RM and semantic NaN must be unsat.

For tiny formats F(2,2) and F(2,3), build an independent reference enumerator
from the bit-field equations in the standard, using exact signed fractions
and a separately coded table of special cases. For finite exact arithmetic,
enumerate ordered target representable values and choose by exact distance or
direction; model overflow using the next unbounded-exponent binade, not host
infinity distance. For sqrt compare exact squares of candidates and their
midpoints against the input. For remainder choose the nearest-even quotient
by independent integer arithmetic. Exhaust unary cases, all pairs and modes,
and all triples/modes for fma where feasible; record any test bound rather
than claiming exhaustive arbitrary formats. Explicitly verify special and
choice outcomes, not just numerical fractions. Compare the reference oracle's
own tiny tables against the hand cases above before using it. This oracle is
independent in structure from the proposed quotient/rounding implementation,
but shares the same standard and can still be wrong.

## Integration, failure, and acceptance

- **X-01**: construct Integer -> modulo BV -> signed/unsigned Integer, BV
  bits -> FP -> concrete operation, and Rational -> FP. Verify checked literal
  construction and modulo conversion differ. Compare direct standalone values
  with borrowed node views for matching sort/payload; destroy or mutate the
  original owner after publication, then verify node value/hash. A view does
  not outlive the node. Wrong sort, manager, format or width fails at the
  relevant independent entry point.
- **X-02**: preserve `numeric_test.cc`, `value_test.cc`, `identity_test.cc`,
  `boundary_test.cc`, `frontend_boundary_test.cc`, `failure_test.cc`,
  `gmp_failure_test.cc`, and their related node lifecycle/collision tests.
  They already cover current parse, normalization, NaN interning, distinct
  zeros, exact indices, borrowing, rollback, exceptions, self-move and fatal
  GMP OOM. They do not exercise a future arithmetic evaluator.
- **X-03**: inject each project-controlled allocation failure in copy,
  assignment, conversion and node publication until the operation succeeds;
  require the old value and all preexisting nodes/views remain valid. Test
  actual representation boundaries and any explicitly supported cancellation
  without attempting enormous allocations. GMP limb OOM follows the existing
  isolated subprocess test, never a fake recoverable C++ allocation injection.
  A large valid sort and a special FP value can exist even if a subsequent
  arithmetic operation encounters an actual allocation or representation failure.
- **X-04**: later solver tests must cover equal semantic arguments across
  syntactically distinct terms for integer division by zero, FP conversions
  and FP min/max, including values passing through UF, arrays or DT fields.
  Preserve model choices and validate original assertions. A numeric test
  cannot by itself establish symbolic encoding or theory combination.

Use GoogleTest for assertions and CTest discovery; a helper may compute
independent expected tables. Run applicable numeric and node labels with
`--no-tests=error`, GCC Debug ASan/UBSan, Clang Debug, and GCC Release and
RelWithDebInfo as available. Sanitizers exercise only reached project paths;
an unavailable Clang sanitizer runtime is recorded, not passed. Optional
differential runs must pin solver, version, flags, formats and backend; compare
defined values only and compare choice sets or satisfiability for unspecified
cases. Bitwuzla and cvc5 may share SymFPU or MPFR, while Z3 or SoftFloat have
their own restrictions and possible bugs. Majority agreement is not proof;
record disagreements, skipped formats, timeouts, crashes and unknowns
separately. No dataset is needed for these bounded tests.
