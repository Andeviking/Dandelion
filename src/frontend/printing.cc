module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {

void Symbol(std::string& output, std::string_view spelling) {
  if (IsSimpleSymbol(spelling) && !IsReservedWord(spelling)) {
    output.append(spelling);
  } else {
    output.push_back('|');
    output.append(spelling);
    output.push_back('|');
  }
}
struct SortSpelling {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string head;
  std::vector<SortView> arguments;
  std::size_t size = 1;
  std::size_t height = 0;
  std::size_t atomic_subsort_height = 0;
  // NOLINTEND(readability-identifier-naming)
};

SortSpelling ExpandSpelling(SortView sort) {
  SortSpelling spelling;
  switch (sort.GetKind()) {
    case SortKind::kBool:
      spelling.head = "Bool";
      break;
    case SortKind::kInt:
      spelling.head = "Int";
      break;
    case SortKind::kRoundingMode:
      spelling.head = "RoundingMode";
      break;
    case SortKind::kBitVector:
      spelling.head = "(_ BitVec " + std::to_string(*sort.GetWidth()) + ")";
      break;
    case SortKind::kFloatingPoint:
      spelling.head = "(_ FloatingPoint " +
                      std::to_string(*sort.GetFpExponentWidth()) + " " +
                      std::to_string(*sort.GetFpSignificandWidth()) + ")";
      break;
    case SortKind::kArray:
      spelling.head = "Array";
      spelling.arguments = {*sort.GetArrayIndex(), *sort.GetArrayElement()};
      break;
    case SortKind::kDatatype:
    case SortKind::kUninterpreted:
      Symbol(spelling.head, sort.GetSortConstructor()->GetNameHint());
      for (const auto argument : sort.GetArguments())
        spelling.arguments.push_back(argument);
      break;
  }
  return spelling;
}

struct AliasCandidate {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view name;
  const Alias* alias;
  std::size_t height;
  // NOLINTEND(readability-identifier-naming)
};

struct MatchKey {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  const SortRecipe* recipe;
  SortView sort;
  // NOLINTEND(readability-identifier-naming)
  friend bool operator==(MatchKey, MatchKey) = default;
};

struct MatchHash {
  std::size_t operator()(MatchKey key) const noexcept {
    const auto hash = std::hash<const SortRecipe*>{}(key.recipe);
    return hash ^ (std::hash<SortId>{}(key.sort.GetId()) +
                   std::size_t{0x9e3779b9} + (hash << 6) + (hash >> 2));
  }
};

SortView MatchArgument(SortView sort, std::size_t index) noexcept {
  if (sort.GetKind() == SortKind::kArray)
    return index == 0 ? *sort.GetArrayIndex() : *sort.GetArrayElement();
  return sort.GetArguments()[index];
}

class SortPrinter {
 public:
  SortPrinter(const Signature& signature, SessionState& session)
      : signature_(signature), session_(session) {}
  void Append(std::string& output, SortView sort);

 private:
  using Bindings = std::unordered_map<std::uint64_t, SortView>;

  void IndexAliases();
  const std::optional<Bindings>& Match(MatchKey root);
  void ChooseSpellings(SortView root);
  std::size_t Size(std::span<const SortView> arguments) const;

  const Signature& signature_;
  SessionState& session_;
  bool indexed_ = false;
  std::vector<AliasCandidate> aliases_;
  std::unordered_map<MatchKey, std::optional<Bindings>, MatchHash> matches_;
  std::unordered_map<SortId, SortSpelling> spellings_;
};

void SortPrinter::IndexAliases() {
  for (const auto& [name, binding] : signature_.sorts) {
    const auto* alias = std::get_if<Alias>(&binding);
    // A parameter root would need the sort itself as a printed argument.
    if (alias && alias->root->kind != RecipeKind::kParameter)
      aliases_.push_back({name, alias, alias->root->height});
  }
  std::ranges::sort(aliases_, {}, &AliasCandidate::height);
  indexed_ = true;
}

const std::optional<SortPrinter::Bindings>& SortPrinter::Match(MatchKey root) {
  if (const auto found = matches_.find(root); found != matches_.end())
    return found->second;
  // Each pair has a unique substitution or fails. Child substitutions must
  // agree on every shared parameter for the parent pair to match.
  std::vector<std::pair<MatchKey, bool>> work{{root, false}};
  while (!work.empty()) {
    const auto [key, expanded] = work.back();
    work.pop_back();
    if (matches_.contains(key)) continue;
    const auto [recipe, actual] = key;
    if (!expanded) {
      session_.Count(&FrontendStats::sort_recipe_pairs);
      switch (recipe->kind) {
        case RecipeKind::kParameter:
          matches_.emplace(key, Bindings{{recipe->param, actual}});
          continue;
        case RecipeKind::kConcrete:
          matches_.emplace(key, actual == recipe->concrete
                                    ? std::optional(Bindings{})
                                    : std::nullopt);
          continue;
        case RecipeKind::kArray:
          if (actual.GetKind() == SortKind::kArray) break;
          matches_.emplace(key, std::nullopt);
          continue;
        case RecipeKind::kApply:
          if (actual.GetSortConstructor() == recipe->constructor) break;
          matches_.emplace(key, std::nullopt);
          continue;
      }
      work.emplace_back(key, true);
      for (std::size_t i = 0; i < recipe->children.size(); ++i)
        work.emplace_back(
            MatchKey{recipe->children[i], MatchArgument(actual, i)}, false);
      continue;
    }
    auto bindings = std::optional<Bindings>(std::in_place);
    for (std::size_t i = 0; i < recipe->children.size(); ++i) {
      const auto& child =
          matches_.at({recipe->children[i], MatchArgument(actual, i)});
      if (!child) {
        bindings.reset();
        break;
      }
      for (auto [parameter, argument] : *child) {
        const auto [it, inserted] = bindings->try_emplace(parameter, argument);
        if (!inserted && it->second != argument) {
          bindings.reset();
          break;
        }
      }
      if (!bindings) break;
    }
    matches_.emplace(key, std::move(bindings));
  }
  return matches_.at(root);
}

std::size_t SortPrinter::Size(std::span<const SortView> arguments) const {
  constexpr auto largest = std::numeric_limits<std::size_t>::max();
  std::size_t size = 1;
  for (const auto argument : arguments) {
    const auto child =
        argument.IsNull() ? 1 : spellings_.at(argument.GetId()).size;
    if (child > largest - size) return largest;
    size += child;
  }
  return size;
}

void SortPrinter::ChooseSpellings(SortView root) {
  if (!indexed_) IndexAliases();
  // Alias arguments are strict subsorts. Choose once per concrete sort, in
  // bottom-up order, so shared children never require unfolding here.
  using Task = std::variant<SortView, std::pair<SortView, SortSpelling>>;
  std::vector<Task> work{root};
  while (!work.empty()) {
    auto task = std::move(work.back());
    work.pop_back();
    if (const auto* sort = std::get_if<SortView>(&task)) {
      if (spellings_.contains(sort->GetId())) continue;
      auto spelling = ExpandSpelling(*sort);
      const auto children = std::span<const SortView>(spelling.arguments);
      work.emplace_back(std::pair{*sort, std::move(spelling)});
      for (auto child : children) work.emplace_back(child);
      continue;
    }
    auto [sort, spelling] = std::move(std::get<1>(task));
    for (const auto argument : spelling.arguments) {
      const auto& child = spellings_.at(argument.GetId());
      spelling.height = std::max(spelling.height, child.height + 1);
      spelling.atomic_subsort_height = std::max(
          spelling.atomic_subsort_height,
          child.size == 1 ? child.height : child.atomic_subsort_height);
    }
    const auto expanded_size = Size(spelling.arguments);
    spelling.size = expanded_size;
    const auto written = signature_.sort_spellings.find(sort.GetId());
    auto best = std::tuple{expanded_size, true, spelling.head.size(), false,
                           std::string_view{}};
    auto next = std::ranges::upper_bound(aliases_, spelling.height, {},
                                         &AliasCandidate::height);
    while (next != aliases_.begin()) {
      const auto [name, alias, height] = *--next;
      // Once at most two symbols suffice, only nullary aliases and aliases
      // with one atomic argument can compete. Substitution of such an argument
      // can add at most its concrete height to the height of the recipe.
      if (spelling.size <= 2 &&
          spelling.height - height > spelling.atomic_subsort_height)
        break;
      if (alias->arity >= spelling.size) continue;
      const auto& bindings = Match({alias->root, sort});
      if (!bindings) continue;
      // An unused parameter accepts any sort. Spell it as Bool.
      std::vector<SortView> arguments(alias->arity);
      for (auto [parameter, argument] : *bindings)
        arguments[parameter] = argument;
      const auto size = Size(arguments);
      const bool in_source =
          written != signature_.sort_spellings.end() && written->second == name;
      // Keep written compound spellings on ties. For atomic spellings a
      // shorter name wins. Inferred applications must abbreviate structure.
      if (size > spelling.size ||
          (size == expanded_size && !in_source && alias->arity != 0))
        continue;
      std::string head;
      Symbol(head, name);
      const auto score = std::tuple{size, !in_source || size == 1, head.size(),
                                    true, std::string_view(name)};
      if (score < best) {
        spelling.head = std::move(head);
        spelling.arguments = std::move(arguments);
        spelling.size = size;
        best = score;
      }
    }
    spellings_.emplace(sort.GetId(), std::move(spelling));
  }
}

void SortPrinter::Append(std::string& output, SortView sort) {
  ChooseSpellings(sort);
  using Task = std::variant<SortView, char>;
  std::vector<Task> work{sort};
  while (!work.empty()) {
    const auto task = work.back();
    work.pop_back();
    if (const auto* ch = std::get_if<char>(&task)) {
      output.push_back(*ch);
      continue;
    }
    const auto next = std::get<SortView>(task);
    if (next.IsNull()) {
      output.append("Bool");
      continue;
    }
    const auto& spelling = spellings_.at(next.GetId());
    if (spelling.arguments.empty()) {
      output.append(spelling.head);
      continue;
    }
    output.push_back('(');
    output.append(spelling.head);
    work.push_back(')');
    for (auto argument : spelling.arguments | std::views::reverse) {
      work.push_back(argument);
      work.push_back(' ');
    }
  }
}
void IntegerText(std::string& out, IntegerView value,
                 bool binary_negative = false) {
  const auto text = value.ToDecimal();
  if (text.starts_with('-')) {
    out.append(binary_negative ? "(- 0 " : "(- ");
    out.append(std::string_view(text).substr(1)).push_back(')');
  } else {
    out.append(text);
  }
}

void Binary(std::string& out, IntegerView bits, std::uint32_t width,
            std::uint64_t offset = 0) {
  out.append("#b");
  const auto start = out.size();
  if (width > out.max_size() - start)
    throw std::length_error("Binary literal size");
  out.resize(start + width, '0');
  const auto end = std::min(offset + width, bits.BitLength());
  for (auto bit = offset; bit < end;) {
    const auto chunk = bits.MagnitudeChunk(bit / 64);
    const auto until = std::min(end, (bit / 64 + 1) * 64);
    for (; bit < until; ++bit)
      out[start + width - 1 - static_cast<std::size_t>(bit - offset)] =
          ((chunk >> (bit % 64)) & 1) != 0 ? '1' : '0';
  }
}
void FpFields(std::string& out, bool negative, std::uint32_t exponent_width,
              std::uint32_t significand_width, IntegerView exponent,
              IntegerView fraction) {
  out.append(negative ? "(fp #b1 " : "(fp #b0 ");
  Binary(out, exponent, exponent_width);
  out.push_back(' ');
  Binary(out, fraction, significand_width - 1);
  out.push_back(')');
}
void FpSpecialText(std::string& out, FpSpecial value, std::uint32_t exp,
                   std::uint32_t sig) {
  out.append("(_ ").append(SpecialSpelling(value)).push_back(' ');
  out.append(std::to_string(exp)).push_back(' ');
  out.append(std::to_string(sig)).push_back(')');
}
void ScalarText(std::string& out, NodeView node, bool binary_negative) {
  const auto value = *node.GetValue();
  switch (node.GetKind()) {
    case Kind::kBoolValue:
      out.append(*value.GetBool() ? "true" : "false");
      break;
    case Kind::kIntValue:
      IntegerText(out, *value.GetInteger(), binary_negative);
      break;
    case Kind::kBvValue:
      Binary(out, *value.GetInteger(), *node.GetSort().GetWidth());
      break;
    case Kind::kRoundingModeValue:
      out.append(RoundingSpelling(*value.GetRoundingMode()));
      break;
    case Kind::kFpValue: {
      const auto sort = node.GetSort();
      const auto e = *sort.GetFpExponentWidth();
      const auto s = *sort.GetFpSignificandWidth();
      const auto cat = *value.GetFpCategory();
      if (cat == FpCategory::kPositiveZero ||
          cat == FpCategory::kNegativeZero) {
        FpFields(out, cat == FpCategory::kNegativeZero, e, s, IntegerView{},
                 IntegerView{});
      } else if (cat == FpCategory::kFiniteBits) {
        const auto bits = *value.GetFpBits();
        const auto sign_offset = static_cast<std::uint64_t>(e) + s - 1;
        const bool negative =
            ((bits.MagnitudeChunk(sign_offset / 64) >> (sign_offset % 64)) &
             1) != 0;
        out.append(negative ? "(fp #b1 " : "(fp #b0 ");
        Binary(out, bits, e, s - 1);
        out.push_back(' ');
        Binary(out, bits, s - 1);
        out.push_back(')');
      } else {
        FpSpecialText(out, static_cast<FpSpecial>(cat), e, s);
      }
      break;
    }
    default:
      std::unreachable();
  }
}

void IndexedHead(std::string& out, std::string_view op, NodeIndices indices) {
  out.append("(_ ").append(op);
  for (std::size_t i = 0; i < indices.size(); ++i) {
    out.push_back(' ');
    out.append(indices[i].ToDecimal());
  }
  out.push_back(')');
}
bool RatioHasDivision(RationalView value) {
  return value.Denominator().TryUint64() != 1;
}
void RatioText(std::string& out, RationalView value) {
  const auto numerator = value.Numerator();
  const auto denominator = value.Denominator();
  if (!RatioHasDivision(value)) {
    IntegerText(out, numerator);
  } else {
    out.append("(/ ");
    IntegerText(out, numerator);
    out.push_back(' ');
    IntegerText(out, denominator);
    out.push_back(')');
  }
}
bool WorthSharing(NodeView node) noexcept {
  if (!node.GetChildren().empty()) return true;
  const auto kind = node.GetKind();
  if (kind == Kind::kBvValue) return *node.GetSort().GetWidth() > 64;
  if (kind == Kind::kIntValue)
    return node.GetValue()->GetInteger()->BitLength() > 64;
  if (kind == Kind::kFpValue)
    return std::uint64_t{*node.GetSort().GetFpExponentWidth()} +
               *node.GetSort().GetFpSignificandWidth() >
           64;
  return false;
}

}  // namespace

