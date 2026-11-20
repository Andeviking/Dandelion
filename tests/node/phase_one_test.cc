#include "test_support.h"

#include "../util/numeric_test_peer.h"

namespace dandelion::testing {
namespace {

using numeric::NumericTestPeer;

class PhaseOne : public NodeTest {};

void ExpectUnpublished(NodeManager& manager, const NodeTestStats& before,
                       const NodeStorageStats& storage,
                       std::uint64_t state_refs) {
  const auto after = NodeTestPeer::GetStats(manager);
  EXPECT_EQ(after.live_nodes, before.live_nodes);
  EXPECT_EQ(after.next_node_id, before.next_node_id);
  EXPECT_EQ(after.sorts, before.sorts);
  const auto current = Take(manager.GetStorageStats());
  EXPECT_EQ(current.live_edges, storage.live_edges);
  EXPECT_EQ(current.node_bytes, storage.node_bytes);
  EXPECT_EQ(current.numeric_backing_bytes, storage.numeric_backing_bytes);
  EXPECT_EQ(NodeTestPeer::StateRefCount(manager), state_refs);
}

void ExpectIndices(NodeView node, std::span<const std::string_view> expected) {
  const auto indices = node.GetIndices();
  ASSERT_EQ(indices.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(indices[i].ToDecimal(), expected[i]) << i;
}

void ExpectRational(NodeView node, std::string_view numerator,
                    std::string_view denominator) {
  const auto rational = node.GetRationalParameter();
  ASSERT_TRUE(rational);
  EXPECT_EQ(rational->Numerator().ToDecimal(), numerator);
  EXPECT_EQ(rational->Denominator().ToDecimal(), denominator);
}

TEST_F(PhaseOne, UnchangedRebuildOnlyRetainsOriginal) {
  // N-REBUILD-01
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow(), "x");
  const auto y = Const(bv8.Borrow(), "y");
  const auto sub = Op(Kind::kBvSub, {x.Borrow(), y.Borrow()});
  const auto value = ExactBv(8, IntegerView(42));
  const auto literal = Take(manager_.MakeBvValue(bv8.Borrow(), value.Borrow()));
  auto attached = Take(NodeManager::Attach(Take(manager_.RetainState())));
  const auto check = [&](NodeManager& facade, const Node& original,
                         std::span<const NodeView> children) {
    const auto before = NodeTestPeer::GetStats(manager_);
    const auto types = NodeTestPeer::TypeEvaluations(manager_);
    const auto shapes = NodeTestPeer::BuiltinShapeChecks(manager_);
    const auto state_refs = NodeTestPeer::StateRefCount(manager_);
    const auto original_refs = NodeTestPeer::RefCount(original);
    const auto x_refs = NodeTestPeer::RefCount(x);
    const auto y_refs = NodeTestPeer::RefCount(y);
    const auto expect_counters = [&] {
      const auto after = NodeTestPeer::GetStats(manager_);
      EXPECT_EQ(after.probe_histogram, before.probe_histogram);
      EXPECT_EQ(after.probe_slots, before.probe_slots);
      EXPECT_EQ(after.insertion_searches, before.insertion_searches);
      EXPECT_EQ(after.allocation_attempts, before.allocation_attempts);
      EXPECT_EQ(after.hash_computations, before.hash_computations);
      EXPECT_EQ(after.next_node_id, before.next_node_id);
      EXPECT_EQ(after.live_nodes, before.live_nodes);
      EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager_), types);
      EXPECT_EQ(NodeTestPeer::BuiltinShapeChecks(manager_), shapes);
      EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), state_refs);
      EXPECT_EQ(NodeTestPeer::RefCount(x), x_refs);
      EXPECT_EQ(NodeTestPeer::RefCount(y), y_refs);
    };
    {
      const auto rebuilt =
          Take(facade.RebuildNode(original.Borrow(), children));
      EXPECT_EQ(rebuilt, original);
      EXPECT_EQ(NodeTestPeer::RefCount(original), original_refs + 1);
      expect_counters();
    }
    NodeTestPeer::FailRetainAfter(manager_, 0);
    const auto failed = facade.RebuildNode(original.Borrow(), children);
    NodeTestPeer::ClearFailures(manager_);
    Resource(failed, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::RefCount(original), original_refs);
    expect_counters();
  };
  check(attached, sub, std::array{x.Borrow(), y.Borrow()});
  check(manager_, literal, {});
  const auto nullary = Const(bv8.Borrow(), "nullary");
  ASSERT_EQ(nullary.Borrow().GetKind(), Kind::kApply);
  check(manager_, nullary, {});
}

