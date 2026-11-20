#include "test_support.h"

namespace dandelion::testing {
namespace {

using Tag = NumericPayloadTag;

template <class T>
T ReadField(const std::byte* address) {
  T value;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

void ExpectInlineLimbs(const NumericNodeLayout& layout, std::size_t offset,
                       std::span<const std::uint64_t> borrowed,
                       std::span<const std::uint64_t> expected) {
  const auto* address = layout.block + offset;
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % alignof(std::uint64_t),
            0);
  EXPECT_EQ(borrowed.data(), reinterpret_cast<const std::uint64_t*>(address));
  ExpectOrderedViews(borrowed, expected);
}

class NumericPayload : public NodeTest {
 protected:
  void ExpectStorage(std::size_t nodes, std::size_t edges, std::size_t bytes) {
    const auto stats = Take(manager_.GetStorageStats());
    EXPECT_EQ(stats.live_nodes, nodes);
    EXPECT_EQ(stats.live_edges, edges);
    EXPECT_EQ(stats.node_bytes, bytes);
    EXPECT_EQ(stats.numeric_backing_bytes, 0);
  }
};

TEST_F(NumericPayload, SignedIntegerWordAndInlineMagnitudes) {
  const auto sort = Take(manager_.IntSort());
  for (const auto value : {std::int64_t{0}, INT64_MIN, INT64_MAX}) {
    SCOPED_TRACE(value);
    const auto node = Int(value);
    const auto layout = NodeTestPeer::InspectNumericNode(node);
    EXPECT_EQ(layout.header_bytes, 56);
    EXPECT_EQ(layout.tag, Tag::kIntWord);
    EXPECT_EQ(layout.payload_offset, 56);
    EXPECT_EQ(ReadField<std::int64_t>(layout.block + 56), value);
    EXPECT_EQ(node.Borrow().GetSort(), sort.Borrow());
    EXPECT_EQ(node.Borrow().GetValue()->GetInteger()->TryInt64(), value);
    ExpectStorage(1, 0, 64);
  }

  struct Case {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::string_view decimal;
    int sign;
    std::array<std::uint64_t, 3> limbs;
    std::uint32_t count;
    std::size_t bytes;
    // NOLINTEND(readability-identifier-naming)
  };
  constexpr std::array cases{
      Case{"9223372036854775808", 1, {std::uint64_t{1} << 63}, 1, 64},
      Case{"-9223372036854775809", -1, {(std::uint64_t{1} << 63) + 1}, 1, 64},
      Case{"-18446744073709551616", -1, {0, 1}, 2, 72},
      Case{"340282366920938463463374607431768211457", 1, {1, 0, 1}, 3, 80},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.decimal);
    const auto input = Number(test.decimal);
    const auto node = Take(manager_.MakeInt(input.Borrow()));
    const auto layout = NodeTestPeer::InspectNumericNode(node);
    EXPECT_EQ(layout.header_bytes, 56);
    EXPECT_EQ(layout.limb_count_offset, 52);
    EXPECT_EQ(layout.tag,
              test.sign < 0 ? Tag::kIntWideNegative : Tag::kIntWidePositive);
    EXPECT_EQ(layout.payload_offset, 56);
    EXPECT_EQ(layout.literal_limbs, test.count);
    const auto view = *node.Borrow().GetValue()->GetInteger();
    EXPECT_EQ(view.Sign(), test.sign);
    EXPECT_EQ(view.ToDecimal(), test.decimal);
    ExpectInlineLimbs(layout, 56, view.MagnitudeLimbs(),
                      std::span(test.limbs).first(test.count));
    EXPECT_EQ(Take(Integer::Negate(view)).ToDecimal(),
              Take(Integer::Negate(input.Borrow())).ToDecimal());
    ExpectStorage(1, 0, test.bytes);
  }
}

TEST_F(NumericPayload, UnsignedWideIntegersShareOwningAndInlineKeys) {
  for (const auto word : {std::uint64_t{1} << 63, UINT64_MAX}) {
    SCOPED_TRACE(word);
    const auto owning = Take(Integer::FromUnsigned(word));
    const auto inline_view = IntegerView::FromUnsigned(word);
    for (const auto inline_first : {false, true}) {
      SCOPED_TRACE(inline_first);
      const auto first =
          Take(manager_.MakeInt(inline_first ? inline_view : owning.Borrow()));
      const auto second =
          Take(manager_.MakeInt(inline_first ? owning.Borrow() : inline_view));
      EXPECT_EQ(first.GetId(), second.GetId());
      EXPECT_EQ(Take(manager_.MakeInt(inline_view)).GetId(), first.GetId());
      EXPECT_EQ(first.Borrow().GetValue()->GetInteger()->TryUint64(), word);
      ExpectStorage(1, 0, 64);
    }
  }
}

TEST_F(NumericPayload, RotatesByBorrowedWideIntegerLiteral) {
  const auto node =
      Take(manager_.MakeInt(View(Number("18446744073709551617"))));
  const auto amount = node.Borrow().GetValue()->GetInteger();
  ASSERT_TRUE(amount);
  EXPECT_FALSE(amount->TryUint64());

  const auto bits = ExactBv(5, IntegerView(0b10011));
  const auto left = Take(BitVector::RotateLeft(bits, *amount));
  const auto right = Take(BitVector::RotateRight(bits, *amount));
  EXPECT_EQ(left.Width(), 5u);
  EXPECT_EQ(left.TryUint64(), 0b01110u);
  EXPECT_EQ(right.Width(), 5u);
  EXPECT_EQ(right.TryUint64(), 0b11100u);
}

TEST_F(NumericPayload, UnsignedWideIntegerChecksFullMagnitudeOnCollision) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  constexpr auto word = std::uint64_t{1} << 63;
  const auto larger = Number("27670116110564327424");  // 2^64 + 2^63
  const auto larger_node = Take(manager_.MakeInt(larger.Borrow()));
  const auto unsigned_node =
      Take(manager_.MakeInt(IntegerView::FromUnsigned(word)));
  const auto other_node =
      Take(manager_.MakeInt(IntegerView::FromUnsigned(UINT64_MAX)));
  EXPECT_NE(unsigned_node.GetId(), larger_node.GetId());
  EXPECT_NE(other_node.GetId(), unsigned_node.GetId());
  EXPECT_EQ(Take(manager_.MakeInt(IntegerView::FromUnsigned(word))).GetId(),
            unsigned_node.GetId());
  ExpectStorage(3, 0, 200);
}