class ModelPrinter {
 public:
  ModelPrinter(ModelOwner& owner, SessionState& session, SourceSpan where)
      : owner_(owner),
        state_(*owner.state_),
        signature_(Signature::Of(owner.GetQuery())),
        scope_(owner.GetQuery()),
        profile_(*signature_.profile),
        where_(where),
        sort_text_(signature_, session),
        staged_keys_(0, state_.keys.hash_function()) {
    if (state_.writing_response) Bad("Model response is already in progress");
    state_.writing_response = true;
  }
  ~ModelPrinter() { state_.writing_response = false; }

  void Values(std::span<const ValueRequest> values, Response& response);
  void Model(Response& response);

 private:
  void Finish(Response& response, SourceSpan where, std::string_view closing);
  void CommitExposures() noexcept;
  [[noreturn]] void Bad(std::string_view message) const {
    Fail(DiagnosticCode::kModelFailure, where_, message);
  }
  std::string_view Expose(const ValueRef& value);
  bool NeedsQualifier(DeclInstanceView instance);
  void Constructor(std::string& out, DeclInstanceView instance);
  std::string Value(const ValueRef& value);
  std::string Function(const ExportedFunction& record,
                       const UserFunction& original);
  void Body(std::string& out, NodeView root);
  void Validate(NodeView node);
  std::vector<NodeView> Postorder(NodeView root);
  std::string FreshName();
  std::string RoundedSuffix(NodeView node, std::string_view name);

