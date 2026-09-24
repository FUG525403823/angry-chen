#pragma once
// 自研断言与用例注册框架（S01 §5.5 冻结契约）：只有 5 个宏、registerCase/runAll，不引第三方。
//
// 输出契约：每个用例恰好一行 —— 通过 "PASS <name>"、失败 "FAIL <name>: <file>:<line>: <expr>"
// （用例内首个失败点），末行 "TESTS <通过>/<总数>"，进程退出码 = 失败用例数。

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace ac::test {

using CaseFn = void (*)();

struct Case {
  const char* name;
  CaseFn fn;
};

constexpr std::size_t kMaxCases = 512;

namespace detail {

struct Registry {
  Case cases[kMaxCases]{};
  std::size_t count = 0;
};

inline Registry& registry() {
  static Registry instance;
  return instance;
}

struct CaseState {
  const char* name = nullptr;
  bool isFailed = false;
  const char* file = nullptr;
  int line = 0;
  std::string expr;
};

inline CaseState& state() {
  static CaseState instance;
  return instance;
}

}  // namespace detail

inline void registerCase(const char* name, CaseFn fn) {
  detail::Registry& registry = detail::registry();
  if (registry.count >= kMaxCases) {
    // 用例上限是框架自身的容量约束，绝不允许静默丢用例（会让 TESTS 统计撒谎）。
    std::fprintf(stderr, "ac::test: 用例数已达上限 %zu，%s 未注册\n", kMaxCases, name);
    return;
  }
  registry.cases[registry.count].name = name;
  registry.cases[registry.count].fn = fn;
  ++registry.count;
}

inline const char* currentCase() noexcept { return detail::state().name; }

inline void reportCheck(bool isOk, const char* expr, const char* file, int line) {
  if (isOk) return;
  detail::CaseState& state = detail::state();
  if (state.isFailed) return;  // 只记首个失败点，保证每用例一行
  state.isFailed = true;
  state.file = file;
  state.line = line;
  state.expr = expr;
}

inline void reportFail(const char* msg, const char* file, int line) {
  reportCheck(false, msg, file, line);
}

inline std::string formatPassLine(const char* name) { return std::string("PASS ") + name; }

inline std::string formatFailLine(const char* name, const char* file, int line, const char* expr) {
  return std::string("FAIL ") + name + ": " + file + ":" + std::to_string(line) + ": " + expr;
}

inline std::string formatSummaryLine(int passed, int total) {
  return "TESTS " + std::to_string(passed) + "/" + std::to_string(total);
}

inline bool matchesFilter(const char* name, const char* filter) {
  const std::string_view pattern = filter == nullptr ? std::string_view{} : std::string_view(filter);
  if (pattern.empty()) return true;
  return std::string_view(name).find(pattern) != std::string_view::npos;
}

inline bool near(double a, double b, double eps) {
  const double delta = a - b;
  return delta <= eps && -delta <= eps;
}

template <typename A, typename B>
inline void checkEq(const A& a, const B& b, const char* expr, const char* file, int line) {
  reportCheck(a == b, expr, file, line);
}

template <typename A, typename B, typename E>
inline void checkNear(A a, B b, E eps, const char* expr, const char* file, int line) {
  reportCheck(near(static_cast<double>(a), static_cast<double>(b), static_cast<double>(eps)), expr,
              file, line);
}

inline int runAll(int argc, char** argv) {
  const char* filter = "";
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i] == nullptr ? std::string_view{} : std::string_view(argv[i]);
    if (arg.rfind("--filter=", 0) == 0) filter = argv[i] + 9;
  }

  detail::Registry& registry = detail::registry();
  int passed = 0;
  int total = 0;
  for (std::size_t i = 0; i < registry.count; ++i) {
    const Case& item = registry.cases[i];
    if (!matchesFilter(item.name, filter)) continue;
    detail::CaseState& state = detail::state();
    state = detail::CaseState{};
    state.name = item.name;
    ++total;
    item.fn();
    if (state.isFailed) {
      std::printf("%s\n",
                  formatFailLine(item.name, state.file, state.line, state.expr.c_str()).c_str());
    } else {
      ++passed;
      std::printf("%s\n", formatPassLine(item.name).c_str());
    }
    std::fflush(stdout);
  }
  std::printf("%s\n", formatSummaryLine(passed, total).c_str());
  std::fflush(stdout);
  return total - passed;
}

}  // namespace ac::test

#define AC_TEST(name)                                 \
  static void name();                                 \
  namespace {                                         \
  [[maybe_unused]] const bool name##_acRegistered =   \
      (::ac::test::registerCase(#name, &name), true); \
  }                                                   \
  static void name()

#define AC_CHECK(cond) ::ac::test::reportCheck(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define AC_CHECK_EQ(a, b) ::ac::test::checkEq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define AC_CHECK_NEAR(a, b, eps) \
  ::ac::test::checkNear((a), (b), (eps), #a " ~= " #b, __FILE__, __LINE__)
#define AC_FAIL(msg) ::ac::test::reportFail((msg), __FILE__, __LINE__)
