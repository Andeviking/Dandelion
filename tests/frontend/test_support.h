#ifndef DANDELION_TESTS_FRONTEND_TEST_SUPPORT_H
#define DANDELION_TESTS_FRONTEND_TEST_SUPPORT_H

#include <gtest/gtest.h>

import std;
import dandelion.frontend;

namespace dandelion::frontend::testing {

template <class T>
T Require(Outcome<T> result) {
  if (!result) {
    ADD_FAILURE() << result.error().where.begin.line << ":"
                  << result.error().where.begin.column << " "
                  << result.error().message;
    throw std::runtime_error("Unexpected frontend diagnostic");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}

template <class T>
T Require(dandelion::Result<T> result) {
  if (!result) {
    ADD_FAILURE() << "Unexpected node error "
                  << static_cast<int>(result.error().code);
    throw std::runtime_error("Unexpected node error");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}

inline Outcome<std::optional<TypedCommand>> Next(Session& session) {
  auto header = session.NextSyntax();
  if (!header) return std::unexpected(header.error());
  if (!*header) return std::nullopt;
  auto applied = session.Apply();
  if (!applied) return std::unexpected(applied.error());
  return std::move(*applied);
}

inline TypedCommand RequireNext(
    Session& session, CommandKind kind,
    std::source_location location = std::source_location::current()) {
  auto command = Require(Next(session));
  if (!command || command->header.kind != kind) {
    ADD_FAILURE_AT(location.file_name(), static_cast<int>(location.line()))
        << "Expected command kind " << static_cast<int>(kind)
        << ", actual kind "
        << (command ? static_cast<int>(command->header.kind) : -1)
        << " (-1 means end of input)";
    throw std::runtime_error("Expected typed command");
  }
  return std::move(*command);
}

inline Outcome<std::shared_ptr<const QueryContext>> Parse(
    std::string_view text) {
  auto made = Session::FromText(text);
  if (!made) return std::unexpected(made.error());
  auto session = std::move(*made);
  std::shared_ptr<const QueryContext> query;
  for (;;) {
    auto command = Next(session);
    if (!command) return std::unexpected(command.error());
    if (!*command) return query;
    if ((*command)->query) query = (*command)->query;
  }
}

inline std::shared_ptr<const QueryContext> RequireQuery(
    Outcome<std::shared_ptr<const QueryContext>> result,
    std::source_location location = std::source_location::current()) {
  auto query = Require(std::move(result));
  if (!query) {
    ADD_FAILURE_AT(location.file_name(), static_cast<int>(location.line()))
        << "Expected a sealed query from check-sat";
    throw std::runtime_error("Missing sealed query");
  }
  return query;
}

inline std::shared_ptr<const QueryContext> RequireCheck(
    Session& session,
    std::source_location location = std::source_location::current()) {
  auto command = RequireNext(session, CommandKind::kCheckSat, location);
  return RequireQuery(std::move(command.query), location);
}

template <class T>
void ExpectError(const Outcome<T>& result, DiagnosticCode expected) {
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, expected) << result.error().message;
}

inline NodeView Argument(const Node& node, std::size_t index) {
  return node.Borrow().GetChildren()[index];
}

inline void ExpectChildren(NodeView node,
                           std::initializer_list<NodeView> expected) {
  const auto children = node.GetChildren();
  ASSERT_EQ(children.size(), expected.size());
  std::size_t index = 0;
  for (auto child : expected) EXPECT_EQ(children[index++], child);
}

inline std::string IntegerValue(NodeView node) {
  return node.GetValue().value().GetInteger().value().ToDecimal();
}

inline DeclView Applied(NodeView node) {
  return node.GetDeclaration().value().GetDeclaration();
}

inline Outcome<TypedCommand> Values(
    std::string_view input, std::string_view setup = "(set-logic ALL)",
    std::shared_ptr<const QueryContext>* query = nullptr) {
  const auto text = std::string("(set-option :produce-models true)") +
                    std::string(setup) + "(check-sat)(get-value (" +
                    std::string(input) + "))";
  auto made = Session::FromText(text);
  if (!made) return std::unexpected(made.error());
  auto session = std::move(*made);
  std::shared_ptr<const QueryContext> sealed;
  for (;;) {
    auto command = Next(session);
    if (!command) return std::unexpected(command.error());
    if (!*command) {
      ADD_FAILURE() << "Expected get-value before EOF for " << input;
      throw std::runtime_error("Missing get-value command");
    }
    if ((*command)->header.kind == CommandKind::kCheckSat)
      sealed = RequireQuery(std::move((*command)->query));
    if ((*command)->header.kind == CommandKind::kGetValue) {
      if (!sealed) {
        ADD_FAILURE() << "Expected check-sat before get-value for " << input;
        throw std::runtime_error("Missing sealed query");
      }
      if (query) *query = std::move(sealed);
      return std::move(**command);
    }
  }
}

}  // namespace dandelion::frontend::testing
#endif
