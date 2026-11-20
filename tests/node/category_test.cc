#include "test_support.h"

namespace dandelion::testing {
namespace {

TEST_F(NodeTest,
       EverySortCategoryHasConcreteQueriesIdentityAndManagerOwnership) {
  std::vector<Sort> sorts;
  sorts.push_back(Take(manager_.BoolSort()));
  sorts.push_back(Take(manager_.IntSort()));
  sorts.push_back(Take(manager_.RoundingModeSort()));
  sorts.push_back(Bv(8));
  sorts.push_back(Fp(2, 2));
  sorts.push_back(
      Take(manager_.MakeArraySort(sorts[1].Borrow(), sorts[4].Borrow())));
  const auto u = Take(manager_.DeclareUninterpretedSort("U", 0));
  sorts.push_back(Take(manager_.InstantiateSort(u.Borrow(), {})));
  const std::array headers = {DatatypeHeader{"D", 0}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  Take(builder.AddConstructor(0, "base", {}));
  const auto group = Take(builder.Commit());
  sorts.push_back(Take(manager_.InstantiateSort(
      Take(group.Borrow().GetSortConstructor(0)), {})));
  const std::array expected_kinds = {
      SortKind::kBool,          SortKind::kInt,
      SortKind::kRoundingMode,  SortKind::kBitVector,
      SortKind::kFloatingPoint, SortKind::kArray,
      SortKind::kUninterpreted, SortKind::kDatatype};
  auto other = Take(NodeManager::Create());
  for (std::size_t category = 0; category < sorts.size(); ++category) {
    const auto sort = sorts[category].Borrow();
    EXPECT_EQ(sort.GetKind(), expected_kinds[category]);
    const auto value = Const(sort);
    const auto condition = Take(manager_.MakeBool(true));
    const auto equality = Op(Kind::kEqual, {value.Borrow(), value.Borrow()});
    const auto distinct = Op(Kind::kDistinct, {value.Borrow(), value.Borrow()});
    EXPECT_EQ(equality.Borrow().GetSort().GetKind(), SortKind::kBool);
    EXPECT_EQ(distinct.Borrow().GetSort().GetKind(), SortKind::kBool);
    const auto ite =
        Op(Kind::kIte, {condition.Borrow(), value.Borrow(), value.Borrow()});
    EXPECT_EQ(ite.Borrow().GetSort(), sort);
    EXPECT_NE(ite, value);
    EXPECT_EQ(Take(manager_.RetainSort(sort)), sorts[category]);
    Error(other.RetainSort(sort), ErrorCode::kForeignManager);
    EXPECT_EQ(sort.GetWidth().has_value(), category == 3);
    EXPECT_EQ(sort.GetFpFormat().has_value(), category == 4);
    EXPECT_EQ(sort.GetArrayIndex().has_value(), category == 5);
    EXPECT_EQ(sort.GetArrayElement().has_value(), category == 5);
    EXPECT_EQ(sort.GetSortConstructor().has_value(), category >= 6);
    for (std::size_t other_category = 0; other_category < sorts.size();
         ++other_category) {
      EXPECT_EQ(sorts[category] == sorts[other_category],
                category == other_category);
    }
  }
}

}  // namespace

TEST_F(NodeTest, EveryDeclarationCategoryChecksItsConcreteSignature) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto z = Take(manager_.IntSort());
  const auto b = Take(manager_.BoolSort());
  const std::array ordinary{z.Borrow(), b.Borrow()};
  const auto f = Take(manager_.DeclareFunction("f", ordinary, b.Borrow()));
  const auto g = Take(manager_.FreshFunction("g", ordinary, z.Borrow()));
  const auto p = Take(manager_.MakeFormal("p", b.Borrow()));
  auto builder =
      Take(manager_.BeginDatatypeGroup(std::array{DatatypeHeader{"Pair", 1}}));
  const auto parameter = Take(builder.Parameter(0, 0));
  const auto fixed = Take(builder.Concrete(b.Borrow()));
  Take(builder.AddConstructor(0, "pair",
                              std::array{DatatypeFieldSpec{"first", parameter},
                                         DatatypeFieldSpec{"second", fixed}}));
  const auto group = Take(builder.Commit());
  const std::array types{z.Borrow()};
  const auto pair = Take(manager_.InstantiateSort(
      Take(group.Borrow().GetSortConstructor(0)), types));
  const std::array pair_domain{pair.Borrow()};
  const auto constructor = Take(group.Borrow().GetConstructor(0, 0));
  const auto first = Take(group.Borrow().GetSelector(0, 0, 0));
  const auto second = Take(group.Borrow().GetSelector(0, 0, 1));
  const auto tester = Take(group.Borrow().GetTester(0, 0));
  EXPECT_NE(first, second);
  EXPECT_EQ(first.GetFieldOrdinal(), 0);
  EXPECT_EQ(second.GetFieldOrdinal(), 1);
  for (const auto decl : {first, second, tester})
    EXPECT_EQ(decl.GetConstructor(), constructor);
  EXPECT_TRUE(tester.GetDomain().empty());
  EXPECT_EQ(tester.GetRange(), b.Borrow());
  EXPECT_EQ(Take(tester.GetDomainRecipe(0)).GetRecursiveSlot(), 0);

  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.MakeBool(true));
  const auto wrong_bool = Take(manager_.MakeBool(true));
  const auto wrong_int = Int(7);
  const auto check = [&](DeclView decl, DeclKind kind,
                         std::span<const SortView> args,
                         std::span<const SortView> domain, SortView range) {
    SCOPED_TRACE(decl.GetNameHint());
    EXPECT_EQ(decl.GetKind(), kind);
    EXPECT_EQ(decl.GetParameterCount(), args.size());
    const auto instance = Take(manager_.InstantiateDeclaration(decl, args));
    const auto view = instance.Borrow();
    EXPECT_EQ(Take(manager_.InstantiateDeclaration(decl, args)), instance);
    Error(other.InstantiateDeclaration(decl, args), ErrorCode::kForeignManager);
    Error(other.MakeApply(view, {}), ErrorCode::kForeignManager);
    EXPECT_EQ(view.GetDeclaration(), decl);
    ExpectOrderedViews(view.GetTypeArguments(), args);
    ExpectOrderedViews(view.GetDomain(), domain);
    EXPECT_EQ(view.GetRange(), range);
    std::vector<Node> owners;
    for (const auto sort : domain) owners.push_back(Const(sort));
    std::vector<NodeView> operands;
    for (const auto& owner : owners) operands.push_back(owner.Borrow());
    const auto applied = Take(manager_.MakeApply(view, operands));
    EXPECT_EQ(applied.Borrow().GetKind(), Kind::kApply);
    EXPECT_EQ(applied.Borrow().GetSort(), range);
    ExpectOrderedViews(applied.Borrow().GetChildren(), operands);
    EXPECT_EQ(applied.Borrow().GetDeclaration(), view);
    EXPECT_TRUE(applied.Borrow().GetIndices().empty());
    EXPECT_FALSE(applied.Borrow().GetValue());
    EXPECT_EQ(Take(manager_.MakeApply(view, operands)), applied);
    auto extra = operands;
    extra.push_back(wrong_int.Borrow());
    Error(manager_.MakeApply(view, extra), ErrorCode::kInvalidArity);
    if (!operands.empty()) {
      Error(manager_.MakeApply(view,
                               std::span{operands}.first(operands.size() - 1)),
            ErrorCode::kInvalidArity);
    }
    for (std::size_t i = 0; i < domain.size(); ++i) {
      auto changed = operands;
      const auto replacement = Const(domain[i]);
      changed[i] = replacement.Borrow();
      EXPECT_NE(Take(manager_.MakeApply(view, changed)), applied);
      changed[i] =
          domain[i] == b.Borrow() ? wrong_int.Borrow() : wrong_bool.Borrow();
      Error(manager_.MakeApply(view, changed), ErrorCode::kSortMismatch);
      changed[i] = foreign.Borrow();
      Error(manager_.MakeApply(view, changed), ErrorCode::kForeignManager);
      changed[i] = {};
      Error(manager_.MakeApply(view, changed), ErrorCode::kNullObject);
    }
    Error(manager_.InstantiateDeclaration(
              decl, args.empty() ? std::span<const SortView>(types)
                                 : std::span<const SortView>{}),
          ErrorCode::kInvalidArity);
  };
  check(f.Borrow(), DeclKind::kFunction, {}, ordinary, b.Borrow());
  check(g.Borrow(), DeclKind::kInternalFunction, {}, ordinary, z.Borrow());
  check(p.Borrow(), DeclKind::kFormal, {}, {}, b.Borrow());
  check(constructor, DeclKind::kDatatypeConstructor, types, ordinary,
        pair.Borrow());
  check(first, DeclKind::kDatatypeSelector, types, pair_domain, z.Borrow());
  check(second, DeclKind::kDatatypeSelector, types, pair_domain, b.Borrow());
  check(tester, DeclKind::kDatatypeTester, types, pair_domain, b.Borrow());
}

}  // namespace dandelion::testing
