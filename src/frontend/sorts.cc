module dandelion.frontend;
import :internal;
import dandelion.util.numeric;
import std;

namespace dandelion::frontend::detail {
std::size_t SortKey::Slot() const noexcept {
  auto hash = TextHash{}(name) ^ static_cast<std::size_t>(indexed);
  for (std::size_t i = 0; i < count; ++i)
    hash ^= std::hash<std::uint64_t>{}(arguments[i]) + std::size_t{0x9e3779b9} +
            (hash << 6) + (hash >> 2);
  return (hash ^ (hash >> 8)) & (kCacheSlots - 1);
}
namespace {
constexpr std::array<std::array<std::int64_t, 2>, 4> kFormats{{
    {5, 11},
    {8, 24},
    {11, 53},
    {15, 113},
}};

template <class Factory>
SortView CachedConcrete(SessionState& session, const SortKey& key,
                        Factory factory) {
  const auto slot = key.Slot();
  if (session.sort_cache) {
    const auto& entry = (*session.sort_cache)[slot];
    if (!entry.sort.IsNull() && entry.key == key) {
      session.Count(&FrontendStats::sort_cache_hits);
      return entry.sort;
    }
  }
  const auto sort = factory();
  if (!session.sort_cache) {
#ifdef DANDELION_FRONTEND_TESTING
    session.Checkpoint();
#endif
    session.sort_cache = std::make_unique<SortCache>();
  }
  (*session.sort_cache)[slot] = {key, sort};
  return sort;
}

class SortIndex {
 public:
  explicit SortIndex(std::uint64_t word) noexcept : word_(word) {}
  explicit SortIndex(numeric::Integer wide) noexcept : wide_(std::move(wide)) {}

  bool Below(std::uint64_t minimum) const noexcept {
    return word_ && *word_ < minimum;
  }

  numeric::IntegerView View() const noexcept {
    return word_ ? numeric::IntegerView::FromUnsigned(*word_) : wide_.Borrow();
  }

 private:
  std::optional<std::uint64_t> word_;
  numeric::Integer wide_;
};

SortIndex ReadIndex(const Syntax& syntax, ExprId expr) {
  const auto token = syntax.GetTokenInfo(expr);
  if (token.kind != TokenKind::kNumeral) [[unlikely]]
    Fail(DiagnosticCode::kInvalidInput, {syntax, expr},
         "Expected a numeral index");
  const auto digits = token.raw;
  if (digits.size() <= 20) {
    if (const auto word = ParseWord(digits)) return SortIndex(*word);
  }
  return SortIndex(
      TakeNumericAt(numeric::Integer::ParseDecimal(digits), {syntax, expr}));
}

void RequireMinimum(const Syntax& syntax, ChildRange indices, std::size_t index,
                    const SortIndex& value, std::uint64_t minimum) {
  if (value.Below(minimum)) [[unlikely]]
    NodeFailure(
        {.code = ErrorCode::kInvalidParameter, .parameter_index = index},
        Location{syntax, indices[index]});
}

void CheckArity(std::size_t actual, std::uint64_t expected, Location where) {
  if (actual != expected)
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong number of sort arguments");
}
void RequireTheory(bool enabled, Location where) {
  if (!enabled)
    Fail(DiagnosticCode::kUnsupported, where,
         "Sort is outside the active signature");
}

class ConcreteSorts {
 public:
  using Value = SortView;
  explicit ConcreteSorts(NodeManager& manager) : manager_(manager) {}
  Value Concrete(SortView sort, Location) const noexcept { return sort; }
  template <class Factory>
  Value CachedNamed(SessionState& session, std::string_view stable_name,
                    std::span<const Value> args, Factory factory) {
    SortKey key{stable_name, {}, args.size()};
    if (args.size() > key.arguments.size()) return factory();
    std::ranges::transform(args, key.arguments.begin(), &SortView::GetId);
    return CachedConcrete(session, key, factory);
  }
  Value Array(std::span<const Value> children, Location where) {
    const auto sort =
        TakeAt(manager_.MakeArraySort(children[0], children[1]), where);
    return sort.Borrow();
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              Location where) {
    const auto sort =
        TakeAt(manager_.InstantiateSort(constructor, children), where);
    return sort.Borrow();
  }

