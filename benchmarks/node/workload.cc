module dandelion.node_workload;
import std;
import dandelion.node;

namespace dandelion::node_bench {
namespace {

using WordSpan = std::span<const std::string>;

std::runtime_error AtLine(std::size_t line, const std::runtime_error& error) {
  return std::runtime_error("Line " + std::to_string(line) + ": " +
                            error.what());
}

std::runtime_error Invalid(std::string_view message) {
  return std::runtime_error("Invalid Node workload: " + std::string(message));
}

template <class Value>
Value Get(Result<Value> result, std::string_view operation) {
  if (!result) {
    const auto error = result.error();
    if (error.code == ErrorCode::kResourceLimit) {
      throw std::runtime_error("Node resource limit during " +
                               std::string(operation));
    }
    throw Invalid(std::string(operation) + " failed with Node error " +
                  std::to_string(static_cast<unsigned>(error.code)));
  }
  if constexpr (!std::is_void_v<Value>) return std::move(*result);
}

std::size_t Id(std::string_view text) {
  std::size_t id = 0;
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, id);
  if (result.ptr != end || result.ec != std::errc{}) {
    throw Invalid("expected an unsigned ID or count");
  }
  return id;
}

void Require(bool valid, std::string_view message) {
  if (!valid) throw Invalid(message);
}

template <class Map>
const auto& Lookup(const Map& map, std::size_t id) {
  const auto found = map.find(id);
  Require(found != map.end(), "undefined metadata ID " + std::to_string(id));
  return found->second;
}

template <class Map, class Value>
void Insert(Map& map, std::size_t id, Value value) {
  Require(id > 0, "zero metadata ID");
  Require(map.emplace(id, std::move(value)).second, "duplicate metadata ID");
}

void Split(std::string_view line, std::vector<std::string_view>& words) {
  constexpr std::string_view kSpace = " \t\r\n\f\v";
  words.clear();
  while (true) {
    const auto begin = line.find_first_not_of(kSpace);
    if (begin == std::string_view::npos) return;
    const auto end = line.find_first_of(kSpace, begin);
    words.push_back(line.substr(begin, end - begin));
    if (end == std::string_view::npos) return;
    line.remove_prefix(end);
  }
}

template <class T>
std::size_t VectorBytes(const std::vector<T>& values) noexcept {
  return values.capacity() * sizeof(T);
}

// The file uses the spelling of public Kind names, independent of enum values.
constexpr std::string_view kKindNames[] = {
    "kInvalid",       "kBoolValue",
    "kIntValue",      "kBvValue",
    "kFpValue",       "kRoundingModeValue",
    "kApply",         "kNot",
    "kAnd",           "kOr",
    "kXor",           "kImplies",
    "kEqual",         "kDistinct",
    "kIte",           "kIntNeg",
    "kIntAdd",        "kIntSub",
    "kIntMul",        "kIntDiv",
    "kIntMod",        "kIntAbs",
    "kIntPow",        "kIntLt",
    "kIntLe",         "kIntGt",
    "kIntGe",         "kIntDivisible",
    "kBvNot",         "kBvNeg",
    "kBvAnd",         "kBvOr",
    "kBvXor",         "kBvAdd",
    "kBvMul",         "kBvNand",
    "kBvNor",         "kBvXnor",
    "kBvSub",         "kBvUdiv",
    "kBvUrem",        "kBvSdiv",
    "kBvSrem",        "kBvSmod",
    "kBvShl",         "kBvLshr",
    "kBvAshr",        "kBvComp",
    "kBvUlt",         "kBvUle",
    "kBvUgt",         "kBvUge",
    "kBvSlt",         "kBvSle",
    "kBvSgt",         "kBvSge",
    "kBvNego",        "kBvUaddo",
    "kBvSaddo",       "kBvUmulo",
    "kBvSmulo",       "kBvUsubo",
    "kBvSsubo",       "kBvSdivo",
    "kBvConcat",      "kBvExtract",
    "kBvRepeat",      "kBvZeroExtend",
    "kBvSignExtend",  "kBvRotateLeft",
    "kBvRotateRight", "kUbvToInt",
    "kSbvToInt",      "kIntToBv",
    "kFpConstruct",   "kFpAbs",
    "kFpNeg",         "kFpAdd",
    "kFpSub",         "kFpMul",
    "kFpDiv",         "kFpFma",
    "kFpSqrt",        "kFpRoundToIntegral",
    "kFpRem",         "kFpMin",
    "kFpMax",         "kFpEq",
    "kFpLt",          "kFpLeq",
    "kFpGt",          "kFpGeq",
    "kFpIsNormal",    "kFpIsSubnormal",
    "kFpIsZero",      "kFpIsInfinite",
    "kFpIsNaN",       "kFpIsNegative",
    "kFpIsPositive",  "kFpFromBits",
    "kFpConvert",     "kFpFromSbv",
    "kFpFromUbv",     "kFpFromRational",
    "kFpToSbv",       "kFpToUbv",
    "kArraySelect",   "kArrayStore",
    "kArrayConst"};
static_assert(std::size(kKindNames) ==
              static_cast<std::size_t>(Kind::kArrayConst) + 1);

Kind ParseKind(std::string_view name) {
  static const auto kKinds = [] {
    std::unordered_map<std::string_view, Kind> kinds;
    for (std::size_t index = 1; index < std::size(kKindNames); ++index) {
      kinds.emplace(kKindNames[index], static_cast<Kind>(index));
    }
    return kinds;
  }();
  const auto found = kKinds.find(name);
  Require(found != kKinds.end(), "unsupported kind " + std::string(name));
  return found->second;
}

void ParsePayload(Instruction& instruction,
                  std::span<const std::string_view> words,
                  const std::unordered_map<std::size_t, std::size_t>& instances,
                  std::vector<Integer>& payloads,
                  std::vector<Rational>& rationals, std::size_t children) {
  using enum Kind;
  const Kind kind = instruction.kind;
  if (kind == kApply) {
    Require(words.size() == 1, "application requires a declaration instance");
    const auto id = Id(words.front());
    const auto instance = instances.find(id);
    if (instance == instances.end())
      throw Invalid("undefined declaration instance ID " + std::to_string(id));
    instruction.auxiliary = instance->second;
    return;
  }
  if (kind >= kBoolValue && kind <= kRoundingModeValue) {
    Require(children == 0 && words.size() == 1, "invalid literal shape");
    if (kind == kFpValue && words[0] == "nan") return;
    if (kind == kBoolValue) {
      Require(words[0] == "0" || words[0] == "1", "invalid Bool payload");
    } else if (kind == kRoundingModeValue) {
      Require(Id(words[0]) <= 4, "invalid rounding mode");
    }
  }
  const auto start = payloads.size();
  Require(words.size() <= std::numeric_limits<std::uint8_t>::max(),
          "too many payload fields");
  for (const auto& word : words) {
    payloads.push_back(Get(Integer::ParseDecimal(word), "integer"));
  }
  if (kind == kFpFromRational) {
    Require(words.size() == 4, "expected FP rational payload");
    instruction.auxiliary = rationals.size();
    rationals.push_back(Get(Rational::Make(payloads[start + 2].Borrow(),
                                           payloads[start + 3].Borrow()),
                            "rational parameter"));
    payloads.resize(start + 2);
  }
}

bool IsMetadata(std::string_view op) {
  return op == "sort" || op == "symbol" || op == "group" || op == "recipe" ||
         op == "ctor" || op == "commit" || op == "decl" || op == "inst";
}

class Metadata {
 public:
  Metadata(NodeManager& manager, std::vector<SortView>& sorts,
           std::vector<DeclInstanceView>& instances)
      : manager_(manager), replay_sorts_(sorts), instances_(instances) {}

