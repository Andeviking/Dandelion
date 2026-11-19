module dandelion.node;
import :internal;
import std;

namespace dandelion::detail {
namespace {
BuilderData* Open(BuilderData* data) {
  Require(data && data->state, ErrorCode::kBuilderClosed);
  return data;
}
void CheckRecipe(BuilderData* data, SortExpr recipe) {
  Require(Access::ValidRecipe(data, recipe), ErrorCode::kInvalidParameter);
}
std::uint64_t AppendRecipe(BuilderData* data, Recipe recipe) {
  ReserveAppend(data->state, data->recipes);
  const auto index = static_cast<std::uint64_t>(data->recipes.size());
  data->recipes.push_back(std::move(recipe));
  return index;
}
void RequireDatatype(bool condition, DatatypeReason reason) {
  if (!condition)
    throw Failure{NodeError{.code = ErrorCode::kInvalidDatatype,
                            .datatype_reason = reason}};
}
struct RecipeScope {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<std::uint64_t> owner;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool mixed_owners = false;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool contains_recursive = false;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool nested_recursive = false;
};
struct ConstructorDependencies {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t datatype;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t remaining = 0;
};

void ValidateGroup(const BuilderData* data) {
  auto* state = data->state;
  AllocationCheckpoint(state);
  std::vector<RecipeScope> scopes(data->recipes.size());
  // Every argument refers to an earlier recipe.
  for (std::size_t i = 0; i < data->recipes.size(); ++i) {
    const auto& recipe = data->recipes[i];
    auto& scope = scopes[i];
    if (recipe.kind == SortRecipeKind::kParameter) scope.owner = recipe.first;
    for (const auto child : recipe.arguments) {
      const auto& dependency = scopes[child];
      scope.nested_recursive |= dependency.contains_recursive;
      scope.mixed_owners |= dependency.mixed_owners;
      if (!dependency.owner) continue;
      if (scope.owner)
        scope.mixed_owners |= scope.owner != dependency.owner;
      else
        scope.owner = dependency.owner;
    }
    scope.contains_recursive =
        scope.nested_recursive || recipe.kind == SortRecipeKind::kRecursive;
  }

  const auto count = data->datatypes.size();
  AllocationCheckpoint(state);
  std::vector<std::vector<std::size_t>> dependents(count);
  AllocationCheckpoint(state);
  std::vector<bool> productive(count);
  AllocationCheckpoint(state);
  std::vector<std::uint64_t> ready;
  ready.reserve(count);
  std::vector<ConstructorDependencies> pending;
  const auto mark_productive = [&](std::uint64_t slot) {
    if (productive[slot]) return;
    productive[slot] = true;
    ready.push_back(slot);
  };

  for (std::size_t slot = 0; slot < count; ++slot) {
    const auto& constructors = data->datatypes[slot].constructors;
    RequireDatatype(!constructors.empty(), DatatypeReason::kNoConstructor);
    for (const auto& constructor : constructors) {
      ConstructorDependencies dependencies{slot};
      for (const auto& field : constructor.fields) {
        const auto& scope = scopes[field.recipe];
        RequireDatatype(
            !scope.mixed_owners && (!scope.owner || *scope.owner == slot),
            DatatypeReason::kInvalidParameterScope);
        RequireDatatype(!scope.nested_recursive,
                        DatatypeReason::kNestedRecursiveSort);

        const auto& recipe = data->recipes[field.recipe];
        if (recipe.kind == SortRecipeKind::kRecursive) {
          auto& users = dependents[recipe.first];
          ReserveAppend(state, users);
          users.push_back(pending.size());
          ++dependencies.remaining;
        }
      }
      if (dependencies.remaining == 0) {
        mark_productive(slot);
      } else {
        ReserveAppend(state, pending);
        pending.push_back(dependencies);
      }
    }
  }

  for (std::size_t next = 0; next < ready.size(); ++next) {
    for (const auto index : dependents[ready[next]]) {
      auto& constructor = pending[index];
      if (--constructor.remaining == 0) mark_productive(constructor.datatype);
    }
  }
  RequireDatatype(ready.size() == count, DatatypeReason::kNonproductive);
}
std::uint64_t GroupRecipe(ManagerState* state, GroupRecord& group,
                          Recipe recipe) {
  ReserveAppend(state, group.recipes);
  const auto index = static_cast<std::uint64_t>(group.recipes.size());
  group.recipes.push_back(std::move(recipe));
  return index;
}
DeclRecord DatatypeDeclaration(ManagerState* state, DeclKind kind,
                               std::string_view name, DatatypeGroupId group,
                               std::uint64_t parameters, DeclId constructor) {
  auto record = PrepareDeclaration(state, kind, name, 0);
  AllocationCheckpoint(state);
  record.datatype = std::make_unique<DatatypeDeclInfo>();
  record.datatype->group = group;
  record.datatype->parameter_count = parameters;
  record.datatype->constructor = constructor;
  return record;
}
}  // namespace
}  // namespace dandelion::detail

