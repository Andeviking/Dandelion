module dandelion.frontend;
import :internal;
import dandelion.util.numeric;
import std;

namespace dandelion::frontend::detail {
namespace {
void HashCombine(std::size_t& value, std::size_t next) noexcept {
  value ^= next + std::size_t{0x9e3779b9} + (value << 6) + (value >> 2);
}
}  // namespace

std::size_t RecipeHash::operator()(const SortRecipe& recipe) const noexcept {
  auto hash = static_cast<std::size_t>(recipe.kind);
  HashCombine(hash, recipe.concrete.GetId());
  HashCombine(hash, recipe.constructor.GetId());
  HashCombine(hash, recipe.param);
  for (const auto* child : recipe.children)
    HashCombine(hash, std::hash<const SortRecipe*>{}(child));
  return hash;
}

Transaction::Transaction(SessionState& state) noexcept
    : state_(state),
      query_(*state.building),
      signature_(*query_.signature_),
      records_(state.source_records.get()),
      pooled_names_(signature_.name_pool.GetMark()),
      term_names_(signature_.terms.Size()),
      sort_names_(signature_.sorts.Size()),
      alias_roots_(signature_.aliases.size()),
      members_(signature_.members.size()),
      assertions_(query_.assertions_.size()),
      sorts_(query_.user_sorts_.size()),
      functions_(query_.functions_.size()),
      function_runs_(query_.function_runs_.size()),
      definitions_(query_.definitions_.size()),
      groups_(query_.datatype_groups_.size()),
      source_definitions_(records_ ? records_->definitions.size() : 0),
      source_named_(records_ ? records_->named_terms.size() : 0),
      source_annotations_(records_ ? records_->annotations.size() : 0) {}

Transaction::~Transaction() noexcept {
  ClearScratch(state_.domain_sorts);
  if (committed_) return;
  while (signature_.terms.Size() > term_names_) signature_.terms.EraseLast();
  while (signature_.sorts.Size() > sort_names_) signature_.sorts.EraseLast();
  signature_.name_pool.Restore(pooled_names_);
  signature_.aliases.resize(alias_roots_);
  signature_.members.resize(members_);
  for (const auto* recipe : added_recipes_ | std::views::reverse)
    if (recipe) signature_.recipes.erase(*recipe);
  query_.function_runs_.resize(function_runs_);
  for (auto i = definitions_; i < query_.definitions_.size(); ++i)
    query_.definition_index_.erase(query_.definitions_[i].decl.GetId());
  query_.functions_.resize(functions_);
  query_.definitions_.resize(definitions_);
  query_.assertions_.resize(assertions_);
  query_.user_sorts_.resize(sorts_);
  query_.datatype_groups_.resize(groups_);
  if (records_) {
    records_->assertions.resize(assertions_);
    records_->user_sorts.resize(sorts_);
    records_->functions.resize(functions_);
    records_->definitions.resize(source_definitions_);
    records_->named_terms.resize(source_named_);
    records_->annotations.resize(source_annotations_);
  }
}

std::string_view Transaction::StoreName(std::string_view name) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  return signature_.name_pool.Append(name);
}

void Transaction::AddTerm(std::string_view name, DeclInstanceView instance) {
  const auto hint = instance.GetDeclaration().GetNameHint();
  const auto key = name == hint ? hint : StoreName(name);
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  signature_.terms.Insert(key, instance);
}
void Transaction::AddTerm(std::string_view name, DatatypeMember member) {
  const auto ordinal = signature_.members.size();
  signature_.members.push_back(member);
  const auto key = StoreName(name);
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  signature_.terms.Insert(key, MemberOrdinal(ordinal));
}
void Transaction::AddSort(std::string_view name, SortBinding binding) {
  const auto* constructor = std::get_if<SortConstructorView>(&binding);
  const auto key = constructor ? constructor->GetNameHint() : StoreName(name);
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  signature_.sorts.Insert(key, std::move(binding));
}
const SortRecipe* Transaction::InternRecipe(SortRecipe recipe) {
  for (const auto* child : recipe.children)
    recipe.height = std::max(recipe.height, child->height + 1);
  added_recipes_.push_back(nullptr);
  const auto [it, inserted] = signature_.recipes.insert(std::move(recipe));
  if (inserted)
    added_recipes_.back() = &*it;
  else
    added_recipes_.pop_back();
  return &*it;
}
void Transaction::AddAlias(std::string_view name, Node term) {
  const auto index = signature_.aliases.size();
  const auto key = StoreName(name);
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  signature_.aliases.push_back(std::move(term));
  signature_.terms.Insert(key, AliasOrdinal(index));
}
void Transaction::AddNamed(std::string_view name, const Node& term,
                           SourceSpan where) {
  AddAlias(name, term);
  if (!records_) return;
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  records_->named_terms.push_back({std::string(name), term, where});
}
void Transaction::AddAnnotation(Annotation annotation) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  records_->annotations.push_back(std::move(annotation));
}
void Transaction::AddAssertion(Node term, SourceSpan where) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  if (records_) {
    records_->assertions.push_back({term, where});
#ifdef DANDELION_FRONTEND_TESTING
    state_.Checkpoint();
#endif
  }
  query_.assertions_.push_back(std::move(term));
}
void Transaction::AddUserSort(SortConstructor sort, std::string_view spelling,
                              SourceSpan where) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  if (records_) {
    records_->user_sorts.push_back({sort, std::string(spelling), where});
#ifdef DANDELION_FRONTEND_TESTING
    state_.Checkpoint();
#endif
  }
  query_.user_sorts_.push_back(std::move(sort));
}
void Transaction::AddFunction(Decl decl, std::string_view spelling,
                              std::vector<std::string> rank, SourceSpan where) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  const auto ordinal = query_.functions_.size();
  const auto id = decl.GetId();
  if (ordinal == 0 || id - query_.functions_.back().GetId() != 1)
    query_.function_runs_.emplace_back(id, ordinal);
  if (records_) {
    records_->functions.push_back(
        {decl, std::string(spelling), std::move(rank), where});
#ifdef DANDELION_FRONTEND_TESTING
    state_.Checkpoint();
#endif
  }
  query_.functions_.push_back(std::move(decl));
}
std::size_t Transaction::AddDefinition(DefinedFunction definition) {
  const auto index = query_.definitions_.size();
  const auto id = definition.decl.GetId();
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  query_.definitions_.push_back(std::move(definition));
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  query_.definition_index_.emplace(id, index);
  return index;
}
void Transaction::AddDefinitionSource(DefinitionSource source) {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  records_->definitions.push_back(std::move(source));
}
std::size_t Transaction::StageGroup() {
#ifdef DANDELION_FRONTEND_TESTING
  state_.Checkpoint();
#endif
  const auto index = query_.datatype_groups_.size();
  query_.datatype_groups_.emplace_back();
  return index;
}
void Transaction::FinishGroup(std::size_t index, DatatypeGroup group) noexcept {
  query_.datatype_groups_[index] = std::move(group);
}
const Node* PendingTerms::Find(std::string_view name) const noexcept {
  if (!named) return nullptr;
  const auto it = named->index.find(name);
  return it == named->index.end() ? nullptr : &named->records[it->second].term;
}

