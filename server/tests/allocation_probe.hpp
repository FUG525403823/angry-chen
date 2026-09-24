#pragma once
// S05 §5.6 / S06 §5.6：热路径零分配的计数缝。
// 计数与全局 new/delete 替换在 alloc_test.cpp 里（本测试二进制的唯一实现），
// 其他测试文件只读这里的接口，测量窗口内不做任何格式化输出。
#include <cstddef>

namespace ac::test {

// 进程启动至今的堆分配次数（含全部翻译单元；aligned / nothrow 变体一并计数）。
std::size_t allocationCount() noexcept;

// 测量窗口：构造时取快照，窗口内只做热路径调用，结束后读 since()。
struct AllocationScope {
  std::size_t start;
  AllocationScope() noexcept : start(allocationCount()) {}
  std::size_t since() const noexcept { return allocationCount() - start; }
};

}  // namespace ac::test
