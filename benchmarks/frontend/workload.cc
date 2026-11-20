module;

#include <cerrno>

module dandelion.frontend_workload;
import dandelion.frontend;
import std;

namespace dandelion::frontend_bench {
namespace {

using frontend::DiagnosticCode;
using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedNanoseconds(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
          .count());
}

template <class Operation>
auto Measure(bool enabled, std::uint64_t& elapsed_ns, Operation operation) {
  if (!enabled) return operation();
  const auto start = Clock::now();
  auto result = operation();
  elapsed_ns += ElapsedNanoseconds(start);
  return result;
}

class MemoryReader {
 public:
  MemoryReader(std::string_view input, ReaderMode mode)
      : remaining_(input), mode_(mode) {}

  std::size_t Read(std::span<char> destination) {
    ++calls_;
    const auto size =
        std::min({NextChunkSize(), destination.size(), remaining_.size()});
    if (size != 0) std::memcpy(destination.data(), remaining_.data(), size);
    remaining_.remove_prefix(size);
    return size;
  }

  std::uint64_t GetCalls() const { return calls_; }

 private:
  std::size_t NextChunkSize() {
    switch (mode_) {
      case ReaderMode::kMemory64K:
        return 65536;
      case ReaderMode::kMemory4K:
        return 4096;
      case ReaderMode::kMemoryIrregular: {
        constexpr std::array<std::size_t, 7> kSizes{1,    7,  31,  257,
                                                    4093, 17, 8191};
        const auto size = kSizes[chunk_index_];
        chunk_index_ = (chunk_index_ + 1) % kSizes.size();
        return size;
      }
    }
    std::unreachable();
  }

