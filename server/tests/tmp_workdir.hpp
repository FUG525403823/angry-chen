#pragma once
// 测试用的临时目录（S13）：容器/构建机上都不写仓库树，析构时整棵删掉。
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace ac::test {

inline std::string makeTempDirName(const char* tag) {
  static int counter = 0;
  ++counter;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d", counter);
  return std::string("ac_test_") + tag + "_" + buffer;
}

class TempDir {
 public:
  explicit TempDir(const char* tag)
      : path_(std::filesystem::temp_directory_path() / makeTempDirName(tag)) {
    std::error_code code;
    std::filesystem::remove_all(path_, code);
    std::filesystem::create_directories(path_, code);
    isReady_ = !code;
  }

  ~TempDir() {
    std::error_code code;
    std::filesystem::remove_all(path_, code);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  bool isReady() const noexcept { return isReady_; }
  const std::filesystem::path& path() const noexcept { return path_; }
  std::string str() const { return path_.string(); }
  std::string file(const char* name) const { return (path_ / name).string(); }

 private:
  std::filesystem::path path_;
  bool isReady_ = false;
};

}  // namespace ac::test
