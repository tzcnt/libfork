
#include "../bench.hpp"

#define TMC_IMPL
#include "tmc/all_headers.hpp"

using namespace tmc;

task<size_t> fib(size_t n) {
  if (n < 2)
    co_return n;

  /* Spawn one, then serially execute the other, then await the first */
  auto xt = spawn(fib(n - 1)).run_early();
  auto y = co_await fib(n - 2);
  auto x = co_await xt;
  co_return x + y;
}

void run(std::string name, int x) {
  benchmark(name, [&](std::size_t n, auto&& bench) {
    // Set up
    tmc::cpu_executor().set_thread_count(n).init();

    int answer = 0;

    bench([&] {
      answer = tmc::post_waitable(tmc::cpu_executor(), fib(x), 0).get();
    });

    tmc::cpu_executor().teardown();

    return answer;
  });
}

auto main() -> int {
  run("tmc-fib-30", 30);
  run("tmc-fib-35", 35);
  run("tmc-fib-40", 40);
  run("tmc-fib-42", 42);
}