 private:
  NodeManager& manager_;
};

class AliasSorts {
 public:
  using Value = const SortRecipe*;
  explicit AliasSorts(PendingSortAlias& pending) : pending_(pending) {}
  Value Concrete(SortView sort, Location) {
    return pending_.InternRecipe({RecipeKind::kConcrete, sort, {}, 0, {}});
  }
  Value Parameter(std::uint64_t index, Location) {
    return pending_.InternRecipe({RecipeKind::kParameter, {}, {}, index, {}});
  }
  Value Array(std::span<const Value> children, Location) {
    return pending_.InternRecipe(
        {RecipeKind::kArray, {}, {}, 0, {children.begin(), children.end()}});
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              Location) {
    return pending_.InternRecipe({RecipeKind::kApply,
                                  {},
                                  constructor,
                                  0,
                                  {children.begin(), children.end()}});
  }

 private:
  PendingSortAlias& pending_;
};

class FieldSorts {
 public:
  using Value = SortExpr;
  FieldSorts(DatatypeBuilder& builder, const TextMap<DatatypeSort>& forward,
             std::uint64_t slot)
      : builder_(builder), forward_(forward), slot_(slot) {}
  Value Concrete(SortView sort, Location where) {
    return TakeAt(builder_.Concrete(sort), where);
  }
  Value Parameter(std::uint64_t index, Location where) {
    return TakeAt(builder_.Parameter(slot_, index), where);
  }
  Value Array(std::span<const Value> children, Location where) {
    return TakeAt(builder_.Array(children[0], children[1]), where);
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              Location where) {
    return TakeAt(builder_.Apply(constructor, children), where);
  }
  const DatatypeSort* FindForward(std::string_view name) const noexcept {
    const auto it = forward_.find(name);
    return it != forward_.end() ? &it->second : nullptr;
  }
  Value Recursive(std::uint64_t slot, std::span<const Value> children,
                  Location where) {
    return TakeAt(builder_.Recursive(slot, children), where);
  }

 private:
  DatatypeBuilder& builder_;
  const TextMap<DatatypeSort>& forward_;
  std::uint64_t slot_;
};

template <class Target>
Target::Value Expand(Target& target, const SortRecipe* root,
                     std::span<const typename Target::Value> parameters,
                     Location where) {
  using Value = typename Target::Value;
  if (root->kind == RecipeKind::kParameter) return parameters[root->param];
  if (root->kind == RecipeKind::kConcrete)
    return target.Concrete(root->concrete, where);
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    const SortRecipe* recipe;
    std::size_t next = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<Frame> stack{{root}};
  std::unordered_map<const SortRecipe*, Value> memo;
  std::vector<Value> args;
  while (!stack.empty()) {
    auto& [recipe, next] = stack.back();
    if (next != recipe->children.size()) {
      const auto* child = recipe->children[next++];
      if (!memo.contains(child)) stack.push_back({child});
      continue;
    }
    switch (recipe->kind) {
      case RecipeKind::kParameter:
        memo.emplace(recipe, parameters[recipe->param]);
        break;
      case RecipeKind::kConcrete:
        memo.emplace(recipe, target.Concrete(recipe->concrete, where));
        break;
      case RecipeKind::kApply:
      case RecipeKind::kArray:
        args.clear();
        for (const auto* child : recipe->children)
          args.push_back(memo.at(child));
        memo.emplace(recipe,
                     recipe->kind == RecipeKind::kArray
                         ? target.Array(args, where)
                         : target.Apply(recipe->constructor, args, where));
        break;
    }
    stack.pop_back();
  }
  return memo.at(root);
}

template <class Target>
class SortResolver {
 public:
  using Value = typename Target::Value;
  SortResolver(SessionState& session, const Syntax& syntax, Target& target,
               const Parameters* parameters = nullptr)
      : session_(session),
        syntax_(syntax),
        target_(target),
        parameters_(parameters),
        signature_(session.GetSignature()),
        profile_(*signature_.profile) {}
  Value Resolve(ExprId root);

 private:
  Value Construct(ExprId expr, Identifier id, std::span<const Value> args);
  Value ConstructBuiltin(ExprId expr, Identifier id, SpellingId spelling,
                         std::span<const Value> args);

