module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
void Response::Write(std::string_view bytes, SourceSpan where) {
  if (bytes.empty()) return;
  if (!sink_.write) OutputError(where, "Missing response writer");
  WriteResult result;
  try {
    result = sink_.write(bytes);
  } catch (...) {
    // A callback may have affected its destination before throwing.
    OutputError(where, "Response writer raised an exception");
  }
  if (!result) OutputError(where, "Response write failed", result.error());
  if (*result != 0) started_ = true;
  if (*result != bytes.size())
    OutputError(where, "Short or invalid response write",
                std::make_error_code(std::errc::io_error));
}
void Response::Finish(SourceSpan where) {
  if (!started_ || !sink_.flush) return;
  std::expected<void, std::error_code> flushed;
  try {
    flushed = sink_.flush();
  } catch (...) {
    OutputError(where, "Response flush raised an exception");
  }
  if (!flushed) OutputError(where, "Response flush failed", flushed.error());
}
}  // namespace dandelion::frontend::detail

namespace dandelion::frontend {
namespace {
std::string_view ResponseText(CommandResponse response) noexcept {
  switch (response) {
    case CommandResponse::kNone:
      return {};
    case CommandResponse::kSuccess:
      return "success";
    case CommandResponse::kUnsupported:
      return "unsupported";
    case CommandResponse::kTrue:
      return "true";
    case CommandResponse::kFalse:
      return "false";
    case CommandResponse::kZero:
      return "0";
    case CommandResponse::kStdout:
      return "\"stdout\"";
    case CommandResponse::kStderr:
      return "\"stderr\"";
    case CommandResponse::kName:
      return "(:name \"Dandelion\")";
    case CommandResponse::kVersion:
      return "(:version \"development\")";
    case CommandResponse::kAuthors:
      return "(:authors \"\")";
    case CommandResponse::kErrorBehavior:
      return "(:error-behavior immediate-exit)";
    case CommandResponse::kUnknownReason:
      std::unreachable();
  }
  std::unreachable();
}

std::string_view AnswerText(CheckAnswer answer) {
  switch (answer) {
    case CheckAnswer::kSat:
      return "sat";
    case CheckAnswer::kUnsat:
      return "unsat";
    case CheckAnswer::kUnknown:
      return "unknown";
  }
  detail::Fail(DiagnosticCode::kEngineFailure, {}, "Invalid engine result");
}
std::string_view ReasonText(UnknownReason reason) {
  switch (reason) {
    case UnknownReason::kUnknown:
      return "unknown";
    case UnknownReason::kIncomplete:
      return "incomplete";
    case UnknownReason::kMemout:
      return "memout";
    case UnknownReason::kTimeout:
      return "timeout";
    case UnknownReason::kResourceout:
      return "resourceout";
  }
  detail::Fail(DiagnosticCode::kEngineFailure, {},
               "Invalid reason for unknown");
}
std::string ErrorText(const Diagnostic& diagnostic) {
  auto text = "(error \"line " + std::to_string(diagnostic.where.begin.line) +
              ", column " + std::to_string(diagnostic.where.begin.column) +
              ": ";
  auto message = diagnostic.message;
  while (!message.empty()) {
    const auto first = static_cast<unsigned char>(message.front());
    if (first < 128) {
      if (first == '"') text.push_back('"');
      text.push_back(detail::IsQuotedByte(first) ? message.front() : '?');
      message.remove_prefix(1);
      continue;
    }
    const auto size = detail::Utf8Size(first);
    if (size != 0 && size <= message.size()) {
      auto scalar = static_cast<unsigned>(first & (0x7f >> size));
      std::size_t i = 1;
      for (; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(message[i]);
        if (!detail::IsUtf8Continuation(byte)) break;
        scalar = (scalar << 6) | static_cast<unsigned>(byte & 0x3f);
      }
      if (i == size && detail::IsUtf8Scalar(scalar, size)) {
        text.append(message.substr(0, size));
        message.remove_prefix(size);
        continue;
      }
    }
    text.push_back('?');
    message.remove_prefix(1);
  }
  text.append("\")\n");
  return text;
}
RunResult Report(Diagnostic error, const ResponseSink& sink,
                 bool response_started, std::shared_ptr<ModelOwner> model) {
  if (response_started || error.code == DiagnosticCode::kOutputIo)
    return {1, error, std::move(model)};
  const auto emitted = detail::Guard(error.where, [&] {
    detail::Response response(sink);
    if (error.code == DiagnosticCode::kUnsupported)
      response.Write("unsupported\n", error.where);
    else
      response.Write(ErrorText(error), error.where);
    response.Finish(error.where);
  });
  if (!emitted) return {1, emitted.error(), std::move(model)};
  return {1, error, std::move(model)};
}
Outcome<CheckResult> InvokeEngine(const Engine& engine,
                                  const QueryContext& query, SourceSpan where) {
  if (!engine)
    detail::Fail(DiagnosticCode::kEngineFailure, where,
                 "No check engine is available");
  try {
    return engine(query, query.GetOptions());
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const ReferenceLimit&) {
    throw;
  } catch (const std::length_error&) {
    throw;
  } catch (...) {
    detail::Fail(DiagnosticCode::kEngineFailure, where,
                 "Check engine raised an exception");
  }
}
}  // namespace

RunResult Session::Run(const Engine& engine, const ResponseSink& sink) {
  if (state_->in_read)
    return {1, detail::ReentrantRead(state_->scanner.Here()),
            state_->inspection_model};
  auto model = state_->inspection_model;
  auto answer = model ? std::optional(model->GetAnswer()) : std::nullopt;
  auto reason = model ? model->GetReason() : UnknownReason::kUnknown;

  for (;;) {
    auto next = state_->ReadCommand(model.get(), &answer);
    if (!next) {
      state_->phase = SessionPhase::kTerminated;
      return Report(next.error(), sink, false, std::move(model));
    }
    if (!*next) return {0, std::nullopt, std::move(model)};
    auto& command = **next;
    const auto [kind, where] = command.header;
    detail::Response response(sink);
    auto applied = detail::Guard(where, [&] {
      if (kind == CommandKind::kCheckSat) {
        auto result =
            detail::Use(InvokeEngine(engine, *command.query, where), where);
        const auto spelling = AnswerText(result.answer);
        answer = result.answer;
        reason = result.reason;
        if (answer == CheckAnswer::kUnknown) (void)ReasonText(reason);
        if (command.query->GetOptions().produce_models &&
            answer == CheckAnswer::kSat && !result.model)
          detail::Fail(DiagnosticCode::kEngineFailure, where,
                       "Sat result omitted the required model");
        if (result.model && answer == CheckAnswer::kUnsat)
          detail::Fail(DiagnosticCode::kEngineFailure, where,
                       "Unsat result supplied a model");
        if (result.model && command.query->GetOptions().produce_models)
          model = detail::Use(
              ModelOwner::Create(command.query, std::move(result.model),
                                 *answer, reason),
              where);
        response.Write(spelling, where);
        response.Write("\n", where);
      } else if (kind == CommandKind::kGetValue) {
        detail::WriteValues(*model, *state_, command.values, response, where);
        return;
      } else if (kind == CommandKind::kGetModel) {
        detail::WriteModel(*model, *state_, response, where);
        return;
      } else if (const auto* echo =
                     std::get_if<std::string>(&command.response)) {
        response.Write(*echo, where);
        response.Write("\n", where);
      } else {
        const auto tag = std::get<CommandResponse>(command.response);
        if (tag == CommandResponse::kUnknownReason) {
          response.Write("(:reason-unknown ", where);
          response.Write(ReasonText(reason), where);
          response.Write(")\n", where);
        } else if (const auto text = ResponseText(tag); !text.empty()) {
          response.Write(text, where);
          response.Write("\n", where);
        }
      }
      response.Finish(where);
    });
    if (!applied) {
      state_->phase = SessionPhase::kTerminated;
      return Report(applied.error(), sink, response.Started(),
                    std::move(model));
    }
  }
}
RunResult Run(Session& session, const Engine& engine,
              const ResponseSink& sink) {
  return session.Run(engine, sink);
}
}  // namespace dandelion::frontend
