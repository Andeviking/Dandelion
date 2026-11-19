module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
constexpr int kEnd = -1;

bool Space(int byte) noexcept {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}
bool Digit(int byte) noexcept { return byte >= '0' && byte <= '9'; }
bool HexDigit(int byte) noexcept {
  return Digit(byte) || (byte >= 'a' && byte <= 'f') ||
         (byte >= 'A' && byte <= 'F');
}
bool SymbolInitial(int byte) noexcept {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= 0 && byte < 128 &&
          std::string_view("+-/*=%?!.$_~&^<>@")
              .contains(static_cast<char>(byte)));
}
bool IsNumeral(std::string_view text) noexcept {
  return !text.empty() && (text.size() == 1 || text.front() != '0') &&
         std::ranges::all_of(text, Digit);
}
bool Delimiter(int byte) noexcept {
  return byte == kEnd || Space(byte) || byte == '(' || byte == ')' ||
         byte == ';' || byte == '|' || byte == '"';
}

TokenKind Classify(std::string_view text, SourceSpan where) {
  const auto first = text.front();
  if (Digit(first)) {
    if (IsNumeral(text)) return TokenKind::kNumeral;
    const auto dot = text.find('.');
    if (dot != std::string_view::npos && IsNumeral(text.substr(0, dot)) &&
        dot + 1 < text.size() &&
        std::ranges::all_of(text.substr(dot + 1), Digit))
      return TokenKind::kDecimal;
    Fail(DiagnosticCode::kSyntax, where, "Malformed numeral or decimal");
  }
  if (first == '#') {
    if (text.size() > 2 && text.starts_with("#b") &&
        std::ranges::all_of(text.substr(2),
                            [](char c) { return c == '0' || c == '1'; }))
      return TokenKind::kBinary;
    if (text.size() > 2 && text.starts_with("#x") &&
        std::ranges::all_of(text.substr(2), HexDigit))
      return TokenKind::kHexadecimal;
    Fail(DiagnosticCode::kSyntax, where, "Malformed bit-vector literal");
  }
  if (first == ':' && IsSimpleSymbol(text.substr(1)) &&
      !IsReservedWord(text.substr(1)))
    return TokenKind::kKeyword;
  if (IsSimpleSymbol(text))
    return IsReservedWord(text) ? TokenKind::kReserved : TokenKind::kSymbol;
  Fail(DiagnosticCode::kSyntax, where, "Invalid SMT-LIB token");
}
}  // namespace

bool IsSimpleSymbol(std::string_view word) noexcept {
  return !word.empty() && SymbolInitial(word.front()) &&
         std::ranges::all_of(word.substr(1), [](char byte) {
           return SymbolInitial(byte) || Digit(byte);
         });
}

std::size_t Utf8Size(int first) noexcept {
  if (first < 0xc2 || first > 0xf4) return 0;
  return first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
}
bool IsUtf8Continuation(int byte) noexcept {
  return byte >= 0x80 && byte <= 0xbf;
}
bool IsUtf8Scalar(unsigned scalar, std::size_t size) noexcept {
  return (size != 3 || scalar >= 0x800) && (size != 4 || scalar >= 0x10000) &&
         (scalar < 0xd800 || scalar > 0xdfff) && scalar <= 0x10ffff;
}
bool IsQuotedByte(int byte) noexcept {
  return (byte >= 32 && byte != 127) || Space(byte);
}