  ModelOwner& owner_;
  ModelState& state_;
  const Signature& signature_;
  ModelScope scope_;
  const Profile& profile_;
  SourceSpan where_;
  SortPrinter sort_text_;
  std::vector<ExposedValue> staged_;
  AbstractNames staged_keys_;
  ProxyBindings staged_proxies_;
  std::unordered_map<DeclId, bool> qualifiers_;
  std::unordered_map<DeclId, std::string> formals_;
  std::unordered_map<DeclId, ValueRef> export_proxies_;
  std::unordered_set<DeclId> written_;
  std::unordered_map<NodeId, std::uint8_t> references_;
  std::unordered_map<NodeId, std::string> shared_;
  std::size_t ordinal_ = 0;
};

std::string_view ModelPrinter::Expose(const ValueRef& value) {
  if (!owner_.Owns(value) || !IsAbstractSort(value.GetSort()))
    Bad("Cannot expose this abstract model value");
  scope_.CheckSort(value.GetSort(), where_);
  const AbstractKey key{value.GetSort().GetId(), value.GetKey()};
  if (const auto it = state_.keys.find(key); it != state_.keys.end())
    return state_.exposed[it->second].spelling;
  const auto first = state_.exposed.size();
  if (const auto it = staged_keys_.find(key); it != staged_keys_.end())
    return staged_[it->second - first].spelling;
  if (staged_.size() == state_.exposed.max_size() - first)
    throw std::length_error("Model exposure count");
  const auto ordinal = first + staged_.size();
  auto text = "(as @value" + std::to_string(ordinal) + " ";
  sort_text_.Append(text, value.GetSort());
  text.push_back(')');
  auto proxy = state_.RegisterProxy(value, staged_proxies_, where_);
  staged_keys_.emplace(key, ordinal);
  return staged_.emplace_back(std::move(proxy), value, std::move(text))
      .spelling;
}

