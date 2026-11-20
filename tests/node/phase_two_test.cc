#include "test_support.h"

#include "../util/numeric_test_peer.h"

namespace dandelion::testing {
namespace {

using numeric::NumericTestPeer;

class PhaseTwo : public NodeTest {
 protected:
  DeclInstance Function(std::span<const SortView> domain, SortView range) {
    const auto declaration = Take(manager_.DeclareFunction("f", domain, range));
    return Take(manager_.InstantiateDeclaration(declaration.Borrow(), {}));
  }
};

template <std::size_t Size>
auto ObserveInputs(NodeManager& manager, std::array<Node, Size>& inputs) {
  std::array<NodeView, Size> views;
  std::array<std::uint64_t, Size> counts{};
  for (std::size_t i = 0; i < Size; ++i) {
    views[i] = inputs[i].Borrow();
    if (!views[i].IsNull()) counts[i] = NodeTestPeer::RefCount(inputs[i]);
  }
  const auto stats = NodeTestPeer::GetStats(manager);
  const auto storage = Take(manager.GetStorageStats());
  const auto state_refs = NodeTestPeer::StateRefCount(manager);
  return [&, views, counts, stats, storage, state_refs] {
    for (std::size_t i = 0; i < Size; ++i) {
      EXPECT_EQ(inputs[i].Borrow(), views[i]) << i;
      if (!inputs[i].Borrow().IsNull()) {
        EXPECT_EQ(NodeTestPeer::RefCount(inputs[i]), counts[i]) << i;
      }
    }
    const auto after = NodeTestPeer::GetStats(manager);
    const auto current = Take(manager.GetStorageStats());
    EXPECT_EQ(after.live_nodes, stats.live_nodes);
    EXPECT_EQ(after.next_node_id, stats.next_node_id);
    EXPECT_EQ(current.live_edges, storage.live_edges);
    EXPECT_EQ(current.node_bytes, storage.node_bytes);
    EXPECT_EQ(current.numeric_backing_bytes, storage.numeric_backing_bytes);
    EXPECT_EQ(NodeTestPeer::StateRefCount(manager), state_refs);
  };
}

void ExpectConsumed(std::span<const Node> inputs) {
  for (const auto& input : inputs) EXPECT_TRUE(input.Borrow().IsNull());
}

TEST_F(PhaseTwo, CheckOwningOperandsPreservesInputsAndDiagnostics) {
  const auto bv8 = Bv(8);
  const auto bv4 = Bv(4);
  std::array inputs{Take(manager_.MakeBvWordValue(bv8.Borrow(), 1)),
                    Take(manager_.MakeBvWordValue(bv8.Borrow(), 2))};
  const BuiltinOpView op{Kind::kBvAdd, {}, {}, {}};
  const auto check = ObserveInputs(manager_, inputs);
  EXPECT_EQ(Take(manager_.CheckBuiltinOperands(op, inputs)), bv8);
  check();
  inputs[1] = Take(manager_.MakeBvWordValue(bv4.Borrow(), 2));
  const auto check_error = ObserveInputs(manager_, inputs);
  const std::array views{inputs[0].Borrow(), inputs[1].Borrow()};
  const auto expected = manager_.CheckBuiltin(op, views);
  const auto actual = manager_.CheckBuiltinOperands(op, inputs);
  ASSERT_FALSE(expected);
  ASSERT_FALSE(actual);
  EXPECT_EQ(actual.error().code, expected.error().code);
  EXPECT_EQ(actual.error().resource, expected.error().resource);
  EXPECT_EQ(actual.error().operand_index, expected.error().operand_index);
  EXPECT_EQ(actual.error().parameter_index, expected.error().parameter_index);
  check_error();
}

TEST_F(PhaseTwo, ConsumeMiss) {
  // N-CONSUME-MISS
  auto sort = Bv(65);
  const BuiltinOpView op{Kind::kBvSub, {}, {}, {}};
  const auto build = [&] {
    const auto n = Number("18446744073709551633");
    const auto left = ExactBv(65, n.Borrow());
    const auto right = ExactBv(65, IntegerView(3));
    std::array inputs{
        Take(manager_.MakeBvValue(sort.Borrow(), left.Borrow())),
        Take(manager_.MakeBvValue(sort.Borrow(), right.Borrow()))};
    const std::array ordered{inputs[0].Borrow(), inputs[1].Borrow()};
    for (const auto& input : inputs)
      EXPECT_EQ(NodeTestPeer::RefCount(input), 1);
    auto parent = Take(manager_.MakeBuiltinConsuming(op, inputs));
    ExpectConsumed(inputs);
    EXPECT_EQ(parent.Borrow().GetSort(), sort.Borrow());
    ExpectOrderedViews(parent.Borrow().GetChildren(), ordered);
    for (std::size_t i = 0; i < inputs.size(); ++i)
      EXPECT_EQ(NodeTestPeer::RefCount(Take(parent.Child(i))), 2);
    return parent;
  };
  auto parent = build();
  const auto id = parent.GetId();
  auto kept = Take(parent.Child(0));
  parent = {};
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 1);
  EXPECT_EQ(NodeTestPeer::RefCount(kept), 1);
  kept = {};
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);

