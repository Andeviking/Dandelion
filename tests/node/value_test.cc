#include "test_support.h"

namespace dandelion::testing {
namespace {

class Value : public NodeTest {};

TEST_F(Value, UnsignedWordAndWideBitVectorValuesRemainExact) {
  NodeTestPeer::ForceCollisions(manager_, 2);
  const auto bv64 = Bv(64);
  const auto bv65 = Bv(65);
  const auto word = Number("18446744073709551615");
  const auto wide = Number("18446744073709551616");
  const auto a = Take(manager_.MakeBvValue(bv64.Borrow(), word.Borrow()));
  const auto b = Take(manager_.MakeBvValue(bv65.Borrow(), word.Borrow()));
  const auto c = Take(manager_.MakeBvValue(bv65.Borrow(), wide.Borrow()));
  EXPECT_NE(a, b);
  EXPECT_NE(b, c);
  const auto value = *b.Borrow().GetValue()->GetInteger();
  EXPECT_EQ(value.Sign(), 1);
  EXPECT_EQ(value.BitLength(), 64);
  EXPECT_EQ(value.TryUint64(), UINT64_MAX);
  EXPECT_FALSE(value.TryInt64());
  EXPECT_EQ(value.Hash(), word.Borrow().Hash());
  EXPECT_EQ(value.Compare(word.Borrow()), 0);
  EXPECT_EQ(value.Copy().ToDecimal(), "18446744073709551615");
  EXPECT_EQ(Take(manager_.MakeBvValue(bv65.Borrow(), value)), b);
  EXPECT_EQ(c.Borrow().GetValue()->GetInteger()->BitLength(), 65);
  const auto fp = Fp(11, 53);
  const auto negative_one = Number("13830554455654793216");
  const auto finite =
      Take(manager_.MakeFpBits(fp.Borrow(), negative_one.Borrow()));
  EXPECT_EQ(finite.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kFiniteBits);
  EXPECT_EQ(finite.Borrow().GetValue()->GetFpBits()->ToDecimal(),
            "13830554455654793216");
}

TEST_F(Value, EveryLiteralKindAndGmpPayloadHasExactImmutableValue) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto t = Take(manager_.MakeBool(true));
  const auto f = Take(manager_.MakeBool(false));
  EXPECT_NE(t, f);
  EXPECT_EQ(t.Borrow().GetKind(), Kind::kBoolValue);
  EXPECT_EQ(t.Borrow().GetSort().GetKind(), SortKind::kBool);
  EXPECT_EQ(t.Borrow().GetValue()->GetBool(), true);
  EXPECT_FALSE(t.Borrow().GetValue()->GetInteger());
  for (const auto text :
       {"-9223372036854775809", "-9223372036854775808", "9223372036854775807",
        "9223372036854775808", "340282366920938463463374607431768211456"}) {
    auto external = Number(text);
    const auto node = Take(manager_.MakeInt(external.Borrow()));
    const auto identity_hash = NodeHash{}(node);
    external = Integer(0);
    EXPECT_EQ(node.Borrow().GetValue()->GetInteger()->ToDecimal(), text);
    EXPECT_EQ(node.Borrow().GetKind(), Kind::kIntValue);
    EXPECT_EQ(node.Borrow().GetSort().GetKind(), SortKind::kInt);
    const auto restored = Number(text);
    EXPECT_EQ(node, Take(manager_.MakeInt(restored.Borrow())));
    EXPECT_EQ(NodeHash{}(node), identity_hash);
    EXPECT_TRUE(node.Borrow().GetChildren().empty());
    EXPECT_TRUE(node.Borrow().GetIndices().empty());
    EXPECT_FALSE(node.Borrow().GetRationalParameter());
  }
  const auto twice = Number("200000000000000000000000000000000000000");
  const auto unit = Number("100000000000000000000000000000000000000");
  const auto reduced = Take(Rational::Make(twice.Borrow(), unit.Borrow()));
  EXPECT_TRUE(reduced.Borrow().Numerator().IsSmall());
  EXPECT_EQ(Take(manager_.MakeInt(reduced.Borrow().Numerator())), Int(2));
  const auto bv65 = Bv(65);
  const auto bv64 = Bv(64);
  const auto big = Number("18446744073709551616");
  const auto exact = Take(manager_.MakeBvValue(bv65.Borrow(), big.Borrow()));
  EXPECT_EQ(exact.Borrow().GetKind(), Kind::kBvValue);
  EXPECT_EQ(exact.GetSort(), bv65);
  EXPECT_TRUE(exact.Borrow().GetChildren().empty());
  EXPECT_EQ(exact.Borrow().GetValue()->GetInteger()->ToDecimal(),
            "18446744073709551616");
  Error(manager_.MakeBvValue(bv64.Borrow(), big.Borrow()),
        ErrorCode::kInvalidLiteral);
  const Integer zero(0), negative(-1), too_big(256);
  const auto bv8 = Bv(8);
  Error(manager_.MakeBvValue(bv8.Borrow(), too_big.Borrow()),
        ErrorCode::kInvalidLiteral);
  Error(manager_.MakeBvValue(bv8.Borrow(), negative.Borrow()),
        ErrorCode::kInvalidLiteral);
  EXPECT_NE(Take(manager_.MakeInt(zero.Borrow())),
            Take(manager_.MakeBvValue(bv8.Borrow(), zero.Borrow())));
  for (const auto mode :
       {RoundingMode::kRne, RoundingMode::kRna, RoundingMode::kRtp,
        RoundingMode::kRtn, RoundingMode::kRtz}) {
    const auto node = Take(manager_.MakeRoundingMode(mode));
    EXPECT_EQ(node.Borrow().GetValue()->GetRoundingMode(), mode);
    EXPECT_EQ(node, Take(manager_.MakeRoundingMode(mode)));
    EXPECT_EQ(node.Borrow().GetKind(), Kind::kRoundingModeValue);
    EXPECT_EQ(node.Borrow().GetSort().GetKind(), SortKind::kRoundingMode);
  }
  Error(manager_.MakeRoundingMode(static_cast<RoundingMode>(5)),
        ErrorCode::kInvalidLiteral);
  const auto large_fp_sort = Fp(2, 64);
  auto external_bits = Number("18446744073709551616");
  const auto large_fp =
      Take(manager_.MakeFpBits(large_fp_sort.Borrow(), external_bits.Borrow()));
  external_bits = Integer(0);
  EXPECT_EQ(large_fp.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kFiniteBits);
  EXPECT_EQ(large_fp.Borrow().GetValue()->GetFpBits()->ToDecimal(),
            "18446744073709551616");
  const auto b = Take(manager_.BoolSort());
  Error(manager_.MakeBvValue(b.Borrow(), zero.Borrow()),
        ErrorCode::kSortMismatch);
  Error(manager_.MakeFpBits(b.Borrow(), zero.Borrow()),
        ErrorCode::kSortMismatch);
  Error(manager_.MakeBvValue({}, zero.Borrow()), ErrorCode::kNullObject);
  auto other = Take(NodeManager::Create());
  Error(other.MakeBvValue(bv8.Borrow(), zero.Borrow()),
        ErrorCode::kForeignManager);
  const auto float_sort = Fp(8, 24);
  Error(manager_.MakeFpBits({}, zero.Borrow()), ErrorCode::kNullObject);
  Error(manager_.MakeFpSpecial({}, FpSpecial::kNaN), ErrorCode::kNullObject);
  Error(manager_.MakeFpSpecial(b.Borrow(), FpSpecial::kNaN),
        ErrorCode::kSortMismatch);
  Error(other.MakeFpBits(float_sort.Borrow(), zero.Borrow()),
        ErrorCode::kForeignManager);
  Error(other.MakeFpSpecial(float_sort.Borrow(), FpSpecial::kNaN),
        ErrorCode::kForeignManager);
}

TEST_F(Value, EveryTinyFpPatternHasTheHandDerivedCategory) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto sort =
      Fp(2, 3);  // 5 total bits: sign, two exponent, two fraction.
  const auto nan = Take(manager_.MakeFpSpecial(sort.Borrow(), FpSpecial::kNaN));
  std::vector<Node> live;
  for (int bits = 0; bits < 32; ++bits) {
    const Integer value(bits);
    const auto node = Take(manager_.MakeFpBits(sort.Borrow(), value.Borrow()));
    live.push_back(node);
    EXPECT_EQ(node.Borrow().GetKind(), Kind::kFpValue);
    EXPECT_EQ(node.GetSort(), sort);
    EXPECT_TRUE(node.Borrow().GetChildren().empty());
    const bool sign = bits >= 16;
    const int magnitude = bits % 16;
    FpCategory expected = FpCategory::kFiniteBits;
    if (magnitude > 12) expected = FpCategory::kNaN;
    if (magnitude == 12)
      expected =
          sign ? FpCategory::kNegativeInfinity : FpCategory::kPositiveInfinity;
    if (magnitude == 0)
      expected = sign ? FpCategory::kNegativeZero : FpCategory::kPositiveZero;
    EXPECT_EQ(node.Borrow().GetValue()->GetFpCategory(), expected) << bits;
    if (magnitude > 12) {
      EXPECT_EQ(node, nan);
    }
    if (expected == FpCategory::kFiniteBits) {
      ASSERT_TRUE(node.Borrow().GetValue()->GetFpBits());
      EXPECT_EQ(node.Borrow().GetValue()->GetFpBits()->Compare(value.Borrow()),
                0);
    } else {
      EXPECT_FALSE(node.Borrow().GetValue()->GetFpBits());
    }
  }
  for (const auto [special, bits] :
       std::array{std::pair{FpSpecial::kNaN, 13},
                  std::pair{FpSpecial::kPositiveInfinity, 12},
                  std::pair{FpSpecial::kNegativeInfinity, 28},
                  std::pair{FpSpecial::kPositiveZero, 0},
                  std::pair{FpSpecial::kNegativeZero, 16}}) {
    EXPECT_EQ(Take(manager_.MakeFpSpecial(sort.Borrow(), special)),
              live[static_cast<std::size_t>(bits)]);
  }
  EXPECT_NE(live[0], live[16]);
  EXPECT_NE(Op(Kind::kEqual, {live[0].Borrow(), live[16].Borrow()}),
            Op(Kind::kFpEq, {live[0].Borrow(), live[16].Borrow()}));
  const Integer overflow(32), negative(-1);
  Error(manager_.MakeFpBits(sort.Borrow(), overflow.Borrow()),
        ErrorCode::kInvalidLiteral);
  Error(manager_.MakeFpBits(sort.Borrow(), negative.Borrow()),
        ErrorCode::kInvalidLiteral);
  Error(manager_.MakeFpSpecial(sort.Borrow(), static_cast<FpSpecial>(5)),
        ErrorCode::kInvalidLiteral);
  const auto huge = Fp(UINT32_MAX - 2, 2);
  const Integer zero(0);
  EXPECT_EQ(
      Take(manager_.MakeFpBits(huge.Borrow(), zero.Borrow())),
      Take(manager_.MakeFpSpecial(huge.Borrow(), FpSpecial::kPositiveZero)));
}

}  // namespace