TEST_F(NumericPayload, IntegerWordKeysMatchAcrossViewForms) {
  constexpr std::array<std::uint64_t, 1> positive{INT64_MAX};
  constexpr std::array<std::uint64_t, 1> negative{std::uint64_t{1} << 63};
  const auto max = Int(INT64_MAX);
  const auto min = Int(INT64_MIN);
  const auto zero = Int(0);
  EXPECT_EQ(Take(manager_.MakeInt(IntegerView::FromUnsigned(INT64_MAX))), max);
  EXPECT_EQ(Take(manager_.MakeInt(
                IntegerView::FromCanonicalMagnitude(positive, false))),
            max);
  EXPECT_EQ(Take(manager_.MakeInt(
                IntegerView::FromCanonicalMagnitude(negative, true))),
            min);
  EXPECT_EQ(Take(manager_.MakeInt(IntegerView::FromUnsigned(0))), zero);
  ExpectStorage(3, 0, 192);
}

TEST_F(NumericPayload, BitVectorAndFiniteFpChooseStorageByEffectiveLength) {
  const auto bv_sort = Bv(65);
  const auto fp_sort = Fp(8, 65);
  const auto format = Format(8, 65);
  const auto wide_bv = Number("18446744073709551616");
  const auto wide_fp = Take(Integer::ParseRadix("7f0000000000000001", 16));
  const std::array bitvectors{
      ExactBv(65, IntegerView::FromUnsigned(UINT64_MAX)),
      ExactBv(65, wide_bv.Borrow())};
  const std::array floats{FpBits(format, IntegerView(1)),
                          FpBits(format, wide_fp.Borrow())};
  const auto check = [&](const Node& node, BitVectorView bits, Tag tag,
                         std::span<const std::uint64_t> limbs) {
    const auto layout = NodeTestPeer::InspectNumericNode(node);
    EXPECT_EQ(layout.tag, tag);
    EXPECT_EQ(layout.header_bytes, 56);
    EXPECT_EQ(layout.payload_offset, 56);
    if (limbs.size() == 1) {
      EXPECT_EQ(ReadField<std::uint64_t>(layout.block + 56), limbs.front());
      EXPECT_EQ(bits.TryUint64(), limbs.front());
    } else {
      EXPECT_EQ(layout.literal_limbs, limbs.size());
      EXPECT_EQ(layout.limb_count_offset, 52);
      ExpectInlineLimbs(layout, 56, bits.Limbs(), limbs);
    }
    ExpectStorage(1, 0, 56 + 8 * limbs.size());
  };
  for (std::size_t i = 0; i < floats.size(); ++i) {
    {
      const auto node =
          Take(manager_.MakeBvValue(bv_sort.Borrow(), bitvectors[i].Borrow()));
      const auto bits = *node.Borrow().GetValue()->GetBitVector();
      EXPECT_EQ(bits.Width(), 65);
      if (i == 0) {
        check(node, bits, Tag::kBvWord,
              std::array<std::uint64_t, 1>{UINT64_MAX});
      } else {
        check(node, bits, Tag::kBvWide, std::array<std::uint64_t, 2>{0, 1});
      }
    }
    const auto node =
        Take(manager_.MakeFpValue(fp_sort.Borrow(), floats[i].Borrow()));
    const auto view = *node.Borrow().GetValue()->GetFloatingPoint();
    EXPECT_EQ(view.Format(), format);
    EXPECT_EQ(view.Category(), FpCategory::kFiniteBits);
    ASSERT_TRUE(view.FiniteBits());
    EXPECT_EQ(view.FiniteBits()->Width(), 73);
    if (i == 0) {
      check(node, *view.FiniteBits(), Tag::kFpWord,
            std::array<std::uint64_t, 1>{1});
    } else {
      check(node, *view.FiniteBits(), Tag::kFpWide,
            std::array<std::uint64_t, 2>{1, 127});
    }
  }
}

