# Foundation Numeric Testing

**Status.** The numeric GoogleTest suites run against `dandelion_util`.
The Node and frontend suites cover the typed numeric boundaries.
[Common testing strategy](../testing/README.md), [numeric specification](../util/README.md),
[test-first rules](../../AGENTS.md)

Add each scoped suite before implementing its behavior. Keep all required
cases enabled and record missing-interface compile failures as such.
Preserve cases until all required checks pass. The AGENTS.md exception
allows a demonstrated test error to be corrected with independent
evidence. The identifiers in this document label required coverage.

## Inventory and coverage matrix

Track every public operation in the type documents and every numeric
and RM kind in the node inventory. Each inventory row identifies the
test family, oracle, normal cases, applicable invalid arguments, size
and ownership boundaries, and execution status. Treat nonapplicable
conditions explicitly. Parameterize by the actual operation, with a
failure name and enough operand data to reproduce it. The following
table maps the node families to concrete tests. Integration suites own
the applicable folds, chains, and parameter semantics.
[Node operator inventory](../node/operators.md)

| Kinds | Required test families |
| --- | --- |
| `kIntValue`, `kBvValue`, `kFpValue`, `kRoundingModeValue` | I-01, B-01, F-01, X-01/X-02, all five modes |
| `kIntNeg`, `kIntAdd`, `kIntSub`, `kIntMul`, `kIntAbs` | I-02 and bounded exact arithmetic, n-ary left-fold cases |
| `kIntDiv`, `kIntMod`, `kIntDivisible`, `kIntPow` | I-03/I-04, X-04 |
| `kIntLt`, `kIntLe`, `kIntGt`, `kIntGe` | I-01/I-02 comparison grid and n-ary adjacent chains |
| `kBvNot`, `kBvNeg`, `kBvAnd`, `kBvOr`, `kBvXor`, `kBvNand`, `kBvNor`, `kBvXnor` | B-02, exhaustive unary and pair checks, applicable n-ary left folds |
| `kBvAdd`, `kBvSub`, `kBvMul` | B-02 and exhaustive arithmetic, including n-ary left folds where specified |
| `kBvUdiv`, `kBvUrem`, `kBvSdiv`, `kBvSrem`, `kBvSmod` | B-03 and exhaustive zero/nonzero sign cases |
| `kBvShl`, `kBvLshr`, `kBvAshr` | B-04 and exhaustive shifts |
| `kBvComp`, `kBvUlt`, `kBvUle`, `kBvUgt`, `kBvUge`, `kBvSlt`, `kBvSle`, `kBvSgt`, `kBvSge` | B-05, exhaustive comparison pairs and BV1 vs Bool result |
| `kBvNego`, `kBvUaddo`, `kBvSaddo`, `kBvUmulo`, `kBvSmulo`, `kBvUsubo`, `kBvSsubo`, `kBvSdivo` | B-02/B-03 and independent exact range tests |
| `kBvConcat`, `kBvExtract`, `kBvRepeat`, `kBvZeroExtend`, `kBvSignExtend`, `kBvRotateLeft`, `kBvRotateRight` | B-05 and all legal tiny indices/checked width failures |
| `kUbvToInt`, `kSbvToInt`, `kIntToBv` | B-01, X-01 and all small-width residues |
| `kFpConstruct`, `kFpFromBits`, `kFpConvert`, `kFpFromSbv`, `kFpFromUbv`, `kFpFromRational`, `kFpToSbv`, `kFpToUbv` | F-01/F-02/F-05, Q-03/Q-04, X-01/X-04 and five modes as applicable |
| `kFpAbs`, `kFpNeg`, `kFpAdd`, `kFpSub`, `kFpMul`, `kFpDiv` | F-01/F-02/F-04 and tiny exhaustive special/finite cases |
| `kFpFma`, `kFpSqrt`, `kFpRoundToIntegral`, `kFpRem` | F-03/F-04 and tiny exhaustive cases |
| `kFpMin`, `kFpMax` | F-06 and all tiny pairs including NaNs and signed zeros |
| `kFpEq`, `kFpLt`, `kFpLeq`, `kFpGt`, `kFpGeq` | tiny-pair comparison, NaN/zero cases and n-ary chains |
| `kFpIsNormal`, `kFpIsSubnormal`, `kFpIsZero`, `kFpIsInfinite`, `kFpIsNaN`, `kFpIsNegative`, `kFpIsPositive` | all tiny bit patterns and special categories |