  std::string_view remaining_;
  ReaderMode mode_;
  std::size_t chunk_index_ = 0;
  std::uint64_t calls_ = 0;
};

std::string_view DiagnosticStatus(DiagnosticCode code) {
  switch (code) {
    case DiagnosticCode::kSyntax:
      return "syntax";
    case DiagnosticCode::kInvalidInput:
    case DiagnosticCode::kInvalidState:
      return "invalid_input";
    case DiagnosticCode::kUnsupported:
      return "unsupported";
    case DiagnosticCode::kModelDependent:
      return "model_dependent";
    case DiagnosticCode::kInputIo:
      return "input_io";
    case DiagnosticCode::kResource:
      return "resource";
    case DiagnosticCode::kOutputIo:
    case DiagnosticCode::kEngineFailure:
    case DiagnosticCode::kModelUnavailable:
    case DiagnosticCode::kModelFailure:
      return "measurement_error";
  }
  std::unreachable();
}

void RecordDiagnostic(ParsedQuery& parsed, const frontend::Diagnostic& error) {
  parsed.status = DiagnosticStatus(error.code);
  parsed.diagnostic_message = error.message;
  parsed.diagnostic_code = error.code;
  parsed.diagnostic_span = error.where;
}

ParsedQuery Parse(frontend::Outcome<frontend::Session> session,
                  std::size_t bytes, bool measure_phases) {
  ParsedQuery parsed;
  parsed.stats.bytes = bytes;
  if (!session) {
    RecordDiagnostic(parsed, session.error());
    return parsed;
  }
  auto& active = parsed.session.emplace(std::move(*session));
  for (;;) {
    auto next = Measure(measure_phases, parsed.stats.read_command_ns,
                        [&] { return active.ReadCommand(); });
    if (!next) {
      RecordDiagnostic(parsed, next.error());
      break;
    }
    if (!*next) break;

    auto& command = *next;
    const auto& [kind, where] = command->header;
    ++parsed.stats.commands;
    parsed.stats.committed_end_offset = where.end.offset;
    parsed.stats.max_command_bytes = std::max(
        parsed.stats.max_command_bytes, where.end.offset - where.begin.offset);
    if (kind == frontend::CommandKind::kDefineFun ||
        kind == frontend::CommandKind::kDefineConst)
      ++parsed.stats.definitions;
    if (kind == frontend::CommandKind::kCheckSat)
      parsed.query = std::move(command->query);
    if (kind == frontend::CommandKind::kExit) break;
    const auto* response =
        std::get_if<frontend::CommandResponse>(&command->response);
    if (kind == frontend::CommandKind::kGetModel ||
        kind == frontend::CommandKind::kGetValue ||
        (response && *response == frontend::CommandResponse::kUnknownReason)) {
      RecordDiagnostic(parsed, {DiagnosticCode::kModelDependent, where,
                                "Command requires a model or check result"});
      break;
    }
  }
  const auto& prefix = active.GetPrefix();
  parsed.stats.assertions = prefix.GetAssertions().size();
  if (parsed.status == "ok" && !parsed.query) {
    parsed.status = "invalid_input";
    parsed.diagnostic_message = "Missing check-sat command";
  }
  return parsed;
}

std::error_code FileError() {
  return errno != 0 ? std::error_code(errno, std::generic_category())
                    : std::make_error_code(std::errc::io_error);
}

#ifdef __linux__
std::optional<std::uint64_t> ParseMemoryKiB(std::string_view field) {
  const auto start = field.find_first_not_of(" \t");
  if (start == std::string_view::npos) return std::nullopt;
  std::uint64_t kib = 0;
  const auto [end, error] =
      std::from_chars(field.data() + start, field.data() + field.size(), kib);
  if (error != std::errc{}) return std::nullopt;
  const std::string_view unit(end, field.data() + field.size());
  const auto first = unit.find_first_not_of(" \t");
  if (first == 0 || first == std::string_view::npos ||
      !unit.substr(first).starts_with("kB") ||
      unit.find_first_not_of(" \t", first + 2) != std::string_view::npos)
    return std::nullopt;
  return kib;
}
#endif

std::optional<double> InputRatio(std::optional<std::uint64_t> kib,
                                 std::uint64_t bytes) {
  if (!kib || bytes == 0) return std::nullopt;
  return static_cast<double>(*kib) * 1024.0 / static_cast<double>(bytes);
}

std::uint64_t CountTokens(std::string_view input) {
  std::uint64_t tokens = 0;
  std::size_t index = 0;
  // The frontend already accepted all lexical data in this prefix.
  while (index < input.size()) {
    switch (input[index]) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        ++index;
        continue;
      case ';':
        index = input.find_first_of("\r\n", index + 1);
        continue;
      case '(':
      case ')':
        ++index;
        break;
      case '|':
        index = input.find('|', index + 1) + 1;
        break;
      case '"':
        do {
          index = input.find('"', index + 1) + 1;
        } while (index < input.size() && input[index] == '"');
        break;
      default:
        index = input.find_first_of(" \t\r\n();|\"", index + 1);
        break;
    }
    ++tokens;
  }
  return tokens;
}

void CollectStorageStats(const frontend::QueryContext& query,
                         InspectReport& report) {
  auto manager = NodeManager::Attach(query.GetManager());
  if (!manager) throw std::runtime_error("Cannot attach query NodeManager");
  const auto stats = manager->GetStorageStats();
  if (!stats) throw std::runtime_error("Cannot measure NodeManager storage");
  report.live_nodes = stats->live_nodes;
  report.live_edges = stats->live_edges;
  report.node_bytes = stats->node_bytes;
  report.numeric_backing_bytes = stats->numeric_backing_bytes;
  report.metadata_bytes = stats->metadata_bytes;
  report.table_bytes = stats->table_bytes;
  report.peak_table_bytes = stats->peak_table_bytes;
}