bool ModelPrinter::NeedsQualifier(DeclInstanceView instance) {
  const auto constructor = instance.GetDeclaration();
  const auto size = constructor.GetParameterCount();
  if (size == 0) return false;
  const auto found = qualifiers_.find(constructor.GetId());
  if (found != qualifiers_.end()) return found->second;
  std::vector<bool> bound(size, false);
  std::unordered_set<std::uint64_t> visited;
  std::vector<SortRecipeView> recipes;
  for (std::size_t i = 0; i < instance.GetDomain().size(); ++i)
    recipes.push_back(Take(constructor.GetDomainRecipe(i), where_));
  while (!recipes.empty()) {
    const auto recipe = recipes.back();
    recipes.pop_back();
    if (!visited.insert(recipe.GetId()).second) continue;
    if (const auto slot = recipe.GetParameter()) bound[slot->second] = true;
    for (std::size_t i = 0; i < recipe.GetArgumentCount(); ++i)
      recipes.push_back(Take(recipe.Argument(i), where_));
  }
  const auto needs =
      std::ranges::any_of(bound, [](bool item) { return !item; });
  qualifiers_.emplace(constructor.GetId(), needs);
  return needs;
}
void ModelPrinter::Constructor(std::string& out, DeclInstanceView instance) {
  const auto constructor = instance.GetDeclaration();
  if (!NeedsQualifier(instance)) {
    Symbol(out, constructor.GetNameHint());
    return;
  }
  out.append("(as ");
  Symbol(out, constructor.GetNameHint());
  out.push_back(' ');
  sort_text_.Append(out, instance.GetRange());
  out.push_back(')');
}

