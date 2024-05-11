#include <algorithm>
#include <iostream>
#include <optional>
#include <span>

#include <benchmark/benchmark.h>

#include "tmc/all_headers.hpp"

#include "../util.hpp"
#include "config.hpp"
#include "external/uts.h"

namespace {

using namespace tmc;

template <bool IsRoot>
auto uts(RNG_state *parentState, int depth, int myIdx, int myType) -> task<result> {
  Node self;
  result r(depth, 1, 0);

  if constexpr (IsRoot) {
    uts_initRoot(&self, type); // 'type' is a global variable...
  } else {
    self.height = depth;
    self.type = myType;
    for (int j = 0; j < computeGranularity; j++) {
      rng_spawn(parentState, self.state.state, myIdx);
    }
  }

  int num_children = uts_numChildren(&self);
  int child_type = uts_childType(&self);

  if (num_children > 0) {
    auto results = co_await spawn_many(
        iter_adapter(0,
                     [state = self.state.state, depth, child_type](int i) -> task<result> {
                       return uts<false>(state, depth + 1, i, child_type);
                     }),
        num_children);

    for (auto &&res : results) {
      r.maxdepth = max(r.maxdepth, res.maxdepth);
      r.size += res.size;
      r.leaves += res.leaves;
    }
  } else {
    r.leaves = 1;
  }
  co_return r;
};

auto uts_alloc(int depth, Node *parent) -> task<result> {
  //
  result r(depth, 1, 0);

  int num_children = uts_numChildren(parent);
  int child_type = uts_childType(parent);

  if (num_children > 0) {

    std::vector<Node> cs(num_children);
    for (int i = 0; i < num_children; i++) {

      cs[i].type = child_type;
      cs[i].height = parent->height + 1;

      for (int j = 0; j < computeGranularity; j++) {
        rng_spawn(parent->state.state, cs[i].state.state, i);
      }
    }

    auto results = co_await spawn_many(iter_adapter(0,
                                                    [&cs, depth](int i) -> task<result> {
                                                      return uts_alloc(depth + 1, &cs[i]);
                                                    }),
                                       num_children);

    for (auto &&res : results) {
      r.maxdepth = max(r.maxdepth, res.maxdepth);
      r.size += res.size;
      r.leaves += res.leaves;
    }
  } else {
    r.leaves = 1;
  }
  co_return r;
};

template <bool Alloc>
void uts_tmc_impl(benchmark::State &state, int tree) {

  state.counters["green_threads"] = state.range(0);

  setup_tree(tree);

  volatile int depth = 0;
  result r;

  tmc::cpu_executor().set_thread_count(state.range(0)).init();

  if constexpr (Alloc) {
    Node root;
    for (auto _ : state) {
      uts_initRoot(&root, type);
      r = tmc::post_waitable(tmc::cpu_executor(), uts_alloc(depth, &root), 0).get();
    }
  } else {
    for (auto _ : state) {
      r = tmc::post_waitable(tmc::cpu_executor(), uts<true>(nullptr, depth, 0, 0), 0).get();
    }
  }

  tmc::cpu_executor().teardown();

  if (r != result_tree(tree)) {
    std::cerr << "lf uts " << tree << " failed" << std::endl;
  }
}

// Passes minimal info so children can construct themselves
void uts_tmc(benchmark::State &state, int tree) { uts_tmc_impl<false>(state, tree); }

// Stores children in std::vector
void uts_tmc_alloc(benchmark::State &state, int tree) { uts_tmc_impl<true>(state, tree); }

} // namespace

MAKE_UTS_FOR(uts_tmc);
MAKE_UTS_FOR(uts_tmc_alloc);
