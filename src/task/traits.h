#ifndef TASK_SYNTHESIZABLE_TRAITS_H_
#define TASK_SYNTHESIZABLE_TRAITS_H_

#include "task/stream.h"

namespace task {

template <typename T> struct elem_type {};
template <typename T> struct elem_type<istream<T> &> { using type = T; };
template <typename T> struct elem_type<ostream<T> &> { using type = T; };
template <typename T, uint64_t S> struct elem_type<istreams<T, S> &> {
  using type = T;
};
template <typename T, uint64_t S> struct elem_type<ostreams<T, S> &> {
  using type = T;
};

} // namespace task

#define TASK_ELEM_TYPE(variable)                                               \
  typename ::task::elem_type<decltype(variable)>::type

#endif // TASK_SYNTHESIZABLE_TRAITS_H_
