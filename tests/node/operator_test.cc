#include "test_support.h"

namespace dandelion::testing {
namespace {

// This test signature table is derived from operators.md, not production rules.
struct OperatorCase {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  const char* name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Kind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::string> arguments;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string result;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool nary;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::string> indices;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool qualifier;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool rational;
};

const std::vector<OperatorCase> kOperatorCases = {
    {"Not", Kind::kNot, {"B"}, "B", false, {}, false, false},
    {"And", Kind::kAnd, {"B", "B"}, "B", true, {}, false, false},
    {"Or", Kind::kOr, {"B", "B"}, "B", true, {}, false, false},
    {"Xor", Kind::kXor, {"B", "B"}, "B", true, {}, false, false},
    {"Implies", Kind::kImplies, {"B", "B"}, "B", true, {}, false, false},
    {"Equal", Kind::kEqual, {"Z", "Z"}, "B", true, {}, false, false},
    {"Distinct", Kind::kDistinct, {"Z", "Z"}, "B", true, {}, false, false},
    {"Ite", Kind::kIte, {"B", "Z", "Z"}, "Z", false, {}, false, false},
    {"IntNeg", Kind::kIntNeg, {"Z"}, "Z", false, {}, false, false},
    {"IntAdd", Kind::kIntAdd, {"Z", "Z"}, "Z", true, {}, false, false},
    {"IntSub", Kind::kIntSub, {"Z", "Z"}, "Z", true, {}, false, false},
    {"IntMul", Kind::kIntMul, {"Z", "Z"}, "Z", true, {}, false, false},
    {"IntDiv", Kind::kIntDiv, {"Z", "Z"}, "Z", true, {}, false, false},
    {"IntMod", Kind::kIntMod, {"Z", "Z"}, "Z", false, {}, false, false},
    {"IntAbs", Kind::kIntAbs, {"Z"}, "Z", false, {}, false, false},
    {"IntPow", Kind::kIntPow, {"Z", "Z"}, "Z", false, {}, false, false},
    {"IntLt", Kind::kIntLt, {"Z", "Z"}, "B", true, {}, false, false},
    {"IntLe", Kind::kIntLe, {"Z", "Z"}, "B", true, {}, false, false},
    {"IntGt", Kind::kIntGt, {"Z", "Z"}, "B", true, {}, false, false},
    {"IntGe", Kind::kIntGe, {"Z", "Z"}, "B", true, {}, false, false},
    {"IntDivisible",
     Kind::kIntDivisible,
     {"Z"},
     "B",
     false,
     {"3"},
     false,
     false},
    {"BvNot", Kind::kBvNot, {"V"}, "V", false, {}, false, false},
    {"BvNeg", Kind::kBvNeg, {"V"}, "V", false, {}, false, false},
    {"BvAnd", Kind::kBvAnd, {"V", "V"}, "V", true, {}, false, false},
    {"BvOr", Kind::kBvOr, {"V", "V"}, "V", true, {}, false, false},
    {"BvXor", Kind::kBvXor, {"V", "V"}, "V", true, {}, false, false},
    {"BvAdd", Kind::kBvAdd, {"V", "V"}, "V", true, {}, false, false},
    {"BvMul", Kind::kBvMul, {"V", "V"}, "V", true, {}, false, false},
    {"BvNand", Kind::kBvNand, {"V", "V"}, "V", false, {}, false, false},
    {"BvNor", Kind::kBvNor, {"V", "V"}, "V", false, {}, false, false},
    {"BvXnor", Kind::kBvXnor, {"V", "V"}, "V", false, {}, false, false},
    {"BvSub", Kind::kBvSub, {"V", "V"}, "V", false, {}, false, false},
    {"BvUdiv", Kind::kBvUdiv, {"V", "V"}, "V", false, {}, false, false},
    {"BvUrem", Kind::kBvUrem, {"V", "V"}, "V", false, {}, false, false},
    {"BvSdiv", Kind::kBvSdiv, {"V", "V"}, "V", false, {}, false, false},
    {"BvSrem", Kind::kBvSrem, {"V", "V"}, "V", false, {}, false, false},
    {"BvSmod", Kind::kBvSmod, {"V", "V"}, "V", false, {}, false, false},
    {"BvShl", Kind::kBvShl, {"V", "V"}, "V", false, {}, false, false},
    {"BvLshr", Kind::kBvLshr, {"V", "V"}, "V", false, {}, false, false},
    {"BvAshr", Kind::kBvAshr, {"V", "V"}, "V", false, {}, false, false},
    {"BvComp", Kind::kBvComp, {"V", "V"}, "V1", false, {}, false, false},
    {"BvUlt", Kind::kBvUlt, {"V", "V"}, "B", false, {}, false, false},
    {"BvUle", Kind::kBvUle, {"V", "V"}, "B", false, {}, false, false},
    {"BvUgt", Kind::kBvUgt, {"V", "V"}, "B", false, {}, false, false},
    {"BvUge", Kind::kBvUge, {"V", "V"}, "B", false, {}, false, false},
    {"BvSlt", Kind::kBvSlt, {"V", "V"}, "B", false, {}, false, false},
    {"BvSle", Kind::kBvSle, {"V", "V"}, "B", false, {}, false, false},
    {"BvSgt", Kind::kBvSgt, {"V", "V"}, "B", false, {}, false, false},
    {"BvSge", Kind::kBvSge, {"V", "V"}, "B", false, {}, false, false},
    {"BvNego", Kind::kBvNego, {"V"}, "B", false, {}, false, false},
    {"BvUaddo", Kind::kBvUaddo, {"V", "V"}, "B", false, {}, false, false},
    {"BvSaddo", Kind::kBvSaddo, {"V", "V"}, "B", false, {}, false, false},
    {"BvUmulo", Kind::kBvUmulo, {"V", "V"}, "B", false, {}, false, false},
    {"BvSmulo", Kind::kBvSmulo, {"V", "V"}, "B", false, {}, false, false},
    {"BvUsubo", Kind::kBvUsubo, {"V", "V"}, "B", false, {}, false, false},
    {"BvSsubo", Kind::kBvSsubo, {"V", "V"}, "B", false, {}, false, false},
    {"BvSdivo", Kind::kBvSdivo, {"V", "V"}, "B", false, {}, false, false},
    {"BvConcat", Kind::kBvConcat, {"V", "V"}, "V16", false, {}, false, false},
    {"BvExtract",
     Kind::kBvExtract,
     {"V"},
     "V4",
     false,
     {"3", "0"},
     false,
     false},
    {"BvRepeat", Kind::kBvRepeat, {"V"}, "V16", false, {"2"}, false, false},
    {"BvZeroExtend",
     Kind::kBvZeroExtend,
     {"V"},
     "V16",
     false,
     {"8"},
     false,
     false},
    {"BvSignExtend",
     Kind::kBvSignExtend,
     {"V"},
     "V16",
     false,
     {"8"},
     false,
     false},
    {"BvRotateLeft",
     Kind::kBvRotateLeft,
     {"V"},
     "V",
     false,
     {"1"},
     false,
     false},
    {"BvRotateRight",
     Kind::kBvRotateRight,
     {"V"},
     "V",
     false,
     {"1"},
     false,
     false},
    {"UbvToInt", Kind::kUbvToInt, {"V"}, "Z", false, {}, false, false},
    {"SbvToInt", Kind::kSbvToInt, {"V"}, "Z", false, {}, false, false},
    {"IntToBv", Kind::kIntToBv, {"Z"}, "V", false, {"8"}, false, false},
    {"FpConstruct",
     Kind::kFpConstruct,
     {"V1", "V", "V23"},
     "F",
     false,
     {},
     false,
     false},
    {"FpAbs", Kind::kFpAbs, {"F"}, "F", false, {}, false, false},
    {"FpNeg", Kind::kFpNeg, {"F"}, "F", false, {}, false, false},
    {"FpAdd", Kind::kFpAdd, {"R", "F", "F"}, "F", false, {}, false, false},
    {"FpSub", Kind::kFpSub, {"R", "F", "F"}, "F", false, {}, false, false},
    {"FpMul", Kind::kFpMul, {"R", "F", "F"}, "F", false, {}, false, false},
    {"FpDiv", Kind::kFpDiv, {"R", "F", "F"}, "F", false, {}, false, false},
    {"FpFma", Kind::kFpFma, {"R", "F", "F", "F"}, "F", false, {}, false, false},
    {"FpSqrt", Kind::kFpSqrt, {"R", "F"}, "F", false, {}, false, false},
    {"FpRoundToIntegral",
     Kind::kFpRoundToIntegral,
     {"R", "F"},
     "F",
     false,
     {},
     false,
     false},
    {"FpRem", Kind::kFpRem, {"F", "F"}, "F", false, {}, false, false},
    {"FpMin", Kind::kFpMin, {"F", "F"}, "F", false, {}, false, false},
    {"FpMax", Kind::kFpMax, {"F", "F"}, "F", false, {}, false, false},
    {"FpEq", Kind::kFpEq, {"F", "F"}, "B", true, {}, false, false},
    {"FpLt", Kind::kFpLt, {"F", "F"}, "B", true, {}, false, false},
    {"FpLeq", Kind::kFpLeq, {"F", "F"}, "B", true, {}, false, false},
    {"FpGt", Kind::kFpGt, {"F", "F"}, "B", true, {}, false, false},
    {"FpGeq", Kind::kFpGeq, {"F", "F"}, "B", true, {}, false, false},
    {"FpIsNormal", Kind::kFpIsNormal, {"F"}, "B", false, {}, false, false},
    {"FpIsSubnormal",
     Kind::kFpIsSubnormal,
     {"F"},
     "B",
     false,
     {},
     false,
     false},
    {"FpIsZero", Kind::kFpIsZero, {"F"}, "B", false, {}, false, false},
    {"FpIsInfinite", Kind::kFpIsInfinite, {"F"}, "B", false, {}, false, false},
    {"FpIsNaN", Kind::kFpIsNaN, {"F"}, "B", false, {}, false, false},
    {"FpIsNegative", Kind::kFpIsNegative, {"F"}, "B", false, {}, false, false},
    {"FpIsPositive", Kind::kFpIsPositive, {"F"}, "B", false, {}, false, false},
    {"FpFromBits",
     Kind::kFpFromBits,
     {"V32"},
     "F",
     false,
     {"8", "24"},
     false,
     false},
    {"FpConvert",
     Kind::kFpConvert,
     {"R", "Fs"},
     "F",
     false,
     {"8", "24"},
     false,
     false},
    {"FpFromSbv",
     Kind::kFpFromSbv,
     {"R", "V"},
     "F",
     false,
     {"8", "24"},
     false,
     false},
    {"FpFromUbv",
     Kind::kFpFromUbv,
     {"R", "V"},
     "F",
     false,
     {"8", "24"},
     false,
     false},
    {"FpFromRational",
     Kind::kFpFromRational,
     {"R"},
     "F",
     false,
     {"8", "24"},
     false,
     true},
    {"FpToSbv", Kind::kFpToSbv, {"R", "F"}, "V", false, {"8"}, false, false},
    {"FpToUbv", Kind::kFpToUbv, {"R", "F"}, "V", false, {"8"}, false, false},
    {"ArraySelect",
     Kind::kArraySelect,
     {"A", "Z"},
     "V",
     false,
     {},
     false,
     false},
    {"ArrayStore",
     Kind::kArrayStore,
     {"A", "Z", "V"},
     "A",
     false,
     {},
     false,
     false},
    {"ArrayConst", Kind::kArrayConst, {"V"}, "A", false, {}, true, false},
};

class OperatorTest : public NodeTest,
                     public ::testing::WithParamInterface<OperatorCase> {
 protected:
  std::map<std::string, Sort> sorts_;
  std::vector<Node> owners_;
  std::vector<NodeView> children_;
  std::vector<Integer> indices_;
  std::optional<Rational> rational_;

  void Prepare(bool collide = false) {
    if (collide) NodeTestPeer::ForceCollisions(manager_, 15);
    sorts_.emplace("B", Take(manager_.BoolSort()));
    sorts_.emplace("Z", Take(manager_.IntSort()));
    sorts_.emplace("R", Take(manager_.RoundingModeSort()));
    for (const auto width : {1, 4, 8, 16, 23, 32}) {
      sorts_.emplace(width == 8 ? "V" : "V" + std::to_string(width), Bv(width));
    }
    sorts_.emplace("F", Fp(8, 24));
    sorts_.emplace("Fs", Fp(2, 3));
    sorts_.emplace("A", Take(manager_.MakeArraySort(View(sorts_.at("Z")),
                                                    View(sorts_.at("V")))));
    for (const auto& arg : GetParam().arguments)
      owners_.push_back(Const(View(sorts_.at(arg))));
    for (const auto& node : owners_) children_.push_back(node.Borrow());
    for (const auto& index : GetParam().indices)
      indices_.push_back(Number(index));
    const Integer two(2), three(3);
    rational_.emplace(Take(Rational::Make(two.Borrow(), three.Borrow())));
  }
  BuiltinOpView Operator() {
    return {GetParam().kind, indices_,
            GetParam().qualifier ? std::optional(View(sorts_.at("A")))
                                 : std::nullopt,
            GetParam().rational ? std::optional(rational_->Borrow())
                                : std::nullopt};
  }
};

TEST_F(NodeTest, ConstantFpConstructionAndConversionsRemainOperations) {
  const auto bv1 = Bv(1);
  const auto bv8 = Bv(8);
  const auto bv23 = Bv(23);
  const auto bv32 = Bv(32);
  const auto fp = Fp(8, 24);
  const auto sign = Take(manager_.MakeBvWordValue(bv1.Borrow(), 0));
  const auto exp = Take(manager_.MakeBvWordValue(bv8.Borrow(), 127));
  const auto frac = Take(manager_.MakeBvWordValue(bv23.Borrow(), 0));
  const auto packed = Take(manager_.MakeBvWordValue(bv32.Borrow(), 0x3f800000));
  const auto one = FpBits(Format(8, 24), IntegerView(0x3f800000));
  const auto literal = Take(manager_.MakeFpValue(fp.Borrow(), one.Borrow()));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const auto rational = Take(Rational::Make(IntegerView(1), IntegerView(1)));
  const std::array format{Integer(8), Integer(24)};
  const auto construct =
      Op(Kind::kFpConstruct, {sign.Borrow(), exp.Borrow(), frac.Borrow()});
  const auto from_bits = Op(Kind::kFpFromBits, {packed.Borrow()}, format);
  const auto from_rational =
      Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, rational.Borrow());
  for (const auto* node : {&construct, &from_bits, &from_rational}) {
    EXPECT_EQ(node->GetSort(), fp);
    EXPECT_NE(*node, literal);
    EXPECT_FALSE(node->Borrow().GetValue());
    EXPECT_EQ(node->Borrow().GetRationalParameter().has_value(),
              node == &from_rational);
  }
}

TEST_P(OperatorTest, ExactStructureIdentityAndForcedCollision) {
  Prepare(true);
  const auto op = Operator();
  const auto before_check = NodeTestPeer::GetStats(manager_);
  const auto checked = Take(manager_.CheckBuiltin(op, children_));
  const auto after_check = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after_check.node_allocations, before_check.node_allocations);
  EXPECT_EQ(after_check.next_node_id, before_check.next_node_id);
  EXPECT_EQ(after_check.live_nodes, before_check.live_nodes);
  const auto result = Take(manager_.MakeBuiltin(op, children_));
  EXPECT_EQ(checked, sorts_.at(GetParam().result));
  EXPECT_EQ(result.GetSort(), checked);
  EXPECT_EQ(result.Borrow().GetKind(), GetParam().kind);
  EXPECT_EQ(result.Borrow().GetChildren().size(), children_.size());
  EXPECT_FALSE(result.Borrow().GetValue());
  EXPECT_FALSE(result.Borrow().GetDeclaration());
  EXPECT_EQ(result.Borrow().GetRationalParameter().has_value(),
            GetParam().rational);
  for (std::size_t i = 0; i < children_.size(); ++i) {
    EXPECT_EQ(result.Borrow().GetChildren()[i].GetId(), children_[i].GetId());
  }
  ASSERT_EQ(result.Borrow().GetIndices().size(), indices_.size());
  for (std::size_t i = 0; i < indices_.size(); ++i) {
    EXPECT_EQ(
        result.Borrow().GetIndices()[i].Borrow().Compare(indices_[i].Borrow()),
        0);
  }
  const auto before = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(result, Take(manager_.MakeBuiltin(op, children_)));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).node_allocations,
            before.node_allocations);
  for (std::size_t i = 0; i < children_.size(); ++i) {
    const auto replacement = Const(owners_[i].Borrow().GetSort());
    auto changed = children_;
    changed[i] = replacement.Borrow();
    EXPECT_NE(result, Take(manager_.MakeBuiltin(op, changed)));
  }
  if (children_.size() >= 2 &&
      GetParam().arguments[0] == GetParam().arguments[1]) {
    auto reverse = children_;
    std::swap(reverse[0], reverse[1]);
    EXPECT_NE(result, Take(manager_.MakeBuiltin(op, reverse)));
  }
  for (std::size_t left = 1; left < children_.size(); ++left) {
    for (std::size_t right = left + 1; right < children_.size(); ++right) {
      if (GetParam().arguments[left] == GetParam().arguments[right]) {
        auto swapped = children_;
        std::swap(swapped[left], swapped[right]);
        EXPECT_NE(result, Take(manager_.MakeBuiltin(op, swapped)));
      }
    }
  }
}

