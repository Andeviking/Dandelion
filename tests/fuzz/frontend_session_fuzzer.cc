import std;
import dandelion.frontend;

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  auto session = dandelion::frontend::Session::FromText(
      std::string_view(reinterpret_cast<const char*>(data), size));
  if (!session) return 0;

  while (auto command = session->ReadCommand()) {
    if (!*command) break;
  }
  return 0;
}