std::string_view Syntax::RawToken(std::size_t i) const noexcept {
  const auto& token = tokens[i];
  return (bytes.empty() ? std::string_view(arena) : bytes)
      .substr(token.start, token.length);
}
std::string_view Syntax::Name(ExprId id) const noexcept {
  const auto raw = Raw(id);
  return Type(id) == TokenKind::kQuoted ? raw.substr(1, raw.size() - 2) : raw;
}
SourceSpan Syntax::Span(ExprId id) const noexcept {
  const auto& expression = expressions[id];
  auto where = tokens[expression.token].where;
  where.end = tokens[expression.last_token].where.end;
  return where;
}
std::span<const ExprId> Syntax::Children(ExprId id) const noexcept {
  const auto& expression = expressions[id];
  return std::span(child_edges)
      .subspan(expression.first_child, expression.child_count);
}
bool Syntax::Is(ExprId id, std::string_view unquoted) const noexcept {
  return Type(id) != TokenKind::kQuoted && Raw(id) == unquoted;
}
bool Syntax::IsSymbol(ExprId id) const noexcept {
  return Type(id) == TokenKind::kSymbol || Type(id) == TokenKind::kQuoted;
}
std::string Syntax::Render(
    ExprId id,
    const std::unordered_map<std::size_t, Replacement>* replacements) const {
  const auto& expression = expressions[id];
  std::string result;
  auto previous = TokenKind::kLeft;
  for (auto index = expression.token; index <= expression.last_token; ++index) {
    const auto kind = tokens[index].kind;
    if (previous != TokenKind::kLeft && kind != TokenKind::kRight)
      result.push_back(' ');
    if (replacements) {
      const auto replacement = replacements->find(index);
      if (replacement != replacements->end()) {
        result.append(replacement->second.spelling);
        index = replacement->second.last_token;
        previous = TokenKind::kRight;
        continue;
      }
    }
    result.append(RawToken(index));
    previous = kind;
  }
  return result;
}

Identifier GetIdentifier(const Syntax& syntax, ExprId id) {
  if (!syntax.List(id)) return {id, {}};
  const auto children = syntax.Children(id);
  if (syntax.Is(children[0], "as")) {
    auto name = GetIdentifier(syntax, children[1]);
    name.qualifier = children[2];
    return name;
  }
  return {children[1], children.subspan(2)};
}

Scanner::Scanner(std::string_view input, std::uint32_t source)
    : text_(input), window_(input), source_(source) {}
Scanner::Scanner(Reader reader, std::uint32_t source)
    : reader_(std::move(reader)),
      buffer_(std::make_unique<std::array<char, kReaderCapacity>>()),
      source_(source) {
  if (!reader_) Fail(DiagnosticCode::kInputIo, Here(), "Missing input reader");
}
int Scanner::Peek() {
  if (cursor_ < window_.size())
    return static_cast<unsigned char>(window_[cursor_]);
  if (!reader_ || eof_) return kEnd;
  ReadResult count;
  try {
    count = reader_(*buffer_);
  } catch (...) {
    Fail(DiagnosticCode::kInputIo, Here(), "Input reader raised an exception");
  }
  if (!count)
    throw Failure{{DiagnosticCode::kInputIo, Here(), "Input read failed",
                   std::nullopt, count.error()}};
  if (*count > buffer_->size())
    Fail(DiagnosticCode::kInputIo, Here(),
         "Reader returned an invalid byte count");
  if (*count == 0) {
    eof_ = true;
    return kEnd;
  }
  window_ = std::string_view(buffer_->data(), *count);
  cursor_ = 0;
  return static_cast<unsigned char>(window_.front());
}
void Scanner::TakeByte(int byte, Syntax* syntax) {
  constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
  if (position_.offset == limit)
    NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, Here());
  if (byte == '\r' || (byte == '\n' && !after_cr_)) {
    if (position_.line == limit)
      NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, Here());
    ++position_.line;
    position_.column = 1;
  } else if (byte != '\n') {
    if (position_.column == limit)
      NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, Here());
    ++position_.column;
  }
  after_cr_ = byte == '\r';
  ++position_.offset;
  ++cursor_;
  if (reader_ && syntax) syntax->arena.push_back(static_cast<char>(byte));
}
void Scanner::Scalar(int first, Syntax* syntax) {
  const auto start = Here();
  if (first == 0) Fail(DiagnosticCode::kSyntax, start, "NUL in input");
  if (first < 128) {
    TakeByte(first, syntax);
    return;
  }
  const auto size = Utf8Size(first);
  if (size == 0)
    Fail(DiagnosticCode::kSyntax, start, "Invalid UTF-8 leading byte");
  auto scalar = static_cast<unsigned>(first & (0x7f >> size));
  TakeByte(first, syntax);
  for (std::size_t i = 1; i < size; ++i) {
    const auto byte = Peek();
    if (!IsUtf8Continuation(byte))
      Fail(DiagnosticCode::kSyntax, start,
           "Invalid or incomplete UTF-8 sequence");
    scalar = (scalar << 6) | static_cast<unsigned>(byte & 0x3f);
    TakeByte(byte, syntax);
  }
  if (!IsUtf8Scalar(scalar, size))
    Fail(DiagnosticCode::kSyntax, start, "Invalid UTF-8 scalar");
}
void Scanner::Trivia() {
  for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
    if (Space(byte)) {
      TakeByte(byte, nullptr);
      continue;
    }
    if (byte != ';') return;
    while (byte != kEnd && byte != '\r' && byte != '\n') {
      Scalar(byte, nullptr);
      byte = Peek();
    }
  }
}
Token Scanner::ScanToken(Syntax& syntax, int first) {
  const auto begin = position_;
  const auto start =
      reader_ ? syntax.arena.size() : static_cast<std::size_t>(begin.offset);
  const auto token = [&](TokenKind kind) {
    return Token{kind,
                 {source_, begin, position_},
                 start,
                 static_cast<std::size_t>(position_.offset - begin.offset)};
  };
  if (first == '(' || first == ')') {
    TakeByte(first, &syntax);
    return token(first == '(' ? TokenKind::kLeft : TokenKind::kRight);
  }
  if (first == '|' || first == '"') {
    const auto kind = first == '|' ? TokenKind::kQuoted : TokenKind::kString;
    TakeByte(first, &syntax);
    for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
      if (byte == first) {
        TakeByte(byte, &syntax);
        if (kind == TokenKind::kQuoted || Peek() != '"') return token(kind);
      } else if (!IsQuotedByte(byte) ||
                 (kind == TokenKind::kQuoted && byte == '\\')) {
        Fail(DiagnosticCode::kSyntax, Here(),
             "Invalid character in quoted token");
      }
      Scalar(byte, &syntax);
    }
    Fail(DiagnosticCode::kSyntax, {source_, begin, position_},
         "Unterminated quoted token");
  }
  auto byte = first;
  do {
    if (byte == 0) Fail(DiagnosticCode::kSyntax, Here(), "NUL in input");
    if (byte >= 128) {
      const auto where = Here();
      Scalar(byte, &syntax);
      Fail(DiagnosticCode::kSyntax, where,
           "Non-ASCII character outside quoted data");
    }
    TakeByte(byte, &syntax);
    byte = Peek();
  } while (!Delimiter(byte));
  auto result = token(TokenKind::kSymbol);
  const auto bytes = reader_ ? std::string_view(syntax.arena) : text_;
  result.kind = Classify(bytes.substr(start, result.length), result.where);
  return result;
}

