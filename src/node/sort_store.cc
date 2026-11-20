module dandelion.node;
import :internal;
import dandelion.util.numeric;
import std;

namespace dandelion::detail {
std::size_t SortHash::operator()(SortKey key) const noexcept {
  auto hash = Append(0x243f6a8885a308d3ULL, std::to_underlying(key.kind));
  hash = Append(hash, key.first);
  hash = Append(hash, key.second);
  hash = Append(hash, key.constructor);
  for (const auto arg : key.arguments) hash = Append(hash, arg);
  return static_cast<std::size_t>(hash);
}
bool SortEqual::operator()(SortKey a, SortKey b) const noexcept {
  return a.kind == b.kind && a.first == b.first && a.second == b.second &&
         a.constructor == b.constructor &&
         std::ranges::equal(a.arguments, b.arguments);
}
std::uint32_t PositiveWidth(IntegerView value, std::uint32_t minimum) {
  if (value.Sign() <= 0 ||
      value.Compare(IntegerView(static_cast<std::int64_t>(minimum))) < 0) {
    Fail(ErrorCode::kInvalidParameter);
  }
  const auto width = value.TryUint32();
  if (!width) Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return *width;
}
std::uint32_t CheckedFpWidth(std::uint32_t exponent,
                             std::uint32_t significand) {
  if (significand > std::numeric_limits<std::uint32_t>::max() - exponent)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return exponent + significand;
}
SortId InternSort(ManagerState* state, SortKey key) {
#ifdef DANDELION_NODE_TESTING
  ++state->sort_table_lookups;
#endif
  const auto found = state->sort_index.find(key);
  if (found != state->sort_index.end()) return (*found)->id;
  const auto id = NextStoreId(state, state->sorts.size());
  AllocationCheckpoint(state);
  auto record =
      std::make_unique<SortRecord>(id, key.kind, key.first, key.second,
                                   key.constructor, std::vector<SortId>{});
  AllocationCheckpoint(state);
  record->arguments.assign(key.arguments.begin(), key.arguments.end());
  ReserveAppend(state, state->sorts);
  AllocationCheckpoint(state);
  state->sort_index.insert(record.get());
  state->sorts.push_back(std::move(record));
  return id;
}
SortId BvSortId(ManagerState* state, std::uint64_t width) {
  Require(width != 0, ErrorCode::kInvalidParameter);
  if (width > std::numeric_limits<std::uint32_t>::max())
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  const SortKey key{
      SortKind::kBitVector, static_cast<std::uint32_t>(width), 0, 0, {}};
  if (width > state->bv_sorts.size()) return InternSort(state, key);
  auto& id = state->bv_sorts[width - 1];
  if (!id) id = InternSort(state, key);
  return id;
}
SortId InstantiateSortId(ManagerState* state, SortConstructorId constructor,
                         std::span<const SortId> arguments) {
  const auto& record = ConstructorAt(state, constructor);
  return InternSort(
      state, {record.group ? SortKind::kDatatype : SortKind::kUninterpreted, 0,
              0, constructor, arguments});
}
}  // namespace dandelion::detail