void CollectGraphStats(const frontend::QueryContext& query,
                       const frontend::SourceRecords& records,
                       InspectReport& report) {
  std::vector<Node> roots;
  roots.reserve(report.assertions + report.definitions + report.named_terms +
                report.annotations);
  roots.insert(roots.end(), query.GetAssertions().begin(),
               query.GetAssertions().end());
  for (const auto& definition : records.definitions) {
    if (const auto ordinal = definition.definition_ordinal)
      roots.push_back(query.GetDefinition(*ordinal).body);
    else
      roots.push_back(definition.body);
  }
  for (const auto& named : records.named_terms) roots.push_back(named.term);
  for (const auto& annotation : records.annotations)
    roots.push_back(annotation.term);

  auto walker = DagWalker::Create(roots);
  if (!walker) throw std::runtime_error("Cannot create query DAG walker");
  std::unordered_map<NodeId, std::uint64_t> depths;
  std::uint64_t nodes = 0;
  std::uint64_t edges = 0;
  std::uint64_t depth = 0;
  std::uint64_t max_fanout = 0;
  for (;;) {
    const auto next = walker->Next();
    if (!next) throw std::runtime_error("Cannot walk query DAG");
    if (!*next) break;
    const auto& event = **next;
    switch (event.kind) {
      case DagEventKind::kNodeEnter:
        ++nodes;
        break;
      case DagEventKind::kEdgeEnter:
        ++edges;
        break;
      case DagEventKind::kNodeExit: {
        const auto children = event.node.GetChildren();
        max_fanout = std::max<std::uint64_t>(max_fanout, children.size());
        std::uint64_t node_depth = 1;
        for (const auto child : children)
          node_depth = std::max(node_depth, 1 + depths.at(child.GetId()));
        depths.emplace(event.node.GetId(), node_depth);
        break;
      }
      case DagEventKind::kRootExit:
        depth = std::max(depth, depths.at(event.node.GetId()));
        break;
      case DagEventKind::kRootEnter:
      case DagEventKind::kEdgeExit:
        break;
    }
  }
  report.roots = roots.size();
  report.nodes = nodes;
  report.edges = edges;
  report.depth = depth;
  report.max_fanout = max_fanout;
}

std::size_t Utf8SequenceSize(std::string_view value) {
  const auto first = static_cast<unsigned char>(value[0]);
  if (first < 0xc2 || first > 0xf4) return 0;
  const std::size_t size = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
  if (value.size() < size) return 0;
  const auto second = static_cast<unsigned char>(value[1]);
  if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
      (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
    return 0;
  for (const auto byte : value.substr(1, size - 1)) {
    const auto continuation = static_cast<unsigned char>(byte);
    if (continuation < 0x80 || continuation > 0xbf) return 0;
  }
  return size;
}

void WriteJsonValue(std::ostream& out, std::string_view value) {
  constexpr std::string_view kHex = "0123456789abcdef";
  out << '"';
  while (!value.empty()) {
    const auto byte = static_cast<unsigned char>(value.front());
    if (byte == '"' || byte == '\\') {
      out << '\\' << value.front();
    } else if (byte < 0x20) {
      out << "\\u00" << kHex[byte >> 4] << kHex[byte & 0xf];
    } else if (byte < 0x80) {
      out << value.front();
    } else if (const auto size = Utf8SequenceSize(value)) {
      out << value.substr(0, size);
      value.remove_prefix(size);
      continue;
    } else {
      out << "\\ufffd";
    }
    value.remove_prefix(1);
  }
  out << '"';
}

void WriteJsonValue(std::ostream& out, std::unsigned_integral auto value) {
  out << value;
}

void WriteJsonValue(std::ostream& out, double value) {
  if (std::isfinite(value))
    out << value;
  else
    out << "null";
}

std::string_view DiagnosticName(DiagnosticCode code) {
  switch (code) {
    case DiagnosticCode::kSyntax:
      return "kSyntax";
    case DiagnosticCode::kInvalidState:
      return "kInvalidState";
    case DiagnosticCode::kInvalidInput:
      return "kInvalidInput";
    case DiagnosticCode::kUnsupported:
      return "kUnsupported";
    case DiagnosticCode::kInputIo:
      return "kInputIo";
    case DiagnosticCode::kOutputIo:
      return "kOutputIo";
    case DiagnosticCode::kResource:
      return "kResource";
    case DiagnosticCode::kEngineFailure:
      return "kEngineFailure";
    case DiagnosticCode::kModelUnavailable:
      return "kModelUnavailable";
    case DiagnosticCode::kModelDependent:
      return "kModelDependent";
    case DiagnosticCode::kModelFailure:
      return "kModelFailure";
  }
  std::unreachable();
}

void WriteJsonValue(std::ostream& out, DiagnosticCode code) {
  WriteJsonValue(out, DiagnosticName(code));
}

void WriteJsonValue(std::ostream& out, const frontend::SourceSpan& span) {
  const auto position = [&](const frontend::SourcePosition& point) {
    out << "{\"offset\":" << point.offset << ",\"line\":" << point.line
        << ",\"column\":" << point.column << '}';
  };
  out << "{\"source\":" << span.source << ",\"begin\":";
  position(span.begin);
  out << ",\"end\":";
  position(span.end);
  out << '}';
}

template <class Value>
void WriteJsonValue(std::ostream& out, const std::optional<Value>& value) {
  if (value)
    WriteJsonValue(out, *value);
  else
    out << "null";
}

}  // namespace

