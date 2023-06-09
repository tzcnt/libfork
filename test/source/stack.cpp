// Copyright © Conor Williams <conorwilliams@outlook.com>

// SPDX-License-Identifier: MPL-2.0

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "libfork/stack.hpp"

// NOLINTBEGIN No linting in tests

using namespace lf;

// Define an alias for simplicity
template <std::size_t N>
using Stack = virtual_stack<N>;

TEST_CASE("virtual_stack - Basic Functionality", "[virtual_stack]") {
  //
  auto stack = Stack<4096>::make_unique();

  SECTION("Stack Creation and Empty Check") {
    REQUIRE(stack->empty());
  }

  SECTION("Allocate and Deallocate") {
    void *ptr = stack->allocate(128);
    REQUIRE_FALSE(stack->empty());
    stack->deallocate(ptr, 128);
    REQUIRE(stack->empty());
  }

  SECTION("Multiple Allocations and Deallocations") {
    void *ptr1 = stack->allocate(64);
    void *ptr2 = stack->allocate(128);
    REQUIRE_FALSE(stack->empty());
    stack->deallocate(ptr2, 128);
    stack->deallocate(ptr1, 64);
    REQUIRE(stack->empty());
  }
}

TEST_CASE("virtual_stack - Stack Overflow", "[virtual_stack]") {
  auto stack = Stack<128>::make_unique();
  REQUIRE_NOTHROW(stack->allocate(10));
  REQUIRE_THROWS_AS(stack->allocate(128), std::exception);
}

TEST_CASE("virtual_stack - Alignment Checks", "[virtual_stack]") {
  auto stack = Stack<128>::make_unique();

  void *ptr1 = stack->allocate(10);
  void *ptr2 = stack->allocate(10);

  REQUIRE(reinterpret_cast<std::uintptr_t>(ptr1) % detail::k_new_align == 0);
  REQUIRE(reinterpret_cast<std::uintptr_t>(ptr2) % detail::k_new_align == 0);

  stack->deallocate(ptr2, 10);
  stack->deallocate(ptr1, 10);

  REQUIRE(stack->empty());
}

TEST_CASE("virtual_stack - Handle Operations", "[virtual_stack]") {

  SECTION("Handle Creation and Access") {
    auto stack = Stack<4096>::make_unique();
    Stack<4096>::handle handle(*stack);
    REQUIRE(handle->empty());
  }

  SECTION("Handle Comparison") {
    auto stack1 = Stack<4096>::make_unique();
    auto stack2 = Stack<4096>::make_unique();
    Stack<4096>::handle handle1(*stack1);
    Stack<4096>::handle handle2(*stack2);
    REQUIRE(handle1 <=> handle2 != 0);
    REQUIRE(handle1 <=> handle1 == 0);
  }

  SECTION("Get Stack from Address") {
    auto stack = Stack<4096>::make_unique();
    void *ptr = stack->allocate(64);
    auto handle = Stack<4096>::from_address(ptr);
    REQUIRE(!handle->empty());
    handle->deallocate(ptr, 64);
    REQUIRE(stack->empty());
  }
}

TEST_CASE("virtual_stack - Unique Pointer Array", "[virtual_stack]") {

  auto stackArray = Stack<4096>::make_unique(5);

  for (std::size_t i = 0; i < 5; ++i) {
    REQUIRE(stackArray[i].empty());
  }
}

TEST_CASE("virtual_stack - full with exception", "[virtual_stack]") {
  auto stack = Stack<4096>::make_unique();

  REQUIRE(stack->empty());

  try {
    throw std::runtime_error("Test exception");
    FAIL("unreachable");
  } catch (...) {
    stack->unhandled_exception();
  }

  REQUIRE(!stack->empty());

  REQUIRE_THROWS_AS(stack->rethrow_if_unhandled(), std::exception);

  REQUIRE(stack->empty());
}

// NOLINTEND