  parent = build();
  EXPECT_GT(parent.GetId(), id);
  const auto destroyed = NodeTestPeer::DestroyedStates();
  sort = {};
  manager_ = {};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), destroyed);
  const auto children = parent.Borrow().GetChildren();
  EXPECT_EQ(parent.Borrow().GetSort().GetWidth(), 65);
  ASSERT_EQ(children.size(), 2);
  EXPECT_EQ(children[0].GetSort(), parent.Borrow().GetSort());
  EXPECT_EQ(children[1].GetSort(), parent.Borrow().GetSort());
  const auto wide = *children[0].GetValue()->GetBitVector();
  EXPECT_EQ(wide.Width(), 65);
  EXPECT_EQ(wide.Chunk(0), 17);
  EXPECT_EQ(wide.Chunk(1), 1);
  EXPECT_EQ(children[1].GetValue()->GetBitVector()->TryUint64(), 3);
  parent = {};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), destroyed + 1);
}

TEST_F(PhaseTwo, ConsumeHit) {
  // N-CONSUME-HIT
  const auto x = Take(manager_.MakeBool(false));
  const auto y = Take(manager_.MakeBool(true));
  const BuiltinOpView op{Kind::kImplies, {}, {}, {}};
  const std::array views{x.Borrow(), y.Borrow()};
  for (const bool borrowed_first : {true, false}) {
    SCOPED_TRACE(borrowed_first);
    std::array inputs{x, y};
    Node parent;
    if (borrowed_first) {
      parent = Take(manager_.MakeBuiltin(op, views));
      const auto unchanged = ObserveInputs(manager_, inputs);
      const auto hit = Take(manager_.MakeBuiltinConsuming(op, inputs));
      EXPECT_EQ(hit, parent);
      EXPECT_EQ(NodeTestPeer::RefCount(parent), 2);
      unchanged();
    } else {
      parent = Take(manager_.MakeBuiltinConsuming(op, inputs));
      ExpectConsumed(inputs);
      const auto hit = Take(manager_.MakeBuiltin(op, views));
      EXPECT_EQ(hit, parent);
      EXPECT_EQ(NodeTestPeer::RefCount(parent), 2);
      inputs = {x, y};
    }
    const auto unchanged = ObserveInputs(manager_, inputs);
    const auto before = NodeTestPeer::GetStats(manager_);
    const auto rebuilt =
        Take(manager_.RebuildNodeConsuming(parent.Borrow(), inputs));
    EXPECT_EQ(rebuilt, parent);
    EXPECT_EQ(NodeTestPeer::RefCount(parent), 2);
    unchanged();
    const auto after = NodeTestPeer::GetStats(manager_);
    EXPECT_EQ(after.hash_computations, before.hash_computations);
    EXPECT_EQ(after.probe_slots, before.probe_slots);
    EXPECT_EQ(after.allocation_attempts, before.allocation_attempts);
    inputs = {};
    parent = {};
    ExpectOrderedViews(rebuilt.Borrow().GetChildren(), views);
  }

  const auto declaration = Function({}, x.Borrow().GetSort());
  const auto nullary = Take(manager_.MakeApply(declaration.Borrow(), {}));
  const auto hit = Take(manager_.MakeApplyConsuming(declaration.Borrow(), {}));
  const auto rebuilt =
      Take(manager_.RebuildNodeConsuming(nullary.Borrow(), {}));
  EXPECT_EQ(hit, nullary);
  EXPECT_EQ(rebuilt, nullary);
  EXPECT_EQ(NodeTestPeer::RefCount(nullary), 3);
  const auto literal = Take(manager_.RebuildNodeConsuming(x.Borrow(), {}));
  EXPECT_EQ(literal, x);
}

