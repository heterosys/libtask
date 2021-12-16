#ifndef task_MMAP_H_
#define task_MMAP_H_

#include <cstddef>

#include <queue>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "task/coroutine.h"

#include "task/stream.h"
#include "task/vec.h"

namespace task {

namespace internal {

template <typename Param, typename Arg> struct accessor;

} // namespace internal

template <typename T> class async_mmap;

/// Defines a view of a piece of consecutive memory with synchronous random
/// accesses.
template <typename T> class mmap {
public:
  /// Constructs a @c task::mmap with unknown size.
  ///
  /// @param ptr Pointer to the start of the mapped memory.
  explicit mmap(T *ptr) : ptr_{ptr}, size_{0} {}

  /// Constructs a @c task::mmap with the given @c size.
  ///
  /// @param ptr  Pointer to the start of the mapped memory.
  /// @param size Size of the mapped memory (in unit of element count).
  mmap(T *ptr, uint64_t size) : ptr_{ptr}, size_{size} {}

  /// Constructs a @c task::mmap from the given @c container.
  ///
  /// @param container Container holding a @c task::mmap. Must implement
  ///                  @c data() and @c size().
  template <typename Container>
  explicit mmap(Container &container)
      : ptr_{container.data()}, size_{container.size()} {}

  /// Implicitly casts to a regular pointer.
  ///
  /// @c task::mmap should be used just like a pointer in the kernel.
  operator T *() { return ptr_; }

  /// Increments the start of the mapped memory.
  ///
  /// @return The incremented @c task::mmap.
  mmap &operator++() {
    ++ptr_;
    return *this;
  }

  /// Decrements the start of the mapped memory.
  ///
  /// @return The decremented @c task::mmap.
  mmap &operator--() {
    --ptr_;
    return *this;
  }

  /// Increments the start of the mapped memory.
  ///
  /// @return The @c task::mmap before incrementation.
  mmap operator++(int) { return mmap(ptr_++); }

  /// Decrements the start of the mapped memory.
  ///
  /// @return The @c task::mmap before decrementation.
  mmap operator--(int) { return mmap(ptr_--); }

  /// Retrieves the start of the mapped memory.
  ///
  /// This should be used on the host only.
  ///
  /// @return The start of the mapped memory.
  T *get() const { return ptr_; }

  /// Retrieves the size of the mapped memory.
  ///
  /// This should be used on the host only.
  ///
  /// @return The size of the mapped memory (in unit of element count).
  uint64_t size() const { return size_; }

  /// Reinterprets the element type of the mapped memory as
  /// <tt>task::vec_t<T, N></tt>.
  ///
  /// This should be used on the host only.
  /// The size of mapped memory must be a multiple of @c N.
  ///
  /// @tparam N  Vector length of the new element type.
  /// @return @c task::mmap of the same piece of memory but of type
  ///         <tt>task::vec_t<T, N></tt>.
  template <uint64_t N> mmap<vec_t<T, N>> vectorized() const {
    CHECK_EQ(size_ % N, 0) << "size must be a multiple of N";
    return mmap<vec_t<T, N>>(reinterpret_cast<vec_t<T, N> *>(ptr_), size_ / N);
  }

  /// Reinterprets the element type of the mapped memory as @c U.
  ///
  /// This should be used on the host only.
  /// Both @c T and @c U must have standard layout.
  /// The host memory pointer must be properly aligned.
  /// The size of mapped memory must be a multiple of @c sizeof(U)/sizeof(T).
  ///
  /// @tparam U  The new element type.
  /// @return @c task::mmap<U> of the same piece of memory.
  template <typename U> mmap<U> reinterpret() const {
    static_assert(std::is_standard_layout<T>::value,
                  "T must have standard layout");
    static_assert(std::is_standard_layout<U>::value,
                  "U must have standard layout");

    if (sizeof(U) > sizeof(T)) {
      constexpr auto N = sizeof(U) / sizeof(T);
      CHECK_EQ(size() % N, 0) << "size must be a multiple of N = " << N;
    }
    CHECK_EQ(reinterpret_cast<size_t>(get()) % alignof(U), 0)
        << "pointer must be " << alignof(U) << "-byte aligned";
    return mmap<U>(reinterpret_cast<U *>(get()),
                   size() * sizeof(T) / sizeof(U));
  }

protected:
  T *ptr_;
  uint64_t size_;
};

/// Defines a view of a piece of consecutive memory with asynchronous random
/// accesses.
template <typename T> class async_mmap : public mmap<T> {
public:
  /// Type of the addresses.
  using addr_t = int64_t;

  /// Type of the write responses.
  using resp_t = uint8_t;

private:
  using super = mmap<T>;

  task::stream<addr_t, 64> read_addr_q_{"read_addr"};
  task::stream<T, 64> read_data_q_{"read_data"};
  task::stream<addr_t, 64> write_addr_q_{"write_addr"};
  task::stream<T, 64> write_data_q_{"write_data"};
  task::stream<resp_t, 64> write_resp_q_{"write_resp"};

  // Only convert when scheduled.
  async_mmap(const super &mem)
      : super(mem), read_addr(read_addr_q_), read_data(read_data_q_),
        write_addr(write_addr_q_), write_data(write_data_q_),
        write_resp(write_resp_q_) {}

  // Must be operated via the read/write addr/data stream APIs.
  operator T *() { return super::ptr_; }

  // Dereference not permitted.
  T &operator[](std::size_t idx) { return super::ptr_[idx]; }
  const T &operator[](std::size_t idx) const { return super::ptr_[idx]; }
  T &operator*() { return *super::ptr_; }
  const T &operator*() const { return *super::ptr_; }

  // Increment / decrement not permitted.
  T &operator++() { return *++super::ptr_; }
  T &operator--() { return *--super::ptr_; }
  T operator++(int) { return *super::ptr_++; }
  T operator--(int) { return *super::ptr_--; }

  // Arithmetic not permitted.
  async_mmap<T> operator+(std::ptrdiff_t diff) { return super::ptr_ + diff; }
  async_mmap<T> operator-(std::ptrdiff_t diff) { return super::ptr_ - diff; }
  std::ptrdiff_t operator-(async_mmap<T> ptr) { return super::ptr_ - ptr; }

public:
  /// Provides access to the <i>read address</i> channel.
  ///
  /// Each value written to this channel triggers an asynchronous memory read
  /// request. Consecutive requests may be coalesced into a long burst request.
  task::ostream<addr_t> read_addr;

  /// Provides access to the read data channel.
  ///
  /// Each value read from this channel represents the data retrieved from the
  /// underlying memory system.
  task::istream<T> read_data;

  /// Provides access to the write address channel.
  ///
  /// Each value written to this channel triggers an asynchronous memory write
  /// request. Consecutive requests may be coalesced into a long burst request.
  task::ostream<addr_t> write_addr;

  /// Provides access to the write data channel.
  ///
  /// Each value written to this channel supplies data to the memory write
  /// request.
  task::ostream<T> write_data;

  /// Provides access to the write response channel.
  ///
  /// Each value read from this channel represents the data count acknowledged
  /// by the underlying memory system.
  task::istream<resp_t> write_resp;

  void operator()() {
    int16_t write_count = 0;
    for (;;) {
      if (!read_addr_q_.empty() && !read_data_q_.full()) {
        const auto addr = read_addr_q_.read();
        CHECK_GE(addr, 0);
        if (addr != 0) {
          CHECK_LT(addr, this->size_);
        }
        read_data_q_.write(this->ptr_[addr]);
      }
      if (write_count != 256 && !write_addr_q_.empty() &&
          !write_data_q_.empty()) {
        const auto addr = write_addr_q_.read();
        CHECK_GE(addr, 0);
        if (addr != 0) {
          CHECK_LT(addr, this->size_);
        }
        this->ptr_[addr] = write_data_q_.read();
        ++write_count;
      } else if (write_count > 0 &&
                 this->write_resp_q_.try_write(resp_t(write_count - 1))) {
        CHECK_LE(write_count, 256);
        write_count = 0;
      }
    }
  }

  static async_mmap schedule(super mem) {
    // a copy of async_mem is stored in std::function<void()>
    async_mmap async_mem(mem);
    internal::schedule(detach, async_mem);
    return async_mem;
  }
};

/// Defines an array of @c task::mmap.
template <typename T, uint64_t S> class mmaps {
protected:
  std::vector<mmap<T>> mmaps_;

public:
  /// Constructs a @c task::mmap array from the given @c pointers and @c sizes.
  ///
  /// @param ptrs  Pointers to the start of the array of mapped memory.
  /// @param sizes Sizes of each mapped memory (in unit of element count).
  template <typename PtrContainer, typename SizeContainer>
  mmaps(const PtrContainer &pointers, const SizeContainer &sizes) {
    for (uint64_t i = 0; i < S; ++i) {
      mmaps_.emplace_back(pointers[i], sizes[i]);
    }
  }

  /// Constructs a @c task::mmap array from the given @c container.
  ///
  /// @param container Container holding an array of @c task::mmap. @c container
  ///                  must implement @c operator[] that returns a container
  ///                  suitable for constructing a @c task::mmap.
  template <typename Container> explicit mmaps(Container &container) {
    for (uint64_t i = 0; i < S; ++i) {
      mmaps_.emplace_back(container[i]);
    }
  }

  // defaultd copy constructor
  mmaps(const mmaps &) = default;
  // default move constructor
  mmaps(mmaps &&) = default;
  // defaultd copy assignment operator
  mmaps &operator=(const mmaps &) = default;
  // default move assignment operator
  mmaps &operator=(mmaps &&) = default;

  /// References a @c task::mmap in the array.
  mmap<T> &operator[](int idx) { return mmaps_[idx]; };

  template <uint64_t offset, uint64_t length> mmaps<T, length> slice() {
    static_assert(offset + length < S, "invalid slice");
    mmaps<T, length> result;
    for (uint64_t i = 0; i < length; ++i) {
      result.mmaps_[i] = this->mmaps_[offset + i];
    }
    return result;
  }

  /// Reinterprets the element type of each mapped memory as
  /// <tt>task::vec_t<T, N></tt>.
  ///
  /// This should be used on the host only.
  /// The size of each mapped memory must be a multiple of @c N.
  ///
  /// @tparam N  Vector length of the new element type.
  /// @return @c task::mmap of the same pieces of memory but of type
  ///         <tt>task::vec_t<T, N></tt>.
  template <uint64_t N> mmaps<vec_t<T, N>, S> vectorized() const {
    std::array<vec_t<T, N> *, S> ptrs;
    std::array<uint64_t, S> sizes;
    for (uint64_t i = 0; i < S; ++i) {
      CHECK_EQ(mmaps_[i].size() % N, 0)
          << "size[" << i << "] must be a multiple of N";
      ptrs[i] = reinterpret_cast<vec_t<T, N> *>(mmaps_[i].get());
      sizes[i] = mmaps_[i].size() / N;
    }
    return mmaps<vec_t<T, N>, S>(ptrs, sizes);
  }

  /// Reinterprets the element type of each mapped memory as @c U.
  ///
  /// This should be used on the host only.
  /// Both @c T and @c U must have standard layout.
  /// The host memory pointers must be properly aligned.
  /// The size of each mapped memory must be a multiple of
  /// @c sizeof(U)/sizeof(T).
  ///
  /// @tparam U  The new element type.
  /// @return <tt>task::mmaps<U, S></tt> of the same pieces of memory.
  template <typename U> mmaps<U, S> reinterpret() const {
    static_assert(std::is_standard_layout<T>::value,
                  "T must have standard layout");
    static_assert(std::is_standard_layout<U>::value,
                  "U must have standard layout");

    std::array<U *, S> ptrs;
    std::array<uint64_t, S> sizes;
    for (uint64_t i = 0; i < S; ++i) {
      if (sizeof(U) > sizeof(T)) {
        constexpr auto N = sizeof(U) / sizeof(T);
        CHECK_EQ(mmaps_[i].size() % N, 0)
            << "size[" << i << "] must be a multiple of N = " << N;
      }
      CHECK_EQ(reinterpret_cast<size_t>(mmaps_[i].get()) % alignof(U), 0)
          << "pointer[" << i << "] must be " << alignof(U) << "-byte aligned";
      ptrs[i] = reinterpret_cast<U *>(mmaps_[i].get());
      sizes[i] = mmaps_[i].size() * sizeof(T) / sizeof(U);
    }
    return mmaps<U, S>(ptrs, sizes);
  }

private:
  template <typename Param, typename Arg> friend struct internal::accessor;

  int access_pos_ = 0;

  mmap<T> access() {
    LOG_IF(WARNING, access_pos_ >= S)
        << "invocation #" << access_pos_ << " accesses mmaps["
        << access_pos_ % S << "]";
    return mmaps_[access_pos_++ % S];
  }
};

// Host-only mmap types that must have correct size.
#define task_DEFINE_MMAP(tag)                                                  \
  template <typename T> class tag##_mmap : public mmap<T> {                    \
    tag##_mmap(T *ptr) : mmap<T>(ptr) {}                                       \
                                                                               \
  public:                                                                      \
    using mmap<T>::mmap;                                                       \
    tag##_mmap(const mmap<T> &base) : mmap<T>(base) {}                         \
                                                                               \
    template <uint64_t N> tag##_mmap<vec_t<T, N>> vectorized() const {         \
      return mmap<T>::template vectorized<N>();                                \
    }                                                                          \
    template <typename U> tag##_mmap<U> reinterpret() const {                  \
      return mmap<T>::template reinterpret<U>();                               \
    }                                                                          \
  }
