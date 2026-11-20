#include "test_support.h"

#include "../util/numeric_test_peer.h"

namespace dandelion::testing {
namespace {

using numeric::NumericTestPeer;

void ExpectErrorDetail(const NodeError& error, ErrorCode code,
                       ResourceKind resource = ResourceKind::kNone,
                       std::uint64_t operand = UINT64_MAX,
                       std::uint64_t parameter = UINT64_MAX) {
  EXPECT_EQ(error.code, code);
  EXPECT_EQ(error.resource, resource);
  EXPECT_EQ(error.operand_index, operand);
  EXPECT_EQ(error.parameter_index, parameter);
  EXPECT_EQ(error.datatype_reason, DatatypeReason::kNone);
}

TEST(NumericTranslation, MapsEachUtilErrorWithAndWithoutPositions) {
  for (const auto error :
       {NumericError::kInvalidText, NumericError::kInvalidArgument,
        NumericError::kValueOutOfRange}) {
    SCOPED_TRACE(static_cast<int>(error));
    ExpectErrorDetail(NodeTestPeer::TranslateNumericError(error, true),
                      ErrorCode::kInvalidLiteral);
    ExpectErrorDetail(NodeTestPeer::TranslateNumericError(error, false),
                      ErrorCode::kInvalidParameter);
    ExpectErrorDetail(NodeTestPeer::TranslateNumericError(error, true, 2),
                      ErrorCode::kInvalidLiteral, ResourceKind::kNone, 2);
    ExpectErrorDetail(
        NodeTestPeer::TranslateNumericError(error, false, UINT64_MAX, 1),
        ErrorCode::kInvalidParameter, ResourceKind::kNone, UINT64_MAX, 1);
  }
  for (const bool literal : {false, true}) {
    ExpectErrorDetail(
        NodeTestPeer::TranslateNumericError(NumericError::kDivisionByZero,
                                            literal, UINT64_MAX, 2),
        ErrorCode::kInvalidParameter, ResourceKind::kNone, UINT64_MAX, 2);
    ExpectErrorDetail(
        NodeTestPeer::TranslateNumericError(NumericError::kSizeLimit, literal),
        ErrorCode::kResourceLimit, ResourceKind::kSize);
    ExpectErrorDetail(NodeTestPeer::TranslateNumericError(
                          NumericError::kCppAllocation, literal, 3, 1),
                      ErrorCode::kResourceLimit, ResourceKind::kCppAllocation,
                      3, 1);
  }
}

TEST_F(NodeTest, CheckedSortWidthsAndWideIndexCopyReportNodeErrors) {
  const auto sort = Bv(65);
  const auto value = ExactBv(65, IntegerView(1));
  const auto narrow = Bv(64);
  const auto operand =
      Take(manager_.MakeBvValue(sort.Borrow(), value.Borrow()));
  const auto wide = Number("18446744073709551617");
  const std::array indices{wide};
  const BuiltinOpView rotation{Kind::kBvRotateLeft, indices, {}, {}};
  const std::array children{operand.Borrow()};
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto storage = Take(manager_.GetStorageStats());
  const auto state_refs = NodeTestPeer::StateRefCount(manager_);

  const auto width_error =
      manager_.MakeBvValue(narrow.Borrow(), value.Borrow());
  ASSERT_FALSE(width_error);
  ExpectErrorDetail(width_error.error(), ErrorCode::kSortMismatch);
  const auto exponent = manager_.MakeFpSort(IntegerView(1), IntegerView(24));
  ASSERT_FALSE(exponent);
  ExpectErrorDetail(exponent.error(), ErrorCode::kInvalidParameter,
                    ResourceKind::kNone, UINT64_MAX, 0);
  const auto significand = manager_.MakeFpSort(IntegerView(8), IntegerView(1));
  ASSERT_FALSE(significand);
  ExpectErrorDetail(significand.error(), ErrorCode::kInvalidParameter,
                    ResourceKind::kNone, UINT64_MAX, 1);
  const auto size =
      manager_.MakeFpSort(IntegerView(UINT32_MAX), IntegerView(2));
  ASSERT_FALSE(size);
  ExpectErrorDetail(size.error(), ErrorCode::kResourceLimit,
                    ResourceKind::kSize);

  NodeTestPeer::SetReferenceLimit(manager_, 1);
  EXPECT_THROW((void)Node(operand), ReferenceLimit);
  NodeTestPeer::SetReferenceLimit(manager_, UINT64_MAX);

  NumericTestPeer::FailAllocationAfter(0);
  const auto copy = manager_.MakeBuiltin(rotation, children);
  NumericTestPeer::ClearFailures();
  ASSERT_FALSE(copy);
  ExpectErrorDetail(copy.error(), ErrorCode::kResourceLimit,
                    ResourceKind::kCppAllocation, UINT64_MAX, 0);
  const auto after = NodeTestPeer::GetStats(manager_);
  const auto current = Take(manager_.GetStorageStats());
  EXPECT_EQ(after.next_node_id, before.next_node_id);
  EXPECT_EQ(after.live_nodes, before.live_nodes);
  EXPECT_EQ(current.live_edges, storage.live_edges);
  EXPECT_EQ(current.node_bytes, storage.node_bytes);
  EXPECT_EQ(current.numeric_backing_bytes, storage.numeric_backing_bytes);
  EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), state_refs);
  EXPECT_EQ(NodeTestPeer::RefCount(operand), 1);
  EXPECT_EQ(after.node_allocations - before.node_allocations,
            after.candidate_frees - before.candidate_frees);
  EXPECT_EQ(Take(manager_.MakeBvValue(sort.Borrow(), value.Borrow())), operand);
  EXPECT_EQ(indices.front().ToDecimal(), "18446744073709551617");
  const auto retry = Take(manager_.MakeBuiltin(rotation, children));
  EXPECT_EQ(retry.GetId(), before.next_node_id);
  EXPECT_EQ(retry.Borrow().GetIndices()[0].ToDecimal(), indices[0].ToDecimal());
}