TEST_F(PhaseTwo, ConsumeDuplicate) {
  // N-CONSUME-DUPLICATE
  const BuiltinOpView op{Kind::kAnd, {}, {}, {}};
  for (const bool independent : {true, false}) {
    SCOPED_TRACE(independent);
    auto child = Take(manager_.MakeBool(true));
    std::array inputs{child, child};
    const std::array views{child.Borrow(), child.Borrow()};
    if (!independent) child = {};
    const auto before = NodeTestPeer::RefCount(inputs[0]);
    const auto unchanged = ObserveInputs(manager_, inputs);
    NodeTestPeer::FailAllocationAfter(manager_, 0);
    const auto failure = manager_.MakeBuiltinConsuming(op, inputs);
    NodeTestPeer::ClearFailures(manager_);
    Resource(failure, ResourceKind::kCppAllocation);
    unchanged();

    auto parent = Take(manager_.MakeBuiltinConsuming(op, inputs));
    ExpectConsumed(inputs);
    ExpectOrderedViews(parent.Borrow().GetChildren(), views);
    EXPECT_EQ(NodeTestPeer::RefCount(Take(parent.Child(0))), before + 1);
    inputs = std::array{Take(parent.Child(0)), Take(parent.Child(1))};
    const auto on_hit = ObserveInputs(manager_, inputs);
    EXPECT_EQ(Take(manager_.MakeBuiltinConsuming(op, inputs)), parent);
    on_hit();
    inputs = {};
    parent = {};
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, independent ? 1 : 0);
    if (independent) {
      EXPECT_EQ(NodeTestPeer::RefCount(child), before - 2);
    }
  }
}