## Integer cases

- **I-01 (construction and inspection).** Check the full signed storage
  boundary around `INT64_MIN/MAX`, adjacent large values, zero, unsigned
  `2^63`, `UINT64_MAX`, `2^64`, and signed multi-limb values. Assert exact
  decimal text, sign, parity, bit length, each relevant 64-bit magnitude
  chunk, and zero for absent chunks. Assert checked narrowing at both
  boundaries of each target and empty outside. Compare independent
  signed, unsigned, owned, and borrowed constructions and their hashes.
  Check decimal negative zero and leading zeros, and binary/hex
  round-trips with mixed hex case and equivalent digit strings.
  Empty text, lone minus, plus, prefixes, whitespace, embedded NUL,
  and invalid digits give `kInvalidText` where invalid. An unsupported
  radix gives `kInvalidArgument`.
- **I-02 (arithmetic).** Compare add, subtract, multiply, unary negation,
  absolute value, and ordering on `[-40,40]` with exact host arithmetic.
  Exercise `max+1`, `min-1`, `min*(-1)`, `abs(min)=2^63`, and the
  reverse operation that canonicalizes the value to `INT64_MIN`.
  Test large-positive/negative cancellation to zero or a word, and
  mixed and large-large arithmetic with independent large expectations.
  Test GCD and LCM on both signs, zero, identical and coprime magnitudes,
  including `Gcd(-12,18)=6`, `Gcd(0,0)=0`, `Lcm(-12,18)=36`, and
  `Lcm(0,x)=0`.
- **I-03 (division and divisibility).** Independently enumerate the
  Euclidean pair for `a=-40..40` and nonzero `b=-9..9`. Check
  `a=b*q+r` and `0<=r<abs(b)`. Include `(-7,3)=(-3,2)`,
  `(7,-3)=(-2,1)`, `(-7,-3)=(3,2)`, and `INT64_MIN/-1`.
  Test exact and inexact signed multi-limb divisions, zero dividend,
  large divisor, and exact divisibility on a positive arbitrary
  Integer SMT index. `ExactDiv` returns `kInvalidArgument` for nonzero
  remainder. All three checked numeric division APIs return
  `kDivisionByZero` on divisor zero. Node `div` and `mod` with zero
  remain well-sorted.
- **I-04 (power).** Check `0^0=1`, exponent zero on both signs, positive
  exponent parity, `(-2)^3=-8`, `0^-3=0`, `(-1)^-3=-1`,
  `1^-3=1`, `(-2)^-3=0`, and positive base greater than one with negative
  exponent. Check `2^1000` using bit length and exact bits. For exponent
  magnitudes beyond 64 bits, use even and odd values and both exponent
  signs for the bases `0,1,-1`. An exponent placing a nontrivial result
  beyond actual GMP bit capacity gives `kSizeLimit` before a giant
  allocation. Check the selected `**` input profiles in frontend tests.

## Rational cases

- **Q-01 (canonical form and access).** Verify `(-2)/(-4)=1/2`,
  `2/(-4)=-1/2`, `0/(-9)=0/1`, and division-by-zero failure at
  construction. Cover both word limits, including
  `INT64_MIN/1`, `1/UINT64_MAX`, and canonical numerator and denominator
  one past each applicable word boundary. Reduce common multi-limb
  factors back to a word fraction. Check inspection of both components
  from word and wide forms without allocation, especially an inline
  denominator above `INT64_MAX`. Check equality, sign, canonical
  fraction or integer output, hash, and exact promotion from signed
  and multi-limb Integers.