std::string ModelPrinter::Value(const ValueRef& value) {
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    ValueRef ref;
    std::optional<DatatypeValue> datatype;
    std::size_t next = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  std::string text;
  std::vector<Frame> stack{{value, {}}};
  std::unordered_set<AbstractKey, AbstractHash> active(0, AbstractHash{});
  while (!stack.empty()) {
    auto& frame = stack.back();
    const auto sort = frame.ref.GetSort();
    if (!frame.datatype) {
      if (!owner_.Owns(frame.ref)) Bad("Value belongs to a different model");
      if (IsAbstractSort(sort)) {
        text.append(Expose(frame.ref));
        stack.pop_back();
        continue;
      }
      auto description =
          Use(InvokeProvider(
                  [&] { return state_.provider->Describe(owner_, frame.ref); }),
              where_);
      if (sort.GetKind() != SortKind::kDatatype) {
        const auto* scalar = std::get_if<Node>(&description);
        if (!scalar || scalar->Borrow().IsNull() ||
            scalar->Borrow().GetSort() != sort || !scalar->Borrow().GetValue())
          Bad("Provider did not supply a scalar of the requested sort");
        ScalarText(text, scalar->Borrow(), false);
        stack.pop_back();
        continue;
      }
      const auto* datatype = std::get_if<DatatypeValue>(&description);
      if (!datatype || datatype->constructor.Borrow().IsNull())
        Bad("Provider did not supply a datatype constructor");
      const auto instance = datatype->constructor.Borrow();
      if (instance.GetRange() != sort ||
          instance.GetDeclaration().GetKind() !=
              DeclKind::kDatatypeConstructor ||
          instance.GetDomain().size() != datatype->fields.size())
        Bad("Provider supplied an invalid datatype value");
      for (std::size_t i = 0; i < datatype->fields.size(); ++i)
        if (datatype->fields[i].GetSort() != instance.GetDomain()[i])
          Bad("Provider supplied a datatype field of the wrong sort");
      if (datatype->fields.empty()) {
        Constructor(text, instance);
        stack.pop_back();
        continue;
      }
      if (!active.insert({sort.GetId(), frame.ref.GetKey()}).second)
        Bad("Provider supplied a cyclic datatype value");
      text.push_back('(');
      Constructor(text, instance);
      frame.datatype = std::move(std::get<DatatypeValue>(description));
      continue;
    }
    if (frame.next == frame.datatype->fields.size()) {
      active.erase({sort.GetId(), frame.ref.GetKey()});
      text.push_back(')');
      stack.pop_back();
    } else {
      text.push_back(' ');
      const auto field = frame.datatype->fields[frame.next++];
      stack.push_back({field, {}});
    }
  }
  return text;
}

