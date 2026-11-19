module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
struct StandardFormat {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view name;
  std::int64_t exponent;
  std::int64_t significand;
  // NOLINTEND(readability-identifier-naming)
};
constexpr std::array kFormats{
    StandardFormat{"Float16", 5, 11}, StandardFormat{"Float32", 8, 24},
    StandardFormat{"Float64", 11, 53}, StandardFormat{"Float128", 15, 113}};
const StandardFormat* FindFormat(std::string_view name) noexcept {
  const auto found = std::ranges::find(kFormats, name, &StandardFormat::name);
  return found != kFormats.end() ? &*found : nullptr;
}

void CheckArity(std::size_t actual, std::uint64_t expected, SourceSpan where) {
  if (actual != expected)
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong number of sort arguments");
}
void RequireTheory(bool enabled, SourceSpan where) {
  if (!enabled)
    Fail(DiagnosticCode::kUnsupported, where,
         "Sort is outside the active signature");
}

class ConcreteSorts {
 public:
  using Value = SortView;
  ConcreteSorts(NodeManager& manager, Transaction* txn)
      : manager_(manager), txn_(txn) {}
  Value Concrete(SortView sort, SourceSpan) const noexcept { return sort; }
  void RememberAlias(SortView sort, std::string_view name) {
    if (txn_) txn_->RememberAlias(sort, name);
  }
  Value Array(std::span<const Value> children, SourceSpan where) {
    const auto sort =
        Take(manager_.MakeArraySort(children[0], children[1]), where);
    return sort.Borrow();
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              SourceSpan where) {
    const auto sort =
        Take(manager_.InstantiateSort(constructor, children), where);
    return sort.Borrow();
  }

 private:
  NodeManager& manager_;
  Transaction* txn_;
};

class AliasSorts {
 public:
  using Value = const SortRecipe*;
  explicit AliasSorts(Transaction& txn) : txn_(txn) {}
  Value Concrete(SortView sort, SourceSpan) {
    return txn_.InternRecipe({RecipeKind::kConcrete, sort, {}, 0, {}});
  }
  Value Parameter(std::uint64_t index, SourceSpan) {
    return txn_.InternRecipe({RecipeKind::kParameter, {}, {}, index, {}});
  }
  Value Array(std::span<const Value> children, SourceSpan) {
    return txn_.InternRecipe(
        {RecipeKind::kArray, {}, {}, 0, {children.begin(), children.end()}});
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              SourceSpan) {
    return txn_.InternRecipe({RecipeKind::kApply,
                              {},
                              constructor,
                              0,
                              {children.begin(), children.end()}});
  }

 private:
  Transaction& txn_;
};

class FieldSorts {
 public:
  using Value = SortExpr;
  FieldSorts(DatatypeBuilder& builder, std::size_t group, std::uint64_t slot)
      : builder_(builder), group_(group), slot_(slot) {}
  Value Concrete(SortView sort, SourceSpan where) {
    return Take(builder_.Concrete(sort), where);
  }
  Value Parameter(std::uint64_t index, SourceSpan where) {
    return Take(builder_.Parameter(slot_, index), where);
  }
  Value Array(std::span<const Value> children, SourceSpan where) {
    return Take(builder_.Array(children[0], children[1]), where);
  }
  Value Apply(SortConstructorView constructor, std::span<const Value> children,
              SourceSpan where) {
    return Take(builder_.Apply(constructor, children), where);
  }
  bool InGroup(const DatatypeSort& datatype) const noexcept {
    return datatype.group == group_;
  }
  Value Recursive(std::uint64_t slot, std::span<const Value> children,
                  SourceSpan where) {
    return Take(builder_.Recursive(slot, children), where);
  }

 private:
  DatatypeBuilder& builder_;
  std::size_t group_;
  std::uint64_t slot_;
};

