#ifndef B47E8824_9B07_4E52_943D_235F61EC526F
#define B47E8824_9B07_4E52_943D_235F61EC526F

// Copyright © Conor Williams <conorwilliams@outlook.com>

// SPDX-License-Identifier: MPL-2.0

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "libfork/macro.hpp"

#include "libfork/core/core.hpp"

/**
 * @file task.hpp
 *
 * @brief Implementation of the core ``lf::task`` type.
 */

namespace lf {

// ----------------------------------------------- //

/**
 * @brief An enumeration that determines the behavior of a coroutine's promise.
 */
enum class tag {
  root, ///< This coroutine is a root task (allocated on heap) from an ``lf::sync_wait``.
  call, ///< Non root task (on a virtual stack) from an ``lf::call``.
  fork, ///< Non root task (on a virtual stack) from an ``lf::fork``.
};

// ----------------------------------------------- //

/**
 * @brief Test if a type is a stateless class.
 */
template <typename T>
concept stateless = std::is_class_v<T> && std::is_trivial_v<T> && std::is_empty_v<T>;

// ------------------------ Forward decl ------------------------ //

namespace detail {

template <typename, typename, thread_context, tag>
struct promise_type;

} // namespace detail

template <typename T = void>
  requires(!std::is_rvalue_reference_v<T>)
class task;

template <stateless Fn>
struct [[nodiscard]] async_fn;

template <stateless Fn>
struct [[nodiscard]] async_mem_fn;

/**
 * @brief The first argument to all async functions will be passes a type derived from a specialization of this class.
 */
template <typename R, tag Tag, typename AsyncFn, typename... Self>
  requires(sizeof...(Self) <= 1)
struct first_arg_t;

// ------------------------ Interfaces ------------------------ //

// clang-format off

/**
 * @brief Disable rvalue references for T&& template types if an async function is forked.
 *
 * This is to prevent the user from accidentally passing a temporary object to an async function that
 * will then destructed in the parent task before the child task returns.
 */
template <typename T, typename Self>
concept no_forked_rvalue = Self::tag_value != tag::fork || std::is_reference_v<T>;

/**
 * @brief The first argument to all coroutines must conform to this concept and be derived from ``lf::first_arg_t``
 */
template <typename Arg>
concept first_arg =  requires {

  typename Arg::lf_first_arg;  ///< Explicit opt-in.

  typename Arg::return_address_t; ///< The type of the return address pointer

  typename Arg::context_type; /* -> */ requires thread_context<typename Arg::context_type>;

  { Arg::context() } -> std::same_as<typename Arg::context_type &>;

  typename Arg::underlying_fn;  /* -> */ requires stateless<typename Arg::underlying_fn>;
  
  { Arg::tag_value } -> std::convertible_to<tag>;
};

namespace detail {

template <typename T>
concept not_first_arg = !first_arg<std::remove_cvref_t<T>>;

// clang-format on

// ------------------------ Packet ------------------------ //

template <typename>
struct is_task_impl : std::false_type {};

template <typename T>
struct is_task_impl<task<T>> : std::true_type {};

template <typename T>
concept is_task = is_task_impl<T>::value;

// clang-format off

template <typename R, tag Tag, typename TaskValueType>
concept result_matches = std::is_void_v<R> || Tag == tag::root || Tag == tag::invoke || std::assignable_from<R &, TaskValueType>;

template <typename Head, typename... Tail>
concept valid_packet = first_arg<Head> && requires(typename Head::underlying_fn fun, Head head, Tail &&...tail) {
  //  
  { std::invoke(fun, std::move(head), std::forward<Tail>(tail)...) } -> is_task;

} && result_matches<typename Head::return_address_t, Head::tag_value, typename std::invoke_result_t<typename Head::underlying_fn, Head, Tail...>::value_type>;

// clang-format on

struct empty {};

/**
 * @brief An awaitable type (in a task) that triggers a fork/call/invoke.
 *
 * This will store a patched version of Head that includes the return type.
 */
template <typename Head, typename... Tail>
  requires valid_packet<Head, Tail...>
struct [[nodiscard]] packet {
  //
  using return_type = typename Head::return_address_t;
  using task_type = typename std::invoke_result_t<typename Head::underlying_fn, Head, Tail...>;
  using value_type = typename task_type::value_type;
  using promise_type = typename stdx::coroutine_traits<task_type, Head, Tail &&...>::promise_type;
  using handle_type = typename stdx::coroutine_handle<promise_type>;

