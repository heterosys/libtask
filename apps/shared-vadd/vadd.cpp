#include <cstdint>

#include <task.h>

using task::istream;
using task::mmap;
using task::ostream;
using task::parallel;
using task::stream;

void Add(istream<float> &a, istream<float> &b, ostream<float> &c) {
  TASK_WHILE_NEITHER_EOT(a, b) { c.write(a.read(nullptr) + b.read(nullptr)); }
  c.close();
}

void Mmap2Stream(mmap<float> mmap, int offset, uint64_t n,
                 ostream<float> &stream) {
  for (uint64_t i = 0; i < n; ++i) {
    stream.write(mmap[n * offset + i]);
  }
  stream.close();
}

void Stream2Mmap(istream<float> &stream, mmap<float> mmap, int offset,
                 uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) {
    mmap[n * offset + i] = stream.read();
  }
}

void Load(mmap<float> srcs, uint64_t n, ostream<float> &a, ostream<float> &b) {
  parallel()
      .invoke(Mmap2Stream, srcs, 0, n, a)
      .invoke(Mmap2Stream, srcs, 1, n, b);
}

void Store(istream<float> &stream, mmap<float> mmap, uint64_t n) {
  parallel().invoke(Stream2Mmap, stream, mmap, 2, n);
}

void VecAdd(mmap<float> data, uint64_t n) {
  stream<float, 8> a("a");
  stream<float, 8> b("b");
  stream<float, 8> c("c");

  parallel()
      .invoke(Load, data, n, a, b)
      .invoke(Add, a, b, c)
      .invoke(Store, c, data, n);
}

void VecAddShared(mmap<float> elems, uint64_t n) {
  parallel().invoke(VecAdd, elems, n);
}