TEST_P(OperatorTest, ArityEveryOperandAndManagerErrors) {
  Prepare();
  const auto op = Operator();
  auto fewer = children_;
  fewer.pop_back();
  Error(manager_.MakeBuiltin(op, fewer), ErrorCode::kInvalidArity);
  Error(manager_.CheckBuiltin(op, fewer), ErrorCode::kInvalidArity);
  auto more = children_;
  more.push_back(children_.back());
  if (GetParam().nary) {
    const auto node = Take(manager_.MakeBuiltin(op, more));
    EXPECT_EQ(node.Borrow().GetChildren().size(), more.size());
    const auto wrong_last = Const(
        sorts_.at(GetParam().arguments.back() == "B" ? "Z" : "B").Borrow());
    more.back() = wrong_last.Borrow();
    Error(manager_.MakeBuiltin(op, more), ErrorCode::kSortMismatch);
  } else {
    Error(manager_.MakeBuiltin(op, more), ErrorCode::kInvalidArity);
  }
  auto foreign_manager = Take(NodeManager::Create());
  const auto foreign = Take(foreign_manager.MakeBool(true));
  for (std::size_t i = 0; i < children_.size(); ++i) {
    auto changed = children_;
    const auto replacement =
        Const(View(sorts_.at(GetParam().arguments[i] == "B" ? "Z" : "B")));
    changed[i] = replacement.Borrow();
    Error(manager_.MakeBuiltin(op, changed), ErrorCode::kSortMismatch);
    Error(manager_.CheckBuiltin(op, changed), ErrorCode::kSortMismatch);
    changed[i] = foreign.Borrow();
    Error(manager_.MakeBuiltin(op, changed), ErrorCode::kForeignManager);
    changed[i] = NodeView{};
    Error(manager_.MakeBuiltin(op, changed), ErrorCode::kNullObject);
  }
}

