export module dandelion.frontend_workload;
import dandelion.frontend;
import std;

export namespace dandelion::frontend_bench {

enum class ReaderMode { kMemory64K, kMemory4K, kMemoryIrregular };

struct MemorySnapshot {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::optional<std::uint64_t> baseline_rss_kib;
  std::optional<std::uint64_t> peak_rss_kib;
  std::optional<std::uint64_t> retained_rss_kib;
  std::optional<std::uint64_t> post_teardown_rss_kib;
  std::optional<std::uint64_t> transient_rss_kib;
  // NOLINTEND(readability-identifier-naming)
};

struct ParseStats {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::uint64_t bytes = 0;
  std::uint64_t commands = 0;
  std::uint64_t committed_end_offset = 0;
  std::uint64_t assertions = 0;
  std::uint64_t definitions = 0;
  std::uint64_t max_command_bytes = 0;
  std::uint64_t reader_calls = 0;
  std::uint64_t read_command_ns = 0;
  std::uint64_t teardown_ns = 0;
  // NOLINTEND(readability-identifier-naming)
};

struct ParsedQuery {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string status = "ok";
  std::string diagnostic_message;
  std::optional<frontend::DiagnosticCode> diagnostic_code;
  std::optional<frontend::SourceSpan> diagnostic_span;
  ParseStats stats;
  std::shared_ptr<const frontend::QueryContext> query;
  std::optional<frontend::Session> session;
  // NOLINTEND(readability-identifier-naming)

  // Release both owners. Repeated calls preserve the first observation.
  void Teardown(bool measure_phase = false);
};

struct InspectReport : ParseStats, MemorySnapshot {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string status = "ok";
  std::string diagnostic_message;
  std::optional<frontend::DiagnosticCode> diagnostic_code;
  std::optional<frontend::SourceSpan> diagnostic_span;
  std::string logic;
  std::uint64_t tokens = 0;
  std::uint64_t checks = 0;
  std::uint64_t user_sorts = 0;
  std::uint64_t user_functions = 0;
  std::uint64_t datatype_groups = 0;
  std::uint64_t named_terms = 0;
  std::uint64_t annotations = 0;
  std::uint64_t metadata = 0;
  std::uint64_t roots = 0;
  std::uint64_t nodes = 0;
  std::uint64_t edges = 0;
  std::uint64_t depth = 0;
  std::uint64_t max_fanout = 0;
  std::optional<std::size_t> live_nodes;
  std::optional<std::size_t> live_edges;
  std::optional<std::size_t> node_bytes;
  std::optional<std::size_t> numeric_backing_bytes;
  std::optional<std::size_t> metadata_bytes;
  std::optional<std::size_t> table_bytes;
  std::optional<std::size_t> peak_table_bytes;
  std::optional<double> peak_input_ratio;
  std::optional<double> retained_input_ratio;
  // NOLINTEND(readability-identifier-naming)
};

std::expected<std::string, std::error_code> LoadFile(
    const std::filesystem::path& path);
// The session borrows input until teardown or destruction.
ParsedQuery ParseFromText(std::string_view input, bool measure_phases = false,
                          bool canonicalize_commutative = false,
                          bool capture_source_records = false);
ParsedQuery ParseFromReader(std::string_view input, ReaderMode mode,
                            bool measure_phases = false,
                            bool canonicalize_commutative = false,
                            bool capture_source_records = false);
InspectReport Inspect(std::string_view input,
                      std::optional<ReaderMode> mode = std::nullopt,
                      bool canonicalize_commutative = false);
std::string InspectToJson(const InspectReport& report);
MemorySnapshot ReadProcessMemory();

}  // namespace dandelion::frontend_bench
