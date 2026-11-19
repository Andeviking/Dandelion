#include "test_support.h"

namespace dandelion::testing {
namespace {

class Datatype : public NodeTest {
 protected:
  DatatypeGroup List() {
    const std::array headers = {DatatypeHeader{"List", 1}};
    auto builder = Take(manager_.BeginDatatypeGroup(headers));
    const auto parameter = Take(builder.Parameter(0, 0));
    const std::array arguments = {parameter};
    const auto recursive = Take(builder.Recursive(0, arguments));
    Take(builder.AddConstructor(0, "nil", {}));
    const std::array fields = {DatatypeFieldSpec{"head", parameter},
                               DatatypeFieldSpec{"tail", recursive}};
    Take(builder.AddConstructor(0, "cons", fields));
    return Take(builder.Commit());
  }
};

TEST_F(Datatype, ParameterizedListSignaturesAndWrongConstructorSelector) {
  const auto group = List();
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), 1);
  EXPECT_EQ(Take(group.Borrow().GetConstructorCount(0)), 2);
  EXPECT_EQ(Take(group.Borrow().GetFieldCount(0, 1)), 2);
  const auto ctor = Take(group.Borrow().GetSortConstructor(0));
  EXPECT_TRUE(ctor.IsDatatype());
  EXPECT_EQ(ctor.GetDatatypeGroup()->GetId(), group.GetId());
  const auto nil = Take(group.Borrow().GetConstructor(0, 0));
  const auto cons = Take(group.Borrow().GetConstructor(0, 1));
  const auto head = Take(group.Borrow().GetSelector(0, 1, 0));
  const auto tail = Take(group.Borrow().GetSelector(0, 1, 1));
  const auto tester = Take(group.Borrow().GetTester(0, 1));
  EXPECT_EQ(nil.GetKind(), DeclKind::kDatatypeConstructor);
  EXPECT_EQ(head.GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(tester.GetKind(), DeclKind::kDatatypeTester);
  EXPECT_EQ(head.GetConstructor()->GetId(), cons.GetId());
  EXPECT_EQ(head.GetFieldOrdinal(), 0);
  EXPECT_EQ(cons.GetParameterCount(), 1);
  EXPECT_EQ(Take(cons.GetDomainRecipe(0)).GetKind(),
            SortRecipeKind::kParameter);
  EXPECT_EQ(Take(cons.GetDomainRecipe(1)).GetKind(),
            SortRecipeKind::kRecursive);
  EXPECT_TRUE(cons.GetRangeRecipe());
  const auto z = Take(manager_.IntSort());
  const auto v = Bv(8);
  std::vector<Node> nil_nodes;
  for (const auto type : {z.Borrow(), v.Borrow()}) {
    const std::array args = {type};
    const auto sort = Take(manager_.InstantiateSort(ctor, args));
    EXPECT_EQ(sort.Borrow().GetKind(), SortKind::kDatatype);
    EXPECT_EQ(sort.Borrow().GetArguments()[0].GetId(), type.GetId());
    const auto ni = Take(manager_.InstantiateDeclaration(nil, args));
    const auto ci = Take(manager_.InstantiateDeclaration(cons, args));
    const auto hi = Take(manager_.InstantiateDeclaration(head, args));
    const auto ti = Take(manager_.InstantiateDeclaration(tail, args));
    const auto test = Take(manager_.InstantiateDeclaration(tester, args));
    EXPECT_EQ(ni, Take(manager_.InstantiateDeclaration(nil, args)));
    EXPECT_EQ(ni.Borrow().GetRange().GetId(), sort.GetId());
    EXPECT_EQ(ci.Borrow().GetDomain().size(), 2);
    EXPECT_EQ(ci.Borrow().GetDomain()[0].GetId(), type.GetId());
    EXPECT_EQ(ci.Borrow().GetDomain()[1].GetId(), sort.GetId());
    EXPECT_EQ(hi.Borrow().GetRange().GetId(), type.GetId());
    EXPECT_EQ(ti.Borrow().GetRange().GetId(), sort.GetId());
    const auto n = Take(manager_.MakeApply(ni.Borrow(), {}));
    nil_nodes.push_back(n);
    const auto value = Const(type);
    const std::array cons_args = {value.Borrow(), n.Borrow()};
    const auto cell = Take(manager_.MakeApply(ci.Borrow(), cons_args));
    const std::array selector_args = {n.Borrow()};
    EXPECT_EQ(
        Take(manager_.MakeApply(hi.Borrow(), selector_args)).GetSort().GetId(),
        type.GetId());
    EXPECT_EQ(Take(manager_.MakeApply(test.Borrow(), selector_args)).GetSort(),
              Take(manager_.BoolSort()));
    const auto mixed_array = Take(manager_.MakeArraySort(sort.Borrow(), type));
    const std::array uf_domain = {sort.Borrow(), mixed_array.Borrow()};
    const auto uf =
        Take(manager_.DeclareFunction("dt_uf", uf_domain, sort.Borrow()));
    const auto instance =
        Take(manager_.InstantiateDeclaration(uf.Borrow(), {}));
    const auto array = Const(mixed_array.Borrow());
    const std::array uf_args = {cell.Borrow(), array.Borrow()};
    EXPECT_EQ(Take(manager_.MakeApply(instance.Borrow(), uf_args)).GetSort(),
              sort);
  }
  EXPECT_NE(nil_nodes[0], nil_nodes[1]);
  const std::array wrong = {nil_nodes[1].Borrow()};
  const std::array z_args = {z.Borrow()};
  const auto h = Take(manager_.InstantiateDeclaration(head, z_args));
  Error(manager_.MakeApply(h.Borrow(), wrong), ErrorCode::kSortMismatch);
  Error(manager_.InstantiateDeclaration(nil, {}), ErrorCode::kInvalidArity);
  Error(group.Borrow().GetSortConstructor(1), ErrorCode::kInvalidParameter);
  Error(group.Borrow().GetConstructor(0, 2), ErrorCode::kInvalidParameter);
  Error(group.Borrow().GetSelector(0, 0, 0), ErrorCode::kInvalidParameter);
  Error(group.Borrow().GetTester(1, 0), ErrorCode::kInvalidParameter);
}

TEST_F(Datatype, EnumerationMutualProductivityAndRepairAfterFailure) {
  const std::array headers = {DatatypeHeader{"A", 0}, DatatypeHeader{"B", 0}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  const auto a = Take(builder.Recursive(0, {}));
  const auto b = Take(builder.Recursive(1, {}));
  const std::array to_b = {DatatypeFieldSpec{"b", b}};
  const std::array to_a = {DatatypeFieldSpec{"a", a}};
  Take(builder.AddConstructor(0, "to_b", to_b));
  Take(builder.AddConstructor(1, "to_a", to_a));
  const auto before = NodeTestPeer::GetStats(manager_);
  Error(builder.Commit(), ErrorCode::kInvalidDatatype);
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after.sort_constructors, before.sort_constructors);
  EXPECT_EQ(after.declarations, before.declarations);
  EXPECT_EQ(after.datatype_groups, before.datatype_groups);
  Take(builder.AddConstructor(1, "base", {}));
  const auto group = Take(builder.Commit());
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), 2);
  Error(builder.Commit(), ErrorCode::kBuilderClosed);
  Error(builder.AddConstructor(0, "late", {}), ErrorCode::kBuilderClosed);
  const auto ca = Take(group.Borrow().GetSortConstructor(0));
  const auto cb = Take(group.Borrow().GetSortConstructor(1));
  const auto sa = Take(manager_.InstantiateSort(ca, {}));
  const auto sb = Take(manager_.InstantiateSort(cb, {}));
  const auto decl = Take(group.Borrow().GetConstructor(0, 0));
  const auto instance = Take(manager_.InstantiateDeclaration(decl, {}));
  EXPECT_EQ(instance.Borrow().GetDomain()[0].GetId(), sb.GetId());
  EXPECT_EQ(instance.Borrow().GetRange().GetId(), sa.GetId());
  const std::array color_headers = {DatatypeHeader{"Color", 0}};
  auto colors = Take(manager_.BeginDatatypeGroup(color_headers));
  Take(colors.AddConstructor(0, "red", {}));
  Take(colors.AddConstructor(0, "blue", {}));
  const auto color = Take(colors.Commit());
  EXPECT_EQ(Take(color.Borrow().GetConstructorCount(0)), 2);
}

