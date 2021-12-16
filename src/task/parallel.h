#ifndef TASK_PARALLEL_H_
#define TASK_PARALLEL_H_

#include <functional>
#include <string>

namespace task {

enum mode { join, detach };

namespace internal {

void schedule(mode m, const std::function<void()> &);
void yield(const std::string &msg);

struct seq {
  int pos = 0;
};

template <typename Param, typename Arg> struct accessor {
  static Param access(Arg &&arg) { return arg; }
};

template <typename T> struct accessor<T, seq> {
  static T access(seq &&arg) { return arg.pos++; }
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
  template <int n = 1, mode m = join, typename... Params, typename... Args>
  parallel &invoke(void (&func)(Params...), Args &&...args) {
    for (int i = 0; i < n; ++i) {
      internal::schedule(
          m, std::bind(func, internal::accessor<Params, Args>::access(
                                 std::forward<Args>(args))...));
    }
    return *this;
  }
};

} // namespace task

#endif // TASK_PARALLEL_H_
