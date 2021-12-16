#include <cstdint>

#include <task.h>

void Add(uint64_t n_int, task::istream<float> &a_int,
         task::istream<float> &b_int, task::ostream<float> &c_int) {
  float a, b;
  bool a_succeed = false, b_succeed = false;
  uint64_t read = 0;

  while (read < n_int) {
    if (!a_succeed) {
      a = a_int.read(a_succeed);
    }
    if (!b_succeed) {
      b = b_int.read(b_succeed);
    }
    if (a_succeed && b_succeed) {
      c_int.write(a + b);
      a_succeed = b_succeed = false;
      read += 1;
    }
  }
  c_int.close();
}

void Compute(uint64_t n_ext, task::istream<float> &a_ext,
             task::istream<float> &b_ext, task::ostream<float> &c_ext) {
  task::parallel().invoke(Add, n_ext, a_ext, b_ext, c_ext);
}

void Mmap2Stream_internal(task::async_mmap<float> mmap_int, uint64_t n_int,
                          task::ostream<float> &stream_int) {

  for (uint64_t rq_i = 0, rs_i = 0; rs_i < n_int;) {
    float elem;
    if (rq_i < n_int && rq_i < rs_i + 50 && // TODO: resolve the DRAM lock issue
        mmap_int.read_addr.try_write(rq_i))
      rq_i++;
    if (mmap_int.read_data.try_read(elem)) {
      stream_int.write(elem);
      rs_i++;
    }
  }

  stream_int.close();
}

void Mmap2Stream(task::mmap<float> mmap_ext, uint64_t n_ext,
                 task::ostream<float> &stream_ext) {
  task::parallel().invoke(Mmap2Stream_internal, mmap_ext, n_ext, stream_ext);
}

void Load(task::mmap<float> a_array, task::mmap<float> b_array,
          task::ostream<float> &a_stream, task::ostream<float> &b_stream,
          uint64_t n) {
  task::parallel()
      .invoke(Mmap2Stream, a_array, n, a_stream)
      .invoke(Mmap2Stream, b_array, n, b_stream);
}

void Store(task::istream<float> &stream, task::mmap<float> mmap, uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) {
    mmap[i] = stream.read();
  }
}

void VecAddNested(task::mmap<float> a_array, task::mmap<float> b_array,
                  task::mmap<float> c_array, uint64_t n) {
  task::stream<float, 8> a_stream("a");
  task::stream<float, 8> b_stream("b");
  task::stream<float, 8> c_stream("c");

  task::parallel()
      .invoke(Load, a_array, b_array, a_stream, b_stream, n)
      .invoke(Compute, n, a_stream, b_stream, c_stream)
      .invoke(Store, c_stream, c_array, n);
}