void PendingTerms::AddNamed(SessionState& state, std::string_view name,
                            const Node& term, SourceSpan where) {
#ifdef DANDELION_FRONTEND_TESTING
  state.Checkpoint();
#endif
  if (!named) {
    state.Count(&FrontendStats::staging_overlays);
    named = std::make_unique<NamedAliases>();
  }
  auto& entry = named->records.emplace_back(std::string(name), term, where);
  named->index.emplace(entry.name, named->records.size() - 1);
}

void PendingTerms::Publish(Transaction& txn) {
  if (named) {
    for (const auto& [name, term, where] : named->records)
      txn.AddNamed(name, term, where);
  }
  for (auto& annotation : annotations) txn.AddAnnotation(std::move(annotation));
}

bool IsReservedName(std::string_view name) noexcept {
  return name.starts_with('@') || name.starts_with('.');
}
void CheckSortName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global) {
  const auto token = syntax.GetTokenInfo(name);
  const auto spelling = syntax.Name(token);
  if (IsReservedName(spelling) ||
      IsActiveSort(*state.GetSignature().profile, token.spelling_id))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Reserved sort name");
  if (global && state.GetSignature().sorts.contains(spelling))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Sort name is already in use");
}
void CheckTermName(const SessionState& state, std::string_view name,
                   SpellingId spelling, SourceSpan where, bool global) {
  if (IsReservedName(name) ||
      IsActiveTerm(*state.GetSignature().profile, name, spelling))
    Fail(DiagnosticCode::kInvalidInput, where, "Reserved term name");
  if (global && state.GetSignature().terms.contains(name))
    Fail(DiagnosticCode::kInvalidInput, where, "Term name is already in use");
}

void CheckTermName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global) {
  const auto token = syntax.GetTokenInfo(name);
  CheckTermName(state, Syntax::Name(token), token.spelling_id, token.where,
                global);
}

std::vector<numeric::Integer> NumericIndices(const Syntax& syntax,
                                             Identifier id,
                                             std::size_t expected,
                                             Location where) {
  if (id.indices.size() != expected)
    Fail(DiagnosticCode::kInvalidInput, where, "Wrong number of indices");
  std::vector<numeric::Integer> indices;
  indices.reserve(expected);
  for (const auto index : id.indices) {
    const auto token = syntax.GetTokenInfo(index);
    if (token.kind != TokenKind::kNumeral)
      Fail(DiagnosticCode::kInvalidInput, syntax.Span(index),
           "Expected a numeral index");
    const auto digits = token.raw;
    if (const auto value = ParseWord(digits);
        value && *value <= static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) {
      indices.emplace_back(static_cast<std::int64_t>(*value));
    } else {
      indices.push_back(TakeNumericAt(numeric::Integer::ParseDecimal(digits),
                                      {syntax, index}));
    }
  }
  return indices;
}
std::uint64_t ParseArity(const Syntax& syntax, ExprId numeral) {
  const auto digits = syntax.Raw(numeral);
  std::uint64_t arity = 0;
  // Command grammar has already established the numeral's syntax.
  if (std::from_chars(digits.data(), digits.data() + digits.size(), arity).ec ==
      std::errc::result_out_of_range)
    NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize},
                syntax.Span(numeral));
  return arity;
}
void CheckMinimumIndex(const Syntax& syntax, Identifier id,
                       std::span<const numeric::Integer> indices,
                       std::size_t index, std::int64_t minimum) {
  if (indices[index].Borrow().Compare(numeric::IntegerView(minimum)) < 0)
    NodeFailure(
        {.code = ErrorCode::kInvalidParameter, .parameter_index = index},
        syntax.Span(id.indices[index]));
}
}  // namespace dandelion::frontend::detail