TEST_F(Value, LiveWidePayloadsAndRoundingModesHaveDistinctKeysAndBytes) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv = Bv(65);
  const auto fp = Fp(2, 64);
  const auto first = Number("18446744073709551617");
  const auto second = Number("18446744073709551618");
  std::array nodes{Take(manager_.MakeBvValue(bv.Borrow(), first.Borrow())),
                   Take(manager_.MakeBvValue(bv.Borrow(), second.Borrow())),
                   Take(manager_.MakeFpBits(fp.Borrow(), first.Borrow())),
                   Take(manager_.MakeFpBits(fp.Borrow(), second.Borrow()))};
  std::size_t bytes = 0;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto view = *nodes[i].Borrow().GetValue();
    const auto value = i < 2 ? *view.GetInteger() : *view.GetFpBits();
    const auto expected = i % 2 == 0 ? first.Borrow() : second.Borrow();
    EXPECT_EQ(value.Compare(expected), 0);
    EXPECT_GT(value.BackingBytes(), 0);
    bytes += value.BackingBytes();
    for (std::size_t j = 0; j < i; ++j) EXPECT_NE(nodes[i], nodes[j]);
    EXPECT_EQ(nodes[i], i < 2
                            ? Take(manager_.MakeBvValue(bv.Borrow(), expected))
                            : Take(manager_.MakeFpBits(fp.Borrow(), expected)));
  }
  EXPECT_EQ(Take(manager_.GetStorageStats()).numeric_backing_bytes, bytes);
  Take(manager_.ReleaseRoots(nodes));
  EXPECT_EQ(Take(manager_.GetStorageStats()).numeric_backing_bytes, 0);

  constexpr std::array modes{RoundingMode::kRne, RoundingMode::kRna,
                             RoundingMode::kRtp, RoundingMode::kRtn,
                             RoundingMode::kRtz};
  std::vector<Node> live;
  for (const auto mode : modes)
    live.push_back(Take(manager_.MakeRoundingMode(mode)));
  for (std::size_t i = 0; i < live.size(); ++i) {
    EXPECT_EQ(Take(manager_.MakeRoundingMode(modes[i])), live[i]);
    for (std::size_t j = 0; j < i; ++j) EXPECT_NE(live[i], live[j]);
  }
}

