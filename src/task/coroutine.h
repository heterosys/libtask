#ifndef task_COROUTINE_H_
#define task_COROUTINE_H_

#include <functional>
#include <string>

namespace task {

enum mode { join, detach };

namespace internal {
void schedule(mode m, const std::function<void()> &);
void yield(const std::string &msg);
} // namespace internal

} // namespace task

#endif // task_COROUTINE_H_