- **Q-02 (arithmetic and ordering).** Check `1/6+1/3=1/2`,
  `1/6-1/3=-1/6`, `(-2/3)*(9/4)=-3/2`, and
  `(2/3)/(-4/5)=-5/6`. Apply each operator to small-small,
  small-wide, and wide-wide operands, with negatives, zero, and
  signed minima, including subtraction and division in both operand
  orders. Include like- and unlike-denominator sums, reduced
  cross-cancelled products, large intermediates with inline results,
  and word overflow that must promote. For example, square
  `1/UINT64_MAX` to promote, and divide by that same fraction to return
  inline. Sum two copies of `INT64_MIN/UINT64_MAX` and check the exact
  promotion. Add `INT64_MIN/(UINT64_MAX-2)` to `INT64_MIN/UINT64_MAX`
  and subtract `(INT64_MAX-1)/(UINT64_MAX-2)` from it. The coprime
  denominators give cross-product sums and differences below the signed
  128-bit minimum. Check the exact reduced fractions and wide storage.
  Negate and take the absolute of both signs, zero, and
  `INT64_MIN` with denominator one. Compare word and multi-limb
  fractions on either side of each other and equivalent reduced
  fractions. Division by zero gives `kDivisionByZero`.
- **Q-03 (integral operations).** For `-7/3` expect floor `-3`,
  ceiling and truncation `-2`. Check corresponding positive values,
  zero, values between zero and one in both signs, and exact
  integral, wide integral, and wide nonintegral inputs. Verify
  `IsInteger` and checked exact conversion on integral and
  nonintegral fractions. Nonintegral exact conversion gives
  `kInvalidArgument`.
