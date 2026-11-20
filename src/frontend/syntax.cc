module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
constexpr int kEnd = -1;

enum ByteClass : std::uint8_t {
  kSpace = 1 << 0,
  kDigit = 1 << 1,
  kHex = 1 << 2,
  kInitial = 1 << 3,
  kDelimiter = 1 << 4,
  kPlainQuoted = 1 << 5,
  kPlainComment = 1 << 6,
};
constexpr auto kByteClasses = [] {
  std::array<std::uint8_t, 256> classes{};
  for (std::size_t i = 0; i < classes.size(); ++i) {
    const auto byte = static_cast<char>(i);
    const bool digit = byte >= '0' && byte <= '9';
    const bool space = std::string_view(" \t\r\n").contains(byte);
    const bool newline = byte == '\r' || byte == '\n';
    if (space) classes[i] |= kSpace;
    if (digit) classes[i] |= kDigit;
    if (digit || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F'))
      classes[i] |= kHex;
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        std::string_view("+-/*=%?!.$_~&^<>@").contains(byte))
      classes[i] |= kInitial;
    if (space || std::string_view("();|\"").contains(byte))
      classes[i] |= kDelimiter;
    if (!newline && ((i >= 32 && i < 127) || space)) classes[i] |= kPlainQuoted;
    if (i > 0 && i < 128 && !newline) classes[i] |= kPlainComment;
  }
  return classes;
}();
bool HasClass(int byte, unsigned mask) noexcept {
  return (kByteClasses[static_cast<unsigned char>(byte)] & mask) != 0;
}
bool Space(int byte) noexcept { return HasClass(byte, kSpace); }
bool Digit(int byte) noexcept { return HasClass(byte, kDigit); }
bool HexDigit(int byte) noexcept { return HasClass(byte, kHex); }
bool SymbolInitial(int byte) noexcept { return HasClass(byte, kInitial); }
bool IsNumeral(std::string_view text) noexcept {
  return !text.empty() && (text.size() == 1 || text.front() != '0') &&
         std::ranges::all_of(text, Digit);
}
bool Delimiter(int byte) noexcept {
  return byte == kEnd || HasClass(byte, kDelimiter);
}

