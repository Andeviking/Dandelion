module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
constexpr auto kAnyArity = std::numeric_limits<std::uint64_t>::max();
constexpr auto kCanonical = std::to_array<Operator>({
    {"not", Kind::kNot, 0, 0, 1},
    {"and", Kind::kAnd, 0, 0, 0, kAnyArity},
    {"or", Kind::kOr, 0, 0, 0, kAnyArity},
    {"xor", Kind::kXor, 0, 0, 2, kAnyArity},
    {"=>", Kind::kImplies, 0, 0, 2, kAnyArity},
    {"=", Kind::kEqual, 0, 0, 2, kAnyArity},
    {"distinct", Kind::kDistinct, 0, 0, 2, kAnyArity},
    {"ite", Kind::kIte, 0, 0, 3},
    {"+", Kind::kIntAdd, kIntegers, 0, 2, kAnyArity},
    {"-", Kind::kIntSub, kIntegers, 0, 1, kAnyArity},
    {"*", Kind::kIntMul, kIntegers, 0, 2, kAnyArity},
    {"div", Kind::kIntDiv, kIntegers, 0, 2, kAnyArity},
    {"mod", Kind::kIntMod, kIntegers, 0, 2},
    {"abs", Kind::kIntAbs, kIntegers, 0, 1},
    {"**", Kind::kIntPow, kIntegers, 0, 2},
    {"<", Kind::kIntLt, kIntegers, 0, 2, kAnyArity},
    {"<=", Kind::kIntLe, kIntegers, 0, 2, kAnyArity},
    {">", Kind::kIntGt, kIntegers, 0, 2, kAnyArity},
    {">=", Kind::kIntGe, kIntegers, 0, 2, kAnyArity},
    {"divisible", Kind::kIntDivisible, kIntegers, 1, 1},
    {"bvnot", Kind::kBvNot, kBitVectors, 0, 1},
    {"bvneg", Kind::kBvNeg, kBitVectors, 0, 1},
    {"bvand", Kind::kBvAnd, kBitVectors, 0, 2, kAnyArity},
    {"bvor", Kind::kBvOr, kBitVectors, 0, 2, kAnyArity},
    {"bvxor", Kind::kBvXor, kBitVectors, 0, 2, kAnyArity},
    {"bvadd", Kind::kBvAdd, kBitVectors, 0, 2, kAnyArity},
    {"bvmul", Kind::kBvMul, kBitVectors, 0, 2, kAnyArity},
    {"bvnand", Kind::kBvNand, kBitVectors, 0, 2},
    {"bvnor", Kind::kBvNor, kBitVectors, 0, 2},
    {"bvxnor", Kind::kBvXnor, kBitVectors, 0, 2},
    {"bvsub", Kind::kBvSub, kBitVectors, 0, 2},
    {"bvudiv", Kind::kBvUdiv, kBitVectors, 0, 2},
    {"bvurem", Kind::kBvUrem, kBitVectors, 0, 2},
    {"bvsdiv", Kind::kBvSdiv, kBitVectors, 0, 2},
    {"bvsrem", Kind::kBvSrem, kBitVectors, 0, 2},
    {"bvsmod", Kind::kBvSmod, kBitVectors, 0, 2},
    {"bvshl", Kind::kBvShl, kBitVectors, 0, 2},
    {"bvlshr", Kind::kBvLshr, kBitVectors, 0, 2},
    {"bvashr", Kind::kBvAshr, kBitVectors, 0, 2},
    {"bvcomp", Kind::kBvComp, kBitVectors, 0, 2},
    {"bvult", Kind::kBvUlt, kBitVectors, 0, 2},
    {"bvule", Kind::kBvUle, kBitVectors, 0, 2},
    {"bvugt", Kind::kBvUgt, kBitVectors, 0, 2},
    {"bvuge", Kind::kBvUge, kBitVectors, 0, 2},
    {"bvslt", Kind::kBvSlt, kBitVectors, 0, 2},
    {"bvsle", Kind::kBvSle, kBitVectors, 0, 2},
    {"bvsgt", Kind::kBvSgt, kBitVectors, 0, 2},
    {"bvsge", Kind::kBvSge, kBitVectors, 0, 2},
    {"bvnego", Kind::kBvNego, kBitVectors, 0, 1},
    {"bvuaddo", Kind::kBvUaddo, kBitVectors, 0, 2},
    {"bvsaddo", Kind::kBvSaddo, kBitVectors, 0, 2},
    {"bvumulo", Kind::kBvUmulo, kBitVectors, 0, 2},
    {"bvsmulo", Kind::kBvSmulo, kBitVectors, 0, 2},
    {"bvusubo", Kind::kBvUsubo, kBitVectors, 0, 2},
    {"bvssubo", Kind::kBvSsubo, kBitVectors, 0, 2},
    {"bvsdivo", Kind::kBvSdivo, kBitVectors, 0, 2},
    {"concat", Kind::kBvConcat, kBitVectors, 0, 2},
    {"extract", Kind::kBvExtract, kBitVectors, 2, 1},
    {"repeat", Kind::kBvRepeat, kBitVectors, 1, 1},
    {"zero_extend", Kind::kBvZeroExtend, kBitVectors, 1, 1},
    {"sign_extend", Kind::kBvSignExtend, kBitVectors, 1, 1},
    {"rotate_left", Kind::kBvRotateLeft, kBitVectors, 1, 1},
    {"rotate_right", Kind::kBvRotateRight, kBitVectors, 1, 1},
    {"ubv_to_int", Kind::kUbvToInt, kBitVectors | kIntegers, 0, 1},
    {"sbv_to_int", Kind::kSbvToInt, kBitVectors | kIntegers, 0, 1},
    {"int_to_bv", Kind::kIntToBv, kBitVectors | kIntegers, 1, 1},
    {"fp", Kind::kFpConstruct, kFloats, 0, 3},
    {"fp.abs", Kind::kFpAbs, kFloats, 0, 1},
    {"fp.neg", Kind::kFpNeg, kFloats, 0, 1},
    {"fp.add", Kind::kFpAdd, kFloats, 0, 3},
    {"fp.sub", Kind::kFpSub, kFloats, 0, 3},
    {"fp.mul", Kind::kFpMul, kFloats, 0, 3},
    {"fp.div", Kind::kFpDiv, kFloats, 0, 3},
    {"fp.fma", Kind::kFpFma, kFloats, 0, 4},
    {"fp.sqrt", Kind::kFpSqrt, kFloats, 0, 2},
    {"fp.roundToIntegral", Kind::kFpRoundToIntegral, kFloats, 0, 2},
    {"fp.rem", Kind::kFpRem, kFloats, 0, 2},
    {"fp.min", Kind::kFpMin, kFloats, 0, 2},
    {"fp.max", Kind::kFpMax, kFloats, 0, 2},
    {"fp.eq", Kind::kFpEq, kFloats, 0, 2, kAnyArity},
    {"fp.lt", Kind::kFpLt, kFloats, 0, 2, kAnyArity},
    {"fp.leq", Kind::kFpLeq, kFloats, 0, 2, kAnyArity},
    {"fp.gt", Kind::kFpGt, kFloats, 0, 2, kAnyArity},
    {"fp.geq", Kind::kFpGeq, kFloats, 0, 2, kAnyArity},
    {"fp.isNormal", Kind::kFpIsNormal, kFloats, 0, 1},
    {"fp.isSubnormal", Kind::kFpIsSubnormal, kFloats, 0, 1},
    {"fp.isZero", Kind::kFpIsZero, kFloats, 0, 1},
    {"fp.isInfinite", Kind::kFpIsInfinite, kFloats, 0, 1},
    {"fp.isNaN", Kind::kFpIsNaN, kFloats, 0, 1},
    {"fp.isNegative", Kind::kFpIsNegative, kFloats, 0, 1},
    {"fp.isPositive", Kind::kFpIsPositive, kFloats, 0, 1},
    {"to_fp", Kind::kFpFromBits, kFloats, 2, 1, 2},
    {"to_fp_unsigned", Kind::kFpFromUbv, kFloats, 2, 2},
    {"fp.to_sbv", Kind::kFpToSbv, kFloats, 1, 2},
    {"fp.to_ubv", Kind::kFpToUbv, kFloats, 1, 2},
    {"select", Kind::kArraySelect, kArrays, 0, 2},
    {"store", Kind::kArrayStore, kArrays, 0, 3},
    {"const", Kind::kArrayConst, kArrays, 0, 1},
});
constexpr auto kAliases = std::to_array<Operator>({
    {"bv2nat", Kind::kUbvToInt, kBitVectors | kIntegers, 0, 1},
    {"int2bv", Kind::kIntToBv, kBitVectors | kIntegers, 1, 1},
});
constexpr auto kByName = [] {
  std::array<Operator, kCanonical.size() + kAliases.size()> result{};
  std::ranges::copy(kAliases,
                    std::ranges::copy(kCanonical, result.begin()).out);
  std::ranges::sort(result, {}, &Operator::name);
  return result;
}();
constexpr auto kByKind = [] {
  std::array<const Operator*, std::to_underlying(Kind::kArrayConst) + 1>
      result{};
  for (const auto& op : kCanonical) result[std::to_underlying(op.kind)] = &op;
  result[std::to_underlying(Kind::kIntNeg)] =
      result[std::to_underlying(Kind::kIntSub)];
  for (auto kind : {Kind::kFpFromSbv, Kind::kFpFromRational, Kind::kFpConvert})
    result[std::to_underlying(kind)] =
        result[std::to_underlying(Kind::kFpFromBits)];
  return result;
}();
static_assert(std::ranges::all_of(
    std::span(kByKind).subspan(std::to_underlying(Kind::kNot)),
    [](const Operator* op) { return op != nullptr; }));
