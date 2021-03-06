#include <chrono>
#include <iostream>
#include <vector>

#include <task.h>

#include "bandwidth.h"

void Bandwidth(task::mmaps<Elem, kBankCount> chan, uint64_t n, uint64_t flags);

int main(int argc, char *argv[]) {
  const uint64_t n = argc > 1 ? atoll(argv[1]) : 1024 * 1024;
  const uint64_t flags = argc > 2 ? atoll(argv[2]) : 6LL;

  std::vector<float> chan[kBankCount];
  for (int64_t i = 0; i < kBankCount; ++i) {
    chan[i].resize(n * Elem::length);
    for (int64_t j = 0; j < n * Elem::length; ++j) {
      chan[i][j] = i ^ j;
    }
  }

  Bandwidth(task::mmaps<float, kBankCount>(chan).vectorized<Elem::length>(), n,
            flags);

  if (!((flags & kRead) && (flags & kWrite)))
    return 0;

  int64_t num_errors = 0;
  const int64_t threshold = 10; // only report up to these errors
  for (int64_t i = 0; i < kBankCount; ++i) {
    for (int64_t j = 0; j < n * Elem::length; ++j) {
      int64_t expected = i ^ j;
      int64_t actual = chan[i][j];
      if (actual != expected) {
        if (num_errors < threshold) {
          LOG(ERROR) << "expected: " << expected << ", actual: " << actual;
        } else if (num_errors == threshold) {
          LOG(ERROR) << "...";
        }
        ++num_errors;
      }
    }
  }
  if (num_errors == 0) {
    LOG(INFO) << "PASS!";
  } else {
    if (num_errors > threshold) {
      LOG(WARNING) << " (+" << (num_errors - threshold) << " more errors)";
    }
    LOG(INFO) << "FAIL!";
  }
  return num_errors > 0 ? 1 : 0;
}