  void Add(WordSpan words) {
    const auto& op = words[0];
    const auto fields = words.subspan(1);
    const bool group_record = op == "recipe" || op == "ctor" || op == "commit";
    Require(group_record == builder_.has_value(),
            "invalid record within datatype group");
    if (op == "sort") {
      Require(fields.size() >= 2, "missing sort fields");
      const auto sort = MakeSort(fields.subspan(1));
      const auto view = sort.Borrow();
      Insert(sorts_, Id(fields[0]), view);
      replay_sorts_.push_back(view);
    } else if (op == "symbol") {
      Require(fields.size() == 2, "invalid sort constructor");
      const auto id = Id(fields[0]);
      Insert(constructors_, id,
             Get(manager_.DeclareUninterpretedSort(Name(id), Id(fields[1])),
                 "sort constructor"));
    } else if (op == "group") {
      BeginGroup(fields);
    } else if (op == "recipe") {
      Require(fields.size() >= 3, "missing recipe fields");
      Insert(recipes_, Id(fields[0]), MakeRecipe(fields.subspan(1)));
    } else if (op == "ctor") {
      AddConstructor(fields);
    } else if (op == "commit") {
      Require(fields.empty(), "unexpected commit fields");
      Commit();
    } else if (op == "decl") {
      Require(fields.size() >= 3, "missing declaration fields");
      const auto id = Id(fields[0]);
      const auto range = SortAt(fields[2]);
      const auto domain = Sorts(fields.subspan(3));
      Require(fields[1] == "function" || fields[1] == "internal",
              "invalid declaration kind");
      auto decl = fields[1] == "function"
                      ? manager_.DeclareFunction(Name(id), domain, range)
                      : manager_.FreshFunction(Name(id), domain, range);
      const auto& owner = Get(std::move(decl), "declaration");
      Insert(declarations_, id, owner.Borrow());
    } else if (op == "inst") {
      Require(fields.size() >= 2, "missing declaration instance fields");
      const auto instance = Get(
          manager_.InstantiateDeclaration(Lookup(declarations_, Id(fields[1])),
                                          Sorts(fields.subspan(2))),
          "declaration instance");
      instances_.push_back(instance.Borrow());
    }
  }

