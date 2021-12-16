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

template <typename T> struct invoker;

template <typename... Params> struct invoker<void (&)(Params...)> {
  template <typename... Args>
  static void invoke(mode m, void (&f)(Params...), Args &&...args) {
    // std::bind creates a copy of args
    internal::schedule(m, std::bind(f, accessor<Params, Args>::access(
                                           std::forward<Args>(args))...));
  }
};

} // namespace internal
/// Defines a parallel task instantiating children task instances.
///
/// Canonical usage:
/// @code{.cpp}
///  task::parallel()
///    .invoke(...)
///    .invoke(...)
///    ...
///    ;
/// @endcode
///
/// A parallel task itself does not do any computation. By default, a parallel
/// task will not finish until all its children task instances finish. Such
/// children task instances are @a joined to their parent. The alternative is to
/// @a detach a child from the parallel task. If a child task instance is
/// instantiated and detached, the parent will no longer wait for the child task
/// to finish. Detached tasks are very useful when infinite loops can be used.
struct parallel {

  /// Constructs a @c task::parallel.
  parallel();
  parallel(parallel &&) = delete;
  parallel(const parallel &) = delete;
  ~parallel();

  parallel &operator=(parallel &&) = delete;
  parallel &operator=(const parallel &) = delete;

  /// Invokes a task @c n times and instantiates @c n child task
  /// instances with the given instatiation mode.
  ///
  /// @tparam n    Instatiation count.
  /// @tparam m    Instatiation mode (@c join or @c detach).
  /// @param func  Task function definition of the instantiated child.
  /// @param args  Arguments passed to @c func.
  /// @return      Reference to the caller @c task::parallel.
  template <int n = 1, mode m = join, typename Func, typename... Args>
  parallel &invoke(Func &&func, Args &&...args) {
    for (int i = 0; i < n; ++i) {
      internal::invoker<Func>::template invoke<Args...>(
          m, std::forward<Func>(func), std::forward<Args>(args)...);
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

} // namespace task

#endif // TASK_LEVEL_PARALLELIZATION_H_