TEST_F(PhaseTwo, ConsumePayload) {
  // N-CONSUME-PAYLOAD
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow(), "x");
  const auto y = Const(bv8.Borrow(), "y");
  const auto check = [&](const auto& borrowed, const auto& consuming,
                         SortView sort, auto old_inputs, auto new_inputs,
                         const auto& payload) {
    const auto views_of = [](const auto& inputs) {
      std::array<NodeView, std::tuple_size_v<std::decay_t<decltype(inputs)>>>
          views;
      for (std::size_t i = 0; i < inputs.size(); ++i)
        views[i] = inputs[i].Borrow();
      return views;
    };
    const auto old_views = views_of(old_inputs);
    const auto new_views = views_of(new_inputs);
    auto source = Take(consuming(old_inputs));
    ExpectConsumed(old_inputs);
    EXPECT_EQ(source, Take(borrowed(old_views)));
    EXPECT_EQ(source.Borrow().GetSort(), sort);
    ExpectOrderedViews(source.Borrow().GetChildren(), old_views);
    payload(source.Borrow());
    const auto retry_inputs = new_inputs;
    const auto rebuilt =
        Take(manager_.RebuildNodeConsuming(source.Borrow(), new_inputs));
    ExpectConsumed(new_inputs);
    EXPECT_NE(rebuilt, source);
    EXPECT_EQ(NodeTestPeer::StructuralHash(rebuilt),
              NodeTestPeer::StructuralHash(source));
    EXPECT_EQ(rebuilt, Take(borrowed(new_views)));
    auto hit_inputs = retry_inputs;
    const auto unchanged = ObserveInputs(manager_, hit_inputs);
    EXPECT_EQ(Take(consuming(hit_inputs)), rebuilt);
    EXPECT_EQ(Take(manager_.RebuildNodeConsuming(source.Borrow(), hit_inputs)),
              rebuilt);
    unchanged();
    source = {};
    EXPECT_EQ(rebuilt.Borrow().GetSort(), sort);
    ExpectOrderedViews(rebuilt.Borrow().GetChildren(), new_views);
    payload(rebuilt.Borrow());
    return rebuilt;
  };
  const auto builtin = [&](BuiltinOpView op, SortView sort, auto old_inputs,
                           auto new_inputs, const auto& payload) {
    return check([&](auto args) { return manager_.MakeBuiltin(op, args); },
                 [&](auto& args) -> Result<Node> {
                   return manager_.MakeBuiltinConsuming(op, args);
                 },
                 sort, std::move(old_inputs), std::move(new_inputs), payload);
  };
  builtin({Kind::kBvSub, {}, {}, {}}, bv8.Borrow(), std::array{x, y},
          std::array{y, x},
          [](NodeView node) { EXPECT_EQ(node.GetKind(), Kind::kBvSub); });
  const std::array wide{Number("18446744073709551617")};
  const auto exact = builtin({Kind::kBvRotateLeft, wide, {}, {}}, bv8.Borrow(),
                             std::array{x}, std::array{y}, [](NodeView node) {
                               ASSERT_EQ(node.GetIndices().size(), 1);
                               EXPECT_EQ(node.GetIndices()[0].ToDecimal(),
                                         "18446744073709551617");
                             });
  const std::array words{std::uint64_t{UINT32_MAX} + 1};
  const std::array word_as_exact{Number("4294967296")};
  const auto word = check(
      [&](auto args) {
        return manager_.MakeBuiltin(
            {Kind::kBvRotateLeft, word_as_exact, {}, {}}, args);
      },
      [&](auto& args) -> Result<Node> {
        return manager_.MakeWordIndexedBuiltinConsuming(Kind::kBvRotateLeft,
                                                        words, args);
      },
      bv8.Borrow(), std::array{x}, std::array{y},
      [](NodeView node) {
        ASSERT_EQ(node.GetIndices().size(), 1);
        EXPECT_EQ(node.GetIndices()[0].ToDecimal(), "4294967296");
      });
  EXPECT_NE(exact, word);
  EXPECT_EQ(NodeTestPeer::StructuralHash(exact),
            NodeTestPeer::StructuralHash(word));

  const auto integer = Take(manager_.IntSort());
  const auto array =
      Take(manager_.MakeArraySort(integer.Borrow(), bv8.Borrow()));
  builtin({Kind::kArrayConst, {}, array.Borrow(), {}}, array.Borrow(),
          std::array{x}, std::array{y}, [&](NodeView node) {
            EXPECT_EQ(node.GetSort().GetArrayIndex(), integer.Borrow());
            EXPECT_EQ(node.GetSort().GetArrayElement(), bv8.Borrow());
          });
  const auto frac = Take(Rational::Make(IntegerView(2), IntegerView(3)));
  const std::array format{Integer(8), Integer(24)};
  const auto fp = Fp(8, 24);
  const auto rne = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const auto rtz = Take(manager_.MakeRoundingMode(RoundingMode::kRtz));
  builtin({Kind::kFpFromRational, format, {}, frac.Borrow()}, fp.Borrow(),
          std::array{rne}, std::array{rtz}, [](NodeView node) {
            ASSERT_EQ(node.GetIndices().size(), 2);
            EXPECT_EQ(node.GetIndices()[0].ToDecimal(), "8");
            EXPECT_EQ(node.GetIndices()[1].ToDecimal(), "24");
            const auto rational = node.GetRationalParameter();
            ASSERT_TRUE(rational);
            EXPECT_EQ(rational->Numerator().ToDecimal(), "2");
            EXPECT_EQ(rational->Denominator().ToDecimal(), "3");
          });

  const auto apply = [&](DeclInstanceView declaration, SortView sort,
                         std::span<const SortView> type_arguments) {
    return check(
        [&](auto args) { return manager_.MakeApply(declaration, args); },
        [&](auto& args) -> Result<Node> {
          return manager_.MakeApplyConsuming(declaration, args);
        },
        sort, std::array{x}, std::array{y},
        [&](NodeView node) {
          ASSERT_EQ(node.GetDeclaration(), declaration);
          ExpectOrderedViews(node.GetDeclaration()->GetTypeArguments(),
                             type_arguments);
        });
  };
  const std::array type_args{bv8.Borrow()};
  const auto function = Function(type_args, integer.Borrow());
  apply(function.Borrow(), integer.Borrow(), {});
  auto builder =
      Take(manager_.BeginDatatypeGroup(std::array{DatatypeHeader{"Box", 1}}));
  const auto parameter = Take(builder.Parameter(0, 0));
  Take(builder.AddConstructor(
      0, "box", std::array{DatatypeFieldSpec{"value", parameter}}));
  const auto group = Take(builder.Commit());
  const auto box = Take(manager_.InstantiateSort(
      Take(group.Borrow().GetSortConstructor(0)), type_args));
  const auto constructor = Take(manager_.InstantiateDeclaration(
      Take(group.Borrow().GetConstructor(0, 0)), type_args));
  apply(constructor.Borrow(), box.Borrow(), type_args);
}