std::string ModelPrinter::FreshName() {
  for (;;) {
    if (ordinal_ == std::numeric_limits<std::size_t>::max())
      NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kIdentitySpace},
                  where_);
    auto name = "model_local" + std::to_string(ordinal_++);
    if (!signature_.terms.contains(name) && !IsActiveTerm(profile_, name))
      return name;
  }
}
void ModelPrinter::Validate(NodeView node) {
  scope_.CheckNode(node, where_);
  if (const auto instance = node.GetDeclaration()) {
    const auto decl = instance->GetDeclaration();
    switch (decl.GetKind()) {
      case DeclKind::kFormal:
        if (!formals_.contains(decl.GetId()))
          Bad("Model body uses an unbound formal");
        break;
      case DeclKind::kFunction:
        if (!written_.contains(decl.GetId()))
          Bad("Model body refers to a function not yet defined");
        break;
      case DeclKind::kInternalFunction:
        if (!export_proxies_.contains(decl.GetId()))
          Bad("Model body uses a proxy absent from its export");
        break;
      case DeclKind::kDatatypeConstructor:
      case DeclKind::kDatatypeSelector:
      case DeclKind::kDatatypeTester:
        break;
    }
  }
}
std::vector<NodeView> ModelPrinter::Postorder(NodeView root) {
  std::vector<NodeView> order;
  std::vector<std::pair<NodeView, bool>> work{{root, false}};
  while (!work.empty()) {
    const auto [node, ready] = work.back();
    work.pop_back();
    if (ready) {
      Validate(node);
      order.push_back(node);
      continue;
    }
    const auto [ref, inserted] = references_.try_emplace(node.GetId(), 1);
    if (!inserted) {
      ref->second = 2;
      continue;
    }
    work.emplace_back(node, true);
    const auto children = node.GetChildren();
    for (auto i = children.size(); i > 0; --i)
      work.emplace_back(children[i - 1], false);
  }
  return order;
}