namespace dandelion {
using namespace detail;
SortExpr::SortExpr(BuilderData* builder, std::uint64_t index) noexcept
    : builder_(builder), index_(index) {}
DatatypeBuilder::DatatypeBuilder(BuilderData* data) noexcept : data_(data) {}
DatatypeBuilder::DatatypeBuilder(DatatypeBuilder&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)) {}
DatatypeBuilder& DatatypeBuilder::operator=(DatatypeBuilder&& other) noexcept {
  if (this != &other) {
    Abort();
    data_ = std::exchange(other.data_, nullptr);
  }
  return *this;
}
DatatypeBuilder::~DatatypeBuilder() noexcept { Abort(); }
void DatatypeBuilder::Abort() noexcept {
  if (!data_) return;
  auto* state = data_->state;
  delete std::exchange(data_, nullptr);
  if (state) ReleaseState(state);
}
Result<DatatypeBuilder> NodeManager::BeginDatatypeGroup(
    std::span<const DatatypeHeader> headers) {
  return Guard<DatatypeBuilder>([&] {
    auto* state = CheckedState(*this);
    RequireDatatype(!headers.empty(), DatatypeReason::kEmptyGroup);
    StateLease lease(state);
    AllocationCheckpoint(state);
    auto data = std::make_unique<BuilderData>();
    data->state = state;
    AllocationCheckpoint(state);
    data->datatypes.reserve(headers.size());
    for (const auto& header : headers) {
      AllocationCheckpoint(state);
      data->datatypes.push_back(
          {std::string(header.name_hint), header.parameter_count, {}});
    }
    lease.Transfer();
    return DatatypeBuilder(data.release());
  });
}
Result<SortExpr> DatatypeBuilder::Concrete(SortView sort) {
  return Guard<SortExpr>([&] {
    auto* data = Open(data_);
    CheckView(data->state, sort);
    return SortExpr(
        data,
        AppendRecipe(data, {SortRecipeKind::kConcrete, sort.GetId(), 0, {}}));
  });
}
Result<SortExpr> DatatypeBuilder::Parameter(std::uint64_t slot,
                                            std::uint64_t parameter) {
  return Guard<SortExpr>([&] {
    auto* data = Open(data_);
    Require(slot < data->datatypes.size() &&
                parameter < data->datatypes[slot].arity,
            ErrorCode::kInvalidParameter);
    return SortExpr(
        data,
        AppendRecipe(data, {SortRecipeKind::kParameter, slot, parameter, {}}));
  });
}
Result<SortExpr> DatatypeBuilder::Array(SortExpr index, SortExpr element) {
  return Guard<SortExpr>([&] {
    auto* data = Open(data_);
    CheckRecipe(data, index);
    CheckRecipe(data, element);
    AllocationCheckpoint(data->state);
    return SortExpr(data, AppendRecipe(data, {SortRecipeKind::kArray,
                                              0,
                                              0,
                                              {Access::RecipeIndex(index),
                                               Access::RecipeIndex(element)}}));
  });
}
Result<SortExpr> DatatypeBuilder::Apply(SortConstructorView constructor,
                                        std::span<const SortExpr> arguments) {
  return Guard<SortExpr>([&] {
    auto* data = Open(data_);
    CheckView(data->state, constructor);
    Require(arguments.size() == constructor.GetArity(),
            ErrorCode::kInvalidArity);
    AllocationCheckpoint(data->state);
    std::vector<std::uint64_t> ids;
    ids.reserve(arguments.size());
    for (const auto arg : arguments) {
      CheckRecipe(data, arg);
      ids.push_back(Access::RecipeIndex(arg));
    }
    return SortExpr(
        data, AppendRecipe(data, {SortRecipeKind::kApply, constructor.GetId(),
                                  0, std::move(ids)}));
  });
}
Result<SortExpr> DatatypeBuilder::Recursive(
    std::uint64_t slot, std::span<const SortExpr> arguments) {
  return Guard<SortExpr>([&] {
    auto* data = Open(data_);
    Require(slot < data->datatypes.size(), ErrorCode::kInvalidParameter);
    Require(arguments.size() == data->datatypes[slot].arity,
            ErrorCode::kInvalidArity);
    AllocationCheckpoint(data->state);
    std::vector<std::uint64_t> ids;
    ids.reserve(arguments.size());
    for (const auto arg : arguments) {
      CheckRecipe(data, arg);
      ids.push_back(Access::RecipeIndex(arg));
    }
    return SortExpr(data, AppendRecipe(data, {SortRecipeKind::kRecursive, slot,
                                              0, std::move(ids)}));
  });
}
Result<void> DatatypeBuilder::AddConstructor(
    std::uint64_t slot, std::string_view name,
    std::span<const DatatypeFieldSpec> fields) {
  return Guard<void>([&] {
    auto* data = Open(data_);
    auto* state = data->state;
    Require(slot < data->datatypes.size(), ErrorCode::kInvalidParameter);
    AllocationCheckpoint(state);
    StagedConstructor constructor{std::string(name), {}};
    AllocationCheckpoint(state);
    constructor.fields.reserve(fields.size());
    for (const auto& field : fields) {
      CheckRecipe(data, field.sort);
      AllocationCheckpoint(state);
      constructor.fields.push_back(
          {std::string(field.name_hint), Access::RecipeIndex(field.sort)});
    }
    ReserveAppend(state, data->datatypes[slot].constructors);
    data->datatypes[slot].constructors.push_back(std::move(constructor));
  });
}
Result<DatatypeGroup> DatatypeBuilder::Commit() {
  return Guard<DatatypeGroup>([&] {
    auto* data = Open(data_);
    auto* state = data->state;
    ValidateGroup(data);
    const auto group_id = NextStoreId(state, state->groups.size());
    const auto first_ctor = NextStoreId(state, state->sort_constructors.size(),
                                        data->datatypes.size());
    const auto limit = IdentityCapacity(state);
    if (state->declarations.size() > limit)
      Fail(ErrorCode::kResourceLimit, ResourceKind::kIdentitySpace);
    const auto available = limit - state->declarations.size();
    std::size_t declaration_count = 0;
    for (const auto& datatype : data->datatypes)
      for (const auto& constructor : datatype.constructors) {
        if (declaration_count > available ||
            available - declaration_count < 2 ||
            constructor.fields.size() > available - declaration_count - 2)
          Fail(ErrorCode::kResourceLimit, ResourceKind::kIdentitySpace);
        declaration_count += constructor.fields.size() + 2;
      }
    const auto first_decl =
        NextStoreId(state, state->declarations.size(), declaration_count);
    const SortKey bool_key{SortKind::kBool, 0, 0, 0, {}};
    const auto bool_found = state->sort_index.find(bool_key);
    std::unique_ptr<SortRecord> staged_bool;
    decltype(state->sort_index) staged_index;
    SortId bool_id = 0;
    if (bool_found != state->sort_index.end())
      bool_id = (*bool_found)->id;
    else {
      bool_id = NextStoreId(state, state->sorts.size());
      AllocationCheckpoint(state);
      staged_bool = std::make_unique<SortRecord>(bool_id, SortKind::kBool, 0, 0,
                                                 0, std::vector<SortId>{});
      AllocationCheckpoint(state);
      staged_index.insert(staged_bool.get());
      ReserveAppend(state, state->sorts);
      AllocationCheckpoint(state);
      state->sort_index.reserve(state->sort_index.size() + 1);
    }
    AllocationCheckpoint(state);
    auto group = std::make_unique<GroupRecord>();
    group->id = group_id;
    AllocationCheckpoint(state);
    group->recipes = data->recipes;
    AllocationCheckpoint(state);
    group->datatypes.reserve(data->datatypes.size());
    AllocationCheckpoint(state);
    std::vector<std::unique_ptr<const SortConstructorRecord>> constructors;
    constructors.reserve(data->datatypes.size());
    AllocationCheckpoint(state);
    DeclarationTransaction transaction(state);
    std::vector<DeclRecord> declarations;
    declarations.reserve(declaration_count);
    const auto bool_recipe =
        GroupRecipe(state, *group, {SortRecipeKind::kConcrete, bool_id, 0, {}});
    for (std::size_t slot = 0; slot < data->datatypes.size(); ++slot) {
      const auto& datatype = data->datatypes[slot];
      const auto ordinal_slot = static_cast<std::uint64_t>(slot);
      const auto ctor_id = first_ctor + ordinal_slot;
      AllocationCheckpoint(state);
      constructors.push_back(std::make_unique<SortConstructorRecord>(
          ctor_id, datatype.name, datatype.arity, group_id, ordinal_slot));
      AllocationCheckpoint(state);
      std::vector<std::uint64_t> parameters;
      parameters.reserve(datatype.arity);
      for (std::uint64_t p = 0; p < datatype.arity; ++p)
        parameters.push_back(GroupRecipe(
            state, *group, {SortRecipeKind::kParameter, ordinal_slot, p, {}}));
      const auto result_recipe = GroupRecipe(
          state, *group,
          {SortRecipeKind::kRecursive, ordinal_slot, 0, std::move(parameters)});
      DatatypeRecord result{ctor_id, {}};
      AllocationCheckpoint(state);
      result.constructors.reserve(datatype.constructors.size());
      for (std::size_t c = 0; c < datatype.constructors.size(); ++c) {
        const auto& source = datatype.constructors[c];
        const auto decl_id =
            first_decl + static_cast<std::uint64_t>(declarations.size());
        auto declaration =
            DatatypeDeclaration(state, DeclKind::kDatatypeConstructor,
                                source.name, group_id, datatype.arity, decl_id);
        declaration.datatype->range_recipe = result_recipe;
        AllocationCheckpoint(state);
        declaration.datatype->domain_recipes.reserve(source.fields.size());
        for (const auto& field : source.fields)
          declaration.datatype->domain_recipes.push_back(field.recipe);
        declarations.push_back(std::move(declaration));
        const auto tester_id =
            first_decl + static_cast<std::uint64_t>(declarations.size());
        AllocationCheckpoint(state);
        const std::string tester_name = "is-" + source.name;
        auto tester =
            DatatypeDeclaration(state, DeclKind::kDatatypeTester, tester_name,
                                group_id, datatype.arity, decl_id);
        AllocationCheckpoint(state);
        tester.datatype->domain_recipes = {result_recipe};
        tester.datatype->range_recipe = bool_recipe;
        tester.range = bool_id;
        declarations.push_back(std::move(tester));
        ConstructorRecord links{decl_id, tester_id, {}};
        AllocationCheckpoint(state);
        links.selectors.reserve(source.fields.size());
        for (std::size_t f = 0; f < source.fields.size(); ++f) {
          const auto selector_id =
              first_decl + static_cast<std::uint64_t>(declarations.size());
          auto selector = DatatypeDeclaration(
              state, DeclKind::kDatatypeSelector, source.fields[f].name,
              group_id, datatype.arity, decl_id);
          selector.datatype->field_ordinal = static_cast<std::uint64_t>(f);
          AllocationCheckpoint(state);
          selector.datatype->domain_recipes = {result_recipe};
          selector.datatype->range_recipe = source.fields[f].recipe;
          declarations.push_back(std::move(selector));
          links.selectors.push_back(selector_id);
        }
        result.constructors.push_back(std::move(links));
      }
      group->datatypes.push_back(std::move(result));
    }
    ReserveAppend(state, state->sort_constructors, constructors.size());
    ReserveAppend(state, state->declarations, declarations.size());
    ReserveAppend(state, state->groups);
    // Only nonthrowing transfers remain. Transfer the builder's state
    // reference.
    if (staged_bool) {
      auto entry = staged_index.extract(staged_bool.get());
      state->sort_index.insert(std::move(entry));
      state->sorts.push_back(std::move(staged_bool));
    }
    for (auto& constructor : constructors)
      state->sort_constructors.push_back(std::move(constructor));
    for (auto& declaration : declarations)
      state->declarations.push_back(std::move(declaration));
    state->groups.push_back(std::move(group));
    transaction.Commit();
    delete std::exchange(data_, nullptr);
    return Access::Own<DatatypeGroupView>(state, group_id);
  });
}