constexpr auto kShortRounding =
    std::to_array<std::string_view>({"RNE", "RNA", "RTP", "RTN", "RTZ"});
constexpr auto kLongRounding = std::to_array<std::string_view>(
    {"roundNearestTiesToEven", "roundNearestTiesToAway", "roundTowardPositive",
     "roundTowardNegative", "roundTowardZero"});
constexpr auto kSpecials =
    std::to_array<std::string_view>({"NaN", "+oo", "-oo", "+zero", "-zero"});
}  // namespace

const Operator* FindOperator(std::string_view name, bool indexed) noexcept {
  const auto found =
      std::ranges::lower_bound(kByName, name, {}, &Operator::name);
  if (found != kByName.end() && found->name == name &&
      indexed == (found->indices != 0))
    return &*found;
  return nullptr;
}
const Operator& OperatorFor(Kind kind) noexcept {
  return *kByKind[std::to_underlying(kind)];
}
bool IsRestrictedInt(Kind kind) noexcept {
  return kind == Kind::kIntAdd || kind == Kind::kIntSub ||
         (kind >= Kind::kIntLt && kind <= Kind::kIntGe);
}
bool IsActiveTerm(const Profile& profile, std::string_view name) noexcept {
  if (name == "true" || name == "false") return true;
  if (profile.Has(kFloats) && (FindRoundingMode(name) || name == "fp.to_real"))
    return true;
  if (profile.Has(kReals) &&
      (name == "/" || name == "+" || name == "-" || name == "*" ||
       name == "<" || name == "<=" || name == ">" || name == ">="))
    return true;
  if (profile.Has(kRealInt) &&
      (name == "to_real" || name == "to_int" || name == "is_int"))
    return true;
  if (const auto* op = FindOperator(name, false))
    return profile.Has(op->features) ||
           (profile.Has(kRestrictedIntegers) && IsRestrictedInt(op->kind));
  return false;
}
bool IsKnownExcludedTerm(std::string_view name) noexcept {
  constexpr auto known = std::to_array<std::string_view>({"/",
                                                          "@",
                                                          "fp.to_real",
                                                          "fp.to_ieee_bv",
                                                          "bv2int",
                                                          "to_real",
                                                          "to_int",
                                                          "is_int",
                                                          "is_digit",
                                                          "str.++",
                                                          "str.len",
                                                          "str.<",
                                                          "str.<=",
                                                          "str.at",
                                                          "str.substr",
                                                          "str.prefixof",
                                                          "str.suffixof",
                                                          "str.contains",
                                                          "str.indexof",
                                                          "str.replace",
                                                          "str.replace_all",
                                                          "str.replace_re",
                                                          "str.replace_re_all",
                                                          "str.to_re",
                                                          "str.in_re",
                                                          "str.is_digit",
                                                          "str.to_code",
                                                          "str.from_code",
                                                          "str.to_int",
                                                          "str.from_int",
                                                          "re.none",
                                                          "re.all",
                                                          "re.allchar",
                                                          "re.++",
                                                          "re.union",
                                                          "re.inter",
                                                          "re.*",
                                                          "re.+",
                                                          "re.opt",
                                                          "re.range",
                                                          "re.comp",
                                                          "re.diff",
                                                          "re.^",
                                                          "re.loop"});
  return std::ranges::find(known, name) != known.end();
}
std::optional<RoundingMode> FindRoundingMode(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kShortRounding.size(); ++i)
    if (name == kShortRounding[i] || name == kLongRounding[i])
      return static_cast<RoundingMode>(i);
  return std::nullopt;
}
std::optional<FpSpecial> FindFpSpecial(std::string_view name) noexcept {
  const auto found = std::ranges::find(kSpecials, name);
  if (found == kSpecials.end()) return std::nullopt;
  return static_cast<FpSpecial>(found - kSpecials.begin());
}
std::string_view RoundingSpelling(RoundingMode mode) noexcept {
  return kShortRounding[std::to_underlying(mode)];
}
std::string_view SpecialSpelling(FpSpecial special) noexcept {
  return kSpecials[std::to_underlying(special)];
}
}  // namespace dandelion::frontend::detail