TEST_P(OperatorTest, PayloadSchemaRejectsMissingAndExtraFields) {
  Prepare();
  const auto op = Operator();
  auto wrong = op;
  std::vector<Integer> extra = indices_;
  extra.emplace_back(0);
  wrong.indices = extra;
  Error(manager_.MakeBuiltin(wrong, children_), ErrorCode::kInvalidParameter);
  if (!indices_.empty()) {
    wrong = op;
    wrong.indices = {};
    Error(manager_.MakeBuiltin(wrong, children_), ErrorCode::kInvalidParameter);
  }
  wrong = op;
  wrong.qualifier =
      GetParam().qualifier ? std::nullopt : std::optional(View(sorts_.at("A")));
  Error(manager_.MakeBuiltin(wrong, children_), ErrorCode::kInvalidParameter);
  if (indices_.size() == 2) {
    auto missing_one = indices_;
    missing_one.pop_back();
    wrong = op;
    wrong.indices = missing_one;
    Error(manager_.MakeBuiltin(wrong, children_), ErrorCode::kInvalidParameter);
  }
  wrong = op;
  wrong.rational =
      GetParam().rational ? std::nullopt : std::optional(rational_->Borrow());
  Error(manager_.MakeBuiltin(wrong, children_), ErrorCode::kInvalidParameter);
}