TEST_F(Datatype,
       RejectRecursiveContainersAcceptChangingParametersAndArrayFields) {
  const auto z = Take(manager_.IntSort());
  const std::array headers = {DatatypeHeader{"D", 1}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  const auto p = Take(builder.Parameter(0, 0));
  const std::array args = {p};
  const auto recurse = Take(builder.Recursive(0, args));
  const auto integer = Take(builder.Concrete(z.Borrow()));
  const auto nested = Take(builder.Array(integer, recurse));
  const std::array fields = {DatatypeFieldSpec{"bad", nested}};
  Take(builder.AddConstructor(0, "leaf", {}));
  Take(builder.AddConstructor(0, "bad", fields));
  Error(builder.Commit(), ErrorCode::kInvalidDatatype);
  builder.Abort();
  Error(builder.Parameter(0, 0), ErrorCode::kBuilderClosed);
  auto good = Take(manager_.BeginDatatypeGroup(headers));
  const auto param = Take(good.Parameter(0, 0));
  const auto int_recipe = Take(good.Concrete(z.Borrow()));
  const auto array_recipe = Take(good.Array(int_recipe, param));
  const std::array next_args = {array_recipe};
  const auto next = Take(good.Recursive(0, next_args));
  const std::array leaf_fields = {DatatypeFieldSpec{"value", param},
                                  DatatypeFieldSpec{"array", array_recipe}};
  const std::array step_fields = {DatatypeFieldSpec{"next", next}};
  Take(good.AddConstructor(0, "leaf", leaf_fields));
  Take(good.AddConstructor(0, "step", step_fields));
  const auto group = Take(good.Commit());
  const auto sort_ctor = Take(group.Borrow().GetSortConstructor(0));
  const std::array types = {z.Borrow()};
  const auto sort = Take(manager_.InstantiateSort(sort_ctor, types));
  const auto step = Take(group.Borrow().GetConstructor(0, 1));
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto instance = Take(manager_.InstantiateDeclaration(step, types));
  const auto next_sort = instance.Borrow().GetDomain()[0];
  EXPECT_EQ(next_sort.GetSortConstructor()->GetId(), sort_ctor.GetId());
  EXPECT_EQ(next_sort.GetArguments()[0].GetKind(), SortKind::kArray);
  EXPECT_EQ(next_sort.GetArguments()[0].GetArrayElement()->GetId(), z.GetId());
  EXPECT_LE(NodeTestPeer::GetStats(manager_).sorts - before.sorts, 2);
  EXPECT_NE(next_sort.GetId(), sort.GetId());
  const auto ctor = Take(manager_.DeclareUninterpretedSort("Box", 1));
  auto blocked = Take(manager_.BeginDatatypeGroup(headers));
  const auto bp = Take(blocked.Parameter(0, 0));
  const std::array ba = {bp};
  const auto br = Take(blocked.Recursive(0, ba));
  const std::array wrapped = {br};
  const auto box = Take(blocked.Apply(ctor.Borrow(), wrapped));
  const std::array bad_fields = {DatatypeFieldSpec{"box", box}};
  Take(blocked.AddConstructor(0, "base", {}));
  Take(blocked.AddConstructor(0, "nested", bad_fields));
  Error(blocked.Commit(), ErrorCode::kInvalidDatatype);
}

TEST_F(Datatype, ValidateRecipeOwnershipParameterScopeArityAndBuilderMoves) {
  const std::array headers = {DatatypeHeader{"A", 1}, DatatypeHeader{"B", 1}};
  auto first = Take(manager_.BeginDatatypeGroup(headers));
  auto second = Take(manager_.BeginDatatypeGroup(headers));
  const auto p = Take(first.Parameter(0, 0));
  Error(first.Parameter(2, 0), ErrorCode::kInvalidParameter);
  Error(first.Parameter(0, 1), ErrorCode::kInvalidParameter);
  Error(first.Recursive(2, {}), ErrorCode::kInvalidParameter);
  Error(first.Recursive(0, {}), ErrorCode::kInvalidArity);
  Error(second.Array(p, p), ErrorCode::kInvalidParameter);
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.BoolSort());
  Error(first.Concrete(foreign.Borrow()), ErrorCode::kForeignManager);
  const auto u = Take(manager_.DeclareUninterpretedSort("U", 1));
  Error(first.Apply(u.Borrow(), {}), ErrorCode::kInvalidArity);
  const std::array wrong_scope = {DatatypeFieldSpec{"p", p}};
  Take(first.AddConstructor(0, "base", {}));
  Take(first.AddConstructor(1, "bad", wrong_scope));
  Error(first.Commit(), ErrorCode::kInvalidDatatype);
  auto moved = std::move(second);
  Error(second.Commit(), ErrorCode::kBuilderClosed);
  auto& self = moved;
  moved = std::move(self);
  Take(moved.AddConstructor(0, "a", {}));
  Take(moved.AddConstructor(1, "b", {}));
  const auto group = Take(moved.Commit());
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), 2);
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Datatype, OrderedMultipleParametersAndEverySupportedFieldCategory) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const std::array headers = {DatatypeHeader{"Pair", 2}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  const auto first = Take(builder.Parameter(0, 0));
  const auto second = Take(builder.Parameter(0, 1));
  const std::array fields = {DatatypeFieldSpec{"first", first},
                             DatatypeFieldSpec{"second", second}};
  Take(builder.AddConstructor(0, "pair", fields));
  const auto group = Take(builder.Commit());
  const auto constructor = Take(group.Borrow().GetSortConstructor(0));
  const auto declaration = Take(group.Borrow().GetConstructor(0, 0));
  const auto z = Take(manager_.IntSort());
  const auto v = Bv(8);
  const std::array forward = {z.Borrow(), v.Borrow()};
  const std::array reverse = {v.Borrow(), z.Borrow()};
  const auto pair = Take(manager_.InstantiateSort(constructor, forward));
  EXPECT_NE(pair, Take(manager_.InstantiateSort(constructor, reverse)));
  EXPECT_NE(Take(manager_.InstantiateDeclaration(declaration, forward)),
            Take(manager_.InstantiateDeclaration(declaration, reverse)));
  std::vector<Sort> types = {z, v, Take(manager_.BoolSort()),
                             Take(manager_.RoundingModeSort()), Fp(2, 3)};
  types.push_back(Take(manager_.MakeArraySort(z.Borrow(), v.Borrow())));
  const auto u = Take(manager_.DeclareUninterpretedSort("U", 0));
  types.push_back(Take(manager_.InstantiateSort(u.Borrow(), {})));
  types.push_back(pair);
  for (const auto& type : types) {
    const std::array arguments = {type.Borrow(), z.Borrow()};
    const auto instance =
        Take(manager_.InstantiateDeclaration(declaration, arguments));
    EXPECT_EQ(instance.Borrow().GetDomain()[0], type.Borrow());
    EXPECT_EQ(instance.Borrow().GetDomain()[1], z.Borrow());
    EXPECT_EQ(instance.Borrow().GetTypeArguments()[0], type.Borrow());
  }
}

}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Datatype, ExistingSortConstructorConcreteAndArrayRecipesAreSubstituted) {
  const auto u = Take(manager_.DeclareUninterpretedSort("U", 1));
  const auto z = Take(manager_.IntSort());
  const auto v = Bv(8);
  const std::array headers = {DatatypeHeader{"Wrapper", 1}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  const auto p = Take(builder.Parameter(0, 0));
  const auto concrete = Take(builder.Concrete(v.Borrow()));
  const auto integer = Take(builder.Concrete(z.Borrow()));
  const std::array arguments = {p};
  const auto applied = Take(builder.Apply(u.Borrow(), arguments));
  const auto array = Take(builder.Array(integer, p));
  const std::array fields = {DatatypeFieldSpec{"u", applied},
                             DatatypeFieldSpec{"bits", concrete},
                             DatatypeFieldSpec{"array", array}};
  Take(builder.AddConstructor(0, "wrapper", fields));
  const auto group = Take(builder.Commit());
  const auto declaration = Take(group.Borrow().GetConstructor(0, 0));
  const auto applied_view = Take(declaration.GetDomainRecipe(0));
  EXPECT_EQ(applied_view.GetKind(), SortRecipeKind::kApply);
  EXPECT_EQ(applied_view.GetConstructor()->GetId(), u.GetId());
  EXPECT_EQ(Take(applied_view.Argument(0)).GetParameter(),
            std::pair(std::uint32_t{0}, std::uint32_t{0}));
  EXPECT_EQ(Take(declaration.GetDomainRecipe(1)).GetConcrete()->GetId(),
            v.GetId());
  const auto array_view = Take(declaration.GetDomainRecipe(2));
  EXPECT_EQ(array_view.GetArgumentCount(), 2);
  Error(array_view.Argument(2), ErrorCode::kInvalidParameter);
  const auto fp = Fp(2, 3);
  const std::array types = {fp.Borrow()};
  const auto instance =
      Take(manager_.InstantiateDeclaration(declaration, types));
  const auto expected_u = Take(manager_.InstantiateSort(u.Borrow(), types));
  const auto expected_array =
      Take(manager_.MakeArraySort(z.Borrow(), fp.Borrow()));
  EXPECT_EQ(instance.Borrow().GetDomain()[0], expected_u.Borrow());
  EXPECT_EQ(instance.Borrow().GetDomain()[1], v.Borrow());
  EXPECT_EQ(instance.Borrow().GetDomain()[2], expected_array.Borrow());
}

TEST_F(Datatype, BorrowedRecipeIdentitiesAreStableAndGroupLocal) {
  const auto build = [](NodeManager& manager) {
    const std::array headers{DatatypeHeader{"Pair", 1}};
    auto builder = Take(manager.BeginDatatypeGroup(headers));
    const auto parameter = Take(builder.Parameter(0, 0));
    const auto shared = Take(builder.Array(parameter, parameter));
    const std::array fields{DatatypeFieldSpec{"left", shared},
                            DatatypeFieldSpec{"right", shared}};
    Take(builder.AddConstructor(0, "pair", fields));
    return Take(builder.Commit());
  };
  const auto first = build(manager_);
  const auto second = build(manager_);
  auto other_manager = Take(NodeManager::Create());
  const auto foreign = build(other_manager);
  const auto ctor = Take(first.Borrow().GetConstructor(0, 0));
  const auto left = Take(ctor.GetDomainRecipe(0));
  const auto right = Take(ctor.GetDomainRecipe(1));
  EXPECT_EQ(left.GetId(), right.GetId());
  EXPECT_NE(left.GetId(), Take(left.Argument(0)).GetId());
  EXPECT_EQ(Take(left.Argument(0)).GetId(), Take(left.Argument(1)).GetId());
  const auto second_ctor = Take(second.Borrow().GetConstructor(0, 0));
  const auto foreign_ctor = Take(foreign.Borrow().GetConstructor(0, 0));
  EXPECT_NE(first, second);
  EXPECT_FALSE(first.Borrow().SameManager(foreign.Borrow()));
  EXPECT_EQ(left.GetId(), Take(second_ctor.GetDomainRecipe(0)).GetId());
  EXPECT_EQ(left.GetId(), Take(foreign_ctor.GetDomainRecipe(0)).GetId());
}

TEST_F(Datatype, ClassifiesValidationFailuresAndAllowsRepair) {
  const auto expect_reason = [](const auto& result, auto reason) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDatatype);
    EXPECT_EQ(result.error().datatype_reason, reason);
  };
  expect_reason(manager_.BeginDatatypeGroup({}), DatatypeReason::kEmptyGroup);
  constexpr std::array headers{DatatypeHeader{"A", 1}, DatatypeHeader{"B", 1}};
  auto missing = Take(manager_.BeginDatatypeGroup(headers));
  expect_reason(missing.Commit(), DatatypeReason::kNoConstructor);
  const auto unrelated = missing.Parameter(7, 0);
  ASSERT_FALSE(unrelated);
  EXPECT_EQ(unrelated.error().code, ErrorCode::kInvalidParameter);
  EXPECT_EQ(unrelated.error().datatype_reason, DatatypeReason::kNone);

  auto wrong = Take(manager_.BeginDatatypeGroup(headers));
  const auto parameter = Take(wrong.Parameter(1, 0));
  const std::array wrong_fields{DatatypeFieldSpec{"belongsToB", parameter}};
  Take(wrong.AddConstructor(0, "wrong", wrong_fields));
  Take(wrong.AddConstructor(1, "b", {}));
  expect_reason(wrong.Commit(), DatatypeReason::kInvalidParameterScope);

  constexpr std::array one{DatatypeHeader{"Loop", 0}};
  auto nested = Take(manager_.BeginDatatypeGroup(one));
  const auto reference = Take(nested.Recursive(0, {}));
  const auto boolean = Take(manager_.BoolSort());
  const auto bool_recipe = Take(nested.Concrete(boolean.Borrow()));
  const auto array = Take(nested.Array(bool_recipe, reference));
  const std::array nested_fields{DatatypeFieldSpec{"underArray", array}};
  Take(nested.AddConstructor(0, "nest", nested_fields));
  expect_reason(nested.Commit(), DatatypeReason::kNestedRecursiveSort);

  auto cycle = Take(manager_.BeginDatatypeGroup(one));
  const std::array loop_fields{
      DatatypeFieldSpec{"next", Take(cycle.Recursive(0, {}))}};
  Take(cycle.AddConstructor(0, "loop", loop_fields));
  expect_reason(cycle.Commit(), DatatypeReason::kNonproductive);
  Take(cycle.AddConstructor(0, "base", {}));
  const auto repaired = Take(cycle.Commit());
  EXPECT_EQ(Take(repaired.Borrow().GetConstructorCount(0)), 2);
}

