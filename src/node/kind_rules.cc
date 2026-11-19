module dandelion.node;
import :internal;
import std;

namespace dandelion::detail {
namespace {
SortId BvSortId(ManagerState* state, std::uint64_t width) {
  if (width > std::numeric_limits<std::uint32_t>::max())
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return InternSort(
      state,
      {SortKind::kBitVector, static_cast<std::uint32_t>(width), 0, 0, {}});
}
struct BuiltinShape {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t arity;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool variadic;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  PayloadTag payload;
};

BuiltinShape Shape(Kind kind) {
  switch (kind) {
    case Kind::kAnd:
    case Kind::kOr:
    case Kind::kXor:
    case Kind::kImplies:
    case Kind::kEqual:
    case Kind::kDistinct:
    case Kind::kIntAdd:
    case Kind::kIntSub:
    case Kind::kIntMul:
    case Kind::kIntDiv:
    case Kind::kIntLt:
    case Kind::kIntLe:
    case Kind::kIntGt:
    case Kind::kIntGe:
    case Kind::kBvAnd:
    case Kind::kBvOr:
    case Kind::kBvXor:
    case Kind::kBvAdd:
    case Kind::kBvMul:
    case Kind::kFpEq:
    case Kind::kFpLt:
    case Kind::kFpLeq:
    case Kind::kFpGt:
    case Kind::kFpGeq:
      return {2, true, PayloadTag::kNone};
    case Kind::kNot:
    case Kind::kIntNeg:
    case Kind::kIntAbs:
    case Kind::kBvNot:
    case Kind::kBvNeg:
    case Kind::kBvNego:
    case Kind::kUbvToInt:
    case Kind::kSbvToInt:
    case Kind::kFpAbs:
    case Kind::kFpNeg:
    case Kind::kFpIsNormal:
    case Kind::kFpIsSubnormal:
    case Kind::kFpIsZero:
    case Kind::kFpIsInfinite:
    case Kind::kFpIsNaN:
    case Kind::kFpIsNegative:
    case Kind::kFpIsPositive:
    case Kind::kArrayConst:
      return {1, false, PayloadTag::kNone};
    case Kind::kIntDivisible:
    case Kind::kBvRepeat:
    case Kind::kBvZeroExtend:
    case Kind::kBvSignExtend:
    case Kind::kBvRotateLeft:
    case Kind::kBvRotateRight:
    case Kind::kIntToBv:
      return {1, false, PayloadTag::kIndex1};
    case Kind::kBvExtract:
    case Kind::kFpFromBits:
      return {1, false, PayloadTag::kIndex2};
    case Kind::kFpFromRational:
      return {1, false, PayloadTag::kRationalConversion};
    case Kind::kIte:
    case Kind::kFpConstruct:
    case Kind::kFpAdd:
    case Kind::kFpSub:
    case Kind::kFpMul:
    case Kind::kFpDiv:
    case Kind::kArrayStore:
      return {3, false, PayloadTag::kNone};
    case Kind::kFpFma:
      return {4, false, PayloadTag::kNone};
    case Kind::kFpToSbv:
    case Kind::kFpToUbv:
      return {2, false, PayloadTag::kIndex1};
    case Kind::kFpConvert:
    case Kind::kFpFromSbv:
    case Kind::kFpFromUbv:
      return {2, false, PayloadTag::kIndex2};
    case Kind::kIntMod:
    case Kind::kIntPow:
    case Kind::kBvNand:
    case Kind::kBvNor:
    case Kind::kBvXnor:
    case Kind::kBvSub:
    case Kind::kBvUdiv:
    case Kind::kBvUrem:
    case Kind::kBvSdiv:
    case Kind::kBvSrem:
    case Kind::kBvSmod:
    case Kind::kBvShl:
    case Kind::kBvLshr:
    case Kind::kBvAshr:
    case Kind::kBvComp:
    case Kind::kBvUlt:
    case Kind::kBvUle:
    case Kind::kBvUgt:
    case Kind::kBvUge:
    case Kind::kBvSlt:
    case Kind::kBvSle:
    case Kind::kBvSgt:
    case Kind::kBvSge:
    case Kind::kBvUaddo:
    case Kind::kBvSaddo:
    case Kind::kBvUmulo:
    case Kind::kBvSmulo:
    case Kind::kBvUsubo:
    case Kind::kBvSsubo:
    case Kind::kBvSdivo:
    case Kind::kBvConcat:
    case Kind::kFpSqrt:
    case Kind::kFpRoundToIntegral:
    case Kind::kFpRem:
    case Kind::kFpMin:
    case Kind::kFpMax:
    case Kind::kArraySelect:
      return {2, false, PayloadTag::kNone};
    case Kind::kInvalid:
    case Kind::kBoolValue:
    case Kind::kIntValue:
    case Kind::kBvValue:
    case Kind::kFpValue:
    case Kind::kRoundingModeValue:
    case Kind::kApply:
      break;
  }
  Fail(ErrorCode::kInvalidKind);
}

std::uint32_t PositiveIndexWidth(const QueryKey& key, std::size_t index = 0,
                                 std::uint32_t minimum = 1) {
  if (key.tag == PayloadTag::kIndexWide)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  const auto value = key.indices[index];
  Require(value >= minimum, ErrorCode::kInvalidParameter);
  return value;
}
}  // namespace

SortId InferBuiltinSort(ManagerState* state, const QueryKey& key) {
#ifdef DANDELION_NODE_TESTING
  ++state->type_evaluations;
#endif
  const auto children = key.children;
  const auto id = [&](std::size_t index) {
    return CheckNode(state, children[index], index)->sort_id;
  };
  const auto sort = [&](std::size_t index) -> const SortRecord& {
    return SortAt(state, id(index));
  };
  const auto expect = [&](std::size_t index,
                          SortKind kind) -> const SortRecord& {
    const auto& record = sort(index);
    Require(record.kind == kind, ErrorCode::kSortMismatch, index);
    return record;
  };
  const auto match = [&](std::size_t first, SortId expected) {
    for (auto i = first + 1; i < children.size(); ++i)
      Require(id(i) == expected, ErrorCode::kSortMismatch, i);
  };
  const auto same = [&](std::size_t first, SortKind kind) {
    const auto expected = expect(first, kind).id;
    match(first, expected);
    return expected;
  };
  const auto boolean = [&] { return ScalarSortId(state, SortKind::kBool); };
  const auto integer = [&] { return ScalarSortId(state, SortKind::kInt); };
  switch (key.kind) {
    case Kind::kNot:
    case Kind::kAnd:
    case Kind::kOr:
    case Kind::kXor:
    case Kind::kImplies:
      return same(0, SortKind::kBool);
    case Kind::kEqual:
    case Kind::kDistinct:
      match(0, id(0));
      return boolean();
    case Kind::kIte: {
      expect(0, SortKind::kBool);
      const auto branch = id(1);
      Require(branch == id(2), ErrorCode::kSortMismatch, 2);
      return branch;
    }
    case Kind::kIntNeg:
    case Kind::kIntAdd:
    case Kind::kIntSub:
    case Kind::kIntMul:
    case Kind::kIntDiv:
    case Kind::kIntMod:
    case Kind::kIntAbs:
    case Kind::kIntPow:
      return same(0, SortKind::kInt);
    case Kind::kIntLt:
    case Kind::kIntLe:
    case Kind::kIntGt:
    case Kind::kIntGe:
      same(0, SortKind::kInt);
      return boolean();
    case Kind::kIntDivisible:
      expect(0, SortKind::kInt);
      Require(key.tag == PayloadTag::kIndexWide || key.indices[0] != 0,
              ErrorCode::kInvalidParameter);
      return boolean();
    case Kind::kBvNot:
    case Kind::kBvNeg:
    case Kind::kBvAnd:
    case Kind::kBvOr:
    case Kind::kBvXor:
    case Kind::kBvAdd:
    case Kind::kBvMul:
    case Kind::kBvNand:
    case Kind::kBvNor:
    case Kind::kBvXnor:
    case Kind::kBvSub:
    case Kind::kBvUdiv:
    case Kind::kBvUrem:
    case Kind::kBvSdiv:
    case Kind::kBvSrem:
    case Kind::kBvSmod:
    case Kind::kBvShl:
    case Kind::kBvLshr:
    case Kind::kBvAshr:
      return same(0, SortKind::kBitVector);
    case Kind::kBvComp:
      same(0, SortKind::kBitVector);
      return BvSortId(state, 1);
    case Kind::kBvUlt:
    case Kind::kBvUle:
    case Kind::kBvUgt:
    case Kind::kBvUge:
    case Kind::kBvSlt:
    case Kind::kBvSle:
    case Kind::kBvSgt:
    case Kind::kBvSge:
    case Kind::kBvNego:
    case Kind::kBvUaddo:
    case Kind::kBvSaddo:
    case Kind::kBvUmulo:
    case Kind::kBvSmulo:
    case Kind::kBvUsubo:
    case Kind::kBvSsubo:
    case Kind::kBvSdivo:
      same(0, SortKind::kBitVector);
      return boolean();
    case Kind::kBvConcat: {
      const auto left = expect(0, SortKind::kBitVector).first;
      const auto right = expect(1, SortKind::kBitVector).first;
      return BvSortId(state, static_cast<std::uint64_t>(left) + right);
    }
    case Kind::kBvExtract: {
      const auto width = expect(0, SortKind::kBitVector).first;
      const auto [high, low] = key.indices;
      Require(low <= high && high < width, ErrorCode::kInvalidParameter);
      return BvSortId(state, static_cast<std::uint64_t>(high) - low + 1);
    }
    case Kind::kBvRepeat: {
      const auto width = expect(0, SortKind::kBitVector).first;
      return BvSortId(
          state, static_cast<std::uint64_t>(PositiveIndexWidth(key)) * width);
    }
    case Kind::kBvZeroExtend:
    case Kind::kBvSignExtend: {
      const auto width = expect(0, SortKind::kBitVector).first;
      const auto amount = PositiveIndexWidth(key, 0, 0);
      return BvSortId(state, static_cast<std::uint64_t>(width) + amount);
    }
    case Kind::kBvRotateLeft:
    case Kind::kBvRotateRight:
      return expect(0, SortKind::kBitVector).id;
    case Kind::kUbvToInt:
    case Kind::kSbvToInt:
      expect(0, SortKind::kBitVector);
      return integer();
    case Kind::kIntToBv:
      expect(0, SortKind::kInt);
      return BvSortId(state, PositiveIndexWidth(key));
    case Kind::kFpConstruct: {
      const auto sign = expect(0, SortKind::kBitVector).first;
      const auto exponent =
          static_cast<std::uint32_t>(expect(1, SortKind::kBitVector).first);
      const auto fraction =
          static_cast<std::uint32_t>(expect(2, SortKind::kBitVector).first);
      Require(sign == 1, ErrorCode::kSortMismatch, 0);
      Require(exponent >= 2, ErrorCode::kInvalidParameter);
      if (fraction == std::numeric_limits<std::uint32_t>::max())
        Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
      const auto significand = fraction + 1;
      CheckedFpWidth(exponent, significand);
      return InternSort(
          state, {SortKind::kFloatingPoint, exponent, significand, 0, {}});
    }
    case Kind::kFpAbs:
    case Kind::kFpNeg:
      return expect(0, SortKind::kFloatingPoint).id;
    case Kind::kFpAdd:
    case Kind::kFpSub:
    case Kind::kFpMul:
    case Kind::kFpDiv:
    case Kind::kFpFma:
    case Kind::kFpSqrt:
    case Kind::kFpRoundToIntegral:
      expect(0, SortKind::kRoundingMode);
      return same(1, SortKind::kFloatingPoint);
    case Kind::kFpRem:
    case Kind::kFpMin:
    case Kind::kFpMax:
      return same(0, SortKind::kFloatingPoint);
    case Kind::kFpEq:
    case Kind::kFpLt:
    case Kind::kFpLeq:
    case Kind::kFpGt:
    case Kind::kFpGeq:
    case Kind::kFpIsNormal:
    case Kind::kFpIsSubnormal:
    case Kind::kFpIsZero:
    case Kind::kFpIsInfinite:
    case Kind::kFpIsNaN:
    case Kind::kFpIsNegative:
    case Kind::kFpIsPositive:
      same(0, SortKind::kFloatingPoint);
      return boolean();
    case Kind::kFpFromBits:
    case Kind::kFpConvert:
    case Kind::kFpFromSbv:
    case Kind::kFpFromUbv:
    case Kind::kFpFromRational: {
      const auto exponent = PositiveIndexWidth(key, 0, 2);
      const auto significand = PositiveIndexWidth(key, 1, 2);
      const auto packed_width = CheckedFpWidth(exponent, significand);
      if (key.kind == Kind::kFpFromBits) {
        Require(expect(0, SortKind::kBitVector).first == packed_width,
                ErrorCode::kSortMismatch, 0);
      } else {
        expect(0, SortKind::kRoundingMode);
        if (key.kind == Kind::kFpConvert) expect(1, SortKind::kFloatingPoint);
        if (key.kind == Kind::kFpFromSbv || key.kind == Kind::kFpFromUbv)
          expect(1, SortKind::kBitVector);
      }
      return InternSort(
          state, {SortKind::kFloatingPoint, exponent, significand, 0, {}});
    }
    case Kind::kFpToSbv:
    case Kind::kFpToUbv:
      expect(0, SortKind::kRoundingMode);
      expect(1, SortKind::kFloatingPoint);
      return BvSortId(state, PositiveIndexWidth(key));
    case Kind::kArraySelect: {
      const auto& array = expect(0, SortKind::kArray);
      Require(id(1) == array.first, ErrorCode::kSortMismatch, 1);
      return array.second;
    }
    case Kind::kArrayStore: {
      const auto& array = expect(0, SortKind::kArray);
      Require(id(1) == array.first, ErrorCode::kSortMismatch, 1);
      Require(id(2) == array.second, ErrorCode::kSortMismatch, 2);
      return array.id;
    }
    case Kind::kArrayConst: {
      const auto& qualifier = SortAt(state, key.input_sort);
      Require(qualifier.kind == SortKind::kArray, ErrorCode::kSortMismatch);
      Require(id(0) == qualifier.second, ErrorCode::kSortMismatch, 0);
      return qualifier.id;
    }
    default:
      std::unreachable();  // Range check excludes dedicated and invalid kinds.
  }
}

namespace {
PayloadTag CheckBuiltinShape([[maybe_unused]] ManagerState* state, Kind kind,
                             std::size_t child_count) {
#ifdef DANDELION_NODE_TESTING
  ++state->builtin_shape_checks;
#endif
  const auto [arity, variadic, tag] = Shape(kind);
  Require(variadic ? child_count >= arity : child_count == arity,
          ErrorCode::kInvalidArity);
  return tag;
}
}  // namespace

void CheckPlainBuiltinShape(ManagerState* state, Kind kind,
                            std::size_t child_count) {
  Require(CheckBuiltinShape(state, kind, child_count) == PayloadTag::kNone,
          ErrorCode::kInvalidParameter);
}

QueryKey BuiltinKey(ManagerState* state, BuiltinOpView op,
                    std::span<const NodeView> children) {
  Require(op.kind >= Kind::kNot && op.kind <= Kind::kArrayConst,
          ErrorCode::kInvalidKind);
  const auto tag = CheckBuiltinShape(state, op.kind, children.size());
  const std::size_t index_count =
      tag == PayloadTag::kIndex1 ? 1
      : (tag == PayloadTag::kIndex2 || tag == PayloadTag::kRationalConversion)
          ? 2
          : 0;
  Require(op.indices.size() == index_count &&
              op.qualifier.has_value() == (op.kind == Kind::kArrayConst) &&
              op.rational.has_value() == (op.kind == Kind::kFpFromRational),
          ErrorCode::kInvalidParameter);
  QueryKey key{op.kind, 0, children, tag};
  if (op.qualifier) {
    CheckView(state, *op.qualifier);
    key.input_sort = op.qualifier->GetId();
  }
  for (std::size_t i = 0; i < index_count; ++i) {
    const auto value = op.indices[i].Borrow();
    if (const auto word = value.TryUint32()) {
      key.indices[i] = *word;
      continue;
    }
    if (value.Sign() < 0)
      Fail(ErrorCode::kInvalidParameter, ResourceKind::kNone,
           std::numeric_limits<std::uint64_t>::max(), i);
    if (index_count == 2) {
      if (op.kind == Kind::kBvExtract) Fail(ErrorCode::kInvalidParameter);
      Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
    }
    key.tag = PayloadTag::kIndexWide;
    key.integer = value;
  }
  key.rational = op.rational;
  return key;
}
}  // namespace dandelion::detail