std::optional<Syntax> Scanner::ReadCommand() {
  Trivia();
  auto first = Peek();
  if (first == kEnd) return std::nullopt;
  if (first != '(')
    Fail(DiagnosticCode::kSyntax, Here(), "Expected a command list");
  Syntax syntax;
  syntax.bytes = text_;
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    ExprId expression;
    std::size_t start;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<Frame> frames;
  std::vector<ExprId> children;
  for (;;) {
    const auto token = ScanToken(syntax, first);
    const auto token_index = syntax.tokens.size();
    syntax.tokens.push_back(token);
    if (token.kind == TokenKind::kRight) {
      const auto frame = frames.back();
      auto& expression = syntax.expressions[frame.expression];
      expression.last_token = token_index;
      expression.first_child = syntax.child_edges.size();
      expression.child_count = children.size() - frame.start;
      const auto edges = std::span(children).subspan(frame.start);
      syntax.child_edges.insert(syntax.child_edges.end(), edges.begin(),
                                edges.end());
      children.resize(frame.start);
      frames.pop_back();
      if (frames.empty()) return syntax;
    } else {
      const auto id = syntax.expressions.size();
      syntax.expressions.push_back({token_index, token_index});
      if (!frames.empty()) children.push_back(id);
      if (token.kind == TokenKind::kLeft)
        frames.push_back({id, children.size()});
    }
    Trivia();
    first = Peek();
    if (first == kEnd)
      Fail(DiagnosticCode::kSyntax, Here(),
           "Incomplete command at end of input");
  }
}

std::optional<Syntax> Scanner::Next() {
  auto syntax = ReadCommand();
  if (syntax) ValidateSyntax(*syntax);
  return syntax;
}

}  // namespace dandelion::frontend::detail