TEST_F(PhaseOne, ChangedRebuildPreservesPayloadAndOrdinaryIdentity) {
  // N-REBUILD-02
  for (const bool collisions : {false, true}) {
    for (const bool ordinary_first : {false, true}) {
      SCOPED_TRACE(collisions);
      SCOPED_TRACE(ordinary_first);
      auto manager = Take(NodeManager::Create());
      if (collisions) NodeTestPeer::ForceCollisions(manager, 15);
      const auto bv8 = Take(manager.MakeBvSort(IntegerView(8)));
      const auto bv4 = Take(manager.MakeBvSort(IntegerView(4)));
      const auto x = Take(manager.MakeFreshConstant("x", bv8.Borrow()));
      const auto y = Take(manager.MakeFreshConstant("y", bv8.Borrow()));
      const std::array x_only{x.Borrow()};
      const std::array y_only{y.Borrow()};
      const auto check = [&](Kind kind, SortView sort, const auto& make,
                             const auto& old_children, const auto& children,
                             const auto& expect_payload) {
        SCOPED_TRACE(static_cast<int>(kind));
        auto original = Take(make(old_children));
        Node target;
        if (ordinary_first) target = Take(make(children));
        std::vector<Node> replacement_owners;
        for (const auto child : children)
          replacement_owners.push_back(Take(manager.RetainNode(child)));
        std::vector<std::uint64_t> expected_refs;
        for (const auto& child : replacement_owners) {
          const auto other_edges = static_cast<std::uint64_t>(
              std::ranges::count(old_children, child.Borrow()));
          const auto new_edges = static_cast<std::uint64_t>(
              std::ranges::count(children, child.Borrow()));
          expected_refs.push_back(NodeTestPeer::RefCount(child) - other_edges +
                                  (ordinary_first ? 0 : new_edges));
        }
        const auto types = NodeTestPeer::TypeEvaluations(manager);
        const auto shapes = NodeTestPeer::BuiltinShapeChecks(manager);
        const auto original_refs = NodeTestPeer::RefCount(original);
        const auto before = NodeTestPeer::GetStats(manager);
        const auto rebuilt =
            Take(manager.RebuildNode(original.Borrow(), children));
        const auto on_repeat = NodeTestPeer::GetStats(manager);
        EXPECT_EQ(on_repeat.next_node_id,
                  before.next_node_id + (ordinary_first ? 0 : 1));
        EXPECT_EQ(Take(manager.RebuildNode(original.Borrow(), children)),
                  rebuilt);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).allocation_attempts,
                  on_repeat.allocation_attempts);
        EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager), types);
        EXPECT_EQ(NodeTestPeer::BuiltinShapeChecks(manager), shapes);
        EXPECT_EQ(NodeTestPeer::RefCount(original), original_refs);
        EXPECT_NE(rebuilt, original);
        EXPECT_EQ(original.Borrow().GetSort(), sort);
        ExpectOrderedViews(original.Borrow().GetChildren(), old_children);
        expect_payload(original.Borrow());
        if (!ordinary_first) target = Take(make(children));
        EXPECT_EQ(rebuilt, target);
        target = {};
        original = {};
        for (std::size_t i = 0; i < replacement_owners.size(); ++i)
          EXPECT_EQ(NodeTestPeer::RefCount(replacement_owners[i]),
                    expected_refs[i]);
        EXPECT_EQ(rebuilt.Borrow().GetKind(), kind);
        EXPECT_EQ(rebuilt.Borrow().GetSort(), sort);
        ExpectOrderedViews(rebuilt.Borrow().GetChildren(), children);
        expect_payload(rebuilt.Borrow());
        return rebuilt;
      };
      const auto builtin = [&](BuiltinOpView op, SortView sort,
                               const auto& old_children, const auto& children,
                               const auto& verify) {
        return check(
            op.kind, sort,
            [&](const auto& args) { return manager.MakeBuiltin(op, args); },
            old_children, children, verify);
      };
      const auto sub = builtin(
          {Kind::kBvSub, {}, {}, {}}, bv8.Borrow(),
          std::array{x.Borrow(), y.Borrow()},
          std::array{y.Borrow(), x.Borrow()},
          [](NodeView node) { EXPECT_TRUE(node.GetIndices().empty()); });
      EXPECT_NE(sub, x);
      const std::array extract{Integer(5), Integer(2)};
      const std::array other_extract{Integer(6), Integer(3)};
      const auto other_slice = Take(manager.MakeBuiltin(
          {Kind::kBvExtract, other_extract, {}, {}}, y_only));
      const auto slice = builtin(
          {Kind::kBvExtract, extract, {}, {}}, bv4.Borrow(), x_only, y_only,
          [](NodeView node) {
            ExpectIndices(node, std::array<std::string_view, 2>{"5", "2"});
          });
      EXPECT_NE(slice, other_slice);
      const std::array rotate{Number("18446744073709551617")};
      const std::array rotate_one{Integer(1)};
      const auto small_rotate = Take(manager.MakeBuiltin(
          {Kind::kBvRotateLeft, rotate_one, {}, {}}, y_only));
      const auto wide_rotate = builtin(
          {Kind::kBvRotateLeft, rotate, {}, {}}, bv8.Borrow(), x_only, y_only,
          [](NodeView node) {
            ExpectIndices(
                node, std::array<std::string_view, 1>{"18446744073709551617"});
          });
      EXPECT_NE(wide_rotate, small_rotate);

      const auto int_sort = Take(manager.IntSort());
      const auto bool_sort = Take(manager.BoolSort());
      const auto array =
          Take(manager.MakeArraySort(int_sort.Borrow(), bv8.Borrow()));
      const auto other_array =
          Take(manager.MakeArraySort(bool_sort.Borrow(), bv8.Borrow()));
      const auto other_const = Take(manager.MakeBuiltin(
          {Kind::kArrayConst, {}, other_array.Borrow(), {}}, y_only));
      const auto constant = builtin(
          {Kind::kArrayConst, {}, array.Borrow(), {}}, array.Borrow(), x_only,
          y_only, [&](NodeView node) {
            EXPECT_EQ(node.GetSort().GetArrayIndex(), int_sort.Borrow());
            EXPECT_EQ(node.GetSort().GetArrayElement(), bv8.Borrow());
          });
      EXPECT_NE(constant, other_const);

      const auto rne = Take(manager.MakeRoundingMode(RoundingMode::kRne));
      const auto rtz = Take(manager.MakeRoundingMode(RoundingMode::kRtz));
      const std::array format{Integer(8), Integer(24)};
      const auto fp =
          Take(manager.MakeFpSort(format[0].Borrow(), format[1].Borrow()));
      const auto two_thirds =
          Take(Rational::Make(IntegerView(2), IntegerView(3)));
      const auto three_fourths =
          Take(Rational::Make(IntegerView(3), IntegerView(4)));
      const auto other_conversion = Take(manager.MakeBuiltin(
          {Kind::kFpFromRational, format, {}, three_fourths.Borrow()},
          std::array{rtz.Borrow()}));
      const auto conversion = builtin(
          {Kind::kFpFromRational, format, {}, two_thirds.Borrow()}, fp.Borrow(),
          std::array{rne.Borrow()}, std::array{rtz.Borrow()},
          [](NodeView node) {
            ExpectIndices(node, std::array<std::string_view, 2>{"8", "24"});
            ExpectRational(node, "2", "3");
          });
      EXPECT_NE(conversion, other_conversion);

      auto builder = Take(
          manager.BeginDatatypeGroup(std::array{DatatypeHeader{"List", 1}}));
      const auto parameter = Take(builder.Parameter(0, 0));
      const auto tail = Take(builder.Recursive(0, std::array{parameter}));
      Take(builder.AddConstructor(0, "nil", {}));
      Take(builder.AddConstructor(
          0, "cons",
          std::array{DatatypeFieldSpec{"head", parameter},
                     DatatypeFieldSpec{"tail", tail}}));
      const auto group = Take(builder.Commit());
      const std::array type_args{bv8.Borrow()};
      const auto list_sort = Take(manager.InstantiateSort(
          Take(group.Borrow().GetSortConstructor(0)), type_args));
      const auto nil_instance = Take(manager.InstantiateDeclaration(
          Take(group.Borrow().GetConstructor(0, 0)), type_args));
      const auto cons = Take(manager.InstantiateDeclaration(
          Take(group.Borrow().GetConstructor(0, 1)), type_args));
      const auto nil = Take(manager.MakeApply(nil_instance.Borrow(), {}));
      const auto other_decl = Take(manager.DeclareFunction(
          "cons", std::array{bv8.Borrow(), list_sort.Borrow()},
          list_sort.Borrow()));
      const auto other_instance =
          Take(manager.InstantiateDeclaration(other_decl.Borrow(), {}));
      const std::array new_args{y.Borrow(), nil.Borrow()};
      const auto other_apply =
          Take(manager.MakeApply(other_instance.Borrow(), new_args));
      const auto apply = check(
          Kind::kApply, list_sort.Borrow(),
          [&](const auto& args) {
            return manager.MakeApply(cons.Borrow(), args);
          },
          std::array{x.Borrow(), nil.Borrow()}, new_args,
          [&](NodeView node) {
            ASSERT_EQ(node.GetDeclaration(), cons.Borrow());
            ExpectOrderedViews(node.GetDeclaration()->GetTypeArguments(),
                               type_args);
          });
      EXPECT_NE(apply, other_apply);
    }
  }
}