  void Finish() const { Require(!builder_, "unterminated datatype group"); }

 private:
  static std::string Name(std::size_t id) {
    return "reference-" + std::to_string(id);
  }

  SortView SortAt(std::string_view word) const {
    return Lookup(sorts_, Id(word));
  }

  std::vector<SortView> Sorts(WordSpan fields) const {
    std::vector<SortView> result;
    for (const auto& field : fields) result.push_back(SortAt(field));
    return result;
  }

  Sort MakeSort(WordSpan fields) {
    const auto& kind = fields[0];
    const auto args = fields.subspan(1);
    if (kind == "bool" && args.empty()) return Get(manager_.BoolSort(), kind);
    if (kind == "int" && args.empty()) return Get(manager_.IntSort(), kind);
    if (kind == "rm" && args.empty()) {
      return Get(manager_.RoundingModeSort(), kind);
    }
    if (kind == "bv" && args.size() == 1) {
      const auto width = Get(Integer::ParseDecimal(args[0]), "BV width");
      return Get(manager_.MakeBvSort(width.Borrow()), kind);
    }
    if (kind == "fp" && args.size() == 2) {
      const auto exponent = Get(Integer::ParseDecimal(args[0]), "FP exponent");
      const auto significand =
          Get(Integer::ParseDecimal(args[1]), "FP significand");
      return Get(manager_.MakeFpSort(exponent.Borrow(), significand.Borrow()),
                 kind);
    }
    if (kind == "array" && args.size() == 2) {
      return Get(manager_.MakeArraySort(SortAt(args[0]), SortAt(args[1])),
                 kind);
    }
    if (kind == "apply" && !args.empty()) {
      const auto constructor = Lookup(constructors_, Id(args[0]));
      return Get(manager_.InstantiateSort(constructor.Borrow(),
                                          Sorts(args.subspan(1))),
                 kind);
    }
    throw Invalid("unknown sort or invalid sort arity " + kind);
  }

  void BeginGroup(WordSpan fields) {
    Require(!fields.empty() && fields.size() % 2 == 0,
            "invalid datatype headers");
    std::vector<std::string> names;
    std::vector<DatatypeHeader> headers;
    names.reserve(fields.size() / 2);
    for (std::size_t index = 0; index < fields.size(); index += 2) {
      const auto id = Id(fields[index]);
      group_sorts_.push_back(id);
      names.push_back(Name(id));
      headers.push_back({names.back(), Id(fields[index + 1])});
    }
    builder_.emplace(
        Get(manager_.BeginDatatypeGroup(headers), "datatype group"));
  }

