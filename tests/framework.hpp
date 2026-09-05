#pragma once
#include <vector>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace cptest {

struct TestCase { const char* name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};
inline int& failures() { static int f = 0; return f; }
inline const char*& currentTestRef() { static const char* t = ""; return t; }
inline const char* currentTest() { return currentTestRef(); }

}  // namespace cptest

#define CP_TEST(name) \
  static void cp_test_##name(); \
  static ::cptest::Registrar cp_reg_##name(#name, cp_test_##name); \
  static void cp_test_##name()

#define CHECK(cond) do { if (!(cond)) { \
  std::printf("CHECK FAILED [%s] %s:%d  %s\n", ::cptest::currentTest(), __FILE__, __LINE__, #cond); \
  ++::cptest::failures(); } } while (0)

#define REQUIRE(cond) do { if (!(cond)) { \
  std::printf("REQUIRE FAILED [%s] %s:%d  %s\n", ::cptest::currentTest(), __FILE__, __LINE__, #cond); \
  ++::cptest::failures(); return; } } while (0)

#define CP_MAIN() \
  int main(int argc, char** argv) { \
    (void)argc; (void)argv; \
    int ran = 0; \
    for (auto& tc : ::cptest::registry()) { \
      ::cptest::currentTestRef() = tc.name; \
      tc.fn(); \
      ++ran; \
    } \
    if (::cptest::failures() == 0) { std::printf("PASS: %d tests\n", ran); return 0; } \
    std::printf("FAIL: %d assertion failures across %d tests\n", ::cptest::failures(), ran); \
    return 1; \
  }