TEST_F(PhaseOne, InvalidReplacementsLeaveOriginalAndStateIntact) {
  // N-REBUILD-03
  const auto bv8 = Bv(8);
  const auto bv4 = Bv(4);
  const auto x = Const(bv8.Borrow());
  const auto y = Const(bv8.Borrow());
  const auto narrow = Const(bv4.Borrow());
  auto other = Take(NodeManager::Create());
  const auto other_sort = Take(other.MakeBvSort(IntegerView(8)));
  const auto foreign = Take(other.MakeFreshConstant("x", other_sort.Borrow()));
  const std::array children{x.Borrow(), y.Borrow()};
  const auto original = Op(Kind::kBvSub, {x.Borrow(), y.Borrow()});
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto storage = Take(manager_.GetStorageStats());
  const auto state_refs = NodeTestPeer::StateRefCount(manager_);
  const auto foreign_refs = NodeTestPeer::StateRefCount(other);
  const auto check = [&](NodeView source, std::span<const NodeView> args,
                         ErrorCode code, std::uint64_t operand = UINT64_MAX) {
    const auto result = manager_.RebuildNode(source, args);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_EQ(result.error().operand_index, operand);
    ExpectUnpublished(manager_, before, storage, state_refs);
    EXPECT_EQ(NodeTestPeer::StateRefCount(other), foreign_refs);
    EXPECT_EQ(NodeTestPeer::RefCount(original), 1);
    EXPECT_EQ(NodeTestPeer::RefCount(x), 2);
    EXPECT_EQ(NodeTestPeer::RefCount(y), 2);
    EXPECT_EQ(NodeTestPeer::RefCount(narrow), 1);
    EXPECT_EQ(NodeTestPeer::RefCount(foreign), 1);
    ExpectOrderedViews(original.Borrow().GetChildren(), children);
  };
  check(original.Borrow(), std::array{x.Borrow()}, ErrorCode::kInvalidArity);
  check(original.Borrow(), std::array{x.Borrow(), y.Borrow(), x.Borrow()},
        ErrorCode::kInvalidArity);
  check(original.Borrow(), std::array{x.Borrow(), narrow.Borrow()},
        ErrorCode::kSortMismatch, 1);
  check({}, children, ErrorCode::kNullObject);
  check(original.Borrow(), std::array{x.Borrow(), NodeView{}},
        ErrorCode::kNullObject, 1);
  check(foreign.Borrow(), {}, ErrorCode::kForeignManager);
  check(original.Borrow(), std::array{x.Borrow(), foreign.Borrow()},
        ErrorCode::kForeignManager, 1);
  check(x.Borrow(), std::array{y.Borrow()}, ErrorCode::kInvalidArity);
}