TEST_F(Datatype, ProductivityRequiresEveryFieldAndCanBeRepaired) {
  auto builder = Take(manager_.BeginDatatypeGroup(std::array{
      DatatypeHeader{"A", 0}, DatatypeHeader{"B", 0}, DatatypeHeader{"C", 0}}));
  const auto a = Take(builder.Recursive(0, {}));
  const auto b = Take(builder.Recursive(1, {}));
  const auto c = Take(builder.Recursive(2, {}));
  Take(builder.AddConstructor(
      0, "from_bc",
      std::array{DatatypeFieldSpec{"b", b}, DatatypeFieldSpec{"c", c}}));
  Take(builder.AddConstructor(1, "base_b", {}));
  Take(builder.AddConstructor(2, "from_a",
                              std::array{DatatypeFieldSpec{"a", a}}));
  const auto invalid = builder.Commit();
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().datatype_reason, DatatypeReason::kNonproductive);
  Take(builder.AddConstructor(2, "base_c", {}));
  const auto group = Take(builder.Commit());
  EXPECT_EQ(Take(group.Borrow().GetFieldCount(0, 0)), 2);
  EXPECT_EQ(Take(group.Borrow().GetConstructorCount(2)), 2);
}

TEST_F(Datatype, ReverseOrderDependencyChainPublishesAllSignatures) {
  constexpr std::uint64_t kCount = 128;
  std::vector<DatatypeHeader> headers(kCount, DatatypeHeader{"D", 0});
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  for (std::uint64_t i = 0; i + 1 < kCount; ++i) {
    const auto dependent = Take(builder.Recursive(i + 1, {}));
    Take(builder.AddConstructor(
        i, "next", std::array{DatatypeFieldSpec{"value", dependent}}));
  }
  Take(builder.AddConstructor(kCount - 1, "base", {}));
  const auto group = Take(builder.Commit());
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), kCount);
  for (std::uint64_t i = 0; i + 1 < kCount; ++i) {
    const auto declaration = Take(group.Borrow().GetConstructor(i, 0));
    const auto instance =
        Take(manager_.InstantiateDeclaration(declaration, {}));
    const auto next = Take(group.Borrow().GetSortConstructor(i + 1));
    ASSERT_EQ(instance.Borrow().GetDomain().size(), 1);
    EXPECT_EQ(instance.Borrow().GetDomain()[0].GetSortConstructor(), next);
  }
}

}  // namespace dandelion::testing