template <class Target>
Target::Value Expand(Target& target, const SortRecipe* root,
                     std::span<const typename Target::Value> parameters,
                     SourceSpan where) {
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
  Value ConstructBuiltin(ExprId expr, Identifier id,
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
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    ExprId expr;
    Identifier head;
    std::span<const ExprId> args;
    std::size_t value_base;
    std::size_t next = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<Frame> stack;
  std::vector<Value> values;
  const auto push = [&](ExprId id) {
    const auto parts = syntax_.Children(id);
    const bool application = !parts.empty() && !syntax_.Is(parts[0], "_");
    const auto head = GetIdentifier(syntax_, application ? parts[0] : id);
    const auto args =
        application ? parts.subspan(1) : std::span<const ExprId>{};
    if (head.indices.empty() && syntax_.Name(head.name) == "Array" &&
        !(parameters_ && parameters_->contains("Array")) &&
        !signature_.sorts.contains("Array"))
      CheckArity(args.size(), 2, syntax_.Span(id));
    stack.push_back({id, head, args, values.size()});
  };
  push(root);
  while (!stack.empty()) {
    auto& top = stack.back();
    if (top.next < top.args.size()) {
      const auto child = top.args[top.next++];
      push(child);
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
  const auto name = syntax_.Name(id.name);
  const auto where = syntax_.Span(expr);
  if (IsReservedName(name))
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(id.name),
         "Reserved sort name");
  if (!id.indices.empty()) return ConstructBuiltin(expr, id, args);

  if constexpr (!std::is_same_v<Target, ConcreteSorts>) {
    const auto parameter = parameters_->find(name);
    if (parameter != parameters_->end()) {
      CheckArity(args.size(), 0, where);
      return target_.Parameter(parameter->second, where);
    }
  }
  const auto bound = signature_.sorts.find(name);
  if (bound == signature_.sorts.end()) return ConstructBuiltin(expr, id, args);

  const auto& binding = bound->second;
  if (const auto* alias = std::get_if<Alias>(&binding)) {
    CheckArity(args.size(), alias->arity, where);
    const auto sort = Expand(target_, alias->root, args, where);
    if constexpr (std::is_same_v<Target, ConcreteSorts>) {
      if (alias->root->kind != RecipeKind::kParameter)
        target_.RememberAlias(sort, bound->first);
    }
    return sort;
  }
  if (const auto* datatype = std::get_if<DatatypeSort>(&binding)) {
    CheckArity(args.size(), datatype->arity, where);
    if constexpr (std::is_same_v<Target, FieldSorts>) {
      if (target_.InGroup(*datatype))
        return target_.Recursive(datatype->slot, args, where);
    }
    const auto group =
        session_.Prefix().GetDatatypeGroups()[datatype->group].Borrow();
    return target_.Apply(Take(group.GetSortConstructor(datatype->slot), where),
                         args, where);
  }
  const auto constructor = std::get<SortConstructorView>(binding);
  CheckArity(args.size(), constructor.GetArity(), where);
  return target_.Apply(constructor, args, where);
}

template <class Target>
auto SortResolver<Target>::ConstructBuiltin(ExprId expr, Identifier id,
                                            std::span<const Value> args)
    -> Value {
  const auto name = syntax_.Name(id.name);
  const auto where = syntax_.Span(expr);
  auto& manager = session_.manager;
  const auto concrete = [&](Result<Sort> result) {
    const auto sort = Take(std::move(result), where);
    return target_.Concrete(sort.Borrow(), where);
  };
  if (name == "BitVec" || name == "FloatingPoint") {
    CheckArity(args.size(), 0, where);
    const bool is_bv = name == "BitVec";
    const auto indices = NumericIndices(syntax_, id, is_bv ? 1 : 2, where);
    CheckMinimumIndex(syntax_, id, indices, 0, is_bv ? 1 : 2);
    if (is_bv) {
      RequireTheory(profile_.Has(kBitVectors) || profile_.Has(kFloats), where);
      return concrete(manager.MakeBvSort(indices[0].Borrow()));
    }
    CheckMinimumIndex(syntax_, id, indices, 1, 2);
    RequireTheory(profile_.Has(kFloats), where);
    return concrete(
        manager.MakeFpSort(indices[0].Borrow(), indices[1].Borrow()));
  }
  if (name == "Real" || name == "String" || name == "RegLan" || name == "->")
    Fail(DiagnosticCode::kUnsupported, where,
         "Sort is outside the input profile");
  if (!id.indices.empty())
    Fail(DiagnosticCode::kInvalidInput, where, "Unknown indexed sort");
  if (name == "Array") {
    RequireTheory(profile_.Has(kArrays), where);
    return target_.Array(args, where);
  }
  if (name == "Bool") {
    CheckArity(args.size(), 0, where);
    return concrete(manager.BoolSort());
  }
  if (name == "Int") {
    CheckArity(args.size(), 0, where);
    RequireTheory(profile_.Has(kIntegers) || profile_.Has(kRestrictedIntegers),
                  where);
    return concrete(manager.IntSort());
  }
  if (name == "RoundingMode") {
    CheckArity(args.size(), 0, where);
    RequireTheory(profile_.Has(kFloats), where);
    return concrete(manager.RoundingModeSort());
  }
  if (const auto* format = FindFormat(name)) {
    CheckArity(args.size(), 0, where);
    RequireTheory(profile_.Has(kFloats), where);
    return concrete(manager.MakeFpSort(IntegerView(format->exponent),
                                       IntegerView(format->significand)));
  }
  Fail(DiagnosticCode::kInvalidInput, syntax_.Span(id.name), "Unknown sort");
}
}  // namespace

bool IsActiveSort(const Profile& profile, std::string_view name) noexcept {
  return name == "Bool" ||
         (name == "Int" &&
          (profile.Has(kIntegers) || profile.Has(kRestrictedIntegers))) ||
         (name == "Array" && profile.Has(kArrays)) ||
         (profile.Has(kFloats) &&
          (name == "RoundingMode" || FindFormat(name) || name == "Real")) ||
         (profile.Has(kReals) && name == "Real");
}

Sort ResolveSort(SessionState& session, const Syntax& syntax, ExprId root,
                 Transaction* txn) {
  ConcreteSorts target(session.manager, txn);
  return Take(session.manager.RetainSort(
                  SortResolver(session, syntax, target).Resolve(root)),
              syntax.Span(root));
}
Alias ResolveAlias(SessionState& session, Transaction& txn,
                   const Syntax& syntax, ExprId root,
                   const Parameters& parameters) {
  AliasSorts target(txn);
  return {parameters.size(),
          SortResolver(session, syntax, target, &parameters).Resolve(root)};
}
SortExpr ResolveField(SessionState& session, const Syntax& syntax, ExprId root,
                      const Parameters& parameters, DatatypeBuilder& builder,
                      std::size_t group, std::uint64_t slot) {
  FieldSorts target(builder, group, slot);
  return SortResolver(session, syntax, target, &parameters).Resolve(root);
}
}  // namespace dandelion::frontend::detail