TEST_F(PhaseOne, RationalRebuildRollsBackEveryPublicationCheckpoint) {
  for (const bool growth : {false, true}) {
    for (const auto decimal : {"2", "18446744073709551617"}) {
      SCOPED_TRACE(growth);
      SCOPED_TRACE(decimal);
      const std::size_t allocations = growth ? 2 : 1;
      for (std::size_t failure = 0; failure < allocations + 2; ++failure) {
        SCOPED_TRACE(failure);
        auto manager = Take(NodeManager::Create());
        const auto rne = Take(manager.MakeRoundingMode(RoundingMode::kRne));
        const auto rtz = Take(manager.MakeRoundingMode(RoundingMode::kRtz));
        const std::array format{Integer(8), Integer(24)};
        const auto numerator = Number(decimal);
        const auto fraction =
            Take(Rational::Make(numerator.Borrow(), IntegerView(3)));
        const BuiltinOpView op{
            Kind::kFpFromRational, format, {}, fraction.Borrow()};
        const std::array source_children{rne.Borrow()};
        const auto original = Take(manager.MakeBuiltin(op, source_children));
        std::vector<Node> fillers;
        for (std::int64_t value = 0; growth && value < 9; ++value)
          fillers.push_back(Take(manager.MakeInt(IntegerView(value))));
        const auto before = NodeTestPeer::GetStats(manager);
        ASSERT_EQ(before.table_capacity, 16);
        ASSERT_EQ(before.live_nodes, growth ? 12 : 3);
        const auto storage = Take(manager.GetStorageStats());
        const auto state_refs = NodeTestPeer::StateRefCount(manager);
        const auto resource = failure < allocations
                                  ? ResourceKind::kCppAllocation
                                  : ResourceKind::kReferenceCount;
        if (failure < allocations) {
          NodeTestPeer::FailAllocationAfter(manager, failure);
        } else {
          NodeTestPeer::FailRetainAfter(manager, failure - allocations);
        }
        const std::array children{rtz.Borrow()};
        NumericTestPeer::FailAllocationAfter(0);
        const auto failed = manager.RebuildNode(original.Borrow(), children);
        NumericTestPeer::ClearFailures();
        NodeTestPeer::ClearFailures(manager);
        Resource(failed, resource);
        ExpectUnpublished(manager, before, storage, state_refs);
        const auto after = NodeTestPeer::GetStats(manager);
        EXPECT_EQ(after.node_allocations - before.node_allocations,
                  after.candidate_frees - before.candidate_frees);
        EXPECT_EQ(NodeTestPeer::RefCount(original), 1);
        EXPECT_EQ(NodeTestPeer::RefCount(rne), 2);
        EXPECT_EQ(NodeTestPeer::RefCount(rtz), 1);
        EXPECT_EQ(Take(manager.MakeBuiltin(op, source_children)), original);
        for (std::size_t i = 0; i < fillers.size(); ++i)
          EXPECT_EQ(
              Take(manager.MakeInt(IntegerView(static_cast<std::int64_t>(i)))),
              fillers[i]);
        ExpectRational(original.Borrow(), decimal, "3");
        const auto retry =
            Take(manager.RebuildNode(original.Borrow(), children));
        EXPECT_EQ(retry.GetId(), before.next_node_id);
        EXPECT_EQ(retry.Borrow().GetSort(), original.Borrow().GetSort());
        ExpectOrderedViews(retry.Borrow().GetChildren(), children);
        ExpectIndices(retry.Borrow(),
                      std::array<std::string_view, 2>{"8", "24"});
        ExpectRational(retry.Borrow(), decimal, "3");
        EXPECT_EQ(NodeTestPeer::RefCount(rtz), 2);
        EXPECT_EQ(Take(manager.MakeBuiltin(op, children)), retry);
      }
    }
  }
}

