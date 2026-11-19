#include "test_support.h"

namespace dandelion::testing {
namespace {

TEST_F(NodeTest, SortIdentityParametersAndNestedCombinations) {
  const auto b = Take(manager_.BoolSort());
  const auto z = Take(manager_.IntSort());
  const auto r = Take(manager_.RoundingModeSort());
  const auto v = Bv(1);
  const auto f = Fp(2, 2);
  EXPECT_EQ(b, Take(manager_.BoolSort()));
  EXPECT_NE(b, v);
  EXPECT_EQ(b.Borrow().GetKind(), SortKind::kBool);
  EXPECT_EQ(z.Borrow().GetKind(), SortKind::kInt);
  EXPECT_EQ(r.Borrow().GetKind(), SortKind::kRoundingMode);
  EXPECT_EQ(v.Borrow().GetWidth(), 1);
  EXPECT_EQ(f.Borrow().GetFpExponentWidth(), 2);
  EXPECT_EQ(f.Borrow().GetFpSignificandWidth(), 2);
  EXPECT_FALSE(b.Borrow().GetWidth());
  EXPECT_FALSE(b.Borrow().GetArrayIndex());
  EXPECT_FALSE(b.Borrow().GetSortConstructor());
  const auto a = Take(manager_.MakeArraySort(z.Borrow(), b.Borrow()));
  EXPECT_EQ(a, Take(manager_.MakeArraySort(z.Borrow(), b.Borrow())));
  EXPECT_NE(a, Take(manager_.MakeArraySort(b.Borrow(), z.Borrow())));
  EXPECT_EQ(a.Borrow().GetArrayIndex()->GetId(), z.GetId());
  EXPECT_EQ(a.Borrow().GetArrayElement()->GetId(), b.GetId());
  const auto ctor = Take(manager_.DeclareUninterpretedSort("U", 2));
  const auto other = Take(manager_.DeclareUninterpretedSort("U", 2));
  const std::array args = {a.Borrow(), f.Borrow()};
  const std::array reverse = {f.Borrow(), a.Borrow()};
  const auto u = Take(manager_.InstantiateSort(ctor.Borrow(), args));
  EXPECT_EQ(u, Take(manager_.InstantiateSort(ctor.Borrow(), args)));
  EXPECT_NE(u, Take(manager_.InstantiateSort(other.Borrow(), args)));
  EXPECT_NE(u, Take(manager_.InstantiateSort(ctor.Borrow(), reverse)));
  EXPECT_EQ(u.Borrow().GetKind(), SortKind::kUninterpreted);
  EXPECT_EQ(u.Borrow().GetArguments().size(), 2);
  EXPECT_EQ(u.Borrow().GetArguments()[0].GetId(), a.GetId());
  EXPECT_EQ(ctor.Borrow().GetArity(), 2);
  EXPECT_FALSE(ctor.Borrow().GetDatatypeGroup());
  const std::array all = {b.Borrow(), z.Borrow(), r.Borrow(), v.Borrow(),
                          f.Borrow(), a.Borrow(), u.Borrow()};
  for (const auto index : all) {
    for (const auto element : all) {
      const auto array = Take(manager_.MakeArraySort(index, element));
      EXPECT_EQ(array.Borrow().GetArrayIndex()->GetId(), index.GetId());
      EXPECT_EQ(array.Borrow().GetArrayElement()->GetId(), element.GetId());
    }
  }
  Error(manager_.InstantiateSort(ctor.Borrow(), {}), ErrorCode::kInvalidArity);
}

TEST_F(NodeTest, ExactSortBoundariesAndForeignInputs) {
  const Integer zero(0), one(1), two(2), max(UINT32_MAX);
  const auto too_big = Number("4294967296");
  const auto negative = Number("-18446744073709551616");
  Error(manager_.MakeBvSort(zero.Borrow()), ErrorCode::kInvalidParameter);
  Error(manager_.MakeBvSort(negative.Borrow()), ErrorCode::kInvalidParameter);
  Resource(manager_.MakeBvSort(too_big.Borrow()), ResourceKind::kSize);
  EXPECT_EQ(View(Take(manager_.MakeBvSort(max.Borrow()))).GetWidth(),
            UINT32_MAX);
  Error(manager_.MakeFpSort(one.Borrow(), two.Borrow()),
        ErrorCode::kInvalidParameter);
  Error(manager_.MakeFpSort(two.Borrow(), one.Borrow()),
        ErrorCode::kInvalidParameter);
  Resource(manager_.MakeFpSort(max.Borrow(), two.Borrow()),
           ResourceKind::kSize);
  Resource(manager_.MakeFpSort(too_big.Borrow(), two.Borrow()),
           ResourceKind::kSize);
  EXPECT_EQ(View(Fp(2, 3)).GetFpSignificandWidth(), 3);
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.BoolSort());
  const auto local = Take(manager_.BoolSort());
  Error(manager_.MakeArraySort(local.Borrow(), foreign.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.MakeArraySort(foreign.Borrow(), local.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.MakeArraySort({}, local.Borrow()), ErrorCode::kNullObject);
  const auto ctor = Take(manager_.DeclareUninterpretedSort("U", 1));
  const std::array args = {foreign.Borrow()};
  Error(manager_.InstantiateSort(ctor.Borrow(), args),
        ErrorCode::kForeignManager);
  Error(other.InstantiateSort(ctor.Borrow(), args), ErrorCode::kForeignManager);
  Error(manager_.RetainSort(foreign.Borrow()), ErrorCode::kForeignManager);
  Error(manager_.RetainSort({}), ErrorCode::kNullObject);
  EXPECT_NE(local, foreign);
}

}  // namespace
}  // namespace dandelion::testing
