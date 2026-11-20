#include "test_support.h"

namespace dandelion::testing {
namespace {

class Identity : public NodeTest {};

TEST_F(Identity, AllApplicableKeyFieldsSurviveForcedCollision) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto z = Take(manager_.IntSort());
  const auto b = Take(manager_.BoolSort());
  const auto x = Int(1);
  const auto y = Int(2);
  EXPECT_NE(x, y);
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntSub, {x.Borrow(), y.Borrow()}));
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntAdd, {y.Borrow(), x.Borrow()}));
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntAdd, {x.Borrow(), y.Borrow(), y.Borrow()}));
  const auto a = Take(manager_.MakeArraySort(z.Borrow(), z.Borrow()));
  const auto c = Take(manager_.MakeArraySort(b.Borrow(), z.Borrow()));
  EXPECT_NE(Op(Kind::kArrayConst, {x.Borrow()}, {}, a.Borrow()),
            Op(Kind::kArrayConst, {x.Borrow()}, {}, c.Borrow()));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const Integer one(1), two(2), three(3), four(4), six(6), eight(8),
      twenty_four(24);
  const auto q1 = Take(Rational::Make(two.Borrow(), three.Borrow()));
  const auto q2 = Take(Rational::Make(four.Borrow(), six.Borrow()));
  const auto q3 = Take(Rational::Make(one.Borrow(), three.Borrow()));
  const std::array fmt = {eight, twenty_four};
  const auto first =
      Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q1.Borrow());
  EXPECT_EQ(first,
            Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q2.Borrow()));
  EXPECT_NE(first,
            Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q3.Borrow()));
  const auto fp = Fp(8, 24);
  const auto other_format = Fp(2, 2);
  const auto nan = SpecialFp(Format(8, 24), FpSpecial::kNaN);
  const auto other_nan = SpecialFp(Format(2, 2), FpSpecial::kNaN);
  EXPECT_NE(
      Take(manager_.MakeFpValue(fp.Borrow(), nan.Borrow())),
      Take(manager_.MakeFpValue(other_format.Borrow(), other_nan.Borrow())));
  const std::array domain = {z.Borrow()};
  const auto d1 = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto d2 = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto i1 = Take(manager_.InstantiateDeclaration(d1.Borrow(), {}));
  const auto i2 = Take(manager_.InstantiateDeclaration(d2.Borrow(), {}));
  const std::array args = {x.Borrow()};
  EXPECT_NE(Take(manager_.MakeApply(i1.Borrow(), args)),
            Take(manager_.MakeApply(i2.Borrow(), args)));
  const std::array pair_domain = {z.Borrow(), z.Borrow()};
  const auto ordered_decl =
      Take(manager_.DeclareFunction("ordered", pair_domain, z.Borrow()));
  const auto ordered_instance =
      Take(manager_.InstantiateDeclaration(ordered_decl.Borrow(), {}));
  const std::array forward = {x.Borrow(), y.Borrow()};
  const std::array reverse = {y.Borrow(), x.Borrow()};
  EXPECT_NE(Take(manager_.MakeApply(ordered_instance.Borrow(), forward)),
            Take(manager_.MakeApply(ordered_instance.Borrow(), reverse)));
}

TEST_F(Identity, NullCrossStateAndReleasedStructureIdentity) {
  Node empty;
  Node other_empty;
  EXPECT_EQ(empty, other_empty);
  EXPECT_EQ(empty.GetId(), 0);
  EXPECT_EQ(NodeHash{}(empty), NodeHash{}(other_empty));
  auto other = Take(NodeManager::Create());
  const auto a = Take(manager_.MakeBool(true));
  const auto b = Take(other.MakeBool(true));
  EXPECT_NE(a, b);
  const auto sort = Take(manager_.BoolSort());
  NodeId old = 0;
  {
    const auto node = Take(manager_.MakeBool(false));
    old = node.GetId();
  }
  const auto reconstructed = Take(manager_.MakeBool(false));
  EXPECT_GT(reconstructed.GetId(), old);
  EXPECT_EQ(reconstructed.GetSort(), sort);
}

}  // namespace