TEST_F(PhaseOne, WordIndicesMatchExactKeysInBothInsertionOrders) {
  // N-WORD-INDEX-01
  for (const bool colliding : {false, true}) {
    for (const bool word_first : {false, true}) {
      SCOPED_TRACE(colliding);
      SCOPED_TRACE(word_first);
      auto manager = Take(NodeManager::Create());
      if (colliding) NodeTestPeer::ForceCollisions(manager, 15);
      const auto bv8 = Take(manager.MakeBvSort(IntegerView(8)));
      const auto bv4 = Take(manager.MakeBvSort(IntegerView(4)));
      const auto boolean = Take(manager.BoolSort());
      const auto x = Take(manager.MakeFreshConstant("x", bv8.Borrow()));
      const auto integer = Take(manager.MakeInt(IntegerView(2)));
      const std::array child{x.Borrow()};
      const auto check = [&](Kind kind, auto words, auto decimals,
                             SortView sort, const auto& children) {
        SCOPED_TRACE(static_cast<int>(kind));
        std::array<Integer, std::tuple_size_v<decltype(words)>> exact;
        for (std::size_t i = 0; i < words.size(); ++i)
          exact[i] = Number(decimals[i]);
        const auto word = [&] {
          return manager.MakeWordIndexedBuiltin(kind, words, children);
        };
        const auto general = [&] {
          return manager.MakeBuiltin({kind, exact, {}, {}}, children);
        };
        const auto first = Take(word_first ? word() : general());
        const auto before = NodeTestPeer::GetStats(manager);
        NodeTestPeer::FailAllocationAfter(manager, 0);
        NumericTestPeer::FailAllocationAfter(0);
        const auto second = word_first ? general() : word();
        NumericTestPeer::ClearFailures();
        NodeTestPeer::ClearFailures(manager);
        EXPECT_EQ(Take(second), first);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).allocation_attempts,
                  before.allocation_attempts);
        EXPECT_EQ(first.Borrow().GetSort(), sort);
        EXPECT_EQ(first.Borrow().GetKind(), kind);
        ExpectOrderedViews(first.Borrow().GetChildren(), children);
        ExpectIndices(first.Borrow(), decimals);
        return first;
      };
      const auto upper =
          check(Kind::kBvExtract, std::array<std::uint64_t, 2>{7, 4},
                std::array<std::string_view, 2>{"7", "4"}, bv4.Borrow(), child);
      const auto lower =
          check(Kind::kBvExtract, std::array<std::uint64_t, 2>{6, 3},
                std::array<std::string_view, 2>{"6", "3"}, bv4.Borrow(), child);
      EXPECT_NE(upper, lower);
      const std::array<std::uint64_t, 5> rotations{
          0, 1, UINT32_MAX, std::uint64_t{UINT32_MAX} + 1, UINT64_MAX};
      const std::array<std::string_view, 5> decimals{
          "0", "1", "4294967295", "4294967296", "18446744073709551615"};
      std::vector<Node> rotated;
      for (std::size_t i = 0; i < rotations.size(); ++i) {
        auto node = check(Kind::kBvRotateLeft, std::array{rotations[i]},
                          std::array{decimals[i]}, bv8.Borrow(), child);
        for (const auto& prior : rotated) EXPECT_NE(node, prior);
        rotated.push_back(std::move(node));
      }
      const std::array huge{Number("18446744073709551617")};
      const auto wide =
          Take(manager.MakeBuiltin({Kind::kBvRotateLeft, huge, {}, {}}, child));
      EXPECT_NE(wide, rotated[1]);
      check(Kind::kIntDivisible, std::array<std::uint64_t, 1>{UINT64_MAX},
            std::array{decimals.back()}, boolean.Borrow(),
            std::array{integer.Borrow()});
    }
  }
}

TEST_F(PhaseOne, WordIndexErrorsMatchExactFactoryUnderCollisions) {
  // N-WORD-INDEX-02
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow());
  const auto boolean = Take(manager_.MakeBool(false));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.MakeFreshConstant(
      "x", View(Take(other.MakeBvSort(IntegerView(8))))));
  const std::array child{x.Borrow()};
  const std::array<std::uint64_t, 2> bounds{7, 4};
  const auto upper =
      Take(manager_.MakeWordIndexedBuiltin(Kind::kBvExtract, bounds, child));
  const auto lower = Take(manager_.MakeWordIndexedBuiltin(
      Kind::kBvExtract, std::array<std::uint64_t, 2>{6, 3}, child));
  EXPECT_NE(upper, lower);
  const auto not_x =
      Take(manager_.MakeWordIndexedBuiltin(Kind::kBvNot, {}, child));
  EXPECT_EQ(not_x, Op(Kind::kBvNot, {x.Borrow()}));
  EXPECT_EQ(not_x.Borrow().GetSort(), bv8.Borrow());
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto storage = Take(manager_.GetStorageStats());
  const auto refs = NodeTestPeer::StateRefCount(manager_);
  const auto check = [&](Kind kind, std::initializer_list<std::uint64_t> words,
                         std::span<const NodeView> children, ErrorCode code,
                         ResourceKind resource = ResourceKind::kNone) {
    SCOPED_TRACE(static_cast<int>(kind));
    std::vector<Integer> exact;
    for (const auto word : words) exact.push_back(Number(std::to_string(word)));
    const auto word_result =
        manager_.MakeWordIndexedBuiltin(kind, words, children);
    const auto exact_result =
        manager_.MakeBuiltin({kind, exact, {}, {}}, children);
    for (const auto* result : {&word_result, &exact_result}) {
      ASSERT_FALSE(*result);
      EXPECT_EQ(result->error().code, code);
      EXPECT_EQ(result->error().resource, resource);
    }
    EXPECT_EQ(word_result.error().operand_index,
              exact_result.error().operand_index);
    EXPECT_EQ(word_result.error().parameter_index,
              exact_result.error().parameter_index);
    ExpectUnpublished(manager_, before, storage, refs);
  };
  check(Kind::kBvExtract, {7}, child, ErrorCode::kInvalidParameter);
  check(Kind::kBvExtract, {4, 7}, child, ErrorCode::kInvalidParameter);
  check(Kind::kBvExtract, {8, 4}, child, ErrorCode::kInvalidParameter);
  check(Kind::kBvRepeat, {0}, child, ErrorCode::kInvalidParameter);
  check(Kind::kArrayConst, {}, child, ErrorCode::kInvalidParameter);
  check(Kind::kFpFromRational, {8, 24}, std::array{rm.Borrow()},
        ErrorCode::kInvalidParameter);
  check(Kind::kBvZeroExtend, {UINT64_MAX}, child, ErrorCode::kResourceLimit,
        ResourceKind::kSize);
  check(Kind::kInvalid, {}, child, ErrorCode::kInvalidKind);
  check(Kind::kBvExtract, {7, 4}, std::array{x.Borrow(), x.Borrow()},
        ErrorCode::kInvalidArity);
  check(Kind::kBvExtract, {7, 4}, std::array{boolean.Borrow()},
        ErrorCode::kSortMismatch);
  check(Kind::kBvExtract, {7, 4}, std::array{NodeView{}},
        ErrorCode::kNullObject);
  check(Kind::kBvExtract, {7, 4}, std::array{foreign.Borrow()},
        ErrorCode::kForeignManager);
  check(Kind::kBvNot, {1}, child, ErrorCode::kInvalidParameter);
  EXPECT_EQ(
      Take(manager_.MakeWordIndexedBuiltin(Kind::kBvExtract, bounds, child)),
      upper);
}