TEST_F(PhaseTwo, ConsumeValidate) {
  // N-CONSUME-VALIDATE
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow());
  const auto y = Const(bv8.Borrow());
  const auto boolean = Take(manager_.MakeBool(true));
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.MakeBool(true));
  const auto foreign_sort = foreign.GetSort();
  const auto sort = boolean.GetSort();
  const auto declaration =
      Function(std::array{bv8.Borrow(), bv8.Borrow()}, bv8.Borrow());
  const auto external_decl =
      Take(other.DeclareFunction("f", {}, foreign_sort.Borrow()));
  const auto external =
      Take(other.InstantiateDeclaration(external_decl.Borrow(), {}));
  const BuiltinOpView sub{Kind::kBvSub, {}, {}, {}};
  const std::array width{std::uint64_t{8}};
  const auto round = Take(manager_.MakeRoundingMode(RoundingMode::kRtz));
  const auto fp = Const(View(Fp(8, 24)));
  const auto original =
      Take(manager_.MakeBuiltin(sub, std::array{x.Borrow(), x.Borrow()}));
  const auto reject = [&](const auto& make, auto inputs, ErrorCode code,
                          std::uint64_t operand = UINT64_MAX,
                          std::uint64_t parameter = UINT64_MAX) {
    const auto unchanged = ObserveInputs(manager_, inputs);
    const auto result = make(inputs);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_EQ(result.error().operand_index, operand);
    EXPECT_EQ(result.error().parameter_index, parameter);
    unchanged();
  };
  const auto binary = [&](const auto& make, const Node& first,
                          const Node& second, const Node& wrong_sort) {
    reject(make, std::array{first, Node{}}, ErrorCode::kNullObject, 1);
    reject(make, std::array{first, foreign}, ErrorCode::kForeignManager, 1);
    reject(make, std::array{first, wrong_sort}, ErrorCode::kSortMismatch, 1);
    reject(make, std::array{first}, ErrorCode::kInvalidArity);
    std::array retry{first, second};
    const auto parent = Take(make(retry));
    ExpectConsumed(retry);
    ExpectOrderedViews(parent.Borrow().GetChildren(),
                       std::array{first.Borrow(), second.Borrow()});
  };
  const auto builtin = [&](BuiltinOpView op) {
    return [&, op](std::span<Node> inputs) {
      return manager_.MakeBuiltinConsuming(op, inputs);
    };
  };
  const auto word = [&](Kind kind, std::span<const std::uint64_t> indices) {
    return [&, kind, indices](std::span<Node> inputs) {
      return manager_.MakeWordIndexedBuiltinConsuming(kind, indices, inputs);
    };
  };
  const auto rebuild = [&](NodeView source) {
    return [&, source](std::span<Node> inputs) {
      return manager_.RebuildNodeConsuming(source, inputs);
    };
  };
  const auto apply = [&](DeclInstanceView instance) {
    return [&, instance](std::span<Node> inputs) {
      return manager_.MakeApplyConsuming(instance, inputs);
    };
  };
  binary(builtin(sub), x, y, boolean);
  binary(word(Kind::kFpToSbv, width), round, fp, boolean);
  binary(rebuild(original.Borrow()), x, y, boolean);
  binary(apply(declaration.Borrow()), x, y, boolean);

  reject(builtin({Kind::kApply, {}, {}, {}}), std::array{x, y},
         ErrorCode::kInvalidKind);
  const std::array negative{Integer(-1)};
  reject(builtin({Kind::kBvRotateLeft, negative, {}, {}}), std::array{x},
         ErrorCode::kInvalidParameter, UINT64_MAX, 0);
  reject(builtin({Kind::kArrayConst, {}, {}, {}}), std::array{x},
         ErrorCode::kInvalidParameter);
  reject(builtin({Kind::kArrayConst, {}, SortView{}, {}}), std::array{x},
         ErrorCode::kNullObject);
  reject(builtin({Kind::kArrayConst, {}, foreign_sort.Borrow(), {}}),
         std::array{x}, ErrorCode::kForeignManager);
  reject(builtin({Kind::kArrayConst, {}, sort.Borrow(), {}}), std::array{x},
         ErrorCode::kSortMismatch);
  const auto frac = Take(Rational::Make(IntegerView(2), IntegerView(3)));
  const std::array format{Integer(8), Integer(24)};
  reject(builtin({Kind::kFpFromRational, format, {}, {}}), std::array{round},
         ErrorCode::kInvalidParameter);
  reject(builtin({Kind::kBvSub, {}, {}, frac.Borrow()}), std::array{x, y},
         ErrorCode::kInvalidParameter);
  reject(word(Kind::kApply, width), std::array{x}, ErrorCode::kInvalidKind);
  reject(word(Kind::kBvExtract, width), std::array{x},
         ErrorCode::kInvalidParameter);
  const std::array reversed{std::uint64_t{0}, std::uint64_t{7}};
  reject(word(Kind::kBvExtract, reversed), std::array{x},
         ErrorCode::kInvalidParameter);
  reject(rebuild({}), std::array{x, y}, ErrorCode::kNullObject);
  reject(rebuild(foreign.Borrow()), std::array{x, y},
         ErrorCode::kForeignManager);
  reject(apply({}), std::array{x, y}, ErrorCode::kNullObject);
  reject(apply(external.Borrow()), std::array{x, y},
         ErrorCode::kForeignManager);
}