  SortExpr MakeRecipe(WordSpan fields) {
    const auto& kind = fields[0];
    const auto args = fields.subspan(1);
    const auto recipe = [this](const std::string& id) {
      return Lookup(recipes_, Id(id));
    };
    if (kind == "concrete" && args.size() == 1) {
      return Get(builder_->Concrete(SortAt(args[0])), kind);
    }
    if (kind == "param" && args.size() == 2) {
      return Get(builder_->Parameter(Id(args[0]), Id(args[1])), kind);
    }
    if (kind == "array" && args.size() == 2) {
      return Get(builder_->Array(recipe(args[0]), recipe(args[1])), kind);
    }
    if ((kind == "apply" || kind == "recursive") && !args.empty()) {
      std::vector<SortExpr> recipes;
      for (const auto& arg : args.subspan(1)) recipes.push_back(recipe(arg));
      if (kind == "recursive") {
        return Get(builder_->Recursive(Id(args[0]), recipes), kind);
      }
      return Get(
          builder_->Apply(Lookup(constructors_, Id(args[0])).Borrow(), recipes),
          kind);
    }
    throw Invalid("unknown or invalid datatype recipe " + kind);
  }

  void AddConstructor(WordSpan fields) {
    Require(fields.size() >= 3 && (fields.size() - 3) % 2 == 0,
            "invalid datatype constructor fields");
    Constructor constructor{Id(fields[0]), Id(fields[1]), Id(fields[2]), {}};
    std::vector<DatatypeFieldSpec> specs;
    std::vector<std::string> names;
    names.reserve(fields.size() / 2);
    for (std::size_t index = 3; index < fields.size(); index += 2) {
      const auto id = Id(fields[index]);
      constructor.selectors.push_back(id);
      names.push_back(Name(id));
      specs.push_back({names.back(), Lookup(recipes_, Id(fields[index + 1]))});
    }
    Get(builder_->AddConstructor(Id(fields[0]), Name(constructor.id), specs),
        "datatype constructor");
    group_decls_.push_back(std::move(constructor));
  }

  void Commit() {
    const auto group = Get(builder_->Commit(), "datatype commit");
    const auto view = group.Borrow();
    std::vector<std::size_t> ordinals(group_sorts_.size());
    for (std::size_t slot = 0; slot < group_sorts_.size(); ++slot) {
      const auto constructor = Get(view.GetSortConstructor(slot), "datatype");
      Insert(constructors_, group_sorts_[slot],
             Get(manager_.RetainSortConstructor(constructor), "datatype sort"));
    }
    for (const auto& ctor : group_decls_) {
      const auto index = ordinals.at(ctor.slot)++;
      Insert(declarations_, ctor.id,
             Get(view.GetConstructor(ctor.slot, index), "constructor"));
      Insert(declarations_, ctor.tester,
             Get(view.GetTester(ctor.slot, index), "tester"));
      for (std::size_t field = 0; field < ctor.selectors.size(); ++field) {
        Insert(declarations_, ctor.selectors[field],
               Get(view.GetSelector(ctor.slot, index, field), "selector"));
      }
    }
    builder_.reset();
    group_sorts_.clear();
    group_decls_.clear();
    recipes_.clear();
  }

  struct Constructor {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t slot;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t id;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t tester;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::vector<std::size_t> selectors;
  };

  NodeManager& manager_;
  std::vector<SortView>& replay_sorts_;
  std::vector<DeclInstanceView>& instances_;
  std::unordered_map<std::size_t, SortView> sorts_;
  std::unordered_map<std::size_t, SortConstructor> constructors_;
  // Sort and declaration records live as long as the manager.
  std::unordered_map<std::size_t, DeclView> declarations_;
  std::optional<DatatypeBuilder> builder_;
  std::unordered_map<std::size_t, SortExpr> recipes_;
  std::vector<std::size_t> group_sorts_;
  std::vector<Constructor> group_decls_;
};

}  // namespace