TEST_F(PhaseOne, WordBvValuesShareExactKeysAndFitChecks) {
  // N-BV-WORD-01
  for (const bool word_first : {false, true}) {
    SCOPED_TRACE(word_first);
    auto manager = Take(NodeManager::Create());
    NodeTestPeer::ForceCollisions(manager, 15);
    const auto bv1 = Take(manager.MakeBvSort(IntegerView(1)));
    const auto bv8 = Take(manager.MakeBvSort(IntegerView(8)));
    const auto bv63 = Take(manager.MakeBvSort(IntegerView(63)));
    const auto bv64 = Take(manager.MakeBvSort(IntegerView(64)));
    const auto bv65 = Take(manager.MakeBvSort(IntegerView(65)));
    const auto boolean = Take(manager.BoolSort());
    auto other = Take(NodeManager::Create());
    const auto foreign = Take(other.MakeBvSort(IntegerView(8)));
    const auto check = [&](SortView sort, std::uint64_t word,
                           std::string_view decimal) {
      const auto integer = Number(decimal);
      const auto exact = ExactBv(*sort.GetWidth(), integer.Borrow());
      const auto from_word = [&] {
        return manager.MakeBvWordValue(sort, word);
      };
      const auto from_exact = [&] {
        return manager.MakeBvValue(sort, exact.Borrow());
      };
      const auto first = Take(word_first ? from_word() : from_exact());
      EXPECT_EQ(Take(word_first ? from_exact() : from_word()), first);
      EXPECT_EQ(first.Borrow().GetKind(), Kind::kBvValue);
      EXPECT_EQ(first.Borrow().GetSort(), sort);
      const auto value = first.Borrow().GetValue()->GetBitVector();
      EXPECT_EQ(value->Width(), sort.GetWidth());
      EXPECT_EQ(value->TryUint64(), word);
      return first;
    };
    const auto zero = check(bv1.Borrow(), 0, "0");
    const auto one = check(bv1.Borrow(), 1, "1");
    const auto byte = check(bv8.Borrow(), 255, "255");
    const auto narrow = check(bv63.Borrow(), INT64_MAX, "9223372036854775807");
    const auto full = check(bv64.Borrow(), UINT64_MAX, "18446744073709551615");
    const auto wide = check(bv65.Borrow(), UINT64_MAX, "18446744073709551615");
    EXPECT_NE(zero, one);
    EXPECT_NE(full, wide);
    EXPECT_NE(byte, narrow);
    const auto before = NodeTestPeer::GetStats(manager);
    const auto storage = Take(manager.GetStorageStats());
    const auto refs = NodeTestPeer::StateRefCount(manager);
    const auto reject = [&](SortView sort, std::uint64_t word, ErrorCode code) {
      Error(manager.MakeBvWordValue(sort, word), code);
      ExpectUnpublished(manager, before, storage, refs);
    };
    reject(bv1.Borrow(), 2, ErrorCode::kInvalidLiteral);
    reject(bv8.Borrow(), 256, ErrorCode::kInvalidLiteral);
    reject(bv63.Borrow(), std::uint64_t{1} << 63, ErrorCode::kInvalidLiteral);
    reject(boolean.Borrow(), 0, ErrorCode::kSortMismatch);
    reject({}, 0, ErrorCode::kNullObject);
    reject(foreign.Borrow(), 0, ErrorCode::kForeignManager);
    const auto valid = ExactBv(8, IntegerView(0));
    const auto reject_typed = [&](SortView sort, ErrorCode code) {
      Error(manager.MakeBvValue(sort, valid.Borrow()), code);
      ExpectUnpublished(manager, before, storage, refs);
    };
    reject_typed(bv1.Borrow(), ErrorCode::kSortMismatch);
    reject_typed(boolean.Borrow(), ErrorCode::kSortMismatch);
    reject_typed({}, ErrorCode::kNullObject);
    reject_typed(foreign.Borrow(), ErrorCode::kForeignManager);
  }
}

