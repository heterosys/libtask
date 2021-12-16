#include <cstdint>

#include <task.h>

void Add(task::istream<float> &a, task::istream<float> &b,
         task::ostream<float> &c, uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) {
    c.write(a.read() + b.read());
  }
}

void Mmap2Stream(task::mmap<const float> mmap, uint64_t n,
                 task::ostream<float> &stream) {
  for (uint64_t i = 0; i < n; ++i) {
    stream.write(mmap[i]);
  }
}

void Stream2Mmap(task::istream<float> &stream, task::mmap<float> mmap,
                 uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) {
    mmap[i] = stream.read();
  }
}

void VecAdd(task::mmap<const float> a, task::mmap<const float> b,
            task::mmap<float> c, uint64_t n) {
  task::stream<float, 2> a_q("a");
  task::stream<float, 2> b_q("b");
  task::stream<float, 2> c_q("c");

  task::task()
      .invoke(Mmap2Stream, a, n, a_q)
      .invoke(Mmap2Stream, b, n, b_q)
      .invoke(Add, a_q, b_q, c_q, n)
      .invoke(Stream2Mmap, c_q, c, n);
}