TEST_F(PhaseTwo, ConsumeAlloc) {
  // N-CONSUME-ALLOC
  for (const bool growth : {false, true}) {
    for (const bool rebuild : {false, true}) {
      for (const auto decimal : {"2", "18446744073709551617"}) {
        SCOPED_TRACE(growth);
        SCOPED_TRACE(rebuild);
        SCOPED_TRACE(decimal);
        const std::size_t allocations = growth ? 2 : 1;
        for (std::size_t checkpoint = 0; checkpoint <= allocations;
             ++checkpoint) {
          SCOPED_TRACE(checkpoint);
          auto manager = Take(NodeManager::Create());
          NodeTestPeer::ForceCollisions(manager, 15);
          const auto sort =
              Take(manager.MakeFpSort(IntegerView(8), IntegerView(24)));
          const auto numerator = Number(decimal);
          const auto fraction =
              Take(Rational::Make(numerator.Borrow(), IntegerView(5)));
          const std::array format{Integer(8), Integer(24)};
          const BuiltinOpView op{
              Kind::kFpFromRational, format, {}, fraction.Borrow()};
          const auto round = Take(manager.MakeRoundingMode(RoundingMode::kRtz));
          const auto neighbor =
              Take(manager.MakeRoundingMode(RoundingMode::kRne));
          Node original;
          if (rebuild)
            original =
                Take(manager.MakeBuiltin(op, std::array{neighbor.Borrow()}));
          std::array inputs{round};
          std::vector<Node> fillers;
          const std::int64_t filler_count = rebuild ? 9 : 10;
          for (std::int64_t i = 0; growth && i < filler_count; ++i)
            fillers.push_back(Take(manager.MakeInt(IntegerView(i))));
          const auto make = [&] {
            return rebuild
                       ? manager.RebuildNodeConsuming(original.Borrow(), inputs)
                       : manager.MakeBuiltinConsuming(op, inputs);
          };
          const auto before = NodeTestPeer::GetStats(manager);
          const auto unchanged = ObserveInputs(manager, inputs);
          if (growth && checkpoint == 0) {
            NodeTestPeer::SetTableCapacityLimit(manager, 16);
            const auto capacity = make();
            NodeTestPeer::SetTableCapacityLimit(manager, SIZE_MAX);
            Resource(capacity, ResourceKind::kSize);
            unchanged();
          }
          if (checkpoint < allocations) {
            NodeTestPeer::FailAllocationAfter(manager, checkpoint);
          } else {
            NodeTestPeer::FailRetainAfter(manager, 0);
          }
          NumericTestPeer::FailAllocationAfter(0);
          const auto failure = make();
          NumericTestPeer::ClearFailures();
          NodeTestPeer::ClearFailures(manager);
          Resource(failure, checkpoint < allocations
                                ? ResourceKind::kCppAllocation
                                : ResourceKind::kReferenceCount);
          unchanged();
          const auto after = NodeTestPeer::GetStats(manager);
          EXPECT_EQ(after.node_allocations - before.node_allocations,
                    after.candidate_frees - before.candidate_frees);
          EXPECT_EQ(Take(manager.MakeRoundingMode(RoundingMode::kRne)),
                    neighbor);
          EXPECT_EQ(NodeTestPeer::RefCount(neighbor), rebuild ? 2 : 1);
          if (rebuild) {
            EXPECT_EQ(
                Take(manager.MakeBuiltin(op, std::array{neighbor.Borrow()})),
                original);
          }
          for (std::size_t i = 0; i < fillers.size(); ++i)
            EXPECT_EQ(Take(manager.MakeInt(
                          IntegerView(static_cast<std::int64_t>(i)))),
                      fillers[i]);
          EXPECT_EQ(fraction.Borrow().Numerator().ToDecimal(), decimal);
          const auto parent = Take(make());
          ExpectConsumed(inputs);
          EXPECT_EQ(parent.GetId(), before.next_node_id);
          EXPECT_EQ(parent.GetSort(), sort);
          EXPECT_EQ(NodeTestPeer::RefCount(round), 2);
          ASSERT_TRUE(parent.Borrow().GetRationalParameter());
          EXPECT_EQ(
              parent.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
              decimal);
          EXPECT_EQ(
              parent.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
              "5");
          EXPECT_EQ(Take(manager.MakeBuiltin(op, std::array{round.Borrow()})),
                    parent);

          inputs[0] = round;
          const auto hit_unchanged = ObserveInputs(manager, inputs);
          NodeTestPeer::FailRetainAfter(manager, 0);
          const auto hit_failure = make();
          NodeTestPeer::ClearFailures(manager);
          Resource(hit_failure, ResourceKind::kReferenceCount);
          hit_unchanged();
          EXPECT_EQ(Take(make()), parent);
          hit_unchanged();
        }
      }
    }
  }

  // Derived width and candidate preparation with two edges to the same child.
  const BuiltinOpView concat{Kind::kBvConcat, {}, {}, {}};
  for (std::size_t checkpoint = 0; checkpoint <= 5; ++checkpoint) {
    SCOPED_TRACE(checkpoint);
    auto manager = Take(NodeManager::Create());
    const auto bv = Take(manager.MakeBvSort(7));
    const auto child = Take(manager.MakeBvWordValue(bv.Borrow(), 21));
    std::array inputs{child, child};
    const auto unchanged = ObserveInputs(manager, inputs);
    NodeTestPeer::FailAllocationAfter(manager, checkpoint);
    auto result = manager.MakeBuiltinConsuming(concat, inputs);
    NodeTestPeer::ClearFailures(manager);
    if (checkpoint < 5) {
      Resource(result, ResourceKind::kCppAllocation);
      unchanged();
      result = manager.MakeBuiltinConsuming(concat, inputs);
    }
    const auto parent = Take(std::move(result));
    ExpectConsumed(inputs);
    EXPECT_EQ(parent.GetId(), 2);
    EXPECT_EQ(parent.Borrow().GetSort().GetWidth(), 14);
    ExpectOrderedViews(parent.Borrow().GetChildren(),
                       std::array{child.Borrow(), child.Borrow()});
    EXPECT_EQ(NodeTestPeer::RefCount(child), 3);
  }

  // Applications carry a declaration payload even with repeated operands.
  const auto child = Take(manager_.MakeBool(true));
  const auto sort = child.Borrow().GetSort();
  const auto declaration = Function(std::array{sort, sort}, sort);
  std::array inputs{child, child};
  const auto unchanged = ObserveInputs(manager_, inputs);
  for (std::size_t checkpoint = 0; checkpoint < 2; ++checkpoint) {
    SCOPED_TRACE(checkpoint);
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    const auto failed =
        manager_.MakeApplyConsuming(declaration.Borrow(), inputs);
    NodeTestPeer::ClearFailures(manager_);
    Resource(failed, ResourceKind::kCppAllocation);
    unchanged();
  }
  const auto applied =
      Take(manager_.MakeApplyConsuming(declaration.Borrow(), inputs));
  ExpectConsumed(inputs);
  EXPECT_EQ(applied.GetId(), 2);
  EXPECT_EQ(applied.Borrow().GetDeclaration(), declaration.Borrow());
  ExpectOrderedViews(applied.Borrow().GetChildren(),
                     std::array{child.Borrow(), child.Borrow()});
}