- **Q-04 (text and frontend boundary).** Decode `1.500` as `3/2`,
  check exact zero and fractions requiring wide denominators, and
  reject missing digits, additional dots, signs, whitespace, radix
  prefixes, and exponent notation. At the frontend verify the
  exact payload of `(/ (- 1.5) (/ 3 2))` is `-1`, including with
  symbolic RM. A nested zero denominator, such as `(/ 1 (/ 0 3))`,
  is outside the compatibility grammar and gives the frontend's
  `kUnsupportedFeature`. Cover the general Real exclusions at that
  boundary. [Frontend profile](../node/operators.md#6-floating-point)

## BitVector cases

- **B-01 (construction, representation, conversions).** Accept BV8
  values 0 through 255. Checked literals reject `-1` and `256`
  with `kValueOutOfRange`. Modulo BV8 conversion gives `#xff` and
  `#x00`, respectively. BV64 accepts `2^63` and `UINT64_MAX` inline
  and rejects `2^64`. BV65 accepts that value with two limbs.
  Check zero and negative widths, width `2^32`, and zero and nonzero
  inline payloads at `UINT32_MAX`. Check text grammar, radix-specific
  output widths including leading zeros, mixed hex case, invalid
  digits, empty text, and exact width-padded binary output. Check
  word narrowing, bits, effective length, chunks including absent
  chunks, width-sensitive identity and equality/hash. Test both
  interpretations and modulo conversion with multi-limb signed
  operands, including exact `2^w` multiples of either sign and
  the signed minimum.
- **B-02 (bitwise, modular arithmetic and overflow).** Exercise every
  operation in the BV inventory. Include BV8
  `#xff + #x01 = #x00`, `#x80 * #x02 = #x00`, and
  `-#x80 = #x80`, with their respective unsigned-add,
  unsigned-multiply, and signed-negation overflows. Check each
  overflow predicate on true and false cases using exact mathematical
  range tests. Include carry, borrow, and sign crossing at 63, 64,
  and 65 bits and at multiple limb boundaries. At width 65, check both
  outcomes for all eight predicates using the signed endpoints and
  the all-ones unsigned maximum. Verify a wide
  result trims back to an inline word, including a BV64 residue
  with its sign bit set.
- **B-03 (division).** BV8 unsigned `#x35 / 0 = #xff` and
  `urem(#x35,0)=#x35`. Signed division by zero returns `#x01` for
  `#x80` and `#xff` for `#x7f`. Signed remainders return the
  dividend. Check zero dividend, BV1, all signed operand signs,
  `srem(-5,3)=-2`, `smod(-5,3)=1`, `smod(5,-3)=-1`, and
  `sdiv(signed_min,-1)=signed_min` with `sdivo=true`.
  Cover numerator shorter than divisor, equal and unequal
  multi-limb magnitudes, single-limb divisors, and exact and
  inexact divisions on the native and conversion paths. Use the same
  wide value as both operands and check that the quotient is one and
  the remainder is zero without allocation. Check separately owned
  equal operands and preserve the zero-divisor results for `0/0`.
- **B-04 (shifts).** Check shift amounts `0,1,w-1,w,w+1`,
  largest representable shifts, and BV65 shift amount `2^64`.
  Include BV8 `#x80` shifted right by 8, giving zero logically
  and `#xff` arithmetically. Use nonnegative and negative
  signed inputs across word boundaries and exercise effective
  bit length changing in either direction. Check 64-bit mask
  and zero/full-width shift paths under UBSan.
- **B-05 (comparisons and structural operators).** Assert all signed
  and unsigned predicates on equality and either order.
  `bvcomp` returns BV1 on both equal and unequal values. Verify
  concat and extract with unequal widths, and offsets that straddle a
  64-bit boundary. Check repeat, zero/sign extension, and both rotates,
  including zero- or sign-fill, high-bit set and clear, identity,
  many-width rotation, and huge positive indices. On BV8
  `#x81`, rotate by `18446744073709551617` as if by one,
  giving `#x03` left and `#xc0` right. Borrow limbs `{1, 1}` for
  the same index and check both rotations by residue three at width
  seven and by residue zero at width 274177. At width `UINT32_MAX`,
  concatenate or extend by one and repeat by two to get
  `kSizeLimit`, even for zero payload. For every applicable
  binary call, exercise mismatched widths. Check negative indices,
  invalid extract order and bound, zero repeat, and legal zero
  extension and rotation.

For each width 1 through 6, enumerate all values and all ordered
same-width pairs. Compare every unary and binary operation, including
all division and overflow predicates, with a separately written
host-integer reference. That reference computes shifts from exact
counts, checks overflow with unwound signed/unsigned range
comparisons, and handles zero division explicitly. Select the
truncating signed quotient by dividing magnitudes and restoring
sign, then reconstruct remainder. Exhaust concat on ordered
pairs of widths 1 through 6, extracts at every legal position,
rotations of every residue and representatives beyond one width,
extensions by 0 through 6, and repeats of 1 through 6. For each
width, check all signed and unsigned interpretations and
`int_to_bv` for integers from `-2*2^w` through `2*2^w`.

Add deterministic large reference cases at widths 8, 32, 63, 64, 65,
and several multi-limb widths, using an independently implemented
arbitrary-integer oracle. Include operands with more than 64 effective
bits for direct GMP and portable conversion paths. Seed the inputs
and report reproducible tuples for discrepancies.

## FP cases and independent oracle

- **F-01 (format, construction, inspection).** Test checked format
  creation, `FromBits`, its Integer overload, `FromFields`,
  specials, and representative bit output. Reject components below
  two, width or sum above `UINT32_MAX`, invalid special categories,
  malformed field widths, mismatched packed BV width, and negative
  or oversized Integer bits with the specified codes. Check
  `e+s=UINT32_MAX` with an inline positive subnormal and a canonical
  special. Verify every predicate, category, sign, and exact
  finite field across tiny formats and machine/limb boundaries.
  Same-format NaNs from different signs and payloads compare equal
  under core equality and hash equally. Opposite zeros differ in
  core identity. Formats participate in identity. Check every IEEE
  predicate on NaNs, opposite zeros, ordered finite pairs, and both
  infinities. Round-trip representative bits and fields, checking
  exact non-NaN bits and NaN identity.
- **F-02 (all rounding modes and boundaries).** In F(2,3), bias is
  1, minimum subnormal is `1/4`, minimum normal is `1`, and
  maximum finite is `7/2`. In mode order RNE, RNA, RTP, RTN, RTZ,
  `9/8` gives `1, 5/4, 5/4, 1, 1` and `-9/8` gives
  `-1, -5/4, -1, -5/4, -1`. For `1/8` expect
  `+0, 1/4, 1/4, +0, +0` and for `-1/8` expect
  `-0, -1/4, -0, -1/4, -0`. On the overflow tie `15/4` expect
  `+inf, +inf, +inf, 7/2, 7/2`. For its negative expect
  `-inf, -inf, -7/2, -inf, -7/2`. Check both adjacent sides of
  each tie, exact endpoints, carry between binades, carry from
  largest subnormal to minimum normal, tiny nonzero directions,
  and exact rational zero. Pass an invalid mode to every independently
  callable operation that accepts one.
- **F-03 (one rounding and exact kernels).** In F(2,3),
  `fma(RNE,3/2,3/2,-2)=1/4`, whereas rounding the product first
  gives `2` and then adding `-2` gives `+0`. Check both signs,
  exact cancellation, `fma(RNE,2,2,-2)=2` despite overflowing
  the separately rounded product, and `fma(RNE,1/4,1/2,1/4)=1/2`
  even though the separately rounded product is zero. Check
  `sqrt(RNE,5/2)=3/2` since `5/2 < (13/8)^2`, exact squares,
  and values on both sides of a candidate midpoint. Check
  `rem(7/2,1)=-1/2` because the halfway quotient rounds to even
  integer 4, plus both quotient parity cases, exact multiples,
  and all sign combinations. For integral rounding include signed
  `1/2`, already integral inputs, and F(2,4) `3.5` and `-3.5`
  in all modes. Positive `3.5` gives `+inf, +inf, +inf, 3, 3`.
  Negative `-3.5` gives `-inf, -inf, -3, -inf, -3`.
  Include F(2,4) `1` to catch small-format integral rounding regressions.
  [Independent arithmetic and solver observations](../util/research.md#small-local-differential-check),
  [small-format integral rounding regression](https://repo.or.cz/bitwuzla.git/commit/ed2819a17af7fd2559770c58dce9f3f91e9d9dd6)
- **F-04 (special arithmetic and signs).** The tiny oracle covers every
  operation and legal mode with NaN, both infinities, both zeros,
  and finite inputs in all operand positions. Include `0/0`,
  `infinity/infinity`, `0*infinity`, opposite infinity addition,
  finite division by both zeros, signed zero/infinity products and
  quotients, fma with zero-times-infinity or opposing infinities,
  `sqrt(-0)=-0`, and `sqrt(-1)=NaN`. Verify remainder with
  infinite divisor, zero divisor, and infinite dividend. Verify
  sign of exact zero from cancellation, equal-sign zeros, signed
  zero product plus zero, remainder and integral rounding, and
  sign after underflow. Pass mismatched formats to each operation
  that requires a single format, even when special dispatch could
  otherwise return a value.
- **F-05 (conversions).** Reinterpret tiny bit patterns through both
  construction routes. Compare FP-to-FP widening and narrowing in
  all modes, both signed zeros/infinities, and NaN. Compare BV8
  `#xff` as signed `-1` and unsigned `255` in a format that can
  hold both exactly. Cover BV arguments across a limb boundary and
  convert in-range results back with the corresponding signedness.
  Compare exact dyadic-to-Rational conversion against the oracle
  for every finite tiny value and reject NaN or infinity with
  `kInvalidArgument`. For FP-to-BV test both destination ranges
  at endpoints and just beyond. In F(2,3), `-1/4` to unsigned BV2
  yields BV zero under RTZ and an unspecified result under RTN.
  NaN and both infinities yield the unspecified tag retaining
  checked width. Width zero gives `kInvalidArgument`. A width
  above `UINT32_MAX` gives `kSizeLimit`. A valid unspecified
  result at width `UINT32_MAX` needs no payload allocation.
- **F-06 (outcomes).** Assert exact tags and payloads. For min and
  max on both orders of opposite zeros, assert set equality with
  `{+0,-0}`. Identical zero signs give one defined result. Test both
  orders of one NaN, both NaNs, and ordered finite and infinite
  inputs. Check the defined and unspecified FP-to-BV tags across
  widths and modes. Exercise the outcome type's unspecified
  destination metadata for both width and format. Inject an outer
  allocation or size error on a path that requires storage and
  verify that it has no outcome tag. Under allocation failure
  injection, an opposite-zero choice and an unspecified BV still
  succeed without allocating.
  Verify the outcome owns its value after temporary operands die
  and has no implicit conversion to the value.
- **F-07 (wide representations and exponents).** In F(2,63), the
  positive finite encoding `2^63` has 64 effective bits and stays
  inline. The negative minimum subnormal encoding `2^64+1` takes
  two limbs and its absolute value trims to inline bits `1`.
  Cover cancellation and category checks at large declared width.
  In F(65,4) check stored bias `2^64-1` for `1`. Let
  `x` be that format's minimum positive subnormal. Then
  `1+x` rounds to `1` under RNE and `9/8` under RTP, while
  `1-x` rounds to `15/16` under RTN. Check wide-exponent
  overflow, underflow, and cancellation using exact power-of-two
  relationships. In F(65,4), `rem(2^(2^64-1),3)=-1` since the
  dividend is 2 modulo 3 and the nearest quotient rounds up.
  In F(65,66), convert `2^65+2^64+1` exactly from BV and Rational.
  Check the two-limb fraction `2^64+1`, the biased exponent
  `2^64+64`, packed bits, and exact rational round-trip.
  Check FP-to-BV range detection for enormous finite values
  without assembling a giant integer. Use finite-to-Rational
  conversion of a huge dyadic to exercise actual materialization
  limits separately from rounding paths that can avoid the shift.

For each of F(2,2), F(2,3), and F(2,4), independently enumerate all
16, 32, or 64 raw bit patterns and decode by the standard's field
equations. Use exact signed fractions with separate zero signs and
canonical NaN. Validate the reference tables against the fixed cases
above. For arithmetic use a separate special-case table and exact
fraction arithmetic. Independently enumerate adjacent target values
and select by exact distances or direction. Use the virtual next
binade endpoint for overflow and its integer counterpart on the
integral grid. For sqrt, compare the input with exact squares of
candidates and midpoints. For rem, compute nearest-even quotient
from the exact fraction using bounded integer reference arithmetic.
Represent allowed result sets explicitly. The oracle uses neither
the util rounder nor its backend helpers.
[Formal FP semantics](https://smt-lib.org/papers/BTRW15.pdf)

Use every encoding for field and identity tests. After canonicalizing
duplicate NaNs, exhaust all distinct semantic operands for unary
operations, every ordered pair for binary operations, and every
ordered triple for fma. Run all five modes wherever the operator
takes a mode. For each ordered pair of tiny formats, convert every
source value in every mode. For widths 1 through 6, convert every tiny
FP value to both integer BV interpretations, and convert every BV
value to every tiny FP format with both input interpretations under
all modes. For exact rational input, check zero, all finite grid
points, all adjacent midpoints and points on both sides, including
the signed underflow and overflow edges. Group bounded loops in
GoogleTest cases and report format, operands, mode, and operation
on failure. The exhaustive bounds apply to tests.

## Allocation, capacity, and integration

- **X-01 (layout, storage, and ownership).** On supported 64-bit
  compilers assert the 16-byte Integer, Rational, and BV and the
  24-byte FP layouts. Assert inline construction, copy,
  inspection, and the specified word paths allocate no heap.
  Include signed Integer minima, Rational denominator
  `UINT64_MAX`, BV64 high-half values, and small BV and FP
  payloads at maximum declared width. Use a test-only observer
  for storage transitions and allocation activity. Test each
  direction of promotion/demotion, large copy with distinct
  backing, checked copy failure where applicable, self-copy,
  self-move, move constructor/assignment resets, and `noexcept`
  destruction and move. Verify lvalue-only borrowed access and
  nonallocating component views, including wide Rational storage.
  Check that hash and equality remain stable after independent
  constructions, copies and successful moves.
- **X-02 (recoverable failures).** Inject failure at every reachable
  project-controlled allocation site for wide copies, assignments,
  parse/import, temporaries, wrapper or buffer takeover, conversions,
  and FP/BV operations as applicable. Sweep the failure index
  through the first successful call and check exact value there.
  Expect `kCppAllocation` from checked operations and
  `std::bad_alloc` from throwing copy/assignment conveniences.
  Use test-only reduced capacity stand-ins at the checked
  allocation/index boundary for GMP result limbs, mandatory
  temporary/spare limbs, limb counts, and byte counts. Cover
  exactly-at and one-past the actual or substituted bound with
  inputs that reach the responsible check. A word-only operation
  or a result that trims within a bound where its kernel fits
  still succeeds. Check size failures before out-of-range narrowing,
  the corresponding allocation, or publication. After each recoverable
  failure assert all inputs, old assignment destinations, and still-valid
  borrows unchanged. Require no leaked or doubly released storage
  and a successful retry after removing the injection. Exercise
  each cause of every error enumerator. Sweep F(65,66) addition of
  `2^65+2^64` and one, checking both inputs, their borrows and backing
  addresses, and the exact 66-bit significand on success.
- **X-03 (fatal GMP OOM).** An isolated uninstrumented subprocess
  uses the default GMP allocator under an address-space bound and
  requests an allocation below GMP's representational capacity
  but above that address-space bound. Assert termination using
  the existing fatal-OOM test method and distinguish harness
  setup errors. This covers the fatal path without exhausting the
  test runner's memory. A checked size error and an injected C++
  allocation failure are separate tests.
- **X-04 (caller contracts).** Exercise Integer -> BV -> Integer,
  BV fields/bits -> FP -> representative BV, and
  Rational -> FP -> checked BV through standalone and caller
  paths. Verify typed Node factories map numeric errors to the
  current diagnostics, and that frontend rational rounding
  retains exact fields and signed categories. Confirm sort/width
  checks, independently owned values, node payload views and
  interning, NaN normalization, signed-zero distinction, exact
  node indices even for equivalent rotations, operator shapes
  and multi-child folds or comparison chains. Use a borrow while
  its source is alive and destroy the source after copying to
  verify independence. Preserve existing node and frontend
  rollback, allocation, literal, and lifetime cases. In later
  solver/model tests exercise zero-divisor Int functions, both
  min/max choices, and conversion congruence for equal semantic
  tuples through UF, arrays, and DT. For example both
  `min(+0,-0) != +0` and `min(+0,-0) != -0` individually admit
  a model, while asserting two distinct conversion results for
  the same RM and semantic NaN is inconsistent.

## Cross-validation

Use independent exact-integer and fraction helpers for bounded and
seeded large cases, including negative arithmetic, promotion and
cancellation, rounding and conversion. Cross-check hand examples
against each oracle before it supplies expected tables. Retain exact
metamorphic tests such as Euclidean reconstruction, modular
conversion round-trips, and value-to-field round-trips. They
complement independent expected values.

Optional differential checks may use reference solvers, SoftFloat,
or MPFR for supported cases. Pin tool versions, actual backend,
formats, modes, seed, and flags. With MPFR account for its exponent
limits, target subnormals, nearest-away, and signed zeros.
Check defined results exactly. For underspecified operations verify
allowed-set membership, or use solver formulas that test both
permitted choices and same-argument congruence. Record unavailable
or unsupported cases, unknowns, disagreements, timeouts, and
crashes separately. Record shared library ancestry. The required
tiny oracles do not depend on these tools. [Numeric research](../util/research.md)

## Build and acceptance

Use GoogleTest with CTest discovery and a `util` label. Include a
consumer that imports only `dandelion.util.numeric` and links only
`dandelion_util`. Verify public use at all five partitions' entry
points through the primary module. Verify that a downstream
consumer cannot import the internal partition, reach GMP or node
contracts through the public module, or compile a production view
of test-only controls. Inspect target and compile/link commands to
verify sole direct GMP ownership and static link-only propagation.
Build without tests or optional MPFR. Use the private test-only
selector to exercise the direct GMP limb and portable `mpz_*` BV
paths on the supported 64-bit GMP ABI, with identical independent
expected results.

Run applicable numeric, node, and frontend suites with
`ctest --no-tests=error`. Verify GCC and Clang Debug, GCC ASan/UBSan,
and GCC Release and RelWithDebInfo on separate build trees. Verify
that an otherwise uninstrumented util consumer receives the required
sanitizer runtime link flags. Record actual tool and GMP versions,
test counts, and unavailable configurations. Run the fatal GMP
check in its uninstrumented setup. The bounded required suite uses
project-authored cases and needs no dataset.
[Build and runtime checks](../cpp-style.md#build-and-runtime-checks)