TEST_F(Identity, RationalKeyKeepsEachChangedComponentLive) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format{Integer(8), Integer(24)};
  const auto make = [&](std::int64_t n, std::int64_t d) {
    const auto value = Take(Rational::Make(IntegerView(n), IntegerView(d)));
    return Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, value.Borrow());
  };
  const auto two_thirds = make(2, 3);
  const auto two_fifths = make(2, 5);
  const auto one_third = make(1, 3);
  EXPECT_NE(two_thirds, two_fifths);
  EXPECT_NE(two_thirds, one_third);
  EXPECT_EQ(two_thirds, make(4, 6));
  EXPECT_EQ(two_fifths, make(6, 15));
  EXPECT_EQ(one_third, make(2, 6));
}

}  // namespace dandelion::testing

namespace dandelion::testing {
namespace {

TEST_F(Identity, BorrowedNumericKeysOutliveSourceValuesAndFacade) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv = Bv(129);
  const auto fp = Fp(8, 65);
  auto integer_input = Number("-340282366920938463463374607431768211457");
  auto bv_input =
      ExactBv(129, View(Number("340282366920938463463374607431768211457")));
  auto fp_input = FpBits(Format(8, 65), View(Number("18446744073709551617")));
  auto integer = Take(manager_.MakeInt(integer_input.Borrow()));
  auto bitvector = Take(manager_.MakeBvValue(bv.Borrow(), bv_input.Borrow()));
  auto floating = Take(manager_.MakeFpValue(fp.Borrow(), fp_input.Borrow()));
  auto rounding = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format{Integer(8), Integer(65)};
  const auto convert = [&](RationalView value) {
    return Op(Kind::kFpFromRational, {rounding.Borrow()}, format, {}, value);
  };
  auto rational = convert(View(Take(
      Rational::Make(View(Number("-18446744073709551617")), IntegerView(3)))));
  const auto rational_view = *rational.Borrow().GetRationalParameter();
  const auto int_view = *integer.Borrow().GetValue()->GetInteger();
  const auto bv_view = *bitvector.Borrow().GetValue()->GetBitVector();
  const auto fp_view = *floating.Borrow().GetValue()->GetFloatingPoint();

  auto i = std::move(integer_input);
  auto b = std::move(bv_input);
  auto f = std::move(fp_input);
  EXPECT_EQ(Take(manager_.MakeInt(i.Borrow())), integer);
  EXPECT_EQ(Take(manager_.MakeBvValue(bv.Borrow(), b.Borrow())), bitvector);
  EXPECT_EQ(Take(manager_.MakeFpValue(fp.Borrow(), f.Borrow())), floating);
  i = Integer(0);
  b = ExactBv(1, IntegerView(0));
  f = SpecialFp(Format(2, 2), FpSpecial::kPositiveZero);

  std::vector<Node> colliders;
  for (const auto text : {"340282366920938463463374607431768211457",
                          "-340282366920938463463374607431768211458",
                          "-680564733841876926926749214863536422913"}) {
    const auto value = Number(text);
    colliders.push_back(Take(manager_.MakeInt(value.Borrow())));
    EXPECT_NE(colliders.back(), integer);
  }
  for (const auto text :
       {"18446744073709551617", "340282366920938463463374607431768211458"}) {
    const auto value = ExactBv(129, View(Number(text)));
    colliders.push_back(
        Take(manager_.MakeBvValue(bv.Borrow(), value.Borrow())));
    EXPECT_NE(colliders.back(), bitvector);
  }
  for (const auto text : {"36893488147419103233", "18446744073709551618"}) {
    const auto value = FpBits(Format(8, 65), View(Number(text)));
    colliders.push_back(
        Take(manager_.MakeFpValue(fp.Borrow(), value.Borrow())));
    EXPECT_NE(colliders.back(), floating);
  }
  const auto ratio_owner = Take(
      Rational::Make(View(Number("-18446744073709551617")), IntegerView(3)));
  const auto changed_numerator = Take(
      Rational::Make(View(Number("-18446744073709551616")), IntegerView(3)));
  const auto changed_denominator = Take(
      Rational::Make(View(Number("-18446744073709551617")), IntegerView(5)));
  colliders.push_back(convert(changed_numerator.Borrow()));
  colliders.push_back(convert(changed_denominator.Borrow()));
  EXPECT_NE(colliders[colliders.size() - 2], rational);
  EXPECT_NE(colliders[colliders.size() - 1], rational);
  EXPECT_NE(colliders[colliders.size() - 2], colliders.back());
  const auto before = NodeTestPeer::GetStats(manager_);
  for (std::int64_t value = 0; value < 40; ++value)
    colliders.push_back(Int(value));
  EXPECT_GT(NodeTestPeer::GetStats(manager_).table_capacity,
            before.table_capacity);
  for (const auto& node : colliders)
    EXPECT_EQ(NodeTestPeer::StructuralHash(node), 15);