TEST_F(PhaseTwo, ConsumeRetain) {
  // N-CONSUME-RETAIN
  const auto x = Take(manager_.MakeBool(true));
  const auto y = Take(manager_.MakeBool(false));
  const auto declaration =
      Function(std::array{x.Borrow().GetSort(), x.Borrow().GetSort()},
               x.Borrow().GetSort());
  const std::array views{x.Borrow(), x.Borrow()};
  std::array inputs{x, x};
  const auto unchanged = ObserveInputs(manager_, inputs);
  NodeTestPeer::FailRetainAfter(manager_, 0);
  const auto state_failure =
      manager_.MakeApplyConsuming(declaration.Borrow(), inputs);
  NodeTestPeer::ClearFailures(manager_);
  Resource(state_failure, ResourceKind::kReferenceCount);
  unchanged();
  NodeTestPeer::FailRetainAfter(manager_, 1);
  const auto borrowed_failure = manager_.MakeApply(declaration.Borrow(), views);
  NodeTestPeer::ClearFailures(manager_);
  Resource(borrowed_failure, ResourceKind::kReferenceCount);
  unchanged();

  NodeTestPeer::FailRetainAfter(manager_, 1);
  auto result = manager_.MakeApplyConsuming(declaration.Borrow(), inputs);
  NodeTestPeer::ClearFailures(manager_);
  auto parent = Take(std::move(result));
  ExpectConsumed(inputs);
  EXPECT_EQ(parent.GetId(), y.GetId() + 1);
  EXPECT_EQ(NodeTestPeer::RefCount(x), 3);
  const auto parent_refs = NodeTestPeer::RefCount(parent);
  inputs = {x, x};
  const auto on_hit = ObserveInputs(manager_, inputs);
  for (const bool rebuild : {false, true}) {
    SCOPED_TRACE(rebuild);
    NodeTestPeer::FailRetainAfter(manager_, 0);
    const auto failed =
        rebuild ? manager_.RebuildNodeConsuming(parent.Borrow(), inputs)
                : manager_.MakeApplyConsuming(declaration.Borrow(), inputs);
    NodeTestPeer::ClearFailures(manager_);
    Resource(failed, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::RefCount(parent), parent_refs);
    on_hit();
  }

  inputs = {};
  parent = {};
  EXPECT_EQ(NodeTestPeer::RefCount(x), 1);
  parent = Take(manager_.MakeApply(declaration.Borrow(), views));
  EXPECT_EQ(NodeTestPeer::RefCount(x), 3);
  parent = {};
  EXPECT_EQ(NodeTestPeer::RefCount(x), 1);
}

