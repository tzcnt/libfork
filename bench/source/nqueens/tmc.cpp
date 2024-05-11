#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <numeric>
#include <ranges>

#include <benchmark/benchmark.h>

#include "tmc/all_headers.hpp"

#include "../util.hpp"
#include "config.hpp"

namespace {

using namespace tmc;

template <size_t N>
auto nqueens(int j, std::array<char, N> const &a) -> task<int> {
  if (N == j) {
    co_return 1;
  }

  std::array<std::array<char, N>, N> buf;
  for (int i = 0; i < N; i++) {
    for (int k = 0; k < j; k++) {
      buf[i][k] = a[k];
    }
    buf[i][j] = i;
  }

  int taskCount = 0;
  auto tasks = std::ranges::views::iota(0UL, N) | std::ranges::views::filter([j, &buf](int i) {
                 return queens_ok(j + 1, buf[i].data());
               }) |
               std::ranges::views::transform([j, &buf, &taskCount](int i) {
                 ++taskCount;
                 return nqueens(j + 1, buf[i]);
               });

  // Spawn up to N tasks (but possibly less, if queens_ok fails)
  auto parts = co_await tmc::spawn_many<N>(tasks.begin(), tasks.end());

  int ret = 0;
  for (size_t i = 0; i < taskCount; ++i) {
    ret += parts[i];
  }

  co_return ret;
};

void nqueens_tmc(benchmark::State &state) {

  state.counters["green_threads"] = state.range(0);
  state.counters["nqueens(n)"] = nqueens_work;

  cpu_executor().set_thread_count(state.range(0)).init();

  volatile int output;

  int retval = tmc::async_main([](benchmark::State &state, volatile int &output) -> task<int> {
    std::array<char, nqueens_work> buf{};

    for (auto _ : state) {
      output = co_await nqueens(0, buf);
    }
    co_return 0;
  }(state, output));

  cpu_executor().teardown();

  if (output != answers[nqueens_work]) {
    std::cerr << "lf wrong answer: " << output << " != " << answers[nqueens_work] << std::endl;
  }
}

} // namespace

BENCHMARK(nqueens_tmc)->Apply(targs)->UseRealTime();