bool RequiresEqualBvWidths(const OperatorCase& spec) {
  return spec.arguments.size() >= 2 && spec.arguments[0] == "V" &&
         spec.arguments[1] == "V" && spec.kind != Kind::kBvConcat;
}

class WidthAndIndexTest : public OperatorTest {};

TEST_P(WidthAndIndexTest, EqualWidthConstraintsAndChangedIndexKeys) {
  Prepare(true);
  const auto op = Operator();
  const auto original = Take(manager_.MakeBuiltin(op, children_));
  if (RequiresEqualBvWidths(GetParam())) {
    const auto other = Const(sorts_.at("V16").Borrow());
    for (std::size_t i = 0; i < children_.size(); ++i) {
      auto changed = children_;
      changed[i] = other.Borrow();
      Error(manager_.MakeBuiltin(op, changed), ErrorCode::kSortMismatch);
    }
  }
  const std::array fp_format_kinds = {Kind::kFpFromBits, Kind::kFpConvert,
                                      Kind::kFpFromSbv, Kind::kFpFromUbv,
                                      Kind::kFpFromRational};
  if (std::ranges::find(fp_format_kinds, GetParam().kind) !=
      fp_format_kinds.end()) {
    const std::array minimal_indices = {Integer(2), Integer(2)};
    auto minimal_op = op;
    minimal_op.indices = minimal_indices;
    auto minimal_children = children_;
    Node minimal_bits;
    if (GetParam().kind == Kind::kFpFromBits) {
      const auto bits_sort = Bv(4);
      minimal_bits = Const(bits_sort.Borrow());
      minimal_children[0] = minimal_bits.Borrow();
    }
    EXPECT_EQ(
        Take(manager_.MakeBuiltin(minimal_op, minimal_children)).GetSort(),
        Fp(2, 2));
    for (std::size_t position = 0; position < 2; ++position) {
      auto bad_indices = indices_;
      bad_indices[position] = Integer(1);
      auto bad_op = op;
      bad_op.indices = bad_indices;
      auto changed_children = children_;
      Node packed;
      if (GetParam().kind == Kind::kFpFromBits) {
        const auto width = Bv(position == 0 ? 25 : 9);
        packed = Const(width.Borrow());
        changed_children[0] = packed.Borrow();
      }
      Error(manager_.MakeBuiltin(bad_op, changed_children),
            ErrorCode::kInvalidParameter);
      bad_indices[position] = Number("4294967296");
      const auto oversized = manager_.MakeBuiltin(bad_op, children_);
      ASSERT_FALSE(oversized);
      // FromBits also has a source-width mismatch. Either real error is valid.
      if (GetParam().kind == Kind::kFpFromBits &&
          oversized.error().code == ErrorCode::kSortMismatch) {
        SUCCEED();
      } else {
        Resource(oversized, ResourceKind::kSize);
      }
    }
  }
  if (!indices_.empty()) {
    auto changed_indices = indices_;
    if (changed_indices.size() == 2) {
      if (GetParam().kind == Kind::kBvExtract) {
        changed_indices[0] = Integer(7);
        changed_indices[1] = Integer(4);
      } else {
        changed_indices[0] = Integer(9);
        changed_indices[1] = Integer(23);
      }
    } else {
      changed_indices[0] = Integer(GetParam().indices[0] == "8" ? 9 : 4);
    }
    auto changed = op;
    changed.indices = changed_indices;
    const auto altered = Take(manager_.MakeBuiltin(changed, children_));
    EXPECT_NE(original, altered);
    EXPECT_EQ(Take(manager_.MakeBuiltin(changed, children_)), altered);
    EXPECT_EQ(Take(manager_.MakeBuiltin(op, children_)), original);
  }
}

