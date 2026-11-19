module dandelion.frontend;
import :internal;
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
      named_(query_.named_terms_.size()),
      annotations_(query_.annotations_.size()),
      assertions_(query_.assertions_.size()),
      sorts_(query_.user_sorts_.size()),
      functions_(query_.functions_.size()),
      definitions_(query_.definitions_.size()),
      groups_(query_.datatype_groups_.size()) {}

Transaction::~Transaction() noexcept {
  if (committed_) return;
  for (auto sort : added_spellings_) signature_.sort_spellings.erase(sort);
  for (const auto& [space, name] : names_ | std::views::reverse) {
    if (space == NameSpace::kTerm) {
      const auto it = signature_.terms.find(name);
      if (it != signature_.terms.end()) signature_.terms.erase(it);
    } else {
      const auto it = signature_.sorts.find(name);
      if (it != signature_.sorts.end()) signature_.sorts.erase(it);
    }
  }
  for (const auto* recipe : added_recipes_ | std::views::reverse)
    if (recipe) signature_.recipes.erase(*recipe);
  for (auto i = functions_; i < query_.functions_.size(); ++i)
    signature_.function_index.erase(query_.functions_[i].decl.GetId());
  for (auto i = definitions_; i < query_.definitions_.size(); ++i)
    signature_.definition_index.erase(query_.definitions_[i].decl.GetId());
  query_.functions_.resize(functions_);
  query_.definitions_.resize(definitions_);
  query_.named_terms_.resize(named_);
  query_.annotations_.resize(annotations_);
  query_.assertions_.resize(assertions_);
  query_.user_sorts_.resize(sorts_);
  query_.datatype_groups_.resize(groups_);
}

void Transaction::AddTerm(std::string_view name, TermBinding binding) {
  names_.push_back({NameSpace::kTerm, name});
  state_.Checkpoint();
  signature_.terms.emplace(std::string(name), std::move(binding));
}
void Transaction::AddSort(std::string_view name, SortBinding binding) {
  names_.push_back({NameSpace::kSort, name});
  state_.Checkpoint();
  signature_.sorts.emplace(std::string(name), std::move(binding));
}
void Transaction::RememberAlias(SortView sort, std::string_view name) {
  added_spellings_.push_back(sort.GetId());
  if (!signature_.sort_spellings.try_emplace(sort.GetId(), name).second)
    added_spellings_.pop_back();
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
void Transaction::AddNamed(std::string_view name, Node term, SourceSpan where) {
  const auto index = query_.named_terms_.size();
  AddTerm(name, index);
  state_.Checkpoint();
  query_.named_terms_.push_back({std::string(name), std::move(term), where});
}
void Transaction::AddAnnotation(Annotation annotation) {
  state_.Checkpoint();
  query_.annotations_.push_back(std::move(annotation));
}
void Transaction::AddAssertion(Assertion assertion) {
  state_.Checkpoint();
  query_.assertions_.push_back(std::move(assertion));
}
void Transaction::AddUserSort(UserSort sort) {
  state_.Checkpoint();
  query_.user_sorts_.push_back(std::move(sort));
}
void Transaction::AddFunction(UserFunction function) {
  const auto index = query_.functions_.size();
  const auto id = function.decl.GetId();
  state_.Checkpoint();
  query_.functions_.push_back(std::move(function));
  signature_.function_index.emplace(id, index);
}
void Transaction::AddDefinition(DefinedFunction definition) {
  const auto index = query_.definitions_.size();
  const auto id = definition.decl.GetId();
  state_.Checkpoint();
  query_.definitions_.push_back(std::move(definition));
  signature_.definition_index.emplace(id, index);
}
std::size_t Transaction::StageGroup() {
  state_.Checkpoint();
  const auto index = query_.datatype_groups_.size();
  query_.datatype_groups_.emplace_back();
  return index;
}
void Transaction::FinishGroup(std::size_t index, DatatypeGroup group) noexcept {
  query_.datatype_groups_[index] = std::move(group);
}
bool IsReservedName(std::string_view name) noexcept {
  return name.starts_with('@') || name.starts_with('.');
}
void CheckSortName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global) {
  const auto spelling = syntax.Name(name);
  if (IsReservedName(spelling) ||
      IsActiveSort(*state.GetSignature().profile, spelling))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Reserved sort name");
  if (global && state.GetSignature().sorts.contains(spelling))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Sort name is already in use");
}
void CheckTermName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global) {
  const auto spelling = syntax.Name(name);
  if (IsReservedName(spelling) ||
      IsActiveTerm(*state.GetSignature().profile, spelling))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Reserved term name");
  if (global && state.GetSignature().terms.contains(spelling))
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Term name is already in use");
}

std::vector<Integer> NumericIndices(const Syntax& syntax, Identifier id,
                                    std::size_t expected, SourceSpan where) {
  if (id.indices.size() != expected)
    Fail(DiagnosticCode::kInvalidInput, where, "Wrong number of indices");
  std::vector<Integer> indices;
  indices.reserve(expected);
  for (const auto index : id.indices) {
    if (syntax.Type(index) != TokenKind::kNumeral)
      Fail(DiagnosticCode::kInvalidInput, syntax.Span(index),
           "Expected a numeral index");
    indices.push_back(
        Take(Integer::ParseDecimal(syntax.Raw(index)), syntax.Span(index)));
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
                       std::span<const Integer> indices, std::size_t index,
                       std::int64_t minimum) {
  if (indices[index].Borrow().Compare(IntegerView(minimum)) < 0)
    NodeFailure(
        {.code = ErrorCode::kInvalidParameter, .parameter_index = index},
        syntax.Span(id.indices[index]));
}
}  // namespace dandelion::frontend::detail