TEST(NumericSize, RejectsCountConversionAndBytesBeforeAllocation) {
  using Tag = NumericPayloadTag;
  constexpr auto kMaxCount = std::uint64_t{UINT32_MAX};
  const auto check = NodeTestPeer::CheckNumericLayout;
  EXPECT_EQ(Take(check(Tag::kIntWidePositive, 2, 0, 72)), 72);
  Resource(check(Tag::kIntWidePositive, 2, 0, 71), ResourceKind::kSize);
  Resource(check(Tag::kIntWideNegative, kMaxCount + 1, 0, SIZE_MAX),
           ResourceKind::kSize);
  const auto check_tail = NodeTestPeer::CheckNumericTailSize;
  EXPECT_EQ(Take(check_tail(SIZE_MAX - 15, 1, SIZE_MAX)), SIZE_MAX - 7);
  Resource(check_tail(SIZE_MAX - 15, 2, SIZE_MAX), ResourceKind::kSize);
  EXPECT_EQ(Take(check(Tag::kRationalWideNegative, 2, 2, 112)), 112);
  Resource(check(Tag::kRationalWideNegative, 2, 2, 111), ResourceKind::kSize);
  Resource(check(Tag::kRationalWidePositive, 2, kMaxCount + 1, SIZE_MAX),
           ResourceKind::kSize);
  Resource(check(Tag::kRationalWidePositive, UINT64_MAX - 1, 2, SIZE_MAX),
           ResourceKind::kSize);
}