INSTANTIATE_TEST_SUITE_P(AllBuiltinKinds, OperatorTest,
                         ::testing::ValuesIn(kOperatorCases),
                         [](const auto& case_info) {
                           return case_info.param.name;
                         });

const auto kWidthAndIndexCases = [] {
  std::vector<OperatorCase> cases;
  for (const auto& spec : kOperatorCases) {
    if (RequiresEqualBvWidths(spec) || !spec.indices.empty())
      cases.push_back(spec);
  }
  return cases;
}();

INSTANTIATE_TEST_SUITE_P(ApplicableBuiltinKinds, WidthAndIndexTest,
                         ::testing::ValuesIn(kWidthAndIndexCases),
                         [](const auto& case_info) {
                           return case_info.param.name;
                         });

TEST_F(NodeTest, InvalidAndDedicatedKindsAreNotBuiltins) {
  for (const auto kind :
       {Kind::kInvalid, Kind::kBoolValue, Kind::kIntValue, Kind::kBvValue,
        Kind::kFpValue, Kind::kRoundingModeValue, Kind::kApply,
        static_cast<Kind>(65535)}) {
    Error(manager_.MakeBuiltin({kind, {}, {}, {}}, {}),
          ErrorCode::kInvalidKind);
  }
}

}  // namespace