TEST_F(NumericPayload, EachFpSpecialLivesEntirelyInTheHeader) {
  const auto sort = Fp(65, 66);
  const auto format = Format(65, 66);
  for (const auto& [special, category, tag] : std::array{
           std::tuple{FpSpecial::kNaN, FpCategory::kNaN, Tag::kFpNaN},
           std::tuple{FpSpecial::kPositiveInfinity,
                      FpCategory::kPositiveInfinity, Tag::kFpPositiveInfinity},
           std::tuple{FpSpecial::kNegativeInfinity,
                      FpCategory::kNegativeInfinity, Tag::kFpNegativeInfinity},
           std::tuple{FpSpecial::kPositiveZero, FpCategory::kPositiveZero,
                      Tag::kFpPositiveZero},
           std::tuple{FpSpecial::kNegativeZero, FpCategory::kNegativeZero,
                      Tag::kFpNegativeZero}}) {
    const auto value = SpecialFp(format, special);
    const auto node = Take(manager_.MakeFpValue(sort.Borrow(), value.Borrow()));
    const auto layout = NodeTestPeer::InspectNumericNode(node);
    EXPECT_EQ(layout.tag, tag);
    EXPECT_EQ(layout.header_bytes, 56);
    const auto view = *node.Borrow().GetValue()->GetFloatingPoint();
    EXPECT_EQ(view.Category(), category);
    EXPECT_EQ(view.Format(), format);
    EXPECT_FALSE(view.FiniteBits());
    ExpectStorage(1, 0, 56);
  }
}

TEST_F(NumericPayload, CompactRationalStoresTwoExactWords) {
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format{Integer(8), Integer(24)};
  const auto initial = Take(manager_.GetStorageStats());
  const auto make = [&](IntegerView n, IntegerView d) {
    const auto fraction = Take(Rational::Make(n, d));
    return Op(Kind::kFpFromRational, {rm.Borrow()}, format, {},
              fraction.Borrow());
  };
  for (const auto& [n, d, expected_n, expected_d] : std::array{
           std::tuple{IntegerView(-6), IntegerView(-9), std::int64_t{2},
                      std::uint64_t{3}},
           std::tuple{IntegerView(0), IntegerView(-9), std::int64_t{0},
                      std::uint64_t{1}},
           std::tuple{IntegerView(INT64_MIN), IntegerView(1), INT64_MIN,
                      std::uint64_t{1}},
           std::tuple{IntegerView(1), IntegerView::FromUnsigned(UINT64_MAX),
                      std::int64_t{1}, UINT64_MAX}}) {
    const auto node = make(n, d);
    EXPECT_EQ(node, make(IntegerView(expected_n),
                         IntegerView::FromUnsigned(expected_d)));
    const auto layout = NodeTestPeer::InspectNumericNode(node);
    EXPECT_EQ(layout.tag, Tag::kRationalConversion);
    EXPECT_EQ(layout.header_bytes, 56);
    EXPECT_EQ(layout.payload_offset, 64);
    EXPECT_EQ(ReadField<std::uint32_t>(layout.block + 64), 8);
    EXPECT_EQ(ReadField<std::uint32_t>(layout.block + 68), 24);
    EXPECT_EQ(ReadField<std::int64_t>(layout.block + 72), expected_n);
    EXPECT_EQ(ReadField<std::uint64_t>(layout.block + 80), expected_d);
    const auto value = node.Borrow().GetRationalParameter();
    ASSERT_TRUE(value);
    EXPECT_EQ(value->Numerator().TryInt64(), expected_n);
    EXPECT_EQ(value->Denominator().TryUint64(), expected_d);
    ExpectStorage(initial.live_nodes + 1, initial.live_edges + 1,
                  initial.node_bytes + 88);
  }
}

