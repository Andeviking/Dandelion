module dandelion.node;
import :internal;
import std;

namespace dandelion::detail {
std::size_t InstanceHash::operator()(InstanceKey key) const noexcept {
  auto hash = Append(0x243f6a8885a308d3ULL, key.declaration);
  for (const auto arg : key.arguments) hash = Append(hash, arg);
  return static_cast<std::size_t>(hash);
}
bool InstanceEqual::operator()(InstanceKey a, InstanceKey b) const noexcept {
  return a.declaration == b.declaration &&
         std::ranges::equal(a.arguments, b.arguments);
}
DeclRecord PrepareDeclaration(ManagerState* state, DeclKind kind,
                              std::string_view name, SortId range) {
  const auto text = state->declaration_names.Append(state, name.size());
  std::ranges::copy(name, text.begin());
  return {kind, range, {text.data(), text.size()}, 0, 0, nullptr};
}
DeclId Declare(ManagerState* state, DeclKind kind, std::string_view name,
               std::span<const SortView> domain, SortView range) {
  CheckView(state, range);
  for (std::size_t i = 0; i < domain.size(); ++i)
    CheckView(state, domain[i], i);
  const auto id = NextStoreId(state, state->declarations.size());
  DeclarationTransaction transaction(state);
  auto record = PrepareDeclaration(state, kind, name, range.GetId());
  ReserveAppend(state, state->declarations);
  if (!domain.empty()) {
    auto& spans = state->declaration_domain_spans;
    record.domain = NextStoreId(state, spans.size());
    const auto ids = state->declaration_domains.Append(state, domain.size());
    for (std::size_t i = 0; i < domain.size(); ++i) ids[i] = domain[i].GetId();
    ReserveAppend(state, spans);
    spans.push_back(ids);
  }
  state->declarations.push_back(std::move(record));
  transaction.Commit();
  return id;
}
namespace {
using RecipeValues = std::unordered_map<std::uint64_t, SortId>;
using RecipeStack = std::vector<std::pair<std::uint64_t, std::size_t>>;

SortId EvaluateRecipe(ManagerState* state, const GroupRecord& group,
                      std::uint64_t root, std::span<const SortId> arguments,
                      RecipeValues& values, RecipeStack& stack) {
  if (const auto cached = values.find(root); cached != values.end())
    return cached->second;
  const auto evaluated = [&](std::uint64_t recipe) {
    return values.find(recipe)->second;
  };
  ReserveAppend(state, stack);
  stack.emplace_back(root, 0);
  while (!stack.empty()) {
    auto& [index, next] = stack.back();
    const auto& recipe = group.recipes[index];
    if (next < recipe.arguments.size()) {
      const auto child = recipe.arguments[next++];
      if (!values.contains(child)) {
        ReserveAppend(state, stack);
        stack.emplace_back(child, 0);
      }
      continue;
    }
    SortId value = 0;
    switch (recipe.kind) {
      case SortRecipeKind::kConcrete:
        value = recipe.first;
        break;
      case SortRecipeKind::kParameter:
        // Commit established scope and arity; only this finite recipe is
        // substituted.
        value = arguments[recipe.second];
        break;
      case SortRecipeKind::kArray:
        value = InternSort(state, {SortKind::kArray,
                                   evaluated(recipe.arguments[0]),
                                   evaluated(recipe.arguments[1]),
                                   0,
                                   {}});
        break;
      case SortRecipeKind::kApply:
      case SortRecipeKind::kRecursive: {
        AllocationCheckpoint(state);
        std::vector<SortId> args;
        args.reserve(recipe.arguments.size());
        for (const auto child : recipe.arguments)
          args.push_back(evaluated(child));
        const auto constructor =
            recipe.kind == SortRecipeKind::kApply
                ? recipe.first
                : group.datatypes[recipe.first].constructor;
        value = InstantiateSortId(state, constructor, args);
        break;
      }
    }
    AllocationCheckpoint(state);
    values.emplace(index, value);
    stack.pop_back();
  }
  return evaluated(root);
}
DeclInstanceId InternInstance(ManagerState* state, DeclId declaration,
                              const DeclRecord& decl,
                              std::span<const SortId> arguments) {
  if (decl.single_instance) return decl.single_instance;
  if (!arguments.empty()) {
    const auto found =
        state->instance_index.find(InstanceKey{declaration, arguments});
    if (found != state->instance_index.end()) return (*found)->id;
  }
  const auto id = NextStoreId(state, state->instances.size());
  if (!decl.datatype) {
    ReserveAppend(state, state->instances);
    PublishSingleInstance(state, declaration, decl, id);
    return id;
  }
  const auto& recipes = *decl.datatype;
  const auto& group = GroupAt(state, recipes.group);
  const auto substitution =
      NextStoreId(state, state->substituted_signatures.size());
  AllocationCheckpoint(state);
  auto record = std::make_unique<InstanceRecord>();
  record->id = id;
  record->declaration = declaration;
  AllocationCheckpoint(state);
  record->arguments.assign(arguments.begin(), arguments.end());
  AllocationCheckpoint(state);
  record->domain.reserve(recipes.domain_recipes.size());
  RecipeValues values;
  RecipeStack stack;
  for (const auto recipe : recipes.domain_recipes)
    record->domain.push_back(
        EvaluateRecipe(state, group, recipe, arguments, values, stack));
  record->range = EvaluateRecipe(state, group, recipes.range_recipe, arguments,
                                 values, stack);
  ReserveAppend(state, state->instances);
  ReserveAppend(state, state->substituted_signatures);
  if (!arguments.empty()) {
    AllocationCheckpoint(state);
    state->instance_index.insert(record.get());
  }
  state->substituted_signatures.push_back(std::move(record));
  state->instances.push_back({declaration, substitution});
  if (arguments.empty()) decl.single_instance = id;
  return id;
}
}  // namespace
}  // namespace dandelion::detail