TokenKind Classify(std::string_view text, SourceSpan where,
                   SpellingId& descriptor) {
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
  if (IsSimpleSymbol(text)) {
    descriptor = ClassifyReserved(text);
    if (descriptor != kUnknownSpelling) return TokenKind::kReserved;
    descriptor = ClassifySymbol(text);
    return TokenKind::kSymbol;
  }
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

ExprId Syntax::Add(Token token, bool borrow) {
  const auto id = free_ == kNoExpr ? slots_ : free_;
  if (free_ == kNoExpr) {
    if (slots_ / kPageSize == pages_.size())
      pages_.push_back(std::make_unique_for_overwrite<Page>());
    ++slots_;
  } else {
    free_ = Entry(id).token;
  }
  ++live_count_;
  auto& entry = Entry(id);
  entry.sort = std::monostate{};
  entry.kind = token.kind;
  entry.spelling = token.spelling_id;
  entry.where = token.where;
  if (borrow) {
    entry.raw = token.raw;
  } else {
    entry.owned.assign(token.raw);
    entry.raw = entry.owned;
  }
  Mark(id);
  return id;
}

void Syntax::Close(ExprId id, SourcePosition end) noexcept {
  auto& entry = Entry(id);
  entry.where.end = end;
  entry.last_token = captured_.size();
}

void Syntax::DiscardChildren(ExprId id) noexcept {
  // Chain the descendants through an unused mark as they leave the live set.
  auto work = kNoExpr;
  const auto enqueue = [&](Expression& entry) {
    for (const auto child : entry.children.Range())
      Entry(child).last_token = std::exchange(work, child);
    entry.children.Clear();
  };
  enqueue(Entry(id));
  while (work != kNoExpr) {
    const auto child = work;
    auto& entry = Entry(child);
    work = entry.last_token;
    enqueue(entry);
    entry.owned.clear();
    if (entry.owned.capacity() > 4096) std::string{}.swap(entry.owned);
    entry.token = std::exchange(free_, child);
    --live_count_;
  }
}

void Syntax::RemoveLast(ExprId parent) noexcept {
  auto& siblings = Entry(parent).children;
  const auto id = siblings.Range().back();
  siblings.Pop();
  Discard(id);
}

void Syntax::Discard(ExprId id) noexcept {
  DiscardChildren(id);
  auto& entry = Entry(id);
  entry.owned.clear();
  entry.token = std::exchange(free_, id);
  --live_count_;
}

void Syntax::Reset() noexcept {
  constexpr std::size_t kRetainedFragments = 256;
  constexpr auto kRetainedPages = kRetainedFragments / kPageSize;
  constexpr std::size_t kRetainedTextBytes = 64 * 1024;
  if (pages_.capacity() > 2 * kRetainedPages) {
    std::vector<std::unique_ptr<Page>>{}.swap(pages_);
    slots_ = 0;
  } else if (slots_ > kRetainedFragments) {
    pages_.resize(kRetainedPages);
    slots_ = kRetainedFragments;
  }
  if (captured_.capacity() > kRetainedTextBytes) std::string{}.swap(captured_);
  ClearCapture();
  free_ = kNoExpr;
  for (auto index = slots_; index != 0;) {
    auto& entry = Entry(--index);
    entry.owned.clear();
    if (entry.owned.capacity() > 4096) std::string{}.swap(entry.owned);
    entry.children.Clear();
    entry.raw = {};
  }
  slots_ = 0;
  live_count_ = 0;
}

void Syntax::Capture(Token token) {
  if (!captured_.empty() && previous_ != TokenKind::kLeft &&
      token.kind != TokenKind::kRight)
    captured_.push_back(' ');
  mark_ = captured_.size();
  captured_.append(token.raw);
  previous_ = token.kind;
}

void Syntax::Mark(ExprId id) noexcept {
  auto& entry = Entry(id);
  entry.token = mark_;
  entry.last_token = captured_.size();
}

std::string Syntax::Render(
    ExprId id, const std::map<std::size_t, Replacement>* replacements) const {
  const auto& entry = GetExpression(id);
  if (entry.token == entry.last_token) return std::string(entry.raw);
  const auto text = std::string_view(captured_);
  if (!replacements)
    return std::string(
        text.substr(entry.token, entry.last_token - entry.token));
  std::string result;
  auto cursor = entry.token;
  for (auto it = replacements->lower_bound(cursor);
       it != replacements->end() && it->first < entry.last_token; ++it) {
    result.append(text.substr(cursor, it->first - cursor));
    result.append(it->second.spelling);
    cursor = it->second.last_token;
  }
  result.append(text.substr(cursor, entry.last_token - cursor));
  return result;
}

Identifier GetIdentifier(const Syntax& syntax, ExprId id) {
  auto children = syntax.Children(id);
  if (children.empty()) return {id, {}};
  auto qualifier = kNoExpr;
  if (syntax.Spelling(children[0]) == SpellingId::kAs) {
    qualifier = children[2];
    id = children[1];
    children = syntax.Children(id);
    if (children.empty()) return {id, {}, qualifier};
  }
  return {children[1], children.subspan(2), qualifier};
}

Scanner::Scanner(std::string_view input, std::uint32_t source)
    : window_(input), source_(source) {}
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
  SaveHeldToken();
  SaveTokenPart();
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
  if (token_start_) token_start_ = 0;
  return static_cast<unsigned char>(window_.front());
}
#ifdef DANDELION_FRONTEND_TESTING
void Scanner::Checkpoint() {
  if (!fail_after_) return;
  if (*fail_after_ == 0) {
    fail_after_.reset();
    throw std::bad_alloc{};
  }
  --*fail_after_;
}
#endif
void Scanner::HoldToken(std::string_view bytes) {
  held_ = bytes;
  held_borrowed_ = static_cast<bool>(reader_);
  if (held_borrowed_ && !scratch_.empty()) SaveHeldToken();
}