TEST_F(NumericPayload, PackedRationalsSurviveBothChangedRebuildForms) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const auto other = Take(manager_.MakeRoundingMode(RoundingMode::kRtz));
  const std::array format{Integer(8), Integer(24)};
  const auto initial = Take(manager_.GetStorageStats());
  struct Case {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::string_view n;
    std::string_view d;
    int sign;
    std::array<std::uint64_t, 2> n_limbs;
    std::array<std::uint64_t, 2> d_limbs;
    std::uint32_t n_count;
    std::uint32_t d_count;
    std::size_t bytes;
    // NOLINTEND(readability-identifier-naming)
  };
  constexpr std::array cases{
      Case{"18446744073709551617", "3", 1, {1, 1}, {3}, 2, 1, 104},
      Case{"-3", "18446744073709551617", -1, {3}, {1, 1}, 1, 2, 104},
      Case{"-18446744073709551617",
           "36893488147419103233",
           -1,
           {1, 1},
           {1, 2},
           2,
           2,
           112},
      Case{"9223372036854775808",
           "1",
           1,
           {std::uint64_t{1} << 63},
           {1},
           1,
           1,
           96},
  };
  for (const auto& test : cases) {
    for (const bool consuming : {false, true}) {
      SCOPED_TRACE(test.n);
      SCOPED_TRACE(test.d);
      SCOPED_TRACE(consuming);
      const auto prepare = [&] {
        const auto n = Number(test.n);
        const auto d = Number(test.d);
        return Take(Rational::Make(n.Borrow(), d.Borrow()));
      };
      Node original;
      {
        const auto input = prepare();
        original = Op(Kind::kFpFromRational, {rm.Borrow()}, format, {},
                      input.Borrow());
      }
      const auto source_value = original.Borrow().GetRationalParameter();
      ASSERT_TRUE(source_value);
      EXPECT_EQ(source_value->Numerator().ToDecimal(), test.n);
      EXPECT_EQ(source_value->Denominator().ToDecimal(), test.d);
      const std::array children{other.Borrow()};
      std::array owners{other};
      const auto rebuilt = Take(
          consuming ? manager_.RebuildNodeConsuming(original.Borrow(), owners)
                    : manager_.RebuildNode(original.Borrow(), children));
      EXPECT_EQ(owners.front().Borrow().IsNull(), consuming);
      EXPECT_NE(rebuilt, original);
      original = {};
      EXPECT_EQ(rebuilt.Borrow().GetChildren()[0], other.Borrow());
      EXPECT_EQ(rebuilt.Borrow().GetIndices()[0].Borrow().TryUint64(), 8);
      EXPECT_EQ(rebuilt.Borrow().GetIndices()[1].Borrow().TryUint64(), 24);
      EXPECT_FALSE(rebuilt.Borrow().GetValue());

      const auto layout = NodeTestPeer::InspectNumericNode(rebuilt);
      EXPECT_EQ(layout.tag, test.sign > 0 ? Tag::kRationalWidePositive
                                          : Tag::kRationalWideNegative);
      EXPECT_EQ(layout.payload_offset, 64);
      EXPECT_EQ(ReadField<std::uint32_t>(layout.block + 72), test.n_count);
      EXPECT_EQ(ReadField<std::uint32_t>(layout.block + 76), test.d_count);
      const auto view = *rebuilt.Borrow().GetRationalParameter();
      EXPECT_EQ(view.Numerator().Sign(), test.sign);
      EXPECT_EQ(view.Numerator().ToDecimal(), test.n);
      EXPECT_EQ(view.Denominator().ToDecimal(), test.d);
      ExpectInlineLimbs(layout, 80, view.Numerator().MagnitudeLimbs(),
                        std::span(test.n_limbs).first(test.n_count));
      ExpectInlineLimbs(layout, 80 + 8 * test.n_count,
                        view.Denominator().MagnitudeLimbs(),
                        std::span(test.d_limbs).first(test.d_count));
      const auto owned = prepare();
      EXPECT_EQ(rebuilt, Op(Kind::kFpFromRational, {other.Borrow()}, format, {},
                            owned.Borrow()));
      EXPECT_EQ(rebuilt,
                Op(Kind::kFpFromRational, {other.Borrow()}, format, {}, view));
      const auto negate_borrowed = Take(Rational::Negate(view));
      const auto negate_owned = Take(Rational::Negate(owned.Borrow()));
      EXPECT_EQ(negate_borrowed.Borrow().Numerator().ToDecimal(),
                negate_owned.Borrow().Numerator().ToDecimal());
      EXPECT_EQ(negate_borrowed.Borrow().Denominator().ToDecimal(), test.d);
      ExpectStorage(initial.live_nodes + 1, initial.live_edges + 1,
                    initial.node_bytes + test.bytes);
    }
  }
}