namespace dandelion {
using namespace detail;

bool operator==(const SortRange::Iterator& left,
                const SortRange::Iterator& right) noexcept {
  return left.state_ == right.state_ && left.current_ == right.current_;
}
SortRange::SortRange(ManagerState* state, std::span<const SortId> ids) noexcept
    : state_(state), ids_(ids) {}
SortRange::Iterator::Iterator(ManagerState* state,
                              const SortId* current) noexcept
    : state_(state), current_(current) {}
SortView SortRange::Iterator::operator*() const noexcept {
  return Access::ViewOf<SortView>(state_, *current_);
}
SortRange::Iterator& SortRange::Iterator::operator++() noexcept {
  ++current_;
  return *this;
}
std::size_t SortRange::size() const noexcept { return ids_.size(); }
bool SortRange::empty() const noexcept { return ids_.empty(); }
SortView SortRange::operator[](std::size_t index) const noexcept {
  return Access::ViewOf<SortView>(state_, ids_[index]);
}
SortRange::Iterator SortRange::begin() const noexcept {
  return Iterator(state_, ids_.data());
}
SortRange::Iterator SortRange::end() const noexcept {
  return Iterator(state_,
                  ids_.empty() ? ids_.data() : ids_.data() + ids_.size());
}
SortKind SortView::GetKind() const noexcept { return SortAt(state_, id_).kind; }
std::optional<std::uint32_t> SortView::GetWidth() const noexcept {
  if (GetKind() != SortKind::kBitVector) return std::nullopt;
  return static_cast<std::uint32_t>(SortAt(state_, id_).first);
}
std::optional<FpFormat> SortView::GetFpFormat() const noexcept {
  const auto& sort = SortAt(state_, id_);
  if (sort.kind != SortKind::kFloatingPoint) return std::nullopt;
  return FpFormat::FromCanonicalWidths(static_cast<std::uint32_t>(sort.first),
                                       static_cast<std::uint32_t>(sort.second));
}
std::optional<SortView> SortView::GetArrayIndex() const noexcept {
  if (GetKind() != SortKind::kArray) return std::nullopt;
  return Access::ViewOf<SortView>(state_, SortAt(state_, id_).first);
}
std::optional<SortView> SortView::GetArrayElement() const noexcept {
  if (GetKind() != SortKind::kArray) return std::nullopt;
  return Access::ViewOf<SortView>(state_, SortAt(state_, id_).second);
}
std::optional<SortConstructorView> SortView::GetSortConstructor()
    const noexcept {
  const auto& record = SortAt(state_, id_);
  if (!record.constructor) return std::nullopt;
  return Access::ViewOf<SortConstructorView>(state_, record.constructor);
}
SortRange SortView::GetArguments() const noexcept {
  return Access::Range(state_, SortAt(state_, id_).arguments);
}
std::uint64_t SortConstructorView::GetArity() const noexcept {
  return ConstructorAt(state_, id_).arity;
}
std::string_view SortConstructorView::GetNameHint() const noexcept {
  return ConstructorAt(state_, id_).name;
}
bool SortConstructorView::IsDatatype() const noexcept {
  return ConstructorAt(state_, id_).group != 0;
}
std::optional<DatatypeGroupView> SortConstructorView::GetDatatypeGroup()
    const noexcept {
  const auto group = ConstructorAt(state_, id_).group;
  if (!group) return std::nullopt;
  return Access::ViewOf<DatatypeGroupView>(state_, group);
}

Result<Sort> NodeManager::BoolSort() {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = ScalarSortId(state, SortKind::kBool);
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
Result<Sort> NodeManager::IntSort() {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = ScalarSortId(state, SortKind::kInt);
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
Result<Sort> NodeManager::RoundingModeSort() {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = ScalarSortId(state, SortKind::kRoundingMode);
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
namespace {
Sort RetainBvSort(ManagerState* state, std::uint64_t width) {
  // Acquire the result's reference before first-use sort publication.
  StateLease lease(state);
  const auto id = BvSortId(state, width);
  lease.Transfer();
  return Access::Own<SortView>(state, id);
}
}  // namespace
Result<Sort> NodeManager::MakeBvSort(IntegerView width) {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    return RetainBvSort(state, PositiveWidth(width));
  });
}
Result<Sort> NodeManager::MakeBvSort(std::uint64_t width) {
  return Guard<Sort>([&] { return RetainBvSort(CheckedState(*this), width); });
}
Result<Sort> NodeManager::MakeFpSort(IntegerView exponent,
                                     IntegerView significand) {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    const auto format = FpFormat::Make(exponent, significand);
    if (!format) {
      using numeric::NumericError;
      const auto error = format.error();
      std::uint64_t parameter = std::numeric_limits<std::uint64_t>::max();
      if (error == NumericError::kInvalidArgument) {
        parameter = exponent.Compare(IntegerView(2)) < 0 ? 0 : 1;
      } else if (error == NumericError::kSizeLimit) {
        if (!exponent.TryUint32())
          parameter = 0;
        else if (!significand.TryUint32())
          parameter = 1;
      }
      throw Failure{TranslateNumericError(
          error, false, std::numeric_limits<std::uint64_t>::max(), parameter)};
    }
    StateLease lease(state);
    const auto id = InternSort(state, {SortKind::kFloatingPoint,
                                       format->ExponentWidth(),
                                       format->SignificandWidth(),
                                       0,
                                       {}});
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
Result<Sort> NodeManager::MakeArraySort(SortView index, SortView element) {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, index, 0);
    CheckView(state, element, 1);
    StateLease lease(state);
    const auto id = InternSort(
        state, {SortKind::kArray, index.GetId(), element.GetId(), 0, {}});
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
Result<SortConstructor> NodeManager::DeclareUninterpretedSort(
    std::string_view name_hint, std::uint64_t arity) {
  return Guard<SortConstructor>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = NextStoreId(state, state->sort_constructors.size());
    AllocationCheckpoint(state);
    auto record = std::make_unique<SortConstructorRecord>(
        id, std::string(name_hint), arity, 0, 0);
    ReserveAppend(state, state->sort_constructors);
    state->sort_constructors.push_back(std::move(record));
    lease.Transfer();
    return Access::Own<SortConstructorView>(state, id);
  });
}
Result<Sort> NodeManager::InstantiateSort(SortConstructorView constructor,
                                          std::span<const SortView> arguments) {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, constructor);
    Require(arguments.size() == constructor.GetArity(),
            ErrorCode::kInvalidArity);
    AllocationCheckpoint(state);
    std::vector<SortId> ids;
    ids.reserve(arguments.size());
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      CheckView(state, arguments[i], i);
      ids.push_back(arguments[i].GetId());
    }
    StateLease lease(state);
    const auto id = InstantiateSortId(state, constructor.GetId(), ids);
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
}  // namespace dandelion
