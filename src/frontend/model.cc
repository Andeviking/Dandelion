module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {

bool operator==(AbstractKey left, AbstractKey right) noexcept {
  return left.sort == right.sort && left.key == right.key;
}
std::size_t AbstractHash::operator()(AbstractKey value) const noexcept {
  if (constant) return 0;
  const auto text = std::hash<std::string_view>{}(value.key);
  return text ^ (std::hash<SortId>{}(value.sort) + std::size_t{0x9e3779b9} +
                 (text << 6) + (text >> 2));
}
ModelState::ModelState(const std::shared_ptr<const QueryContext>& context,
                       CheckAnswer result, UnknownReason why)
    : query(context),
      manager(Take(NodeManager::Attach(query->GetManager()), {})),
      identity(std::make_shared<ModelIdentity>()),
      answer(result),
      reason(why),
      keys(0, AbstractHash{}) {}

Node ModelState::RegisterProxy(const ValueRef& value, ProxyBindings& bindings,
                               SourceSpan where) {
  auto node = Take(manager.MakeFreshConstant("@model", value.GetSort()), where);
  bindings.emplace(node.Borrow().GetDeclaration()->GetDeclaration().GetId(),
                   value);
  return node;
}

SessionState::SessionState(Scanner input, std::shared_ptr<ModelOwner> model)
    : scanner(std::move(input)),
      manager([&]() {
        if (!model)
          Fail(DiagnosticCode::kModelUnavailable, scanner.Here(),
               "An owned model is required");
        return Take(NodeManager::Attach(model->GetQuery().GetManager()),
                    scanner.Here());
      }()),
      sealed(model->GetSharedQuery()),
      inspection_model(std::move(model)),
      metadata(sealed->GetMetadata().begin(), sealed->GetMetadata().end()),
      options(sealed->GetOptions()),
      phase(SessionPhase::kAfterCheck) {
#ifdef DANDELION_FRONTEND_TESTING
  scanner.Pool().stats = &stats;
#endif
}

std::pair<Node, std::string> ResolveModelReference(
    const ModelOwner& model, const QueryContext& query, std::string_view name,
    std::optional<SortView> qualifier, SourceSpan where) {
  if (&model.GetQuery() != &query)
    Fail(DiagnosticCode::kModelFailure, where,
         "Model belongs to a different query");
  return Use(model.ResolveName(name, qualifier, where), where);
}

bool IsAbstractSort(SortView sort) noexcept {
  return sort.GetKind() == SortKind::kArray ||
         sort.GetKind() == SortKind::kUninterpreted;
}
std::optional<std::size_t> AbstractOrdinal(std::string_view name) noexcept {
  if (!name.starts_with("@value")) return std::nullopt;
  const auto digits = name.substr(6);
  if (digits.empty() || (digits.size() > 1 && digits.front() == '0'))
    return std::nullopt;
  std::size_t index = 0;
  const auto* end = digits.data() + digits.size();
  const auto parsed = std::from_chars(digits.data(), end, index);
  return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional(index)
                                                       : std::nullopt;
}
ModelScope::ModelScope(const QueryContext& query) noexcept
    : query_(query), signature_(Signature::Of(query)) {}

void ModelScope::CheckSort(SortView root, SourceSpan where) {
  if (seen_sorts_.contains(root.GetId())) return;
  const auto& profile = *signature_.profile;
  const auto require = [&](bool enabled) {
    if (!enabled)
      Fail(DiagnosticCode::kModelFailure, where,
           "Model term uses a sort outside the sealed signature");
  };
  work_.push_back(root);
  while (!work_.empty()) {
    const auto sort = work_.back();
    work_.pop_back();
    if (!seen_sorts_.insert(sort.GetId()).second) continue;
    switch (sort.GetKind()) {
      case SortKind::kBool:
        break;
      case SortKind::kInt:
        require(profile.Has(kIntegers) || profile.Has(kRestrictedIntegers));
        break;
      case SortKind::kBitVector:
        require(profile.Has(kBitVectors) || profile.Has(kFloats));
        break;
      case SortKind::kFloatingPoint:
      case SortKind::kRoundingMode:
        require(profile.Has(kFloats));
        break;
      case SortKind::kArray:
        require(profile.Has(kArrays));
        work_.push_back(*sort.GetArrayElement());
        work_.push_back(*sort.GetArrayIndex());
        break;
      case SortKind::kUninterpreted:
      case SortKind::kDatatype: {
        const auto ctor = *sort.GetSortConstructor();
        const auto entry = signature_.sorts.find(ctor.GetNameHint());
        require(entry != nullptr);
        const auto& binding = entry->second;
        if (const auto* free = std::get_if<SortConstructorView>(&binding)) {
          require(ctor == *free);
        } else if (const auto* datatype = std::get_if<DatatypeSort>(&binding)) {
          const auto group =
              query_.GetDatatypeGroups()[datatype->group].Borrow();
          require(ctor ==
                  Take(group.GetSortConstructor(datatype->slot), where));
        } else {
          require(false);
        }
        for (const auto argument : sort.GetArguments())
          work_.push_back(argument);
        break;
      }
    }
  }
}
void ModelScope::CheckNode(NodeView node, SourceSpan where) {
  CheckSort(node.GetSort(), where);
  if (node.GetValue() || node.GetKind() == Kind::kApply) return;
  const auto kind = node.GetKind();
  const auto& profile = *signature_.profile;
  if (profile.Has(OperatorFor(kind).features) ||
      (profile.Has(kRestrictedIntegers) && IsRestrictedInt(kind) &&
       node.GetChildren().size() == 2))
    return;
  Fail(DiagnosticCode::kModelFailure, where,
       "Model term uses an operator outside the active signature");
}
}  // namespace dandelion::frontend::detail