std::string ModelPrinter::RoundedSuffix(NodeView node, std::string_view name) {
  constexpr std::array modes{RoundingMode::kRne, RoundingMode::kRna,
                             RoundingMode::kRtp, RoundingMode::kRtn,
                             RoundingMode::kRtz};
  const auto sort = node.GetSort();
  const auto exp = *sort.GetFpExponentWidth();
  const auto sig = *sort.GetFpSignificandWidth();
  const auto value = *node.GetRationalParameter();
  std::string text = ")) ";
  for (auto mode : modes) {
    if (mode != modes.back())
      text.append("(ite (= ")
          .append(name)
          .append(" ")
          .append(RoundingSpelling(mode))
          .append(") ");
    const auto actual = Take(RoundRationalToFp(exp, sig, mode, value), where_);
    if (actual.infinite) {
      FpSpecialText(text,
                    actual.negative ? FpSpecial::kNegativeInfinity
                                    : FpSpecial::kPositiveInfinity,
                    exp, sig);
    } else {
      FpFields(text, actual.negative, exp, sig, actual.exponent.Borrow(),
               actual.fraction.Borrow());
    }
    if (mode != modes.back()) text.push_back(' ');
  }
  text.append(modes.size(), ')');
  return text;
}
void ModelPrinter::Body(std::string& out, NodeView root) {
  using Task = std::variant<NodeView, char, std::string>;
  std::vector<Task> work{root};
  while (!work.empty()) {
    auto task = std::move(work.back());
    work.pop_back();
    if (const auto* ch = std::get_if<char>(&task)) {
      out.push_back(*ch);
      continue;
    }
    if (const auto* text = std::get_if<std::string>(&task)) {
      out.append(*text);
      continue;
    }
    const auto node = std::get<NodeView>(task);
    if (const auto it = shared_.find(node.GetId()); it != shared_.end()) {
      out.append(it->second);
      continue;
    }
    if (node.GetValue()) {
      ScalarText(out, node, profile_.Has(kRestrictedIntegers));
      continue;
    }
    const auto children = node.GetChildren();
    if (node.GetKind() == Kind::kFpFromRational) {
      const auto value = *node.GetRationalParameter();
      if ((value.Numerator().Sign() < 0 && signature_.terms.contains("-")) ||
          (RatioHasDivision(value) && signature_.terms.contains("/"))) {
        const auto name = FreshName();
        out.append("(let ((").append(name).push_back(' ');
        work.push_back(RoundedSuffix(node, name));
      } else {
        out.push_back('(');
        IndexedHead(out, "to_fp", node.GetIndices());
        out.push_back(' ');
        std::string tail = " ";
        RatioText(tail, value);
        tail.push_back(')');
        work.push_back(std::move(tail));
      }
      work.push_back(children[0]);
      continue;
    }
    if (!children.empty()) out.push_back('(');
    if (const auto instance = node.GetDeclaration()) {
      const auto decl = instance->GetDeclaration();
      switch (decl.GetKind()) {
        case DeclKind::kFormal:
          out.append(formals_.at(decl.GetId()));
          break;
        case DeclKind::kInternalFunction:
          out.append(Expose(export_proxies_.at(decl.GetId())));
          break;
        case DeclKind::kFunction:
        case DeclKind::kDatatypeSelector:
          Symbol(out, decl.GetNameHint());
          break;
        case DeclKind::kDatatypeConstructor:
          Constructor(out, *instance);
          break;
        case DeclKind::kDatatypeTester:
          out.append("(_ is ");
          Symbol(out, decl.GetConstructor()->GetNameHint());
          out.push_back(')');
          break;
      }
    } else if (node.GetKind() == Kind::kArrayConst) {
      out.append("(as const ");
      sort_text_.Append(out, node.GetSort());
      out.push_back(')');
    } else {
      const auto& op = OperatorFor(node.GetKind());
      if (op.indices != 0)
        IndexedHead(out, op.name, node.GetIndices());
      else
        out.append(op.name);
    }
    if (!children.empty()) {
      work.push_back(')');
      for (std::size_t i = children.size(); i > 0; --i) {
        work.push_back(children[i - 1]);
        work.push_back(' ');
      }
    }
  }
}