TEST_F(PhaseTwo, ConsumeBounds) {
  // N-CONSUME-BOUNDS
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow());
  const auto y = Const(bv8.Borrow());
  const std::array wide{Number("18446744073709551617")};
  const BuiltinOpView rotate{Kind::kBvRotateLeft, wide, {}, {}};
  std::array inputs{Op(rotate.kind, {x.Borrow()}, wide)};
  const auto source = inputs[0].Borrow();
  EXPECT_EQ(NodeTestPeer::RefCount(inputs[0]), 1);
  {
    const auto unchanged = ObserveInputs(manager_, inputs);
    NodeTestPeer::SetSizeLimit(manager_, 1);
    const auto failed = manager_.RebuildNodeConsuming(source, inputs);
    NodeTestPeer::SetSizeLimit(manager_, SIZE_MAX);
    Resource(failed, ResourceKind::kSize);
    unchanged();
  }

  // The sole source owner is also the replacement. Its borrowed index is
  // needed to construct the new parent.
  NodeTestPeer::SetNextNodeId(manager_, UINT64_MAX);
  const auto parent = Take(manager_.RebuildNodeConsuming(source, inputs));
  ExpectConsumed(inputs);
  EXPECT_EQ(parent.GetId(), UINT64_MAX);
  ASSERT_EQ(parent.Borrow().GetIndices().size(), 1);
  EXPECT_EQ(parent.Borrow().GetIndices()[0].ToDecimal(),
            "18446744073709551617");
  ExpectOrderedViews(parent.Borrow().GetChildren(), std::array{source});
  EXPECT_EQ(NodeTestPeer::RefCount(Take(parent.Child(0))), 2);
  EXPECT_EQ(source.GetChildren()[0], x.Borrow());
  EXPECT_EQ(Take(manager_.MakeBuiltin(rotate, std::array{source})), parent);

  inputs[0] = y;
  const auto miss = ObserveInputs(manager_, inputs);
  Resource(manager_.MakeBuiltinConsuming(rotate, inputs),
           ResourceKind::kIdentitySpace);
  miss();
  inputs[0] = Take(parent.Child(0));
  const auto hit = ObserveInputs(manager_, inputs);
  NodeTestPeer::FailRetainAfter(manager_, 1);
  auto result = manager_.MakeBuiltinConsuming(rotate, inputs);
  NodeTestPeer::ClearFailures(manager_);
  EXPECT_EQ(Take(std::move(result)), parent);
  hit();
}

}  // namespace
}  // namespace dandelion::testing