TEST_F(NodeTest, VariadicOrderPreservesNestedSameKindAndRepeatedEdges) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto x = Int(1);
  const auto y = Int(2);
  const auto nested = Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()});
  const std::array children{y.Borrow(), nested.Borrow(), x.Borrow(),
                            nested.Borrow()};
  const BuiltinOpView op{Kind::kIntAdd, {}, {}, {}};
  const auto original = Take(manager_.MakeBuiltin(op, children));
  const auto changed = Op(Kind::kIntAdd, {y.Borrow(), x.Borrow(),
                                          nested.Borrow(), nested.Borrow()});
  EXPECT_NE(original, changed);
  ExpectOrderedViews(original.Borrow().GetChildren(), children);
  EXPECT_EQ(Take(manager_.MakeBuiltin(op, children)), original);
}

TEST_F(NodeTest, CheckBuiltinInternsAFreshComputedSortWithoutAllocatingANode) {
  const auto left = Const(View(Bv(19)));
  const auto right = Const(View(Bv(23)));
  const BuiltinOpView op{Kind::kBvConcat, {}, {}, {}};
  const std::array children{left.Borrow(), right.Borrow()};
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto computed = Take(manager_.CheckBuiltin(op, children));
  EXPECT_EQ(computed.Borrow().GetWidth(), 42);
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after.sorts, before.sorts + 1);
  EXPECT_EQ(after.node_allocations, before.node_allocations);
  EXPECT_EQ(after.next_node_id, before.next_node_id);
  EXPECT_EQ(computed, Bv(42));
  EXPECT_EQ(Take(manager_.MakeBuiltin(op, children)).GetSort(), computed);
}

}  // namespace dandelion::testing