std::expected<std::string, std::error_code> LoadFile(
    const std::filesystem::path& path) {
  try {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::unexpected(error);
    std::string input;
    if (size > input.max_size() ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max()))
      return std::unexpected(std::make_error_code(std::errc::file_too_large));

    errno = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(FileError());
    input.resize(static_cast<std::size_t>(size));
    errno = 0;
    if (!file.read(input.data(), static_cast<std::streamsize>(size)) ||
        file.peek() != std::char_traits<char>::eof() || file.bad())
      return std::unexpected(FileError());
    return input;
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
  } catch (const std::length_error&) {
    return std::unexpected(std::make_error_code(std::errc::file_too_large));
  }
}

ParsedQuery ParseFromText(std::string_view input, bool measure_phases,
                          bool canonicalize_commutative,
                          bool capture_source_records) {
  return Parse(frontend::Session::FromText(
                   input,
                   frontend::ParseOptions{
                       .canonicalize_commutative = canonicalize_commutative,
                       .capture_source_records = capture_source_records}),
               input.size(), measure_phases);
}

ParsedQuery ParseFromReader(std::string_view input, ReaderMode mode,
                            bool measure_phases, bool canonicalize_commutative,
                            bool capture_source_records) {
  try {
    const auto reader = std::make_shared<MemoryReader>(input, mode);
    auto session = frontend::Session::FromReader(
        [reader](std::span<char> destination) -> frontend::ReadResult {
          return reader->Read(destination);
        },
        frontend::ParseOptions{
            .canonicalize_commutative = canonicalize_commutative,
            .capture_source_records = capture_source_records});
    auto parsed = Parse(std::move(session), input.size(), measure_phases);
    parsed.stats.reader_calls = reader->GetCalls();
    return parsed;
  } catch (const std::bad_alloc&) {
    ParsedQuery parsed;
    parsed.stats.bytes = input.size();
    parsed.status = "resource";
    parsed.diagnostic_code = DiagnosticCode::kResource;
    parsed.diagnostic_message = "Out of memory";
    return parsed;
  }
}

void ParsedQuery::Teardown(bool measure_phase) {
  if (!session && !query) return;
  const auto start = measure_phase ? Clock::now() : Clock::time_point{};
  session.reset();
  query.reset();
  if (measure_phase) stats.teardown_ns = ElapsedNanoseconds(start);
}

MemorySnapshot ReadProcessMemory() {
  MemorySnapshot memory;
#ifdef __linux__
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    const std::string_view field(line);
    if (field.starts_with("VmRSS:"))
      memory.retained_rss_kib = ParseMemoryKiB(field.substr(6));
    else if (field.starts_with("VmHWM:"))
      memory.peak_rss_kib = ParseMemoryKiB(field.substr(6));
    if (memory.retained_rss_kib && memory.peak_rss_kib) break;
  }
#endif
  return memory;
}