  SessionState& session_;
  const Syntax& syntax_;
  Target& target_;
  const Parameters* parameters_;
  const Signature& signature_;
  const Profile& profile_;
};

template <class Target>
auto SortResolver<Target>::Resolve(ExprId root) -> Value {
  if (const auto* reduced = syntax_.ReducedSort<Value>(root)) return *reduced;
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    ExprId expr;
    Identifier head;
    ChildRange args;
    std::size_t value_base;
    std::size_t next = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  const auto frame = [&](ExprId id, std::size_t base) -> Frame {
    const auto parts = syntax_.Children(id);
    const bool application =
        !parts.empty() && syntax_.Spelling(parts[0]) != SpellingId::kUnderscore;
    const auto head = GetIdentifier(syntax_, application ? parts[0] : id);
    const auto args = application ? parts.subspan(1) : ChildRange{};
    if (args.size() != 2 && head.indices.empty() &&
        syntax_.Spelling(head.name) == SpellingId::kArray) {
      bool shadowed = false;
      if (!profile_.Has(kArrays)) {
        shadowed = (parameters_ && parameters_->contains("Array")) ||
                   signature_.sorts.contains("Array");
        if constexpr (std::is_same_v<Target, FieldSorts>)
          shadowed = shadowed || target_.FindForward("Array");
      }
      if (!shadowed) CheckArity(args.size(), 2, {syntax_, id});
    }
    return {id, head, args, base};
  };
  const auto first = frame(root, 0);
  if (first.args.empty()) return Construct(root, first.head, {});
  std::vector<Frame> stack{first};
  std::vector<Value> values;
  while (!stack.empty()) {
    auto& top = stack.back();
    if (top.next < top.args.size()) {
      const auto child = top.args[top.next++];
      if (const auto* reduced = syntax_.ReducedSort<Value>(child))
        values.push_back(*reduced);
      else
        stack.push_back(frame(child, values.size()));
      continue;
    }
    const auto arguments =
        std::span<const Value>(values).subspan(top.value_base);
    const auto value = Construct(top.expr, top.head, arguments);
    values.resize(top.value_base);
    values.push_back(value);
    stack.pop_back();
  }
  return values.front();
}

template <class Target>
auto SortResolver<Target>::Construct(ExprId expr, Identifier id,
                                     std::span<const Value> args) -> Value {
  const auto token = syntax_.GetTokenInfo(id.name);
  const auto spelling = token.spelling_id;
  const Location where{syntax_, expr};
  if (IsActiveSort(profile_, spelling))
    return ConstructBuiltin(expr, id, spelling, args);
  const auto name = syntax_.Name(token);
  if (IsReservedName(name)) [[unlikely]]
    Fail(DiagnosticCode::kInvalidInput, {syntax_, id.name},
         "Reserved sort name");
  if (!id.indices.empty()) return ConstructBuiltin(expr, id, spelling, args);

  if constexpr (!std::is_same_v<Target, ConcreteSorts>) {
    const auto parameter = parameters_->find(name);
    if (parameter != parameters_->end()) {
      CheckArity(args.size(), 0, where);
      return target_.Parameter(parameter->second, where);
    }
  }
  if constexpr (std::is_same_v<Target, FieldSorts>) {
    if (const auto* forward = target_.FindForward(name)) {
      CheckArity(args.size(), forward->arity, where);
      return target_.Recursive(forward->slot, args, where);
    }
  }
  const auto bound = signature_.sorts.find(name);
  if (!bound) return ConstructBuiltin(expr, id, spelling, args);

  const auto& binding = bound->second;
  if (const auto* concrete = std::get_if<Sort>(&binding)) {
    CheckArity(args.size(), 0, where);
    return target_.Concrete(concrete->Borrow(), where);
  }
  const auto build = [&]() -> Value {
    if (const auto* alias = std::get_if<Alias>(&binding)) {
      CheckArity(args.size(), alias->arity, where);
      return Expand(target_, alias->root, args, where);
    }
    if (const auto* datatype = std::get_if<DatatypeSort>(&binding)) {
      CheckArity(args.size(), datatype->arity, where);
      const auto group =
          session_.Prefix().GetDatatypeGroups()[datatype->group].Borrow();
      return target_.Apply(
          TakeAt(group.GetSortConstructor(datatype->slot), where), args, where);
    }
    const auto constructor = std::get<SortConstructorView>(binding);
    CheckArity(args.size(), constructor.GetArity(), where);
    return target_.Apply(constructor, args, where);
  };
  if constexpr (std::is_same_v<Target, ConcreteSorts>) {
    return target_.CachedNamed(session_, bound->first, args, build);
  } else {
    return build();
  }
}

template <class Target>
auto SortResolver<Target>::ConstructBuiltin(ExprId expr, Identifier id,
                                            SpellingId spelling,
                                            std::span<const Value> args)
    -> Value {
  const Location where{syntax_, expr};
  auto& manager = session_.manager;
  const auto concrete = [&](Result<Sort> result) {
    const auto sort = TakeAt(std::move(result), where);
    return sort.Borrow();
  };
  const auto cached = [&](std::string_view stable_name, auto build) {
    const auto sort = [&] {
      if constexpr (std::is_same_v<Target, ConcreteSorts>)
        return target_.CachedNamed(session_, stable_name, args, build);
      else
        return build();
    }();
    return target_.Concrete(sort, where);
  };
  if (spelling == SpellingId::kBitVec ||
      spelling == SpellingId::kFloatingPoint) {
    CheckArity(args.size(), 0, where);
    const bool is_bv = spelling == SpellingId::kBitVec;
    if (id.indices.size() != (is_bv ? 1u : 2u)) [[unlikely]]
      Fail(DiagnosticCode::kInvalidInput, where, "Wrong number of indices");
    const auto first = ReadIndex(syntax_, id.indices[0]);
    if (is_bv) {
      RequireMinimum(syntax_, id.indices, 0, first, 1);
      RequireTheory(profile_.Has(kBitVectors) || profile_.Has(kFloats), where);
      return target_.Concrete(
          CachedBitVectorSort(session_, first.View(), where), where);
    }
    const auto second = ReadIndex(syntax_, id.indices[1]);
    RequireMinimum(syntax_, id.indices, 0, first, 2);
    RequireMinimum(syntax_, id.indices, 1, second, 2);
    RequireTheory(profile_.Has(kFloats), where);
    return target_.Concrete(
        CachedFloatingPointSort(session_, first.View(), second.View(), where),
        where);
  }
  if (spelling >= SpellingId::kReal && spelling <= SpellingId::kFunctionSort)
    Fail(DiagnosticCode::kUnsupported, where,
         "Sort is outside the input profile");
  if (!id.indices.empty()) [[unlikely]]
    Fail(DiagnosticCode::kInvalidInput, where, "Unknown indexed sort");
  switch (spelling) {
    case SpellingId::kArray:
      RequireTheory(profile_.Has(kArrays), where);
      if constexpr (std::is_same_v<Target, ConcreteSorts>)
        return target_.CachedNamed(session_, "Array", args,
                                   [&] { return target_.Array(args, where); });
      else
        return target_.Array(args, where);
    case SpellingId::kBool:
      CheckArity(args.size(), 0, where);
      return cached("Bool", [&] { return concrete(manager.BoolSort()); });
    case SpellingId::kInt:
      CheckArity(args.size(), 0, where);
      RequireTheory(
          profile_.Has(kIntegers) || profile_.Has(kRestrictedIntegers), where);
      return cached("Int", [&] { return concrete(manager.IntSort()); });
    case SpellingId::kRoundingMode:
      CheckArity(args.size(), 0, where);
      RequireTheory(profile_.Has(kFloats), where);
      return cached("RoundingMode",
                    [&] { return concrete(manager.RoundingModeSort()); });
    case SpellingId::kFloat16:
    case SpellingId::kFloat32:
    case SpellingId::kFloat64:
    case SpellingId::kFloat128: {
      CheckArity(args.size(), 0, where);
      RequireTheory(profile_.Has(kFloats), where);
      const auto [exponent, significand] =
          kFormats[std::to_underlying(spelling) -
                   std::to_underlying(SpellingId::kFloat16)];
      return target_.Concrete(
          CachedFloatingPointSort(session_, numeric::IntegerView(exponent),
                                  numeric::IntegerView(significand), where),
          where);
    }
    default:
      Fail(DiagnosticCode::kInvalidInput, {syntax_, id.name}, "Unknown sort");
  }
}
}  // namespace

bool IsActiveSort(const Profile& profile, SpellingId spelling) noexcept {
  switch (spelling) {
    case SpellingId::kBool:
      return true;
    case SpellingId::kInt:
      return profile.Has(kIntegers) || profile.Has(kRestrictedIntegers);
    case SpellingId::kArray:
      return profile.Has(kArrays);
    case SpellingId::kRoundingMode:
    case SpellingId::kFloat16:
    case SpellingId::kFloat32:
    case SpellingId::kFloat64:
    case SpellingId::kFloat128:
      return profile.Has(kFloats);
    case SpellingId::kReal:
      return profile.Has(kFloats) || profile.Has(kReals);
    default:
      return false;
  }
}

SortView CachedBitVectorSort(SessionState& session, std::uint64_t width,
                             Location where) {
  return CachedConcrete(session, SortKey{"BitVec", {width}, 1, true}, [&] {
    const auto sort = TakeAt(session.manager.MakeBvSort(width), where);
    return sort.Borrow();
  });
}
SortView CachedBitVectorSort(SessionState& session, numeric::IntegerView width,
                             Location where) {
  if (const auto word = width.TryUint64())
    return CachedBitVectorSort(session, *word, where);
  const auto sort = TakeAt(session.manager.MakeBvSort(width), where);
  return sort.Borrow();
}

SortView CachedFloatingPointSort(SessionState& session,
                                 numeric::IntegerView exponent,
                                 numeric::IntegerView significand,
                                 Location where) {
  const auto build = [&] {
    const auto sort =
        TakeAt(session.manager.MakeFpSort(exponent, significand), where);
    return sort.Borrow();
  };
  const auto exp = exponent.TryUint64();
  const auto sig = significand.TryUint64();
  if (exp && sig)
    return CachedConcrete(
        session, SortKey{"FloatingPoint", {*exp, *sig}, 2, true}, build);
  return build();
}

SortView ResolveSort(SessionState& session, const Syntax& syntax, ExprId root) {
  ConcreteSorts target(session.manager);
  return SortResolver(session, syntax, target).Resolve(root);
}
PendingSortAlias::PendingSortAlias(SessionState& state, const Syntax& syntax,
                                   ExprId name, ChildRange parameters)
    : state_(state), syntax_(syntax) {
  CheckSortName(state, syntax, name, true);
  for (const auto parameter : parameters) {
    CheckSortName(state, syntax, parameter, false);
    const auto index = parameters_.size();
    if (!parameters_.emplace(syntax.Name(parameter), index).second)
      Fail(DiagnosticCode::kInvalidInput, syntax.Span(parameter),
           "Duplicate sort parameter");
  }
}

const SortRecipe* PendingSortAlias::Reduce(ExprId expr) {
  AliasSorts target(*this);
  return SortResolver(state_, syntax_, target, &parameters_).Resolve(expr);
}

const SortRecipe* PendingSortAlias::InternRecipe(SortRecipe recipe) {
  for (const auto* child : recipe.children)
    recipe.height = std::max(recipe.height, child->height + 1);
  const auto [it, inserted] = recipes_.insert(std::move(recipe));
  if (inserted) order_.push_back(&*it);
  return &*it;
}

void PendingSortAlias::Publish(Transaction& txn, ExprId name, ExprId body) {
  std::unordered_map<const SortRecipe*, const SortRecipe*> published;
  published.reserve(order_.size());
  for (const auto* pending : order_) {
    auto recipe = *pending;
    for (auto*& child : recipe.children) child = published.at(child);
    published.emplace(pending, txn.InternRecipe(std::move(recipe)));
  }
  txn.AddSort(
      syntax_.Name(name),
      Alias{parameters_.size(),
            published.at(*syntax_.ReducedSort<const SortRecipe*>(body))});
}

SortExpr ResolveField(SessionState& session, const Syntax& syntax, ExprId root,
                      const Parameters& parameters, DatatypeBuilder& builder,
                      const TextMap<DatatypeSort>& forward,
                      std::uint64_t slot) {
  FieldSorts target(builder, forward, slot);
  return SortResolver(session, syntax, target, &parameters).Resolve(root);
}
}  // namespace dandelion::frontend::detail