void Scanner::SaveHeldToken() noexcept {
  if (!held_borrowed_) return;
  held_borrowed_ = false;
  try {
    held_text_.assign(held_);
    held_ = held_text_;
  } catch (const std::bad_alloc&) {
    held_error_ = ResourceKind::kCppAllocation;
  } catch (const std::length_error&) {
    held_error_ = ResourceKind::kSize;
  }
}

std::string_view Scanner::HeldToken(SourceSpan where) const {
  if (held_error_ != ResourceKind::kNone)
    NodeFailure({ErrorCode::kResourceLimit, held_error_}, where);
  return held_;
}

void Scanner::ReleaseToken() noexcept {
  held_ = {};
  held_borrowed_ = false;
  held_error_ = ResourceKind::kNone;
  held_text_.clear();
  if (held_text_.capacity() > kReaderCapacity) std::string{}.swap(held_text_);
}

void Scanner::SaveTokenPart() {
  if (!token_start_) return;
  const auto part = window_.substr(*token_start_, cursor_ - *token_start_);
#ifdef DANDELION_FRONTEND_TESTING
  if (part.size() > scratch_.capacity() - scratch_.size()) Checkpoint();
#endif
  scratch_.append(part);
  token_start_ = cursor_;
}
std::string_view Scanner::TokenBytes() {
  if (scratch_.empty())
    return window_.substr(*token_start_, cursor_ - *token_start_);
  SaveTokenPart();
  return scratch_;
}
void Scanner::Advance(std::size_t count) {
  constexpr auto limit = std::numeric_limits<SyntaxWord>::max();
  if (count > limit - position_.offset || count > limit - position_.column)
    NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, Here());
  position_.offset += count;
  position_.column += count;
  cursor_ += count;
  after_cr_ = false;
}
void Scanner::TakeByte(int byte) {
  if (byte != '\r' && byte != '\n') {
    Advance(1);
    return;
  }
  constexpr auto limit = std::numeric_limits<SyntaxWord>::max();
  const bool continuation = byte == '\n' && after_cr_;
  if (position_.offset == limit || (!continuation && position_.line == limit))
    NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, Here());
  if (!continuation) ++position_.line;
  position_.column = 1;
  after_cr_ = byte == '\r';
  ++position_.offset;
  ++cursor_;
}
void Scanner::Scalar(int first) {
  const auto start = Here();
  if (first == 0) Fail(DiagnosticCode::kSyntax, start, "NUL in input");
  if (first < 128) {
    TakeByte(first);
    return;
  }
  const auto size = Utf8Size(first);
  if (size == 0)
    Fail(DiagnosticCode::kSyntax, start, "Invalid UTF-8 leading byte");
  auto scalar = static_cast<unsigned>(first & (0x7f >> size));
  TakeByte(first);
  for (std::size_t i = 1; i < size; ++i) {
    const auto byte = Peek();
    if (!IsUtf8Continuation(byte))
      Fail(DiagnosticCode::kSyntax, start,
           "Invalid or incomplete UTF-8 sequence");
    scalar = (scalar << 6) | static_cast<unsigned>(byte & 0x3f);
    TakeByte(byte);
  }
  if (!IsUtf8Scalar(scalar, size))
    Fail(DiagnosticCode::kSyntax, start, "Invalid UTF-8 scalar");
}
void Scanner::Trivia() {
  for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
    if (Space(byte)) {
      const auto rest = window_.substr(cursor_);
      const auto horizontal = rest.find_first_not_of(" \t");
      if (horizontal != 0) {
        Advance(std::min(horizontal, rest.size()));
      } else {
        TakeByte(byte);
      }
      continue;
    }
    if (byte != ';') return;
    do {
      const auto rest = window_.substr(cursor_);
      const auto stop = std::ranges::find_if_not(
          rest, [](char c) { return HasClass(c, kPlainComment); });
      const auto count = static_cast<std::size_t>(stop - rest.begin());
      if (count != 0)
        Advance(count);
      else
        Scalar(byte);
      byte = Peek();
    } while (byte != kEnd && byte != '\r' && byte != '\n');
  }
}
Token Scanner::ScanToken(int first) {
  const auto begin = position_;
  if (first == '(' || first == ')') {
    Advance(1);
    return {first == '(' ? TokenKind::kLeft : TokenKind::kRight,
            kUnknownSpelling,
            first == '(' ? "(" : ")",
            {source_, begin, position_}};
  }

  if (reader_) {
    if (scratch_.capacity() > kReaderCapacity) std::string{}.swap(scratch_);
    scratch_.clear();
    token_start_ = cursor_;
  }
  const auto bytes = [&] {
    auto length = static_cast<std::size_t>(position_.offset - begin.offset);
    return reader_ ? TokenBytes() : window_.substr(cursor_ - length, length);
  };
  const auto token = [&](TokenKind kind, std::string_view raw,
                         SpellingId descriptor = kUnknownSpelling) {
    token_start_.reset();
    return Token{kind, descriptor, raw, {source_, begin, position_}};
  };
  if (first == '|' || first == '"') {
    const auto kind = first == '|' ? TokenKind::kQuoted : TokenKind::kString;
    Advance(1);
    for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
      if (byte == first) {
        Advance(1);
        if (kind == TokenKind::kQuoted || Peek() != '"') {
          const auto raw = bytes();
          const auto spelling =
              kind == TokenKind::kQuoted
                  ? ClassifySymbol(raw.substr(1, raw.size() - 2))
                  : kUnknownSpelling;
          return token(kind, raw, spelling);
        }
        Advance(1);
        continue;
      }
      if (!IsQuotedByte(byte) || (kind == TokenKind::kQuoted && byte == '\\'))
        Fail(DiagnosticCode::kSyntax, Here(),
             "Invalid character in quoted token");
      const auto rest = window_.substr(cursor_);
      const auto stop = std::ranges::find_if_not(rest, [first](char c) {
        return HasClass(c, kPlainQuoted) && c != first &&
               (first != '|' || c != '\\');
      });
      const auto count = static_cast<std::size_t>(stop - rest.begin());
      if (count != 0)
        Advance(count);
      else
        Scalar(byte);
    }
    Fail(DiagnosticCode::kSyntax, {source_, begin, position_},
         "Unterminated quoted token");
  }
  auto byte = first;
  do {
    if (byte == 0) Fail(DiagnosticCode::kSyntax, Here(), "NUL in input");
    if (byte >= 128) {
      const auto where = Here();
      Scalar(byte);
      Fail(DiagnosticCode::kSyntax, where,
           "Non-ASCII character outside quoted data");
    }
    const auto rest = window_.substr(cursor_);
    const auto stop = std::ranges::find_if(rest, [](unsigned char c) {
      return c == 0 || c >= 128 || HasClass(c, kDelimiter);
    });
    Advance(static_cast<std::size_t>(stop - rest.begin()));
    byte = Peek();
  } while (!Delimiter(byte));
  const auto raw = bytes();
  auto descriptor = kUnknownSpelling;
  const auto kind = Classify(raw, {source_, begin, position_}, descriptor);
  return token(kind, raw, descriptor);
}

std::optional<Token> Scanner::Next() {
  Trivia();
  const auto first = Peek();
  if (first == kEnd) return std::nullopt;
  return ScanToken(first);
}

}  // namespace dandelion::frontend::detail