namespace dandelion::frontend {
namespace {
using detail::Fail;

template <class T>
T CheckModelNode(Result<T> result, std::string_view error) {
  if (!result) {
    if (result.error().code == ErrorCode::kResourceLimit)
      detail::NodeFailure(result.error(), {});
    Fail(DiagnosticCode::kModelFailure, {}, error);
  }
  return std::move(*result);
}
}  // namespace

ValueRef::ValueRef(std::shared_ptr<const detail::ModelValue> value) noexcept
    : value_(std::move(value)) {}
SortView ValueRef::GetSort() const noexcept {
  return value_ ? value_->sort.Borrow() : SortView{};
}
std::string_view ValueRef::GetKey() const noexcept {
  return value_ ? std::string_view(value_->key) : std::string_view{};
}
std::shared_ptr<const void> ValueRef::GetPayload() const noexcept {
  return value_ ? value_->payload : nullptr;
}
ModelProxy::ModelProxy(Node term, ValueRef value) noexcept
    : term_(std::move(term)), value_(std::move(value)) {}

ModelOwner::ModelOwner(std::unique_ptr<detail::ModelState> state) noexcept
    : state_(std::move(state)) {}
ModelOwner::~ModelOwner() = default;
Outcome<std::shared_ptr<ModelOwner>> ModelOwner::Create(
    std::shared_ptr<const QueryContext> query,
    std::unique_ptr<ModelProvider> provider, CheckAnswer answer,
    UnknownReason reason) {
  return detail::Guard({}, [&] {
    // Keep the parameter's query reference through all fallible construction.
    auto source = std::move(provider);
    if (!query || !query->GetOptions().produce_models || !source ||
        (answer != CheckAnswer::kSat && answer != CheckAnswer::kUnknown))
      Fail(DiagnosticCode::kModelFailure, {},
           "Model requires an eligible sealed query and provider");
    auto state = std::make_unique<detail::ModelState>(query, answer, reason);
    state->provider = std::move(source);
    return std::shared_ptr<ModelOwner>(new ModelOwner(std::move(state)));
  });
}
const QueryContext& ModelOwner::GetQuery() const noexcept {
  return *state_->query;
}
const std::shared_ptr<const QueryContext>& ModelOwner::GetSharedQuery()
    const noexcept {
  return state_->query;
}
CheckAnswer ModelOwner::GetAnswer() const noexcept { return state_->answer; }
UnknownReason ModelOwner::GetReason() const noexcept { return state_->reason; }
bool ModelOwner::Owns(const ValueRef& value) const noexcept {
  return value.value_ && value.value_->owner == state_->identity;
}
Outcome<ValueRef> ModelOwner::MakeValue(
    SortView sort, std::string key, std::shared_ptr<const void> payload) const {
  return detail::Guard({}, [&] {
    auto owned = CheckModelNode(state_->manager.RetainSort(sort),
                                "Invalid or foreign model value sort");
    return ValueRef(std::make_shared<detail::ModelValue>(
        detail::ModelValue{state_->identity, std::move(owned), std::move(key),
                           std::move(payload)}));
  });
}
Outcome<ModelProxy> ModelOwner::MakeProxy(const ValueRef& value) {
  return detail::Guard({}, [&] {
    if (!Owns(value) || !detail::IsAbstractSort(value.GetSort()))
      Fail(DiagnosticCode::kModelFailure, {},
           "Proxy requires an abstract value in this model");
    return ModelProxy(state_->RegisterProxy(value, state_->proxies, {}), value);
  });
}
Outcome<ValueRef> ModelOwner::LookupProxy(NodeView term) const {
  return detail::Guard({}, [&] {
    const auto retained = CheckModelNode(state_->manager.RetainNode(term),
                                         "Foreign or null proxy");
    const auto apply = retained.Borrow().GetDeclaration();
    if (apply && retained.Borrow().GetChildren().empty()) {
      const auto decl = apply->GetDeclaration();
      if (decl.GetKind() == DeclKind::kInternalFunction)
        if (const auto it = state_->proxies.find(decl.GetId());
            it != state_->proxies.end())
          return it->second;
    }
    Fail(DiagnosticCode::kModelFailure, {}, "Proxy has no value in this model");
  });
}
Outcome<ValueRef> ModelOwner::Evaluate(NodeView term) {
  return detail::Guard({}, [&] {
    const auto retained = CheckModelNode(state_->manager.RetainNode(term),
                                         "Foreign or null evaluation term");
    std::vector<NodeView> stack{retained.Borrow()};
    std::unordered_set<NodeId> visited;
    detail::ModelScope scope(GetQuery());
    while (!stack.empty()) {
      const auto node = stack.back();
      stack.pop_back();
      if (!visited.insert(node.GetId()).second) continue;
      scope.CheckNode(node, {});
      if (const auto instance = node.GetDeclaration()) {
        const auto decl = instance->GetDeclaration();
        if ((decl.GetKind() == DeclKind::kFunction &&
             !GetQuery().HasFunction(decl)) ||
            (decl.GetKind() == DeclKind::kInternalFunction &&
             !GetQuery().FindDefinition(decl) &&
             !state_->proxies.contains(decl.GetId())) ||
            decl.GetKind() == DeclKind::kFormal)
          Fail(DiagnosticCode::kModelFailure, {},
               "Term contains an unbound model symbol");
      }
      for (const auto child : node.GetChildren()) stack.push_back(child);
    }
    return EvaluateResolved(term, {});
  });
}
ValueRef ModelOwner::EvaluateResolved(NodeView term, SourceSpan where) {
  auto value = detail::Use(detail::InvokeProvider([&] {
                             return state_->provider->Evaluate(*this, term);
                           }),
                           where);
  if (!Owns(value) || value.GetSort() != term.GetSort())
    Fail(DiagnosticCode::kModelFailure, where,
         "Provider returned a foreign or ill-sorted value");
  return value;
}
Outcome<std::pair<Node, std::string>> ModelOwner::ResolveName(
    std::string_view name, std::optional<SortView> qualifier,
    SourceSpan where) const {
  return detail::Guard(where, [&] {
    const auto index = detail::AbstractOrdinal(name);
    if (!index || *index >= state_->exposed.size())
      Fail(DiagnosticCode::kInvalidInput, where,
           "Abstract value is not exposed in this model");
    const auto& binding = state_->exposed[*index];
    if (qualifier && *qualifier != binding.value.GetSort())
      detail::NodeFailure({ErrorCode::kSortMismatch}, where);
    return std::pair{binding.proxy, binding.spelling};
  });
}
#ifdef DANDELION_FRONTEND_TESTING
void ModelOwner::ForceKeyHashCollisions() {
  decltype(state_->keys) replacement(state_->keys.bucket_count(),
                                     detail::AbstractHash{true});
  replacement.insert(state_->keys.begin(), state_->keys.end());
  state_->keys.swap(replacement);
}
#endif

Outcome<Session> Session::ForModelText(std::shared_ptr<ModelOwner> model,
                                       std::string_view text,
                                       std::uint32_t source) {
  return detail::Guard({source, {}, {}}, [&] {
    return Session(std::make_unique<detail::SessionState>(
        detail::Scanner(text, source), std::move(model)));
  });
}
Outcome<Session> Session::ForModelReader(std::shared_ptr<ModelOwner> model,
                                         Reader reader, std::uint32_t source) {
  return detail::Guard({source, {}, {}}, [&] {
    return Session(std::make_unique<detail::SessionState>(
        detail::Scanner(std::move(reader), source), std::move(model)));
  });
}
}  // namespace dandelion::frontend