InspectReport Inspect(std::string_view input, std::optional<ReaderMode> mode,
                      bool canonicalize_commutative) {
  InspectReport report;
  report.baseline_rss_kib = ReadProcessMemory().retained_rss_kib;
  auto parsed =
      mode ? ParseFromReader(input, *mode, true, canonicalize_commutative, true)
           : ParseFromText(input, true, canonicalize_commutative, true);
  const auto retained = ReadProcessMemory();
  report.peak_rss_kib = retained.peak_rss_kib;
  report.retained_rss_kib = retained.retained_rss_kib;

  static_cast<ParseStats&>(report) = parsed.stats;
  report.status = std::move(parsed.status);
  report.diagnostic_message = std::move(parsed.diagnostic_message);
  report.diagnostic_code = parsed.diagnostic_code;
  report.diagnostic_span = parsed.diagnostic_span;
  report.checks = parsed.query ? 1 : 0;
  report.tokens = CountTokens(input.substr(
      0, static_cast<std::size_t>(parsed.stats.committed_end_offset)));
  if (parsed.session) {
    const auto& query =
        parsed.query ? *parsed.query : parsed.session->GetPrefix();
    try {
      CollectStorageStats(query, report);
      report.logic = query.GetLogic();
      report.user_sorts = query.GetUserSorts().size();
      report.user_functions = query.GetFunctions().size();
      report.datatype_groups = query.GetDatatypeGroups().size();
      const auto& records = *parsed.session->GetSourceRecords();
      report.named_terms = records.named_terms.size();
      report.annotations = records.annotations.size();
      report.metadata = records.metadata.size();
      CollectGraphStats(query, records, report);
    } catch (const std::exception& error) {
      if (report.status == "ok") {
        report.status = "measurement_error";
        report.diagnostic_message = error.what();
      }
    }
  }

  parsed.Teardown(true);
  report.post_teardown_rss_kib = ReadProcessMemory().retained_rss_kib;
  report.teardown_ns = parsed.stats.teardown_ns;
  if (report.peak_rss_kib && report.retained_rss_kib) {
    const auto peak = *report.peak_rss_kib;
    const auto rss = *report.retained_rss_kib;
    report.transient_rss_kib = std::max(peak, rss) - rss;
  }
  report.peak_input_ratio = InputRatio(report.peak_rss_kib, report.bytes);
  report.retained_input_ratio =
      InputRatio(report.retained_rss_kib, report.bytes);
  return report;
}

std::string InspectToJson(const InspectReport& report) {
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << std::setprecision(std::numeric_limits<double>::max_digits10)
       << "{\"schema_version\":2";
  const auto field = [&](std::string_view key, const auto& value) {
    json << ",\"" << key << "\":";
    WriteJsonValue(json, value);
  };
  field("status", report.status);
  field("diagnostic_message", report.diagnostic_message);
  field("diagnostic_code", report.diagnostic_code);
  field("diagnostic_span", report.diagnostic_span);
  field("logic", report.logic);
  field("bytes", report.bytes);
  field("tokens", report.tokens);
  field("commands", report.commands);
  field("committed_end_offset", report.committed_end_offset);
  field("assertions", report.assertions);
  field("definitions", report.definitions);
  field("max_command_bytes", report.max_command_bytes);
  field("reader_calls", report.reader_calls);
  field("read_command_ns", report.read_command_ns);
  field("teardown_ns", report.teardown_ns);
  field("checks", report.checks);
  field("user_sorts", report.user_sorts);
  field("user_functions", report.user_functions);
  field("datatype_groups", report.datatype_groups);
  field("named_terms", report.named_terms);
  field("annotations", report.annotations);
  field("metadata", report.metadata);
  field("roots", report.roots);
  field("nodes", report.nodes);
  field("edges", report.edges);
  field("depth", report.depth);
  field("max_fanout", report.max_fanout);
  field("live_nodes", report.live_nodes);
  field("live_edges", report.live_edges);
  field("node_bytes", report.node_bytes);
  field("numeric_backing_bytes", report.numeric_backing_bytes);
  field("metadata_bytes", report.metadata_bytes);
  field("table_bytes", report.table_bytes);
  field("peak_table_bytes", report.peak_table_bytes);
  field("baseline_rss_kib", report.baseline_rss_kib);
  field("peak_rss_kib", report.peak_rss_kib);
  field("retained_rss_kib", report.retained_rss_kib);
  field("post_teardown_rss_kib", report.post_teardown_rss_kib);
  field("transient_rss_kib", report.transient_rss_kib);
  field("peak_input_ratio", report.peak_input_ratio);
  field("retained_input_ratio", report.retained_input_ratio);
  json << '}';
  return std::move(json).str();
}

}  // namespace dandelion::frontend_bench