task_DEFINE_MMAP(placeholder);
task_DEFINE_MMAP(read_only);
task_DEFINE_MMAP(write_only);
task_DEFINE_MMAP(read_write);
#undef task_DEFINE_MMAP
#define task_DEFINE_MMAPS(tag)                                                 \
  template <typename T, uint64_t S> class tag##_mmaps : public mmaps<T, S> {   \
    tag##_mmaps(const std::array<T *, S> &ptrs) : mmaps<T, S>(ptrs){};         \
                                                                               \
  public:                                                                      \
    using mmaps<T, S>::mmaps;                                                  \
    tag##_mmaps(const mmaps<T, S> &base) : mmaps<T, S>(base) {}                \
                                                                               \
    template <uint64_t N> tag##_mmaps<vec_t<T, N>, S> vectorized() const {     \
      return mmaps<T, S>::template vectorized<N>();                            \
    }                                                                          \
    template <typename U> tag##_mmaps<U, S> reinterpret() const {              \
      return mmaps<T, S>::template reinterpret<U>();                           \
    }                                                                          \
  }
task_DEFINE_MMAPS(placeholder);
task_DEFINE_MMAPS(read_only);
task_DEFINE_MMAPS(write_only);
task_DEFINE_MMAPS(read_write);
#undef task_DEFINE_MMAPS

namespace internal {

template <typename T> struct accessor<async_mmap<T>, mmap<T> &> {
  static async_mmap<T> access(mmap<T> &arg) {
    return async_mmap<T>::schedule(arg);
  }
};

template <typename T, uint64_t S> struct accessor<mmap<T>, mmaps<T, S> &> {
  static mmap<T> access(mmaps<T, S> &arg) { return arg.access(); }
};

template <typename T, uint64_t S>
struct accessor<async_mmap<T>, mmaps<T, S> &> {
  static async_mmap<T> access(mmaps<T, S> &arg) {
    return async_mmap<T>::schedule(arg.access());
  }
};

template <typename T> struct accessor<mmap<T>, mmap<T>> {
  static_assert(!std::is_same<T, T>::value,
                "must use one of "
                "placeholder_mmap/read_only_mmap/write_only_mmap/"
                "read_write_mmap in task::invoke");
};
template <typename T, int64_t S> struct accessor<mmaps<T, S>, mmaps<T, S>> {
  static_assert(!std::is_same<T, T>::value,
                "must use one of "
                "placeholder_mmaps/read_only_mmaps/write_only_mmaps/"
                "read_write_mmaps in task::invoke");
};

} // namespace internal

} // namespace task

#endif // task_MMAP_H_