namespace dandelion {
using namespace detail;
DeclKind DeclView::GetKind() const noexcept { return DeclAt(state_, id_).kind; }
std::string_view DeclView::GetNameHint() const noexcept {
  return DeclAt(state_, id_).name;
}
std::uint64_t DeclView::GetParameterCount() const noexcept {
  return DeclAt(state_, id_).ParameterCount();
}
SortRange DeclView::GetDomain() const noexcept {
  return Access::Range(state_, DomainAt(state_, DeclAt(state_, id_)));
}
std::optional<SortView> DeclView::GetRange() const noexcept {
  const auto range = DeclAt(state_, id_).range;
  if (!range) return std::nullopt;
  return Access::ViewOf<SortView>(state_, range);
}
Result<SortRecipeView> DeclView::GetDomainRecipe(
    std::size_t index) const noexcept {
  const auto* info = DeclAt(state_, id_).datatype.get();
  if (!info || index >= info->domain_recipes.size())
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::RecipeView(state_, info->group, info->domain_recipes[index]);
}
std::optional<SortRecipeView> DeclView::GetRangeRecipe() const noexcept {
  const auto* info = DeclAt(state_, id_).datatype.get();
  if (!info) return std::nullopt;
  return Access::RecipeView(state_, info->group, info->range_recipe);
}
std::optional<DeclView> DeclView::GetConstructor() const noexcept {
  const auto* info = DeclAt(state_, id_).datatype.get();
  if (!info) return std::nullopt;
  return Access::ViewOf<DeclView>(state_, info->constructor);
}
std::optional<std::uint64_t> DeclView::GetFieldOrdinal() const noexcept {
  const auto& record = DeclAt(state_, id_);
  if (record.kind != DeclKind::kDatatypeSelector) return std::nullopt;
  return record.datatype->field_ordinal;
}
DeclView DeclInstanceView::GetDeclaration() const noexcept {
  return Access::ViewOf<DeclView>(state_, InstanceAt(state_, id_).declaration);
}
SortRange DeclInstanceView::GetTypeArguments() const noexcept {
  const auto substitution = InstanceAt(state_, id_).substitution;
  return Access::Range(
      state_, substitution ? std::span<const SortId>(
                                 SubstitutionAt(state_, substitution).arguments)
                           : std::span<const SortId>{});
}
SortRange DeclInstanceView::GetDomain() const noexcept {
  return Access::Range(state_, SignatureAt(state_, id_).domain);
}
SortView DeclInstanceView::GetRange() const noexcept {
  return Access::ViewOf<SortView>(state_, SignatureAt(state_, id_).range);
}
SortRecipeView::SortRecipeView(ManagerState* state, DatatypeGroupId group,
                               std::uint64_t index) noexcept
    : state_(state), group_(group), index_(index) {}
SortRecipeKind SortRecipeView::GetKind() const noexcept {
  return Access::GetRecipe(*this).kind;
}
std::optional<SortView> SortRecipeView::GetConcrete() const noexcept {
  if (GetKind() != SortRecipeKind::kConcrete) return std::nullopt;
  return Access::ViewOf<SortView>(state_, Access::GetRecipe(*this).first);
}
std::optional<std::pair<std::uint64_t, std::uint64_t>>
SortRecipeView::GetParameter() const noexcept {
  const auto& recipe = Access::GetRecipe(*this);
  if (recipe.kind != SortRecipeKind::kParameter) return std::nullopt;
  return std::pair(recipe.first, recipe.second);
}
std::optional<SortConstructorView> SortRecipeView::GetConstructor()
    const noexcept {
  if (GetKind() != SortRecipeKind::kApply) return std::nullopt;
  return Access::ViewOf<SortConstructorView>(state_,
                                             Access::GetRecipe(*this).first);
}
std::optional<std::uint64_t> SortRecipeView::GetRecursiveSlot() const noexcept {
  if (GetKind() != SortRecipeKind::kRecursive) return std::nullopt;
  return Access::GetRecipe(*this).first;
}
std::size_t SortRecipeView::GetArgumentCount() const noexcept {
  return Access::GetRecipe(*this).arguments.size();
}
Result<SortRecipeView> SortRecipeView::Argument(
    std::size_t index) const noexcept {
  const auto& recipe = Access::GetRecipe(*this);
  if (index >= recipe.arguments.size())
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::RecipeView(state_, group_, recipe.arguments[index]);
}

Result<Decl> NodeManager::DeclareFunction(std::string_view name,
                                          std::span<const SortView> domain,
                                          SortView range) {
  return Guard<Decl>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = Declare(state, DeclKind::kFunction, name, domain, range);
    lease.Transfer();
    return Access::Own<DeclView>(state, id);
  });
}
Result<Decl> NodeManager::FreshFunction(std::string_view name,
                                        std::span<const SortView> domain,
                                        SortView range) {
  return Guard<Decl>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id =
        Declare(state, DeclKind::kInternalFunction, name, domain, range);
    lease.Transfer();
    return Access::Own<DeclView>(state, id);
  });
}
Result<Decl> NodeManager::MakeFormal(std::string_view name, SortView sort) {
  return Guard<Decl>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = Declare(state, DeclKind::kFormal, name, {}, sort);
    lease.Transfer();
    return Access::Own<DeclView>(state, id);
  });
}
Result<DeclInstance> NodeManager::InstantiateDeclaration(
    DeclView declaration, std::span<const SortView> arguments) {
  return Guard<DeclInstance>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, declaration);
    const auto decl_id = declaration.GetId();
    const auto& decl = DeclAt(state, decl_id);
    Require(arguments.size() == decl.ParameterCount(),
            ErrorCode::kInvalidArity);
    std::vector<SortId> ids;
    if (!arguments.empty()) {
      AllocationCheckpoint(state);
      ids.reserve(arguments.size());
      for (std::size_t i = 0; i < arguments.size(); ++i) {
        CheckView(state, arguments[i], i);
        ids.push_back(arguments[i].GetId());
      }
    }
    StateLease lease(state);
    const auto id = InternInstance(state, decl_id, decl, ids);
    lease.Transfer();
    return Access::Own<DeclInstanceView>(state, id);
  });
}
}  // namespace dandelion
