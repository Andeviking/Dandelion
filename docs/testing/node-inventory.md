# Node feature inventory

The original acceptance run discovered 466 node tests. The test inventory covers
108 publishable Kinds, the invalid-kind guard,
eight sort categories, two sort-constructor categories and six declaration
categories. Final execution evidence is in [node-acceptance.md](node-acceptance.md).
The API/typing oracle is [operators.md](../node/operators.md); identity,
ownership and datatype oracles are [design.md](../node/design.md).
The \`Status\` columns below summarize the original acceptance configurations.
The signature table in the tests was authored before production code and is
independent of the implementation's rule dispatch.

Each builtin row is an actual named GoogleTest parameter, with these common
CTest identifiers (replace NAME with the row's suffix):

- `AllBuiltinKinds/OperatorTest.ExactStructureIdentityAndForcedCollision/NAME`
- `AllBuiltinKinds/OperatorTest.ArityEveryOperandAndManagerErrors/NAME`
- `AllBuiltinKinds/OperatorTest.PayloadSchemaRejectsMissingAndExtraFields/NAME`

The 48 applicable width, format, and changed-index instances use
\`ApplicableBuiltinKinds/WidthAndIndexTest.EqualWidthConstraintsAndChangedIndexKeys/NAME\`.
See the [rectification regression inventory](node.md#6-node-rectification-regressions).

They check the exact result sort, ordered children, indices, applicable
rational payload, absence of declaration/literal payload, all operand positions
for type/null/foreign errors, structural hits, replacement of each child,
permitted same-sort operand permutations, and forced hash collisions. CheckBuiltin
must create no node or consume NodeIds. Missing/excess arity and payload are
checked independently. N-ary rows also verify that additional children are
retained. Relational width and format failures have explicit additional cases.
For a unary operator or positions with necessarily different sorts, a valid
operand permutation is N/A. Extra index/qualifier/rational rejection is tested
for every row even when that payload category itself is N/A.

Abbreviations: B=Bool, Z=Int, R=RoundingMode, V=BV8, Vn=BVn,
F=FP(8,24), Fs=FP(2,3), A=Array(Int,BV8). Every case uses distinct fresh
children where sorting could otherwise conceal operand reordering.

| Kind / test suffix | Valid signature and payload | Arity | Sort/manager failures | Bounds; inapplicable dimensions | Status |
| --- | --- | --- | --- | --- | --- |
| kBoolValue | Bool literal → B | Dedicated factory (public arity input N/A) | Native bool input; sort/index/arity N/A | Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue; false/true differ; literal/declaration keys under collisions | passed in all four configurations |
| kIntValue | Exact Integer → Z | Dedicated factory (public arity input N/A) | Text grammar, signed limits and GMP; sort/index/arity N/A | Numeric.*; Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue; literal/declaration keys under collisions | passed in all four configurations |
| kBvValue | (V(w), v), 0≤v<2^w | Dedicated factory (public arity input N/A) | Wrong sort/null/foreign; negative, overflow, widths 1/2/8/32/64/65; index/arity N/A | Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue; Boundary.DivisionPowerAndUnderspecifiedOperationsAreRetained; literal/declaration keys under collisions | passed in all four configurations |
| kFpValue | FP sort, canonical category or finite bits | Dedicated factory (public arity input N/A) | Wrong sort/null/foreign schema; negative/oversized bits, all categories, GMP copy, tiny/huge formats; index/arity N/A | Value.EveryTinyFpPatternHasTheHandDerivedCategory; Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue; literal/declaration keys under collisions | passed in all four configurations |
| kRoundingModeValue | Five distinct RM literals → R | Dedicated factory (public arity input N/A) | Invalid enum rejected; native sort/index/arity N/A | Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue; literal/declaration keys under collisions | passed in all four configurations |
| kApply | Resolved declaration instance and ordered domain arguments | Dedicated factory (public arity input N/A) | Each of six declaration categories: extra/missing args, every domain position, null/foreign/type, changed child/decl/type args; numeric index/qualifier/rational N/A | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; Identity.*; Datatype.*; literal/declaration keys under collisions | passed in all four configurations |
| kNot / Not | B → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kAnd / And | B,B → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kOr / Or | B,B → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kXor / Xor | B,B → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kImplies / Implies | B,B → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kEqual / Equal | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kDistinct / Distinct | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIte / Ite | B,Z,Z → Z | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntNeg / IntNeg | Z → Z | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntAdd / IntAdd | Z,Z → Z | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntSub / IntSub | Z,Z → Z | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntMul / IntMul | Z,Z → Z | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntDiv / IntDiv | Z,Z → Z | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntMod / IntMod | Z,Z → Z | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntAbs / IntAbs | Z → Z | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntPow / IntPow | Z,Z → Z | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntLt / IntLt | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntLe / IntLe | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntGt / IntGt | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntGe / IntGe | Z,Z → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntDivisible / IntDivisible | Z → B; indices 3 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n>0; −1/0/1/2^64+1 exact; qualifier N/A; rational N/A | passed in all four configurations |
| kBvNot / BvNot | V → V | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvNeg / BvNeg | V → V | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvAnd / BvAnd | V,V → V | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvOr / BvOr | V,V → V | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvXor / BvXor | V,V → V | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvAdd / BvAdd | V,V → V | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvMul / BvMul | V,V → V | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvNand / BvNand | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvNor / BvNor | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvXnor / BvXnor | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSub / BvSub | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUdiv / BvUdiv | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUrem / BvUrem | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSdiv / BvSdiv | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSrem / BvSrem | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSmod / BvSmod | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvShl / BvShl | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvLshr / BvLshr | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvAshr / BvAshr | V,V → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvComp / BvComp | V,V → V1 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUlt / BvUlt | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUle / BvUle | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUgt / BvUgt | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUge / BvUge | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSlt / BvSlt | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSle / BvSle | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSgt / BvSgt | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSge / BvSge | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvNego / BvNego | V → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUaddo / BvUaddo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSaddo / BvSaddo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUmulo / BvUmulo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSmulo / BvSmulo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvUsubo / BvUsubo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSsubo / BvSsubo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvSdivo / BvSdivo | V,V → B | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kBvConcat / BvConcat | V,V → V16 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | No payload; heterogeneous widths, checked a+b and overflow; indices/qualifier/rational N/A | passed in all four configurations |
| kBvExtract / BvExtract | V → V4; indices 3,0 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | 0≤l≤h<w; endpoints, reversed/out-of-range/negative/huge; equal result widths with distinct h/l; qualifier N/A; rational N/A | passed in all four configurations |
| kBvRepeat / BvRepeat | V → V16; indices 2 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n>0; −1/0/1, product overflow and huge input; qualifier N/A; rational N/A | passed in all four configurations |
| kBvZeroExtend / BvZeroExtend | V → V16; indices 8 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n≥0; −1/0, addition overflow and huge input; qualifier N/A; rational N/A | passed in all four configurations |
| kBvSignExtend / BvSignExtend | V → V16; indices 8 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n≥0; −1/0, addition overflow and huge input; qualifier N/A; rational N/A | passed in all four configurations |
| kBvRotateLeft / BvRotateLeft | V → V; indices 1 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n≥0; −1/0/1/2^64+1; preserve exact unreduced index; qualifier N/A; rational N/A | passed in all four configurations |
| kBvRotateRight / BvRotateRight | V → V; indices 1 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | n≥0; −1/0/1/2^64+1; preserve exact unreduced index; qualifier N/A; rational N/A | passed in all four configurations |
| kUbvToInt / UbvToInt | V → Z | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kSbvToInt / SbvToInt | V → Z | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kIntToBv / IntToBv | Z → V; indices 8 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | w>0; −1/0/1/UINT32_MAX, >uint32_t resource error; qualifier N/A; rational N/A | passed in all four configurations |
| kFpConstruct / FpConstruct | V1,V,V23 → F | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | No payload; V1 sign, e>1, fraction≥1, checked s and e+s; indices/qualifier/rational N/A | passed in all four configurations |
| kFpAbs / FpAbs | F → F | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpNeg / FpNeg | F → F | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpAdd / FpAdd | R,F,F → F | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpSub / FpSub | R,F,F → F | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpMul / FpMul | R,F,F → F | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpDiv / FpDiv | R,F,F → F | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpFma / FpFma | R,F,F,F → F | exactly 4; missing/excess rejected | Every slot 0..3; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpSqrt / FpSqrt | R,F → F | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpRoundToIntegral / FpRoundToIntegral | R,F → F | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpRem / FpRem | F,F → F | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpMin / FpMin | F,F → F | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpMax / FpMax | F,F → F | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpEq / FpEq | F,F → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpLt / FpLt | F,F → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpLeq / FpLeq | F,F → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpGt / FpGt | F,F → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpGeq / FpGeq | F,F → B | ≥2; below min rejected, extra accepted | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsNormal / FpIsNormal | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsSubnormal / FpIsSubnormal | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsZero / FpIsZero | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsInfinite / FpIsInfinite | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsNaN / FpIsNaN | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsNegative / FpIsNegative | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpIsPositive / FpIsPositive | F → B | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kFpFromBits / FpFromBits | V32 → F; indices 8,24 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | e,s≥2 and e+s≤UINT32_MAX; F(2,2), −1/1 and oversized parameters; changed e/s keys; packed width e+s; either real error allowed when oversized format also mismatches source width; qualifier N/A; rational N/A | passed in all four configurations |
| kFpConvert / FpConvert | R,Fs → F; indices 8,24 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | e,s≥2 and e+s≤UINT32_MAX; F(2,2), −1/1 and oversized parameters; changed e/s keys; qualifier N/A; rational N/A | passed in all four configurations |
| kFpFromSbv / FpFromSbv | R,V → F; indices 8,24 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | e,s≥2 and e+s≤UINT32_MAX; F(2,2), −1/1 and oversized parameters; changed e/s keys; qualifier N/A; rational N/A | passed in all four configurations |
| kFpFromUbv / FpFromUbv | R,V → F; indices 8,24 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | e,s≥2 and e+s≤UINT32_MAX; F(2,2), −1/1 and oversized parameters; changed e/s keys; qualifier N/A; rational N/A | passed in all four configurations |
| kFpFromRational / FpFromRational | R → F; indices 8,24; normalized rational 2/3 | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | e,s≥2 and e+s≤UINT32_MAX; F(2,2), −1/1 and oversized parameters; changed e/s keys; normalized/changed rational and input independence, zero denominator rejected at numeric API; qualifier N/A | passed in all four configurations |
| kFpToSbv / FpToSbv | R,F → V; indices 8 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | w>0; −1/0/1/UINT32_MAX, >uint32_t resource error; qualifier N/A; rational N/A | passed in all four configurations |
| kFpToUbv / FpToUbv | R,F → V; indices 8 | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | w>0; −1/0/1/UINT32_MAX, >uint32_t resource error; qualifier N/A; rational N/A | passed in all four configurations |
| kArraySelect / ArraySelect | A,Z → V | exactly 2; missing/excess rejected | Every slot 0..1; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kArrayStore / ArrayStore | A,Z,V → A | exactly 3; missing/excess rejected | Every slot 0..2; null, foreign, wrong sort | Indices, qualifier and rational N/A | passed in all four configurations |
| kArrayConst / ArrayConst | V → A; qualifier A | exactly 1; missing/excess rejected | Every slot 0..0; null, foreign, wrong sort | Full array qualifier required; wrong category/element/foreign qualifier; different index qualifiers differ; indices/rational N/A | passed in all four configurations |

`kInvalid` and an out-of-enum value are rejected by
`NodeTest.InvalidAndDedicatedKindsAreNotBuiltins`; dedicated literal and Apply
kinds are rejected by MakeBuiltin. They cannot publish a node.

| Sort | Valid key, boundaries and errors | Actual cases | Status |
| --- | --- | --- | --- |
| kBool | Kind only; distinct from BV1; parameters/arity N/A. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kInt | Kind only; parameters/arity N/A. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kRoundingMode | Kind only; parameters/arity N/A. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kBitVector | Positive exact width; zero/negative rejected; UINT32_MAX accepted; larger width is resource limit. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kFloatingPoint | e,s>1 and checked sum; F(2,2), F(2,3), invalid 1, large positive resource limits. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kArray | Ordered concrete index and element; nested combinations; reversed key; null/foreign inputs. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kUninterpreted | Fresh constructor + ordered type args; zero/one/two arities; bad arity/foreign constructor and args. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |
| kDatatype | Committed constructor + ordered concrete type args; nil instance specificity; multiple parameter order and all field categories. All categories have concrete query applicability, identity, foreign retain, Core equality/distinct/ite checks. | NodeTest.EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership; NodeTest.SortIdentityParametersAndNestedCombinations; NodeTest.ExactSortBoundariesAndForeignInputs; Datatype.* | passed in all four configurations |

| Declaration category | Actual behavior / N/A | Actual cases | Status |
| --- | --- | --- | --- |
| kFunction | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Numeric indices/qualifiers/rational payload N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |
| kInternalFunction | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Numeric indices/qualifiers/rational payload N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |
| kFormal | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Formal has zero domain; per-operand errors N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |
| kDatatypeConstructor | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Numeric indices/qualifiers/rational payload N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |
| kDatatypeSelector | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Numeric indices/qualifiers/rational payload N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |
| kDatatypeTester | Exact category, signature, same-instance hit, applicable changed children, extra/missing arity, each domain position wrong/null/foreign, foreign declaration/instance and type-argument arity. Numeric indices/qualifiers/rational payload N/A. | NodeTest.EveryDeclarationCategoryChecksItsConcreteSignature; NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Datatype.* | passed in all four configurations |

Sort-constructor declarations are separately covered: fresh uninterpreted
constructors (arity and ordered arguments) in `SortIdentity*`; datatype
constructors (group ownership, arity and links) in `Datatype.*`. They are
identities, not first-class function sorts. Names and source namespace
conflicts are distinct: the API copies hints, and the future frontend resolves
source declarations.

| Required design ID | Concrete evidence in GoogleTest | Status |
| --- | --- | --- |
| N-SORT-01 | NodeTest.SortIdentityParametersAndNestedCombinations | passed in all four configurations |
| N-SORT-02 | NodeTest.ExactSortBoundariesAndForeignInputs | passed in all four configurations |
| N-DECL-01 | NodeTest.EveryOrdinaryDeclarationCategoryHasFreshIdentity; Identity.AllApplicableKeyFieldsSurviveForcedCollision | passed in all four configurations |
| N-BV-01 | Boundary.ExtractEndpointsAndExactLargeUnboundedIndices | passed in all four configurations |
| N-BV-02 | Value.EveryLiteralKindAndGmpPayloadHasExactImmutableValue (API); input spellings deferred | passed in all four configurations |
| N-IDX-01 | Boundary.ExtractEndpointsAndExactLargeUnboundedIndices | passed in all four configurations |
| N-FP-01 | Value.EveryTinyFpPatternHasTheHandDerivedCategory | passed in all four configurations |
| N-FP-02 | FpFromSbv/FpFromUbv Kind cases; Boundary.FpFormatsConstructionConversionAndArrayQualifiers | passed in all four configurations |
| N-VALUE-01 | NodeTest.MovedFromRationalsStayCanonicalThroughMovesAndNodePublication; Numeric.*; Value.*; NodeTest.IndexedRationalAndNamePayloadsOwnIndependentInputCopies | passed in all four configurations |
| N-COLLIDE-01 | Identity.*; every builtin ExactStructureIdentityAndForcedCollision case | passed in all four configurations |
| N-DT-01 | Datatype.ParameterizedListSignaturesAndWrongConstructorSelector | passed in all four configurations |
| N-DT-02 / N-DT-03 | Datatype.EnumerationMutualProductivityAndRepairAfterFailure | passed in all four configurations |
| N-DT-04 | Datatype.RejectRecursiveContainersAcceptChangingParametersAndArrayFields | passed in all four configurations |
| N-DT-05 | Datatype.ValidateRecipeOwnershipParameterScopeArityAndBuilderMoves; ParameterizedListSignatures*; ExistingSortConstructorConcreteAndArrayRecipesAreSubstituted | passed in all four configurations |
| N-RC-01 / N-RC-04 | Lifetime.RepeatedEdgesCopyMovesAssignmentAndOwningChild | passed in all four configurations |
| N-RC-02 | Lifetime.DeepTwentyThousandChainWalkAndReleaseAreIterative | passed in all four configurations |
| N-RC-03 | Lifetime.FacadeBeforeAllHandleCategoriesAndFinalStateDestruction; LastNodeOwnsState*; BuilderOutlivesFacade*; simulated external owner, model evaluation N/A | passed in all four configurations |
| N-RC-05 | Lifetime.LastNodeOwnsStateAndBorrowedChildCanBeRetained; NodeTest.AllRetainViewCategoriesRejectForeignAndNullOwners | passed in all four configurations |
| N-WALK-01 | Traversal.EmptyRootsAndExactDiamondRepeatedEdgesAndRoots | passed in all four configurations |
| N-WALK-02 | Traversal.SkipOnlyImmediatelyAfterEnterAndNeverExpandSkippedSharedNode | passed in all four configurations |
| N-WALK-03 | Traversal.SharedChildExitsBeforeParentAndViewsSurviveGrowth | passed in all four configurations |
| N-TABLE-01 | Table.ThresholdGrowthAndWrapAroundBackwardShiftAgainstReference; FailureDuringGrowthAndAfterGrowthPreservesLiveNodes | passed in all four configurations |
| N-ID-01 | Failure.NodeIdExhaustionKeepsHitsUsable; EveryStoreIdentitySpaceChecksItsOwnBoundaryAndKeepsHits | passed in all four configurations |
| N-FAIL-01 | Failure.EveryRetainBoundaryRollsBackRepeatedChildPrefix | passed in all four configurations |
| N-FAIL-02 | Failure.ManagerCreationFailuresPublishNoStateOrFacade; Failure.EveryFactoryCheckpointFromAnIdenticalFreshState (25 operations); StoreAndDatatypeAllocationCheckpoints*; DatatypeCommitStagesItsImplicitBoolSortWithoutPartialIds; TraversalAllocationFailure*; NumericFailure.*; GmpFailure.* | passed in all four configurations |
| N-FAIL-03 | Failure.EveryFreshConstantAllocationAndRetainIsAtomic; fresh-state checkpoint sweep | passed in all four configurations |
| Finite recipes / parameter order | NodeTest.DeepFiniteDatatypeRecipeInstantiationIsIterative (5000); Datatype.OrderedMultipleParametersAndEverySupportedFieldCategory | passed in all four configurations |
| No allocation during reclaim | Lifetime.WalkerAndFacadeMovesPreserveOwnershipAndReclamationDoesNotAllocate | passed in all four configurations |
| Walker copied-root rollback | Failure.WalkerRootRetainFailuresRollBackEveryCopiedPrefix | passed in all four configurations |

| Frontend boundary | Node API evidence | Parser status |
| --- | --- | --- |
| Formal identities and substitution foundation | NodeTest.FormalIdentityAndSubstitutionFoundationWithoutAParser | define-fun substitution/capture/shadowing integration deferred |
| Testers/selectors/ite and first-match structure | NodeTest.LoweredDatatypeTestersAndIteKeepPatternOrderAndBranchTyping; Datatype.* | match parsing, exhaustiveness, all-branch typing and duplicate-pattern ordering deferred |
| Indexed forms / source literal spelling / qualified sorts | All operator, value and sort cases above | source grammar, aliases and overload/qualifier resolution deferred |
| Simultaneous let and :named | Owning handles can share source-independent nodes | source scope, closedness, label visibility/rollback deferred |
| Logic / profile restrictions | Internal well-sorted nodes represent the full selected inventory | set-logic registration, active-signature rules and unsupported Real/String/source forms deferred |
| Occurrences / roundtrip | Copying a Node shares identity while external owners can store metadata | source ranges, annotations and parse/print/parse deferred |

No parser case is skipped and counted as a pass: these parser tests do not
exist yet. The suite evaluates representation, typing and lifetime, not
arithmetic semantics, model evaluation, SAT or UNSAT. GMP limb OOM uses a
separate bounded fatal subprocess, while project C++ allocation/retain hooks
exercise recoverable paths. Capacity/ID/reference boundaries use test-only
lower limits or the last NodeId, without allocating infeasible arrays or
forging invalid spans. The normal suite needs no corpus, network or reference
solver, and does not access the dataset directory.