Workload Workload::Read(std::istream& input) {
  std::string line;
  std::vector<std::string_view> words;
  std::getline(input, line);
  Workload workload;
  Split(line, words);
  Require(
      words.size() == 3 && words[0] == "dandelion-node" && words[1] == "1" &&
          (words[2] == "graph" || words[2] == "mixed" || words[2] == "lookup"),
      "missing or unsupported workload header");
  workload.graph_ = words[2] == "graph";
  const bool lookup = words[2] == "lookup";
  std::unordered_map<std::size_t, std::size_t> sorts;
  std::unordered_map<std::size_t, std::size_t> instances;
  std::vector<bool> live;
  std::size_t live_count = 0;
  bool ended = false;
  std::size_t line_number = 1;
  const auto use = [&live](std::string_view word) {
    const auto slot = Id(word);
    Require(slot < live.size() && live[slot], "unowned operand slot");
    return slot;
  };
  const auto allocate = [&](std::string_view word) {
    const auto slot = Id(word);
    Require(slot <= live.size(), "slots must be dense");
    if (slot == live.size()) live.push_back(false);
    Require(!live[slot], "destination slot already owns a node");
    live[slot] = true;
    workload.peak_slots_ = std::max(workload.peak_slots_, ++live_count);
    return slot;
  };
  try {
    while (std::getline(input, line)) {
      ++line_number;
      Split(line, words);
      if (words.empty()) continue;
      const auto& op = words.front();
      Require(!ended, "trailing records after end");
      if (op == "end") {
        Require(words.size() == 1, "unexpected fields after end");
        ended = true;
        continue;
      }
      Require(workload.roots_.empty(), "record after root list");
      Require(!workload.IsLookup() || op == "lookup",
              "non-lookup record after lookup sequence begins");
      if (IsMetadata(op)) {
        Require(workload.instructions_.empty(), "metadata after replay start");
        if (op == "sort" || op == "inst") {
          Require(words.size() >= 3, "missing metadata fields");
          auto& ids = op == "sort" ? sorts : instances;
          const auto id = Id(words[1]);
          Require(id && ids.emplace(id, ids.size()).second,
                  "duplicate or zero metadata ID");
        }
        for (const auto word : words) workload.metadata_.emplace_back(word);
        workload.metadata_ends_.push_back(workload.metadata_.size());
      } else if (op == "make") {
        Require(words.size() >= 5, "missing make fields");
        const auto children = Id(words[4]);
        Require(children <= words.size() - 5, "missing child slots");
        Instruction instruction{};
        instruction.action = Action::kMake;
        instruction.line = line_number;
        instruction.source = Lookup(sorts, Id(words[2]));
        instruction.kind = ParseKind(words[3]);
        instruction.child_begin = workload.operands_.size();
        instruction.child_count = children;
        const std::span<const std::string_view> fields(words);
        for (const auto child : fields.subspan(5, children)) {
          workload.operands_.push_back(use(child));
        }
        instruction.slot = allocate(words[1]);
        Require(!lookup || instruction.slot == workload.instructions_.size(),
                "lookup seed slots must be consecutive");
        instruction.payload_begin = workload.payloads_.size();
        ParsePayload(instruction, fields.subspan(5 + children), instances,
                     workload.payloads_, workload.rationals_, children);
        instruction.payload_count = static_cast<std::uint8_t>(
            workload.payloads_.size() - instruction.payload_begin);
        workload.max_children_ = std::max(workload.max_children_, children);
        workload.instructions_.push_back(instruction);
        ++workload.requests_;
      } else if (op == "lookup") {
        Require(lookup && words.size() > 1, "invalid lookup record");
        for (const auto word : std::span(words).subspan(1)) {
          workload.AppendLookup(use(word));
        }
      } else if (op == "hold") {
        Require(!lookup && words.size() == 3, "invalid hold record");
        const auto source = use(words[2]);
        Instruction instruction{};
        instruction.action = Action::kHold;
        instruction.slot = allocate(words[1]);
        instruction.source = source;
        workload.instructions_.push_back(instruction);
      } else if (op == "drop") {
        Require(!lookup && words.size() == 2, "invalid drop record");
        const auto slot = use(words[1]);
        live[slot] = false;
        --live_count;
        Instruction instruction{};
        instruction.action = Action::kDrop;
        instruction.slot = slot;
        workload.instructions_.push_back(instruction);
      } else if (op == "roots") {
        Require(workload.graph_ && words.size() > 1, "invalid root list");
        std::unordered_set<std::size_t> root_slots;
        for (const auto word : std::span(words).subspan(1)) {
          const auto slot = use(word);
          Require(root_slots.insert(slot).second, "duplicate root slot");
          workload.roots_.push_back(slot);
        }
      } else {
        throw Invalid("unknown record " + std::string(op));
      }
    }
  } catch (const std::runtime_error& error) {
    throw AtLine(line_number, error);
  }
  Require(input.eof() && ended, "incomplete workload");
  Require(workload.requests_ > 0, "empty workload");
  Require(!workload.graph_ || !workload.roots_.empty(), "empty graph roots");
  if (lookup) {
    Require(workload.IsLookup(), "empty lookup sequence");
  } else {
    Require(live_count == workload.roots_.size(),
            "live temporary slots at end");
  }
  workload.slots_ = live.size();
  workload.sort_count_ = sorts.size();
  workload.instance_count_ = instances.size();
  return workload;
}