TEST_F(NumericPayload, RequestedBytesFollowLiveRootsAndExternalIndexBacking) {
  // The first Int literal interns its sort, which contributes metadata bytes.
  (void)Take(manager_.IntSort());
  const auto bv = Bv(65);
  const auto fp = Fp(8, 65);
  auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const auto baseline = Take(manager_.GetStorageStats());
  const auto wide = Number("18446744073709551617");
  const auto bv_value = ExactBv(65, wide.Borrow());
  const auto nan = SpecialFp(Format(8, 65), FpSpecial::kNaN);
  const auto small_q = Take(Rational::Make(IntegerView(2), IntegerView(3)));
  const auto wide_q = Take(Rational::Make(wide.Borrow(), IntegerView(3)));
  const std::array format{Integer(8), Integer(65)};
  std::array roots{
      Int(INT64_MIN),
      Take(manager_.MakeBvValue(bv.Borrow(), bv_value.Borrow())),
      Take(manager_.MakeFpValue(fp.Borrow(), nan.Borrow())),
      Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, small_q.Borrow()),
      Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, wide_q.Borrow()),
  };
  ExpectStorage(baseline.live_nodes + 5, 2,
                baseline.node_bytes + 64 + 72 + 56 + 88 + 104);
  auto hit = Take(manager_.MakeBvValue(bv.Borrow(), bv_value.Borrow()));
  EXPECT_EQ(hit, roots[1]);
  ExpectStorage(baseline.live_nodes + 5, 2,
                baseline.node_bytes + 64 + 72 + 56 + 88 + 104);

  const std::array indices{wide};
  auto index = Op(Kind::kBvRotateLeft, {roots[1].Borrow()}, indices);
  const auto with_index = Take(manager_.GetStorageStats());
  EXPECT_GT(with_index.numeric_backing_bytes, 0);
  EXPECT_EQ(with_index.live_edges, 3);
  EXPECT_EQ(with_index.node_bytes,
            baseline.node_bytes + 64 + 72 + 56 + 88 + 104 + 80);
  EXPECT_EQ(with_index.table_bytes, baseline.table_bytes);
  EXPECT_EQ(with_index.metadata_bytes, baseline.metadata_bytes);
  index = {};
  Take(manager_.ReleaseRoots(std::span(roots).first(2)));
  ExpectStorage(baseline.live_nodes + 4, 2,
                baseline.node_bytes + 72 + 56 + 88 + 104);
  hit = {};
  Take(manager_.ReleaseRoots(std::span(roots).last(3)));
  ExpectStorage(baseline.live_nodes, 0, baseline.node_bytes);
  rm = {};
  ExpectStorage(0, 0, 0);
  EXPECT_EQ(Take(manager_.GetStorageStats()).metadata_bytes,
            baseline.metadata_bytes);
}

}  // namespace
}  // namespace dandelion::testing