  [[no_unique_address]] std::conditional_t<std::is_void_v<return_type>, detail::empty, std::add_lvalue_reference_t<return_type>> ret;
  [[no_unique_address]] Head context;
  [[no_unique_address]] std::tuple<Tail &&...> args;

  [[no_unique_address]] immovable _anon = {}; // NOLINT

  /**
   * @brief Call the underlying async function and return a handle to it, sets the return address if ``R != void``.
   */
  auto invoke_bind(stdx::coroutine_handle<promise_base> parent) && -> handle_type {

    LF_ASSERT(parent || Head::tag_value == tag::root);

    auto unwrap = [&]<class... Args>(Args &&...xargs) -> handle_type {
      return handle_type::from_address(std::invoke(typename Head::underlying_fn{}, std::move(context), std::forward<Args>(xargs)...).m_handle);
    };

    handle_type child = std::apply(unwrap, std::move(args));

    child.promise().set_parent(parent);

    if constexpr (!std::is_void_v<return_type>) {
      child.promise().set_ret_address(ret);
    }

    return child;
  }
};

} // namespace detail

// ----------------------------- Task ----------------------------- //

/**
 * @brief The return type for libfork's async functions/coroutines.
 */
template <typename T>
  requires(!std::is_rvalue_reference_v<T>)
class task {
public:
  using value_type = T; ///< The type of the value returned by the coroutine (cannot be a reference, use ``std::reference_wrapper``).

private:
  template <typename Head, typename... Tail>
    requires detail::valid_packet<Head, Tail...>
  friend struct detail::packet;

  template <typename, typename, thread_context, tag>
  friend struct detail::promise_type;

  // Promise constructs, packets accesses.
  explicit constexpr task(void *handle) noexcept : m_handle{handle} {
    LF_ASSERT(handle != nullptr);
  }

  void *m_handle = nullptr; ///< The handle to the coroutine.
};

// ----------------------------- Async function defs ----------------------------- //

/**
 * @brief Wraps a stateless callable that returns an ``lf::task``.
 */
template <stateless Fn>
struct [[nodiscard]] async_fn {
  /**
   * @brief Use with explicit template-parameter.
   */
  consteval async_fn() = default;

  /**
   * @brief Implicitly constructible from an invocable, deduction guide generated from this.
   */
  explicit(false) consteval async_fn([[maybe_unused]] Fn invocable_which_returns_a_task) {} // NOLINT