TEST_F(Value, MultiChunkFpClassificationUsesAllExponentAndFractionBits) {
  const auto fp = Fp(65, 66);
  // Sign is bit 130, the exponent occupies bits 65..129, fraction 0..64.
  const auto bits = [&](std::string_view hex) {
    const auto value = Take(Integer::ParseRadix(hex, 16));
    return Take(manager_.MakeFpBits(fp.Borrow(), value.Borrow()));
  };
  const auto negative_zero = bits("400000000000000000000000000000000");
  const auto positive_inf = bits("3fffffffffffffffe0000000000000000");
  const auto negative_inf = bits("7fffffffffffffffe0000000000000000");
  const auto nan = bits("3ffffffffffffffff0000000000000000");
  const auto max_finite = bits("3fffffffffffffffdffffffffffffffff");
  EXPECT_EQ(negative_zero.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kNegativeZero);
  EXPECT_EQ(positive_inf.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kPositiveInfinity);
  EXPECT_EQ(negative_inf.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kNegativeInfinity);
  EXPECT_EQ(nan, Take(manager_.MakeFpSpecial(fp.Borrow(), FpSpecial::kNaN)));
  EXPECT_EQ(max_finite.Borrow().GetValue()->GetFpCategory(),
            FpCategory::kFiniteBits);
  const auto finite_bits = *max_finite.Borrow().GetValue()->GetFpBits();
  EXPECT_EQ(finite_bits.MagnitudeChunk(0), UINT64_MAX);
  EXPECT_EQ(finite_bits.MagnitudeChunk(1), UINT64_MAX - 2);
  EXPECT_EQ(finite_bits.MagnitudeChunk(2), 3);
}

}  // namespace dandelion::testing