void Workload::AppendLookup(std::size_t slot) {
  if (auto* narrow = std::get_if<0>(&lookups_)) {
    if (slot <= std::numeric_limits<std::uint32_t>::max()) {
      narrow->push_back(static_cast<std::uint32_t>(slot));
      return;
    }
    std::vector<std::size_t> wide(narrow->begin(), narrow->end());
    lookups_.emplace<1>(std::move(wide));
  }
  std::get<1>(lookups_).push_back(slot);
}

std::size_t Workload::LookupCount() const noexcept {
  return std::visit([](const auto& indices) { return indices.size(); },
                    lookups_);
}

std::size_t Workload::StorageBytes() const noexcept {
  std::size_t bytes =
      sizeof(Workload) + VectorBytes(metadata_) + VectorBytes(metadata_ends_) +
      VectorBytes(instructions_) + VectorBytes(operands_) +
      VectorBytes(payloads_) + VectorBytes(rationals_) + VectorBytes(roots_) +
      std::visit([](const auto& indices) { return VectorBytes(indices); },
                 lookups_);
  for (const auto& word : metadata_) {
    const auto offset = reinterpret_cast<std::uintptr_t>(word.data()) -
                        reinterpret_cast<std::uintptr_t>(&word);
    if (offset >= sizeof(word)) bytes += word.capacity() + 1;
  }
  for (const auto& value : payloads_) bytes += value.Borrow().BackingBytes();
  for (const auto& value : rationals_) {
    const auto rational = value.Borrow();
    bytes += rational.Numerator().BackingBytes() +
             rational.Denominator().BackingBytes();
  }
  return bytes;
}

Workload Workload::Load(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read workload " + path.string());
  return Read(input);
}

Replay::Replay(const Workload& workload, bool verify_results)
    : workload_(workload),
      verify_results_(verify_results),
      manager_(Get(NodeManager::Create(), "manager creation")),
      slots_(workload.slots_) {
  children_.reserve(workload.max_children_);
  PrepareMetadata();
}

void Replay::PrepareMetadata() {
  sorts_.reserve(workload_.sort_count_);
  instances_.reserve(workload_.instance_count_);
  Metadata metadata(manager_, sorts_, instances_);
  const WordSpan words(workload_.metadata_);
  std::size_t start = 0;
  for (const auto end : workload_.metadata_ends_) {
    metadata.Add(words.subspan(start, end - start));
    start = end;
  }
  metadata.Finish();
}

std::size_t Replay::StorageBytes() const noexcept {
  return sizeof(Replay) + VectorBytes(slots_) + VectorBytes(children_) +
         VectorBytes(sorts_) + VectorBytes(instances_);
}