  /**
   * @brief Wrap the arguments into an awaitable (in an ``lf::task``) that triggers an invoke.
   *
   * An invoke should not be triggered inside a ``fork``/``call``/``join`` region as the exceptions
   * will be muddled, use ``lf:call`` instead.
   */
  template <typename... Args>
  LF_STATIC_CALL constexpr auto operator()(Args &&...args) LF_STATIC_CONST noexcept -> detail::packet<first_arg_t<void, tag::invoke, async_fn<Fn>>, Args...> {
    return {{}, {}, {std::forward<Args>(args)...}};
  }
};

/**
 * @brief Wraps a stateless callable that returns an ``lf::task``.
 */
template <stateless Fn>
struct [[nodiscard]] async_mem_fn {
  /**
   * @brief Use with explicit template-parameter.
   */
  consteval async_mem_fn() = default;
  /**
   * @brief Implicitly constructible from an invocable, deduction guide generated from this.
   */
  explicit(false) consteval async_mem_fn([[maybe_unused]] Fn invocable_which_returns_a_task) {} // NOLINT
  /**
   * @brief Wrap the arguments into an awaitable (in an ``lf::task``) that triggers an invoke.
   *
   * An invoke should not be triggered inside a ``fork``/``call``/``join`` region as the exceptions
   * will be muddled, use ``lf::call`` instead.
   */
  template <detail::not_first_arg Self, typename... Args>
  LF_STATIC_CALL constexpr auto operator()(Self &&self, Args &&...args) LF_STATIC_CONST noexcept -> detail::packet<first_arg_t<void, tag::invoke, async_mem_fn<Fn>, Self>, Args...> {
    return {{}, {std::forward<Self>(self)}, {std::forward<Args>(args)...}};
  }
};

// ----------------------------- first_arg_t impl ----------------------------- //

namespace detail {

/**
 * @brief A type that satisfies the ``thread_context`` concept.
 */
struct dummy_context {
  static auto context() -> dummy_context &;

  auto max_threads() -> std::size_t;

  auto stack_top() -> typename virtual_stack::handle;
  auto stack_pop() -> void;
  auto stack_push(typename virtual_stack::handle) -> void;

  auto task_pop() -> std::optional<task_handle>;
  auto task_push(task_handle) -> void;
};

static_assert(thread_context<dummy_context>, "dummy_context is not a thread_context");

template <typename R, stateless F, tag Tag>
struct first_arg_base : private move_only {
  using lf_first_arg = std::true_type;
  using context_type = dummy_context;
  using return_address_t = R;
  static auto context() -> context_type &;
  using underlying_fn = F;
  static constexpr tag tag_value = Tag;
};

static_assert(first_arg<first_arg_base<void, decltype([] {}), tag::invoke>>, "first_arg_base is not a first_arg_t!");

} // namespace detail

/**
 * @brief A specialization of ``first_arg_t`` for asynchronous global functions.
 *
 * This derives from the global function to allow to allow for use as a y-combinator.
 */
template <typename R, tag Tag, stateless F>
struct first_arg_t<R, Tag, async_fn<F>> : detail::first_arg_base<R, F, Tag>, async_fn<F> {};

/**
 * @brief A specialization of ``first_arg_t`` for asynchronous member functions.
 *
 * This wraps a pointer to an instance of the parent type to allow for use as an explicit ``this`` parameter.
 */
template <typename R, tag Tag, stateless F, typename Self>
struct first_arg_t<R, Tag, async_mem_fn<F>, Self> : detail::first_arg_base<R, F, Tag> {

  static_assert(Tag != tag::fork || std::is_reference_v<Self>, "A forked task passed a temporary self parameter will dangle!");

  /**
   * @brief The type of the deduced forwarding reference.
   */
  using self_type = Self;

  /**
   * @brief Construct an ``lf::async_mem_fn_for`` from a reference to ``self``.
   */
  explicit(false) constexpr first_arg_t(Self &&self) : m_self{std::forward<Self>(self)} {} // NOLINT

  /**
   * @brief Access the underlying class instance.
   */
  [[nodiscard]] constexpr auto operator*() & noexcept -> Self & { return m_self; }

  /**
   * @brief Access the underlying class with a value category corresponding to forwarding a forwarding-reference.
   */
  [[nodiscard]] constexpr auto operator*() && noexcept -> Self && { return std::forward<Self>(m_self); }

  /**
   * @brief Access the underlying ``this`` pointer.
   */
  [[nodiscard]] constexpr auto operator->() noexcept -> std::remove_reference_t<Self> * { return std::addressof(m_self); }

private:
  std::add_rvalue_reference_t<Self> m_self;
};

} // namespace lf

#endif /* B47E8824_9B07_4E52_943D_235F61EC526F */
