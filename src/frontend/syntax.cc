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
    descriptor = ClassifyOperator(text);
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

Syntax::Syntax(SyntaxPool& pool, std::string_view bytes,
               SourceSpan start) noexcept
    : pool_(pool),
      bytes_(bytes),
      start_(start),
      tokens_(pool.tokens, pool.threshold, pool.stats),
      expressions_(pool.expressions, pool.threshold, pool.stats),
      edges_(pool.edges, pool.threshold, pool.stats),
      lines_(pool.lines, pool.threshold, pool.stats) {}

Syntax::~Syntax() {
  for (auto& slab : slabs_) {
    if (!slab.small) continue;
    for (auto& spare : pool_.slabs) {
      if (spare) continue;
      spare = std::move(slab.small);
      break;
    }
  }
}

Token Syntax::GetToken(std::size_t index) const noexcept {
  const auto [offset, storage, length, classification] = tokens_.Get(index);
  return {offset, storage, length,
          static_cast<TokenKind>(classification & 0xff),
          static_cast<SpellingId>(classification >> 8)};
}
void Syntax::AddToken(Token token) {
  tokens_.Append({token.offset, token.storage, token.length,
                  (static_cast<SyntaxWord>(token.spelling_id) << 8) |
                      std::to_underlying(token.kind)});
}
Expression Syntax::GetExpression(ExprId id) const noexcept {
  const auto [first, last, child, count] = expressions_.Get(id);
  return {static_cast<std::size_t>(first), static_cast<std::size_t>(last),
          static_cast<std::size_t>(child), static_cast<std::size_t>(count)};
}
SyntaxWord Syntax::Store(std::string_view bytes, std::string& scratch) {
  if (bytes.size() <= inline_bytes_.size() - inline_used_) {
    const auto start = inline_used_;
    std::ranges::copy(bytes, inline_bytes_.data() + start);
    inline_used_ += bytes.size();
    return start;
  }
  if (bytes.size() > kSlabSize || bytes.size() > kSlabSize - slab_used_) {
    if (slabs_.size() >= std::numeric_limits<SyntaxWord>::max() / kSlabSize)
      throw std::length_error("Syntax spelling offsets");
    if (bytes.size() > kSlabSize) {
      const auto start = static_cast<SyntaxWord>(slabs_.size() + 1) * kSlabSize;
      Slab slab;
      if (bytes.data() == scratch.data())
        slab.large = std::move(scratch);
      else
        slab.large.assign(bytes);
      slabs_.push_back(std::move(slab));
      slab_used_ = kSlabSize;
      if (pool_.stats) ++pool_.stats->syntax_page_allocations;
      return start;
    }
    Slab slab;
    for (auto& spare : pool_.slabs) {
      if (!spare) continue;
      slab.small = std::move(spare);
      break;
    }
    if (!slab.small) {
      slab.small = std::make_unique_for_overwrite<char[]>(kSlabSize);
      if (pool_.stats) ++pool_.stats->syntax_page_allocations;
    }
    slabs_.push_back(std::move(slab));
    slab_used_ = 0;
  }
  const auto start =
      static_cast<SyntaxWord>(slabs_.size()) * kSlabSize + slab_used_;
  std::ranges::copy(bytes, slabs_.back().small.get() + slab_used_);
  slab_used_ += bytes.size();
  return start;
}
std::string_view Syntax::Stored(SyntaxWord start,
                                SyntaxWord length) const noexcept {
  const auto page = static_cast<std::size_t>(start / kSlabSize);
  if (page == 0)
    return {inline_bytes_.data() + start, static_cast<std::size_t>(length)};
  const auto& slab = slabs_[page - 1];
  const auto* data = slab.small ? slab.small.get() : slab.large.data();
  return {data + start % kSlabSize, static_cast<std::size_t>(length)};
}
std::string_view Syntax::RawToken(std::size_t i) const noexcept {
  const auto token = GetToken(i);
  if (token.kind == TokenKind::kLeft) return "(";
  if (token.kind == TokenKind::kRight) return ")";
  return bytes_.empty() ? Stored(token.storage, token.length)
                        : bytes_.substr(static_cast<std::size_t>(token.offset),
                                        static_cast<std::size_t>(token.length));
}
std::string_view Syntax::Name(ExprId id) const noexcept {
  const auto raw = Raw(id);
  return Type(id) == TokenKind::kQuoted ? raw.substr(1, raw.size() - 2) : raw;
}
void Syntax::LineBreak(SyntaxWord offset, bool continuation) {
  if (continuation)
    lines_.Set(lines_.Size() - 1, {offset - 1, 1});
  else
    lines_.Append({offset, 0});
}
SourcePosition Syntax::Position(SyntaxWord offset) const noexcept {
  std::size_t first = 0;
  auto last = lines_.Size();
  while (first != last) {
    const auto middle = first + (last - first) / 2;
    if (lines_.Get(middle)[0] <= offset)
      first = middle + 1;
    else
      last = middle;
  }
  const auto base = start_.begin;
  if (first == 0)
    return {offset, base.line, base.column + (offset - base.offset)};
  const auto [line_start, continuation] = lines_.Get(first - 1);
  const auto effective_start = std::min(offset, line_start + continuation);
  return {offset, base.line + first, offset - effective_start + 1};
}
SourceSpan Syntax::Span(ExprId id) const noexcept {
  const auto expression = GetExpression(id);
  const auto first = GetToken(expression.token);
  const auto last = GetToken(expression.last_token);
  return {start_.source, Position(first.offset),
          Position(last.offset + last.length)};
}
ChildRange Syntax::Children(ExprId id) const noexcept {
  const auto expression = GetExpression(id);
  return {edges_, expression.first_child, expression.child_count};
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
  const auto expression = GetExpression(id);
  std::string result;
  auto previous = TokenKind::kLeft;
  for (auto index = expression.token; index <= expression.last_token; ++index) {
    const auto kind = GetToken(index).kind;
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
void Scanner::SaveTokenPart() {
  if (!token_start_) return;
  scratch_.append(window_.substr(*token_start_, cursor_ - *token_start_));
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
void Scanner::TakeByte(int byte, Syntax* syntax) {
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
  if (syntax) syntax->LineBreak(position_.offset, continuation);
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
void Scanner::Trivia(Syntax* syntax) {
  for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
    if (Space(byte)) {
      const auto rest = window_.substr(cursor_);
      const auto horizontal = rest.find_first_not_of(" \t");
      if (horizontal != 0) {
        Advance(std::min(horizontal, rest.size()));
      } else {
        TakeByte(byte, syntax);
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
        Scalar(byte, syntax);
      byte = Peek();
    } while (byte != kEnd && byte != '\r' && byte != '\n');
  }
}
Token Scanner::ScanToken(Syntax& syntax, int first) {
  const auto begin = position_;
  if (first == '(' || first == ')') {
    Advance(1);
    return {begin.offset, 0, 1,
            first == '(' ? TokenKind::kLeft : TokenKind::kRight};
  }

  if (reader_) {
    scratch_.clear();
    token_start_ = cursor_;
  }
  const auto bytes = [&] {
    return reader_
               ? TokenBytes()
               : text_.substr(
                     static_cast<std::size_t>(begin.offset),
                     static_cast<std::size_t>(position_.offset - begin.offset));
  };
  const auto token = [&](TokenKind kind, std::string_view raw,
                         SpellingId descriptor = kUnknownSpelling) {
    token_start_.reset();
    const auto storage = reader_ ? syntax.Store(raw, scratch_) : 0;
    return Token{begin.offset, storage, position_.offset - begin.offset, kind,
                 descriptor};
  };
  if (first == '|' || first == '"') {
    const auto kind = first == '|' ? TokenKind::kQuoted : TokenKind::kString;
    Advance(1);
    for (auto byte = Peek(); byte != kEnd; byte = Peek()) {
      if (byte == first) {
        Advance(1);
        if (kind == TokenKind::kQuoted || Peek() != '"')
          return token(kind, bytes());
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

void Scanner::ReadCommand(std::optional<Syntax>& pending) {
  Trivia();
  auto first = Peek();
  if (first == kEnd) return;
  if (first != '(')
    Fail(DiagnosticCode::kSyntax, Here(), "Expected a command list");
  auto& syntax = pending.emplace(pool_, text_, Here());
  struct Frame {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    ExprId expression;
    std::size_t token;
    std::size_t children = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<Frame> frames;
  for (;;) {
    auto token = ScanToken(syntax, first);
    const auto token_index = syntax.tokens_.Size();
    if (token.kind == TokenKind::kRight) {
      const auto [expression, opening, count] = frames.back();
      // A closing token records the preorder successor of its entire list.
      // This lets the parent emit each child's edge directly into its range.
      token.storage = syntax.expressions_.Size();
      const auto start = syntax.edges_.Size();
      auto child = expression + 1;
      for (std::size_t i = 0; i < count; ++i) {
        syntax.edges_.Append({child});
        const auto subtree = syntax.GetExpression(child);
        child = subtree.token == subtree.last_token
                    ? child + 1
                    : static_cast<ExprId>(
                          syntax.GetToken(subtree.last_token).storage);
      }
      syntax.expressions_.Set(expression, {opening, token_index, start, count});
      frames.pop_back();
      syntax.AddToken(token);
      if (frames.empty()) return;
    } else {
      syntax.AddToken(token);
      const auto id = syntax.expressions_.Size();
      syntax.expressions_.Append({token_index, token_index, 0, 0});
      if (!frames.empty()) ++frames.back().children;
      if (token.kind == TokenKind::kLeft) frames.push_back({id, token_index});
    }
    Trivia(&syntax);
    first = Peek();
    if (first == kEnd)
      Fail(DiagnosticCode::kSyntax, Here(),
           "Incomplete command at end of input");
  }
}

void Scanner::Next(std::optional<Syntax>& pending) {
  try {
    ReadCommand(pending);
  } catch (...) {
    std::string{}.swap(scratch_);
    token_start_.reset();
    throw;
  }
  if (pending) ValidateSyntax(*pending);
}

}  // namespace dandelion::frontend::detail