Node Replay::Make(const Instruction& instruction) {
  using enum Kind;
  const auto kind = instruction.kind;
  const auto literal = [this, &instruction] {
    return workload_.payloads_[instruction.payload_begin].Borrow();
  };
  const auto sort = [this, &instruction] { return sorts_[instruction.source]; };
  if (kind == kBoolValue)
    return Get(manager_.MakeBool(literal().Sign() != 0), "Bool");
  if (kind == kIntValue) return Get(manager_.MakeInt(literal()), "Int");
  if (kind == kBvValue)
    return Get(manager_.MakeBvValue(sort(), literal()), "BV");
  if (kind == kFpValue) {
    return Get(instruction.payload_count == 0
                   ? manager_.MakeFpSpecial(sort(), FpSpecial::kNaN)
                   : manager_.MakeFpBits(sort(), literal()),
               "FP");
  }
  if (kind == kRoundingModeValue) {
    return Get(manager_.MakeRoundingMode(
                   static_cast<RoundingMode>(*literal().TryUint32())),
               "rounding mode");
  }
  children_.clear();
  for (const auto child :
       std::span(workload_.operands_)
           .subspan(instruction.child_begin, instruction.child_count)) {
    children_.push_back(slots_[child].Borrow());
  }
  if (kind == kApply) {
    return Get(manager_.MakeApply(instances_[instruction.auxiliary], children_),
               "application");
  }
  const std::optional<SortView> qualifier =
      kind == kArrayConst ? std::optional(sort()) : std::nullopt;
  const std::optional<RationalView> rational =
      kind == kFpFromRational
          ? std::optional(workload_.rationals_[instruction.auxiliary].Borrow())
          : std::nullopt;
  const auto payload =
      std::span(workload_.payloads_)
          .subspan(instruction.payload_begin, instruction.payload_count);
  return Get(
      manager_.MakeBuiltin({kind, payload, qualifier, rational}, children_),
      kKindNames[static_cast<std::size_t>(kind)]);
}

bool Replay::Step() {
  if (next_ == workload_.instructions_.size()) return false;
  const auto& instruction = workload_.instructions_[next_++];
  auto& destination = slots_[instruction.slot];
  switch (instruction.action) {
    case Action::kMake:
      try {
        destination = Make(instruction);
        if (verify_results_) {
          Require(destination.Borrow().GetSort() == sorts_[instruction.source],
                  "result sort differs from capture");
        }
      } catch (const std::runtime_error& error) {
        throw AtLine(instruction.line, error);
      }
      break;
    case Action::kHold:
      destination = slots_[instruction.source];
      break;
    case Action::kDrop:
      destination = {};
      break;
  }
  return true;
}

void Replay::Run() {
  while (Step()) {
  }
}

Node Replay::Lookup(std::size_t request) {
  return std::visit(
      [&](const auto& slots) { return LookupSlot(slots.at(request)); },
      workload_.lookups_);
}

Node Replay::LookupSlot(std::size_t slot) {
  auto node = Make(workload_.instructions_[slot]);
  if (verify_results_) {
    Require(node.GetId() == slots_[slot].GetId(),
            "lookup identity differs from the seed");
  }
  return node;
}

void Replay::RunLookups() {
  std::visit(
      [&](const auto& slots) {
        for (const auto slot : slots) LookupSlot(slot);
      },
      workload_.lookups_);
}

std::vector<Node> Replay::TakeRoots() {
  std::vector<Node> roots;
  roots.reserve(workload_.roots_.size());
  for (const auto slot : workload_.roots_) {
    roots.push_back(std::move(slots_[slot]));
  }
  return roots;
}

void Replay::Reset() {
  for (const auto slot : workload_.roots_) slots_[slot] = {};
  next_ = 0;
}

NodeView Replay::GetNode(std::size_t slot) const {
  return slots_.at(slot).Borrow();
}

WalkStats Walk(std::span<const Node> roots, bool describe_shape) {
  WalkStats stats;
  stats.roots_ = roots.size();
  std::unordered_map<NodeId, std::size_t> depths;
  Get(VisitDagPostorder(roots,
                        [&](NodeView node) -> Result<void> {
                          ++stats.nodes_;
                          const auto children = node.GetChildren();
                          stats.edges_ += children.size();
                          if (describe_shape) {
                            std::size_t depth = 1;
                            for (const auto child : children) {
                              depth =
                                  std::max(depth, 1 + depths.at(child.GetId()));
                            }
                            depths.emplace(node.GetId(), depth);
                            stats.depth_ = std::max(stats.depth_, depth);
                            stats.max_fanout_ =
                                std::max(stats.max_fanout_, children.size());
                          }
                          return {};
                        }),
      "postorder");
  return stats;
}

}  // namespace dandelion::node_bench
