#include "test_support.h"

#include "../util/numeric_test_peer.h"

namespace dandelion::testing {
namespace {

template <class T>
concept CanBorrowTemporary = requires(T value) { std::move(value).Borrow(); };
static_assert(!CanBorrowTemporary<Node> && !CanBorrowTemporary<Sort> &&
              !CanBorrowTemporary<Integer> && !CanBorrowTemporary<Rational>);
static_assert(std::is_nothrow_move_constructible_v<Node> &&
              std::is_nothrow_move_assignable_v<Node> &&
              std::is_nothrow_destructible_v<Node> &&
              std::is_nothrow_destructible_v<DagWalker>);

TEST_F(NodeTest, FormalIdentityAndSubstitutionFoundationWithoutAParser) {
  const auto z = Take(manager_.IntSort());
  const auto formal = Take(manager_.MakeFormal("x", z.Borrow()));
  const auto shadow = Take(manager_.MakeFormal("x", z.Borrow()));
  const auto fi = Take(manager_.InstantiateDeclaration(formal.Borrow(), {}));
  const auto si = Take(manager_.InstantiateDeclaration(shadow.Borrow(), {}));
  const auto x = Take(manager_.MakeApply(fi.Borrow(), {}));
  const auto y = Take(manager_.MakeApply(si.Borrow(), {}));
  const auto body = Op(Kind::kIntAdd, {x.Borrow(), x.Borrow()});
  EXPECT_NE(x, y);
  const auto actual = Int(3);
  // A frontend can rebuild the template with a fixed formal→actual environment.
  // This checks node foundations; it does not execute define-fun or let
  // parsing.
  const auto instantiated =
      Op(Kind::kIntAdd, {actual.Borrow(), actual.Borrow()});
  EXPECT_EQ(instantiated.Borrow().GetChildren()[0],
            instantiated.Borrow().GetChildren()[1]);
  EXPECT_EQ(body.Borrow().GetChildren()[0].GetId(), x.GetId());
  const std::array roots = {instantiated};
  auto walker = Take(DagWalker::Create(roots));
  while (const auto event = Take(walker.Next())) {
    if (const auto decl = event->node.GetDeclaration()) {
      EXPECT_NE(decl->GetDeclaration().GetKind(), DeclKind::kFormal);
    }
  }
}

TEST_F(NodeTest, LoweredDatatypeTestersAndIteKeepPatternOrderAndBranchTyping) {
  const std::array headers = {DatatypeHeader{"Color", 0}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  Take(builder.AddConstructor(0, "red", {}));
  Take(builder.AddConstructor(0, "blue", {}));
  const auto group = Take(builder.Commit());
  const auto constructor = Take(group.Borrow().GetSortConstructor(0));
  const auto sort = Take(manager_.InstantiateSort(constructor, {}));
  const auto scrutinee = Const(sort.Borrow(), "c");
  const auto tester = Take(group.Borrow().GetTester(0, 0));
  const auto ti = Take(manager_.InstantiateDeclaration(tester, {}));
  const std::array args = {scrutinee.Borrow()};
  const auto condition = Take(manager_.MakeApply(ti.Borrow(), args));
  const auto one = Int(1), two = Int(2), three = Int(3);
  const auto inner =
      Op(Kind::kIte, {condition.Borrow(), two.Borrow(), three.Borrow()});
  const auto outer =
      Op(Kind::kIte, {condition.Borrow(), one.Borrow(), inner.Borrow()});
  EXPECT_EQ(outer.Borrow().GetChildren()[1].GetId(), one.GetId());
  EXPECT_EQ(outer.Borrow().GetChildren()[2].GetId(), inner.GetId());
  const auto truth = Take(manager_.MakeBool(true));
  const std::array bad = {condition.Borrow(), truth.Borrow(), three.Borrow()};
  Error(manager_.MakeBuiltin({Kind::kIte, {}, {}, {}}, bad),
        ErrorCode::kSortMismatch);
}

TEST_F(NodeTest, AllRetainViewCategoriesRejectForeignAndNullOwners) {
  auto other = Take(NodeManager::Create());
  const auto sort = Take(other.BoolSort());
  const auto constructor = Take(other.DeclareUninterpretedSort("U", 0));
  const auto decl = Take(other.DeclareFunction("c", {}, sort.Borrow()));
  const auto instance = Take(other.InstantiateDeclaration(decl.Borrow(), {}));
  const std::array headers = {DatatypeHeader{"D", 0}};
  auto builder = Take(other.BeginDatatypeGroup(headers));
  Take(builder.AddConstructor(0, "base", {}));
  const auto group = Take(builder.Commit());
  Error(manager_.RetainSortConstructor(constructor.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.RetainDeclaration(decl.Borrow()), ErrorCode::kForeignManager);
  Error(manager_.RetainDeclInstance(instance.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.RetainDatatypeGroup(group.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.RetainSortConstructor({}), ErrorCode::kNullObject);
  Error(manager_.RetainDeclaration({}), ErrorCode::kNullObject);
  Error(manager_.RetainDeclInstance({}), ErrorCode::kNullObject);
  Error(manager_.RetainDatatypeGroup({}), ErrorCode::kNullObject);
  Error(NodeManager::Attach(ManagerRef{}), ErrorCode::kNullObject);
  NodeManager moved;
  Error(moved.BoolSort(), ErrorCode::kNullObject);
}

TEST_F(NodeTest, IndexedRationalAndNamePayloadsOwnIndependentInputCopies) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto sort = Bv(8);
  const auto x = Const(sort.Borrow());
  std::vector<Integer> indices;
  indices.push_back(Number("18446744073709551617"));
  const auto rotation = Op(Kind::kBvRotateLeft, {x.Borrow()}, indices);
  indices[0] = Integer(0);
  indices.clear();
  EXPECT_EQ(rotation.Borrow().GetIndices()[0].ToDecimal(),
            "18446744073709551617");
  const Integer one(1), two(2), three(3), eight(8), twenty_four(24);
  auto rational = Take(Rational::Make(one.Borrow(), two.Borrow()));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format = {eight, twenty_four};
  const auto conversion =
      Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, rational.Borrow());
  rational = Take(Rational::Make(one.Borrow(), three.Borrow()));
  EXPECT_EQ(
      conversion.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
      "2");
  const auto z = Take(manager_.IntSort());
  std::string name(128, 'f');
  const auto declaration = Take(manager_.DeclareFunction(name, {}, z.Borrow()));
  name.assign("changed");
  EXPECT_EQ(declaration.Borrow().GetNameHint(), std::string(128, 'f'));
}

TEST_F(NodeTest,
       MovedFromRationalsStayCanonicalThroughMovesAndNodePublication) {
  const Integer one(1), two(2), three(3);
  auto original = Take(Rational::Make(one.Borrow(), two.Borrow()));
  numeric::NumericTestPeer::FailAllocationAfter(0);
  auto constructed = std::move(original);
  numeric::NumericTestPeer::ClearFailures();
  EXPECT_EQ(original.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(original.Borrow().Denominator().ToDecimal(), "1");
  EXPECT_EQ(constructed.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(constructed.Borrow().Denominator().ToDecimal(), "2");
  auto assigned = Take(Rational::Make(two.Borrow(), three.Borrow()));
  numeric::NumericTestPeer::FailAllocationAfter(0);
  assigned = std::move(constructed);
  numeric::NumericTestPeer::ClearFailures();
  EXPECT_EQ(constructed.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(constructed.Borrow().Denominator().ToDecimal(), "1");
  auto& alias = assigned;
  assigned = std::move(alias);
  EXPECT_EQ(assigned.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(assigned.Borrow().Denominator().ToDecimal(), "2");
  const auto rounding = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array indices = {two, two};
  const auto conversion = Op(Kind::kFpFromRational, {rounding.Borrow()},
                             indices, {}, constructed.Borrow());
  EXPECT_EQ(conversion.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
            "0");
  EXPECT_EQ(
      conversion.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
      "1");
  const auto big = Number("340282366920938463463374607431768211456");
  const auto denominator = Number("340282366920938463463374607431768211457");
  auto big_original = Take(Rational::Make(big.Borrow(), denominator.Borrow()));
  numeric::NumericTestPeer::FailAllocationAfter(0);
  auto big_moved = std::move(big_original);
  numeric::NumericTestPeer::ClearFailures();
  EXPECT_EQ(big_moved.Borrow().Numerator().Compare(big.Borrow()), 0);
  EXPECT_EQ(big_original.Borrow().Denominator().ToDecimal(), "1");
  numeric::NumericTestPeer::FailAllocationAfter(0);
  assigned = std::move(big_moved);
  numeric::NumericTestPeer::ClearFailures();
  EXPECT_EQ(assigned.Borrow().Denominator().Compare(denominator.Borrow()), 0);
  EXPECT_EQ(big_moved.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(big_moved.Borrow().Denominator().ToDecimal(), "1");
}

TEST_F(NodeTest, DeepFiniteDatatypeRecipeInstantiationIsIterative) {
  const auto z = Take(manager_.IntSort());
  const std::array headers = {DatatypeHeader{"Box", 1}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  auto recipe = Take(builder.Parameter(0, 0));
  const auto integer = Take(builder.Concrete(z.Borrow()));
  for (int i = 0; i < 5000; ++i) recipe = Take(builder.Array(integer, recipe));
  const std::array fields = {DatatypeFieldSpec{"nested", recipe}};
  Take(builder.AddConstructor(0, "box", fields));
  const auto group = Take(builder.Commit());
  const auto constructor = Take(group.Borrow().GetConstructor(0, 0));
  const auto b = Take(manager_.BoolSort());
  const std::array args = {b.Borrow()};
  const auto instance =
      Take(manager_.InstantiateDeclaration(constructor, args));
  auto field = instance.Borrow().GetDomain()[0];
  for (int i = 0; i < 5000; ++i) {
    ASSERT_EQ(field.GetKind(), SortKind::kArray);
    EXPECT_EQ(field.GetArrayIndex()->GetId(), z.GetId());
    field = *field.GetArrayElement();
  }
  EXPECT_EQ(field.GetId(), b.GetId());
}

TEST_F(NodeTest, SharedDatatypeFieldRecipeUsesOneEvaluation) {
  const auto integer = Take(manager_.IntSort());
  const auto boolean = Take(manager_.BoolSort());
  const auto expected =
      Take(manager_.MakeArraySort(integer.Borrow(), boolean.Borrow()));
  auto builder = Take(
      manager_.BeginDatatypeGroup(std::array{DatatypeHeader{"Shared", 1}}));
  const auto index = Take(builder.Concrete(integer.Borrow()));
  const auto argument = Take(builder.Parameter(0, 0));
  const auto array = Take(builder.Array(index, argument));
  const std::vector<DatatypeFieldSpec> fields(32, {"field", array});
  Take(builder.AddConstructor(0, "shared", fields));
  auto deep_array = array;
  for (int i = 0; i < 4096; ++i)
    deep_array = Take(builder.Array(index, deep_array));
  Take(builder.AddConstructor(
      0, "deep", std::array{DatatypeFieldSpec{"nested", deep_array}}));
  const auto group = Take(builder.Commit());
  const auto constructor = Take(group.Borrow().GetConstructor(0, 0));
  const auto sort_constructor = Take(group.Borrow().GetSortConstructor(0));
  const std::array arguments{boolean.Borrow()};
  const auto range =
      Take(manager_.InstantiateSort(sort_constructor, arguments));
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto instance =
      Take(manager_.InstantiateDeclaration(constructor, arguments));
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_LT(after.allocation_attempts - before.allocation_attempts,
            fields.size());
  EXPECT_EQ(after.sorts, before.sorts);
  ASSERT_EQ(instance.Borrow().GetDomain().size(), fields.size());
  for (const auto field : instance.Borrow().GetDomain())
    EXPECT_EQ(field, expected.Borrow());
  EXPECT_EQ(instance.Borrow().GetRange(), range.Borrow());
}

}  // namespace
}  // namespace dandelion::testing
