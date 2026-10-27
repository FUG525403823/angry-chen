#include "tiny_test.hpp"

namespace {
void passes() {}
void fails() { AC_CHECK(false); }
}

int main(int argc, char** argv) {
  const bool isFailure = argc > 1 && std::strcmp(argv[1], "failure") == 0;
  ac::test::registerCase("probe", isFailure ? &fails : &passes);
  if (argc > 1 && std::strcmp(argv[1], "duplicate") == 0) {
    ac::test::registerCase("probe", &passes);
  }
  std::string names[ac::test::kMaxCases];
  if (argc > 1 && std::strcmp(argv[1], "overflow") == 0) {
    for (std::size_t i = 0; i < ac::test::kMaxCases; ++i) {
      names[i] = "extra_" + std::to_string(i);
      ac::test::registerCase(names[i].c_str(), &passes);
    }
  }
  return ac::test::runAll(argc, argv);
}