std::uint64_t DatatypeGroupView::GetDatatypeCount() const noexcept {
  return static_cast<std::uint64_t>(GroupAt(state_, id_).datatypes.size());
}
Result<std::uint64_t> DatatypeGroupView::GetConstructorCount(
    std::size_t datatype) const noexcept {
  const auto& group = GroupAt(state_, id_);
  if (datatype >= group.datatypes.size())
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return static_cast<std::uint64_t>(
      group.datatypes[datatype].constructors.size());
}
Result<std::uint64_t> DatatypeGroupView::GetFieldCount(
    std::size_t datatype, std::size_t constructor) const noexcept {
  const auto count = GetConstructorCount(datatype);
  if (!count || constructor >= *count)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return static_cast<std::uint64_t>(GroupAt(state_, id_)
                                        .datatypes[datatype]
                                        .constructors[constructor]
                                        .selectors.size());
}
Result<SortConstructorView> DatatypeGroupView::GetSortConstructor(
    std::size_t datatype) const noexcept {
  const auto& group = GroupAt(state_, id_);
  if (datatype >= group.datatypes.size())
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::ViewOf<SortConstructorView>(
      state_, group.datatypes[datatype].constructor);
}
Result<DeclView> DatatypeGroupView::GetConstructor(
    std::size_t datatype, std::size_t constructor) const noexcept {
  const auto count = GetConstructorCount(datatype);
  if (!count || constructor >= *count)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::ViewOf<DeclView>(state_, GroupAt(state_, id_)
                                              .datatypes[datatype]
                                              .constructors[constructor]
                                              .declaration);
}
Result<DeclView> DatatypeGroupView::GetTester(
    std::size_t datatype, std::size_t constructor) const noexcept {
  const auto count = GetConstructorCount(datatype);
  if (!count || constructor >= *count)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::ViewOf<DeclView>(state_, GroupAt(state_, id_)
                                              .datatypes[datatype]
                                              .constructors[constructor]
                                              .tester);
}
Result<DeclView> DatatypeGroupView::GetSelector(
    std::size_t datatype, std::size_t constructor,
    std::size_t field) const noexcept {
  const auto count = GetFieldCount(datatype, constructor);
  if (!count || field >= *count)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return Access::ViewOf<DeclView>(state_, GroupAt(state_, id_)
                                              .datatypes[datatype]
                                              .constructors[constructor]
                                              .selectors[field]);
}
}  // namespace dandelion