  const auto b_owner =
      ExactBv(129, View(Number("340282366920938463463374607431768211457")));
  const auto f_owner =
      FpBits(Format(8, 65), View(Number("18446744073709551617")));
  EXPECT_EQ(Take(manager_.MakeInt(
                View(Number("-340282366920938463463374607431768211457")))),
            integer);
  EXPECT_EQ(Take(manager_.MakeBvValue(bv.Borrow(), b_owner.Borrow())),
            bitvector);
  EXPECT_EQ(Take(manager_.MakeFpValue(fp.Borrow(), f_owner.Borrow())),
            floating);
  EXPECT_EQ(Take(manager_.MakeInt(int_view)), integer);
  EXPECT_EQ(Take(manager_.MakeBvValue(bv.Borrow(), bv_view)), bitvector);
  EXPECT_EQ(Take(manager_.MakeFpValue(fp.Borrow(), fp_view)), floating);
  EXPECT_EQ(convert(ratio_owner.Borrow()), rational);
  EXPECT_EQ(convert(rational_view), rational);

  NodeTestPeer::FailAllocationAfter(manager_, 0);
  NodeTestPeer::FailRetainAfter(manager_, 0);
  EXPECT_EQ(integer.Borrow().GetValue()->GetInteger()->MagnitudeChunk(2), 1);
  EXPECT_EQ(bitvector.Borrow().GetValue()->GetBitVector()->Chunk(2), 1);
  EXPECT_EQ(
      floating.Borrow().GetValue()->GetFloatingPoint()->FiniteBits()->Chunk(1),
      1);
  const auto fraction = rational.Borrow().GetRationalParameter();
  ASSERT_TRUE(fraction);
  EXPECT_EQ(fraction->Numerator().MagnitudeChunk(1), 1);
  EXPECT_EQ(fraction->Denominator().TryUint64(), 3);
  EXPECT_EQ(Take(manager_.GetStorageStats()).numeric_backing_bytes, 0);
  const auto retain = manager_.RetainNode(integer.Borrow());
  Resource(retain, ResourceKind::kReferenceCount);
  const auto allocate = manager_.MakeInt(IntegerView(40));
  Resource(allocate, ResourceKind::kCppAllocation);
  NodeTestPeer::ClearFailures(manager_);

  colliders.clear();
  manager_ = {};
  EXPECT_EQ(int_view.ToDecimal(), "-340282366920938463463374607431768211457");
  EXPECT_EQ(bv_view.Chunk(2), 1);
  EXPECT_EQ(fp_view.Format(), Format(8, 65));
  const auto int_copy = int_view.Copy();
  const auto bv_copy = bv_view.Copy();
  EXPECT_EQ(rational_view.Numerator().ToDecimal(), "-18446744073709551617");
  const auto fp_copy = fp_view.Copy();
  const auto rational_copy = rational_view.Copy();
  integer = {};
  bitvector = {};
  floating = {};
  rational = {};
  rounding = {};
  EXPECT_EQ(rational_copy.Borrow().Numerator().ToDecimal(),
            "-18446744073709551617");
  EXPECT_EQ(rational_copy.Borrow().Denominator().TryUint64(), 3);
  EXPECT_EQ(int_copy.ToDecimal(), "-340282366920938463463374607431768211457");
  EXPECT_EQ(bv_copy.Borrow(), b_owner.Borrow());
  EXPECT_EQ(fp_copy.Borrow(), f_owner.Borrow());
}

}  // namespace
}  // namespace dandelion::testing