std::string ModelPrinter::Function(const ExportedFunction& record,
                                   const UserFunction& original) {
  formals_.clear();
  references_.clear();
  shared_.clear();
  ordinal_ = 0;
  const auto decl = record.declaration.Borrow();
  const auto domain = decl.GetDomain();
  const auto body = record.body.Borrow();
  if (body.IsNull() || body.GetSort() != decl.GetRange() ||
      record.formals.size() != domain.size())
    Bad("Model definition does not have the original rank");
  auto out = "(define-fun " + original.source_name + " (";
  for (std::size_t i = 0; i < domain.size(); ++i) {
    const auto formal = record.formals[i].Borrow();
    if (formal.IsNull() || formal.GetKind() != DeclKind::kFormal ||
        formal.GetRange() != domain[i] || !formal.SameManager(decl))
      Bad("Invalid model formal");
    const auto [entry, unique] = formals_.emplace(formal.GetId(), FreshName());
    if (!unique) Bad("Duplicate model formal");
    if (i != 0) out.push_back(' ');
    out.push_back('(');
    out.append(entry->second).push_back(' ');
    out.append(original.source_rank[i]).push_back(')');
  }
  out.append(") ").append(original.source_rank.back()).push_back(' ');
  for (const auto term : Postorder(body)) {
    if (references_.at(term.GetId()) < 2 || !WorthSharing(term)) continue;
    auto name = FreshName();
    out.append("(let ((").append(name).push_back(' ');
    Body(out, term);
    out.append(")) ");
    shared_.emplace(term.GetId(), std::move(name));
  }
  Body(out, body);
  out.append(shared_.size() + 1, ')');
  return out;
}

void ModelPrinter::Values(std::span<const ValueRequest> values,
                          Response& response) {
  const auto command = where_;
  bool first = true;
  for (const auto& request : values) {
    where_ = request.where;
    const auto value = owner_.EvaluateResolved(request.term.Borrow(), where_);
    auto pair = first ? std::string("((") : std::string(" (");
    pair.append(request.input).push_back(' ');
    pair.append(Value(value)).push_back(')');
    response.Write(pair, where_);
    first = false;
  }
  Finish(response, command, ")\n");
}
void ModelPrinter::Model(Response& response) {
  const auto model =
      Use(InvokeProvider([&] { return state_.provider->ExportModel(owner_); }),
          where_);
  const auto originals = state_.query->GetFunctions();
  if (model.functions.size() != originals.size())
    Bad("Model export does not cover the original signature");
  for (const auto& proxy : model.proxies) {
    if (!owner_.Owns(proxy.GetValue()))
      Bad("Model export contains a foreign proxy");
    const auto node = proxy.GetTerm().Borrow();
    if (node.IsNull()) Bad("Model export contains a null proxy");
    const auto decl = node.GetDeclaration()->GetDeclaration();
    if (!export_proxies_.emplace(decl.GetId(), proxy.GetValue()).second)
      Bad("Model export contains a repeated proxy declaration");
  }
  for (const auto& entry : model.functions) {
    const auto decl = entry.declaration.Borrow();
    if (decl.IsNull() || !state_.query->HasFunction(decl) ||
        written_.contains(decl.GetId()))
      Bad("Model export repeats or introduces a user symbol");
    auto text =
        Function(entry, originals[signature_.function_index.at(decl.GetId())]);
    text.insert(0, written_.empty() ? "(" : " ");
    written_.insert(decl.GetId());
    response.Write(text, where_);
  }
  Finish(response, where_, written_.empty() ? "()\n" : ")\n");
}
void ModelPrinter::Finish(Response& response, SourceSpan where,
                          std::string_view closing) {
  if (!staged_.empty()) {
    const auto total = state_.exposed.size() + staged_.size();
    if (total > state_.exposed.capacity())
      state_.exposed.reserve(
          total + std::min(total, state_.exposed.max_size() - total));
    state_.keys.reserve(total);
  }
  response.Write(closing, where_);
  response.Finish(where);
  CommitExposures();
}
void ModelPrinter::CommitExposures() noexcept {
  // The staged maps already own their nodes, including the ordered proxy map.
  // Both node transfers and the moves into reserved vector storage are safe.
  static_assert(std::is_nothrow_move_constructible_v<ExposedValue>);
  state_.keys.merge(staged_keys_);
  state_.proxies.merge(staged_proxies_);
  for (auto& entry : staged_) state_.exposed.push_back(std::move(entry));
}
void WriteValues(ModelOwner& model, SessionState& session,
                 std::span<const ValueRequest> values, Response& response,
                 SourceSpan where) {
  ModelPrinter printer(model, session, where);
  printer.Values(values, response);
}
void WriteModel(ModelOwner& model, SessionState& session, Response& response,
                SourceSpan where) {
  ModelPrinter printer(model, session, where);
  printer.Model(response);
}
}  // namespace dandelion::frontend::detail