TEST_F(NodeTest, CompactNumericMissesAndHitsRollBackEveryCheckpoint) {
  enum class Form {
    kInt,
    kWideInt,
    kBv,
    kWideBv,
    kFp,
    kWideFp,
    kSpecial,
    kRational,
    kWideRational
  };
  constexpr std::array forms{
      Form::kInt,     Form::kWideInt,  Form::kBv,
      Form::kWideBv,  Form::kFp,       Form::kWideFp,
      Form::kSpecial, Form::kRational, Form::kWideRational};
  constexpr std::array<std::size_t, forms.size()> bytes{64, 72, 64, 72, 64,
                                                        72, 56, 88, 104};
  const auto wide = Number("18446744073709551617");
  const auto bv_word = ExactBv(65, IntegerView(7));
  const auto bv_wide = ExactBv(65, wide.Borrow());
  const auto fp_word = FpBits(Format(8, 65), IntegerView(1));
  const auto fp_wide = FpBits(Format(8, 65), wide.Borrow());
  const auto fp_special = SpecialFp(Format(8, 65), FpSpecial::kNaN);
  const auto compact = Take(Rational::Make(IntegerView(2), IntegerView(3)));
  const auto packed = Take(Rational::Make(wide.Borrow(), IntegerView(3)));
  const std::array format{Integer(8), Integer(65)};

  for (std::size_t i = 0; i < forms.size(); ++i) {
    const auto form = forms[i];
    const bool rational =
        form == Form::kRational || form == Form::kWideRational;
    for (const bool growth : {false, true}) {
      SCOPED_TRACE(static_cast<int>(form));
      SCOPED_TRACE(growth);
      const std::size_t allocation_count = growth ? 2 : 1;
      const std::size_t retain_count = rational ? 2 : 1;
      for (std::size_t failure = 0; failure <= allocation_count + retain_count;
           ++failure) {
        SCOPED_TRACE(failure);
        auto manager = Take(NodeManager::Create());
        NodeTestPeer::ForceCollisions(manager, 15);
        const auto int_sort = Take(manager.IntSort());
        const auto bv_sort = Take(manager.MakeBvSort(65));
        const auto fp_sort =
            Take(manager.MakeFpSort(IntegerView(8), IntegerView(65)));
        const auto rm = Take(manager.MakeRoundingMode(RoundingMode::kRne));
        const auto z = Take(manager.MakeInt(IntegerView(0)));
        std::vector<Node> fillers;
        for (std::int64_t j = 1; growth && j <= 10; ++j)
          fillers.push_back(Take(manager.MakeInt(IntegerView(j))));
        const auto create = [&]() -> Result<Node> {
          switch (form) {
            case Form::kInt:
              return manager.MakeInt(IntegerView(INT64_MIN));
            case Form::kWideInt:
              return manager.MakeInt(wide.Borrow());
            case Form::kBv:
              return manager.MakeBvValue(bv_sort.Borrow(), bv_word.Borrow());
            case Form::kWideBv:
              return manager.MakeBvValue(bv_sort.Borrow(), bv_wide.Borrow());
            case Form::kFp:
              return manager.MakeFpValue(fp_sort.Borrow(), fp_word.Borrow());
            case Form::kWideFp:
              return manager.MakeFpValue(fp_sort.Borrow(), fp_wide.Borrow());
            case Form::kSpecial:
              return manager.MakeFpValue(fp_sort.Borrow(), fp_special.Borrow());
            case Form::kRational:
            case Form::kWideRational:
              return manager.MakeBuiltin(
                  {Kind::kFpFromRational,
                   format,
                   {},
                   form == Form::kRational ? compact.Borrow()
                                           : packed.Borrow()},
                  std::array{rm.Borrow()});
          }
          std::unreachable();
        };
        const auto before = NodeTestPeer::GetStats(manager);
        ASSERT_EQ(before.table_capacity, 16);
        ASSERT_EQ(before.live_nodes, growth ? 12 : 2);
        const auto storage = Take(manager.GetStorageStats());
        const auto state = NodeTestPeer::StateRefCount(manager);
        const auto unchanged = [&] {
          const auto after = NodeTestPeer::GetStats(manager);
          const auto current = Take(manager.GetStorageStats());
          EXPECT_EQ(after.next_node_id, before.next_node_id);
          EXPECT_EQ(after.live_nodes, before.live_nodes);
          EXPECT_EQ(current.live_edges, storage.live_edges);
          EXPECT_EQ(current.node_bytes, storage.node_bytes);
          EXPECT_EQ(current.numeric_backing_bytes,
                    storage.numeric_backing_bytes);
          EXPECT_EQ(NodeTestPeer::StateRefCount(manager), state);
          EXPECT_EQ(NodeTestPeer::RefCount(rm), 1);
          EXPECT_EQ(NodeTestPeer::RefCount(z), 1);
          EXPECT_EQ(after.node_allocations - before.node_allocations,
                    after.candidate_frees - before.candidate_frees);
          EXPECT_EQ(Take(manager.MakeInt(IntegerView(0))), z);
          EXPECT_EQ(Take(manager.MakeRoundingMode(RoundingMode::kRne)), rm);
          for (std::size_t j = 0; j < fillers.size(); ++j)
            EXPECT_EQ(Take(manager.MakeInt(
                          IntegerView(static_cast<std::int64_t>(j + 1)))),
                      fillers[j]);
          EXPECT_EQ(wide.ToDecimal(), "18446744073709551617");
          EXPECT_EQ(bv_wide.Borrow().Chunk(1), 1);
          EXPECT_EQ(fp_wide.Borrow().FiniteBits()->Chunk(1), 1);
          EXPECT_EQ(packed.Borrow().Numerator().ToDecimal(),
                    "18446744073709551617");
        };

        if (failure == 0 && !growth) {
          NodeTestPeer::SetSizeLimit(manager, bytes[i] - 1);
          const auto failed = create();
          NodeTestPeer::SetSizeLimit(manager, SIZE_MAX);
          ASSERT_FALSE(failed);
          ExpectErrorDetail(failed.error(), ErrorCode::kResourceLimit,
                            ResourceKind::kSize);
          unchanged();
        }

        auto resource = ResourceKind::kNone;
        if (failure < allocation_count) {
          resource = ResourceKind::kCppAllocation;
          NodeTestPeer::FailAllocationAfter(manager, failure);
        } else if (failure < allocation_count + retain_count) {
          resource = ResourceKind::kReferenceCount;
          NodeTestPeer::FailRetainAfter(manager, failure - allocation_count);
        } else {
          NodeTestPeer::FailAllocationAfter(manager, allocation_count);
        }
        NumericTestPeer::FailAllocationAfter(0);
        auto result = create();
        NumericTestPeer::ClearFailures();
        NodeTestPeer::ClearFailures(manager);
        if (resource == ResourceKind::kNone) {
          ASSERT_TRUE(result);
          const auto after = NodeTestPeer::GetStats(manager);
          EXPECT_EQ(after.allocation_attempts - before.allocation_attempts,
                    allocation_count);
          EXPECT_EQ(after.node_allocations - before.node_allocations, 1);
        } else {
          ASSERT_FALSE(result);
          ExpectErrorDetail(result.error(), ErrorCode::kResourceLimit,
                            resource);
          unchanged();
          result = create();
        }

        auto node = Take(std::move(result));
        EXPECT_EQ(node.GetId(), before.next_node_id);
        const auto published = Take(manager.GetStorageStats());
        EXPECT_EQ(published.live_nodes, storage.live_nodes + 1);
        EXPECT_EQ(published.live_edges,
                  storage.live_edges + (rational ? 1 : 0));
        EXPECT_EQ(published.node_bytes, storage.node_bytes + bytes[i]);
        EXPECT_EQ(published.numeric_backing_bytes, 0);
        EXPECT_EQ(NodeTestPeer::RefCount(rm), rational ? 2 : 1);
        const auto hit_before = NodeTestPeer::GetStats(manager);
        const auto hit_state_refs = NodeTestPeer::StateRefCount(manager);
        NodeTestPeer::FailAllocationAfter(manager, 0);
        NodeTestPeer::FailRetainAfter(manager, 0);
        const auto failed_hit = create();
        NodeTestPeer::ClearFailures(manager);
        ASSERT_FALSE(failed_hit);
        ExpectErrorDetail(failed_hit.error(), ErrorCode::kResourceLimit,
                          ResourceKind::kReferenceCount);
        EXPECT_EQ(NodeTestPeer::RefCount(node), 1);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).allocation_attempts,
                  hit_before.allocation_attempts);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).next_node_id,
                  hit_before.next_node_id);
        const auto after_hit_failure = Take(manager.GetStorageStats());
        EXPECT_EQ(after_hit_failure.live_nodes, published.live_nodes);
        EXPECT_EQ(after_hit_failure.live_edges, published.live_edges);
        EXPECT_EQ(after_hit_failure.node_bytes, published.node_bytes);
        EXPECT_EQ(after_hit_failure.numeric_backing_bytes,
                  published.numeric_backing_bytes);
        EXPECT_EQ(NodeTestPeer::StateRefCount(manager), hit_state_refs);
        EXPECT_EQ(NodeTestPeer::RefCount(rm), rational ? 2 : 1);

        NodeTestPeer::FailAllocationAfter(manager, 0);
        NumericTestPeer::FailAllocationAfter(0);
        auto hit = create();
        NumericTestPeer::ClearFailures();
        NodeTestPeer::ClearFailures(manager);
        ASSERT_TRUE(hit);
        EXPECT_EQ(*hit, node);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).allocation_attempts,
                  hit_before.allocation_attempts);
        *hit = {};
        node = {};
        const auto released = Take(manager.GetStorageStats());
        EXPECT_EQ(released.live_nodes, storage.live_nodes);
        EXPECT_EQ(released.live_edges, storage.live_edges);
        EXPECT_EQ(released.node_bytes, storage.node_bytes);
        EXPECT_EQ(NodeTestPeer::RefCount(rm), 1);
        EXPECT_EQ(NodeTestPeer::GetStats(manager).node_reclamations,
                  hit_before.node_reclamations + 1);
      }
    }
  }
}

}  // namespace
}  // namespace dandelion::testing