TEST_F(PhaseOne, ExplicitAndDerivedWidthsShareManagerLocalCache) {
  // N-BV-WIDTH-01
  for (const bool explicit_first : {false, true}) {
    SCOPED_TRACE(explicit_first);
    auto manager = Take(NodeManager::Create());
    const auto bv16 = Take(manager.MakeBvSort(IntegerView(16)));
    const auto child = Take(manager.MakeFreshConstant("x", bv16.Borrow()));
    const auto extract = [&](std::int64_t high, std::int64_t low) {
      const std::array indices{Integer(high), Integer(low)};
      return Take(manager.MakeBuiltin({Kind::kBvExtract, indices, {}, {}},
                                      std::array{child.Borrow()}));
    };
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), 0);
    const auto cold = NodeTestPeer::GetStats(manager).sort_table_lookups;
    Sort bv8;
    Node upper;
    if (explicit_first) {
      bv8 = Take(manager.MakeBvSort(8));
    } else {
      upper = extract(15, 8);
      bv8 = upper.GetSort();
    }
    EXPECT_EQ(NodeTestPeer::GetStats(manager).sort_table_lookups, cold + 1);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), bv8.GetId());
    EXPECT_EQ(bv8.Borrow().GetWidth(), 8);
    const auto warm = NodeTestPeer::GetStats(manager).sort_table_lookups;
    const auto types = NodeTestPeer::TypeEvaluations(manager);
    const auto lower = extract(7, 0);
    EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager), types + 1);
    EXPECT_EQ(lower.Borrow().GetSort(), bv8.Borrow());
    EXPECT_EQ(Take(manager.MakeBvSort(8)), bv8);
    EXPECT_EQ(Take(manager.MakeBvSort(IntegerView(8))), bv8);
    auto attached = Take(NodeManager::Attach(Take(manager.RetainState())));
    EXPECT_EQ(Take(attached.MakeBvSort(8)), bv8);
    EXPECT_EQ(NodeTestPeer::GetStats(manager).sort_table_lookups, warm);

    auto other = Take(NodeManager::Create());
    const auto other_bv8 = Take(other.MakeBvSort(8));
    EXPECT_EQ(other_bv8.Borrow().GetWidth(), 8);
    EXPECT_NE(other_bv8.GetId(), bv8.GetId());
    EXPECT_FALSE(other_bv8.Borrow().SameManager(bv8.Borrow()));
    Error(manager.RetainSort(other_bv8.Borrow()), ErrorCode::kForeignManager);

    const auto bv1 = Take(manager.MakeBvSort(1));
    EXPECT_NE(bv1, Take(manager.BoolSort()));
    const auto bv64 = Take(manager.MakeBvSort(64));
    EXPECT_EQ(bv64.Borrow().GetWidth(), 64);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 64), bv64.GetId());
    const auto after_64 = NodeTestPeer::GetStats(manager).sort_table_lookups;
    EXPECT_EQ(Take(manager.MakeBvSort(64)), bv64);
    EXPECT_EQ(Take(manager.MakeBvSort(IntegerView(64))), bv64);
    EXPECT_EQ(NodeTestPeer::GetStats(manager).sort_table_lookups, after_64);
    for (const auto width : {std::uint64_t{65}, std::uint64_t{UINT32_MAX}}) {
      SCOPED_TRACE(width);
      const auto before = NodeTestPeer::GetStats(manager).sort_table_lookups;
      const auto sort = Take(manager.MakeBvSort(width));
      EXPECT_EQ(sort.Borrow().GetWidth(), width);
      EXPECT_EQ(Take(manager.MakeBvSort(IntegerView::FromUnsigned(width))),
                sort);
      EXPECT_EQ(NodeTestPeer::GetStats(manager).sort_table_lookups, before + 2);
    }
    const auto largest = Take(manager.MakeBvSort(UINT32_MAX));
    const auto large_term =
        Take(manager.MakeFreshConstant("large", largest.Borrow()));
    const auto one_bit = Take(manager.MakeFreshConstant("one", bv1.Borrow()));
    const auto before = NodeTestPeer::GetStats(manager);
    const auto storage = Take(manager.GetStorageStats());
    const auto refs = NodeTestPeer::StateRefCount(manager);
    Error(manager.MakeBvSort(0), ErrorCode::kInvalidParameter);
    Resource(manager.MakeBvSort(std::uint64_t{UINT32_MAX} + 1),
             ResourceKind::kSize);
    Resource(manager.MakeBvSort(UINT64_MAX), ResourceKind::kSize);
    Resource(
        manager.MakeBuiltin({Kind::kBvConcat, {}, {}, {}},
                            std::array{large_term.Borrow(), one_bit.Borrow()}),
        ResourceKind::kSize);
    ExpectUnpublished(manager, before, storage, refs);
  }
}

