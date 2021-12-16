#include <cassert>
#include <cstring>

#include <task.h>

// p x p PEs
const int p = 2;

// Handles kN x kN matrices maximum.
const int kN = 64; // Use fixed value for efficient hardware generation.

// Scatter n*n matrix into p*p blocks, each block.
void Scatter(task::mmap<const float> matrix_ptr, task::ostream<float> &block_00,
             task::ostream<float> &block_01, task::ostream<float> &block_10,
             task::ostream<float> &block_11) {
  const uint64_t kNumElems = (kN / p) * (kN / p);
  for (uint64_t i = 0; i < kNumElems; ++i) {
    block_00.write(*matrix_ptr);
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    block_01.write(*matrix_ptr);
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    block_10.write(*matrix_ptr);
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    block_11.write(*matrix_ptr);
    ++matrix_ptr;
  }
}

void Gather(task::mmap<float> matrix_ptr, task::istream<float> &block_00,
            task::istream<float> &block_01, task::istream<float> &block_10,
            task::istream<float> &block_11) {
  const uint64_t kNumElems = (kN / p) * (kN / p);
  for (uint64_t i = 0; i < kNumElems; ++i) {
    *matrix_ptr = block_00.read();
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    *matrix_ptr = block_01.read();
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    *matrix_ptr = block_10.read();
    ++matrix_ptr;
  }
  for (uint64_t i = 0; i < kNumElems; ++i) {
    *matrix_ptr = block_11.read();
    ++matrix_ptr;
  }
}

// Each PE processes n/p * n/p block of matrix.
void ProcElem(task::istream<float> &a_fifo, task::istream<float> &b_fifo,
              task::ostream<float> &c_fifo, task::ostream<float> &i_prev,
              task::istream<float> &i_next, task::ostream<float> &j_prev,
              task::istream<float> &j_next) {
  const uint64_t kNumElems = (kN / p) * (kN / p);
  [[task::parallel("cyclic", 32)]] float a[kN / p * kN / p];
  [[task::parallel("block", 32)]] float b[kN / p * kN / p];
  [[task::parallel("cyclic", 32)]] float c[kN / p * kN / p];

  // Initialize local a, b, and c.
  for (uint64_t i = 0; i < kNumElems; ++i) {
    a[i] = a_fifo.read();
    b[i] = b_fifo.read();
    c[i] = 0.f;
  }

  for (int l = 0; l < p; ++l) {
    for (uint64_t i = 0; i < kN / p; ++i) {
      for (uint64_t j = 0; j < kN / p; ++j) {
        float tmp = std::kill_dependency(c[i * (kN / p) + j]);
        [[unroll]] for (uint64_t k = 0; k < kN / p; ++k) {
          tmp += a[i * (kN / p) + k] * b[k * (kN / p) + j];
        }
        c[i * (kN / p) + j] = tmp;
      }
    }
    [[task::trip(kNumElems, kNumElems)]] //
    for (uint64_t a_wr = 0, b_wr = 0, a_rd = 0, b_rd = 0;
         a_wr < kNumElems || b_wr < kNumElems || a_rd < kNumElems ||
         b_rd < kNumElems;) {
      if (b_wr < kNumElems && i_prev.try_write(std::kill_dependency(b[b_wr])))
        ++b_wr;
      if (a_wr < kNumElems && j_prev.try_write(std::kill_dependency(a[a_wr])))
        ++a_wr;
      if (b_rd < b_wr && i_next.try_read(b[b_rd]))
        ++b_rd;
      if (a_rd < a_wr && j_next.try_read(a[a_rd]))
        ++a_rd;
    }
  }

  for (uint64_t i = 0; i < kNumElems; ++i) {
    c_fifo.write(c[i]);
  }
}

void Cannon(task::mmap<const float> a_vec, task::mmap<const float> b_vec,
            task::mmap<float> c_vec, uint64_t n) {
  assert(kN % p == 0);
  assert(n <= kN);

  task::stream<float, 2> a_00("a->PE00");
  task::stream<float, 2> a_01("a->PE01");
  task::stream<float, 2> a_10("a->PE10");
  task::stream<float, 2> a_11("a->PE11");
  task::stream<float, 2> b_00("b->PE00");
  task::stream<float, 2> b_01("b->PE01");
  task::stream<float, 2> b_10("b->PE10");
  task::stream<float, 2> b_11("b->PE11");
  task::stream<float, 2> c_00("c->PE00");
  task::stream<float, 2> c_01("c->PE01");
  task::stream<float, 2> c_10("c->PE10");
  task::stream<float, 2> c_11("c->PE11");
  task::stream<float, 8> fifo_00_01("PE00->PE01");
  task::stream<float, 8> fifo_01_00("PE01->PE00");
  task::stream<float, 8> fifo_10_11("PE10->PE11");
  task::stream<float, 8> fifo_11_10("PE11->PE10");
  task::stream<float, 8> fifo_00_10("PE00->PE10");
  task::stream<float, 8> fifo_10_00("PE10->PE00");
  task::stream<float, 8> fifo_01_11("PE01->PE11");
  task::stream<float, 8> fifo_11_01("PE11->PE01");

  task::parallel()
      .invoke(Scatter, a_vec, a_00, a_01, a_10, a_11)
      .invoke(Scatter, b_vec, b_00, b_01, b_10, b_11)
      .invoke(ProcElem, a_00, b_00, c_00, fifo_00_10, fifo_10_00, fifo_00_01,
              fifo_01_00)
      .invoke(ProcElem, a_01, b_01, c_01, fifo_01_11, fifo_11_01, fifo_01_00,
              fifo_00_01)
      .invoke(ProcElem, a_10, b_10, c_10, fifo_10_00, fifo_00_10, fifo_10_11,
              fifo_11_10)
      .invoke(ProcElem, a_11, b_11, c_11, fifo_11_01, fifo_01_11, fifo_11_10,
              fifo_10_11)
      .invoke(Gather, c_vec, c_00, c_01, c_10, c_11);
}
