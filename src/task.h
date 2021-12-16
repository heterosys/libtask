#ifndef TASK_LEVEL_PARALLELIZATION_H_
#define TASK_LEVEL_PARALLELIZATION_H_

#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>

#include <glog/logging.h>

#include "task/mmap.h"
#include "task/stream.h"
#include "task/traits.h"
#include "task/util.h"
#include "task/vec.h"

namespace task {

namespace internal {

void *allocate(size_t length);
void deallocate(void *addr, size_t length);

template <typename T> struct invoker;

template <typename... Params> struct invoker<void (&)(Params...)> {
  template <typename... Args>
  static void invoke(bool detach, void (&f)(Params...), Args &&...args) {
    // std::bind creates a copy of args
    internal::schedule(detach, std::bind(f, accessor<Params, Args>::access(
                                                std::forward<Args>(args))...));
  }
};

} // namespace internal

/// Defines a parent task instantiating children task instances.
///
/// Canonical usage:
/// @code{.cpp}
///  task::task()
///    .invoke(...)
///    .invoke(...)
///    ...
///    ;
/// @endcode
///
/// A parent task itself does not do any computation. By default, a parent task
/// will not finish until all its children task instances finish. Such children
/// task instances are @a joined to their parent. The alternative is to @a
/// detach a child from the parent. If a child task instance is instantiated and
/// detached, the parent will no longer wait for the child task to finish.
/// Detached tasks are very useful when infinite loops can be used.
struct task {

  /// Constructs a @c task::task.
  task();
  task(task &&) = delete;
  task(const task &) = delete;
  ~task();

  task &operator=(task &&) = delete;
  task &operator=(const task &) = delete;

  /// Invokes a task and instantiates a child task instance.
  ///
  /// @param func Task function definition of the instantiated child.
  /// @param args Arguments passed to @c func.
  /// @return     Reference to the caller @c task::task.
  template <typename Func, typename... Args>
  task &invoke(Func &&func, Args &&...args) {
    return invoke<join>(std::forward<Func>(func), "",
                        std::forward<Args>(args)...);
  }

  /// Invokes a task and instantiates a child task instance with the given
  /// instatiation mode.
  ///
  /// @tparam mode Instatiation mode (@c join or @c detach).
  /// @param func  Task function definition of the instantiated child.
  /// @param args  Arguments passed to @c func.
  /// @return      Reference to the caller @c task::task.
  template <int mode, typename Func, typename... Args>
  task &invoke(Func &&func, Args &&...args) {
    return invoke<mode>(std::forward<Func>(func), "",
                        std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args, size_t name_size>
  task &invoke(Func &&func, const char (&name)[name_size], Args &&...args) {
    return invoke<join>(std::forward<Func>(func), name,
                        std::forward<Args>(args)...);
  }

  template <int mode, typename Func, typename... Args, size_t name_size>
  task &invoke(Func &&func, const char (&name)[name_size], Args &&...args) {
    internal::invoker<Func>::template invoke<Args...>(
        /* detach= */ mode < 0, std::forward<Func>(func),
        std::forward<Args>(args)...);
    return *this;
  }

  /// Invokes a task @c n times and instantiates @c n child task
  /// instances with the given instatiation mode.
  ///
  /// @tparam mode Instatiation mode (@c join or @c detach).
  /// @tparam n    Instatiation count.
  /// @param func  Task function definition of the instantiated child.
  /// @param args  Arguments passed to @c func.
  /// @return      Reference to the caller @c task::task.
  template <int mode, int n, typename Func, typename... Args>
  task &invoke(Func &&func, Args &&...args) {
    return invoke<mode, n>(std::forward<Func>(func), "",
                           std::forward<Args>(args)...);
  }

  template <int mode, int n, typename Func, typename... Args, size_t name_size>
  task &invoke(Func &&func, const char (&name)[name_size], Args &&...args) {
    for (int i = 0; i < n; ++i) {
      invoke<mode>(std::forward<Func>(func), std::forward<Args>(args)...);
    }
    return *this;
  }
};

struct seq {
  int pos = 0;
};

namespace internal {

template <typename Param, typename Arg> struct accessor {
  static Param access(Arg &&arg) { return arg; }
};

template <typename T> struct accessor<T, seq> {
  static T access(seq &&arg) { return arg.pos++; }
};

} // namespace internal

// Host-only invoke that takes path to a bistream file as an argument. Returns
// the kernel time in nanoseconds.
template <typename Func, typename... Args>
inline task &invoke(Func &&f, Args &&...args) {
  return task().invoke(std::forward<Func>(f), std::forward<Args>(args)...);
}

template <typename T> struct aligned_allocator {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  template <typename U> void construct(U *ptr) {
    ::new (static_cast<void *>(ptr)) U;
  }
  template <class U, class... Args> void construct(U *ptr, Args &&...args) {
    ::new (static_cast<void *>(ptr)) U(std::forward<Args>(args)...);
  }
  T *allocate(size_t count) {
    return reinterpret_cast<T *>(internal::allocate(count * sizeof(T)));
  }
  void deallocate(T *ptr, std::size_t count) {
    internal::deallocate(ptr, count * sizeof(T));
  }
};

} // namespace task

#endif // TASK_LEVEL_PARALLELIZATION_H_