TEST_F(PhaseOne, WidthCachePublishesOnlyAfterSuccessfulInterning) {
  // N-BV-WIDTH-02
  for (std::size_t failure = 0; failure < 4; ++failure) {
    SCOPED_TRACE(failure);
    auto manager = Take(NodeManager::Create());
    const auto bv16 = Take(manager.MakeBvSort(IntegerView(16)));
    const auto x = Take(manager.MakeFreshConstant("x", bv16.Borrow()));
    const auto extract = [&](std::int64_t high, std::int64_t low) {
      const std::array indices{Integer(high), Integer(low)};
      return manager.MakeBuiltin({Kind::kBvExtract, indices, {}, {}},
                                 std::array{x.Borrow()});
    };
    const auto before = NodeTestPeer::GetStats(manager);
    const auto storage = Take(manager.GetStorageStats());
    const auto refs = NodeTestPeer::StateRefCount(manager);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), 0);
    NodeTestPeer::FailRetainAfter(manager, 0);
    const auto cold_retain = manager.MakeBvSort(8);
    NodeTestPeer::ClearFailures(manager);
    Resource(cold_retain, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), 0);
    ExpectUnpublished(manager, before, storage, refs);

    // Sort record, argument storage, sort vector growth, and index insertion.
    NodeTestPeer::FailAllocationAfter(manager, failure);
    const auto failed = manager.MakeBvSort(8);
    NodeTestPeer::ClearFailures(manager);
    Resource(failed, ResourceKind::kCppAllocation);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), 0);
    ExpectUnpublished(manager, before, storage, refs);

    const auto upper = Take(extract(15, 8));
    const auto bv8 = upper.Borrow().GetSort();
    EXPECT_EQ(bv8.GetId(), before.sorts + 1);
    EXPECT_EQ(bv8.GetWidth(), 8);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), bv8.GetId());
    const auto published = NodeTestPeer::GetStats(manager);
    const auto published_storage = Take(manager.GetStorageStats());
    const auto published_refs = NodeTestPeer::StateRefCount(manager);
    NodeTestPeer::FailRetainAfter(manager, 0);
    const auto warm_retain = manager.MakeBvSort(8);
    NodeTestPeer::ClearFailures(manager);
    Resource(warm_retain, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::CachedBvSortId(manager, 8), bv8.GetId());
    ExpectUnpublished(manager, published, published_storage, published_refs);
    EXPECT_EQ(Take(manager.MakeBvSort(8)).GetId(), bv8.GetId());
    const auto lower = Take(extract(7, 0));
    EXPECT_EQ(lower.Borrow().GetSort(), bv8);
    EXPECT_EQ(NodeTestPeer::GetStats(manager).sort_table_lookups,
              published.sort_table_lookups);
  }
}

TEST_F(PhaseOne, ChildTailRetainFailureRestoresOnlyRetainedEdges) {
  // N-TAIL-01
  const auto a = Take(manager_.MakeBool(false));
  const auto b = Take(manager_.MakeBool(true));
  const BuiltinOpView op{Kind::kAnd, {}, {}, {}};
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto storage = Take(manager_.GetStorageStats());
  const auto refs = NodeTestPeer::StateRefCount(manager_);
  const auto baseline = [&] {
    EXPECT_EQ(NodeTestPeer::RefCount(a), 1);
    EXPECT_EQ(NodeTestPeer::RefCount(b), 1);
    EXPECT_EQ(Take(manager_.MakeBool(false)), a);
    EXPECT_EQ(Take(manager_.MakeBool(true)), b);
  };
  const auto fail = [&](std::span<const NodeView> children,
                        std::size_t checkpoint) {
    SCOPED_TRACE(checkpoint);
    const auto before_failure = NodeTestPeer::GetStats(manager_);
    NodeTestPeer::FailRetainAfter(manager_, checkpoint);
    const auto result = manager_.MakeBuiltin(op, children);
    NodeTestPeer::ClearFailures(manager_);
    Resource(result, ResourceKind::kReferenceCount);
    ExpectUnpublished(manager_, before_failure, storage, refs);
    baseline();
  };
  const auto retry = [&](std::span<const NodeView> children,
                         std::uint64_t a_edges, std::uint64_t b_edges,
                         NodeId id) {
    {
      const auto parent = Take(manager_.MakeBuiltin(op, children));
      EXPECT_EQ(parent.GetId(), id);
      ExpectOrderedViews(parent.Borrow().GetChildren(), children);
      EXPECT_EQ(NodeTestPeer::RefCount(a), 1 + a_edges);
      EXPECT_EQ(NodeTestPeer::RefCount(b), 1 + b_edges);
      EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), refs + 1);
      EXPECT_EQ(Take(manager_.GetStorageStats()).live_edges, a_edges + b_edges);
    }
    baseline();
    EXPECT_EQ(Take(manager_.GetStorageStats()).live_edges, storage.live_edges);
    EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), refs);
  };
  const std::array small{a.Borrow(), b.Borrow(), a.Borrow()};
  for (std::size_t checkpoint = 0; checkpoint <= small.size(); ++checkpoint)
    fail(small, checkpoint);
  retry(small, 2, 1, before.next_node_id);

  const auto external = ExternalRetainChildren(a.Borrow(), b.Borrow());
  fail(external, external.size());
  retry(external, external.size() - 2, 2, before.next_node_id + 1);
}

}  // namespace
}  // namespace dandelion::testing
