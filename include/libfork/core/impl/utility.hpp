#ifndef DF63D333_F8C0_4BBA_97E1_32A78466B8B7
#define DF63D333_F8C0_4BBA_97E1_32A78466B8B7

// Copyright © Conor Williams <conorwilliams@outlook.com>

// SPDX-License-Identifier: MPL-2.0

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <version>

#include "libfork/core/macro.hpp"

/**
 * @file utility.hpp
 *
 * @brief A collection of internal utilities.
 */

/**
 * @brief The ``libfork`` namespace.
 *
 * Everything in ``libfork`` is contained within this namespace.
 */
namespace lf {

/**
 * @brief An inline namespace that wraps core functionality.
 *
 * This is the namespace that contains the minimal user-facing API of ``libfork``, notably
 * this excludes schedulers and algorithms.
 */
inline namespace core {}

/**
 * @brief An inline namespace that wraps extension functionality.
 *
 * This namespace is part of ``libfork``s public API but is intended for advanced users
 * writing schedulers, It exposes the scheduler/context API's alongside some implementation
 * details (such as lock-free dequeues, a `hwloc` abstraction, and synchronization primitives)
 * that could be useful when implementing custom schedulers.
 */
inline namespace ext {}

/**
 * @brief An internal namespace that wraps implementation details.
 *
 * \rst
 *
 * .. warning::
 *
 *    This is exposed as internal documentation only it is not part of the public facing API.
 *
 * \endrst
 */
namespace impl {}

} // namespace lf

namespace lf::impl {

// ---------------- Constants ---------------- //

/**
 * @brief The cache line size (bytes) of the current architecture.
 */
#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t k_cache_line = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t k_cache_line = 64;
#endif

/**
 * @brief The default alignment of `operator new`, a power of two.
 */
inline constexpr std::size_t k_new_align = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

static_assert(std::has_single_bit(k_new_align));

/**
 * @brief Shorthand for `std::numeric_limits<std::unt32_t>::max()`.
 */
static constexpr std::uint16_t k_u16_max = std::numeric_limits<std::uint16_t>::max();

/**
 * @brief Number of bytes in a kibibyte.
 */
inline constexpr std::size_t k_kibibyte = 1024;

// ---------------- Utility classes ---------------- //

/**
 * @brief An empty type.
 */
struct empty {};

static_assert(std::is_empty_v<empty>);

// -------------------------------- //

/**
 * @brief A functor that returns ``std::nullopt``.
 */
template <typename T>
struct return_nullopt {
  LF_STATIC_CALL constexpr auto operator()() LF_STATIC_CONST noexcept -> std::optional<T> { return {}; }
};

// -------------------------------- //

/**
 * @brief An empty base class that is not copiable or movable.
 *
 * The template parameter prevents multiple empty bases when inheriting multiple classes.
 */
template <typename CRTP>
struct immovable {
  immovable(const immovable &) = delete;
  immovable(immovable &&) = delete;
  auto operator=(const immovable &) -> immovable & = delete;
  auto operator=(immovable &&) -> immovable & = delete;

 protected:
  immovable() = default;
  ~immovable() = default;
};

static_assert(std::is_empty_v<immovable<void>>);

// -------------------------------- //

/**
 * @brief An empty base class that is move-only.
 *
 * The template parameter prevents multiple empty bases when inheriting multiple classes.
 */
template <typename CRTP>
struct move_only {

  move_only(const move_only &) = delete;
  move_only(move_only &&) noexcept = default;
  auto operator=(const move_only &) -> move_only & = delete;
  auto operator=(move_only &&) noexcept -> move_only & = default;

  ~move_only() = default;

 protected:
  move_only() = default;
};

static_assert(std::is_empty_v<immovable<void>>);

/**
 * @brief Basic implementation of a Golang like defer.
 *
 * Use like:
 *
 * .. code::
 *
 *    auto * ptr = c_api_init();
 *
 *    defer _ = [&ptr] () noexcept {
 *      c_api_clean_up(ptr);
 *    };
 *
 *    // Code that may throw
 *
 */
template <class F>
  requires std::is_nothrow_invocable_v<F>
class [[nodiscard("Defer will execute immediately unless bound to a name!")]] defer : immovable<defer<F>> {
 public:
  /**
   * @brief Construct a new Defer object.
   *
   * @param f Nullary invocable forwarded into object and invoked by destructor.
   */
  constexpr defer(F &&f) noexcept(std::is_nothrow_constructible_v<F, F &&>) : m_f(std::forward<F>(f)) {}

  /**
   * @brief Call the invocable.
   */
  constexpr ~defer() noexcept { std::invoke(std::forward<F>(m_f)); }

 private:
  [[no_unique_address]] F m_f;
};

// TODO: we could make manual_lifetime<T> empty if T is empty?

/**
 * @brief Provides storage for a single object of type ``T``.
 *
 * Every instance of manual_lifetime is trivially constructible/destructible.
 */
template <typename T>
class manual_lifetime : immovable<manual_lifetime<T>> {
 public:
  /**
   * @brief Start lifetime of object.
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto construct(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> T * {
    return ::new (static_cast<void *>(m_buf.data())) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Start lifetime of object.
   */
  template <typename U>
    requires std::constructible_from<T, U>
  void operator=(U &&expr) noexcept(std::is_nothrow_constructible_v<T, U>) {
    this->construct(std::forward<U>(expr));
  }

  /**
   * @brief Destroy the contained object, must have been constructed first.
   *
   * A noop if ``T`` is trivially destructible.
   */
  void destroy() noexcept(std::is_nothrow_destructible_v<T>)
    requires std::is_destructible_v<T>
  {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_at(data());
    }
  }

  /**
   * @brief Get a pointer to the contained object, must have been constructed first.
   */
  [[nodiscard]] auto data() noexcept -> T * { return std::launder(reinterpret_cast<T *>(m_buf.data())); }

  /**
   * @brief Get a pointer to the contained object, must have been constructed first.
   */
  [[nodiscard]] auto data() const noexcept -> T * {
    return std::launder(reinterpret_cast<T const *>(m_buf.data()));
  }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator->() noexcept -> T * { return data(); }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator->() const noexcept -> T const * { return data(); }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator*() & noexcept -> T & { return *data(); }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator*() const & noexcept -> T const & { return *data(); }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator*() && noexcept -> T && { return std::move(*data()); }

  /**
   * @brief Access the contained object, must have been constructed first.
   */
  [[nodiscard]] auto operator*() const && noexcept -> T const && { return std::move(*data()); }

 private:
  [[no_unique_address]] alignas(T) std::array<std::byte, sizeof(T)> m_buf;
};

// ---------------- Meta programming ---------------- //

/**
 * @brief Test if we can form a reference to an instance of type T.
 */
template <typename T>
concept can_reference = requires () { typename std::type_identity_t<T &>; };

// Ban constructs like (T && -> T const &) which would dangle.

namespace detail {

template <typename To, typename From>
struct safe_ref_bind_impl : std::false_type {};

// All reference types can bind to a non-dangling reference of the same kind without dangling.

template <typename T>
struct safe_ref_bind_impl<T, T> : std::true_type {};

// `T const X` can additionally bind to `T X` without dangling//

template <typename To, typename From>
  requires (!std::same_as<To const &, From &>)
struct safe_ref_bind_impl<To const &, From &> : std::true_type {};

template <typename To, typename From>
  requires (!std::same_as<To const &&, From &&>)
struct safe_ref_bind_impl<To const &&, From &&> : std::true_type {};

} // namespace detail

/**
 * @brief Verify that ``To expr = From`` is valid and does not dangle.
 *
 * This requires that ``To`` and ``From`` are both the same reference type or that ``To`` is a
 * const qualified version of ``From``. This explicitly bans conversions like ``T && -> T const &``
 * which would dangle.
 */
template <typename From, typename To>
concept safe_ref_bind_to =                          //
    std::is_reference_v<To> &&                      //
    can_reference<From> &&                          //
    detail::safe_ref_bind_impl<To, From &&>::value; //

namespace detail {

template <class Lambda, int = (((void)Lambda{}()), 0)>
consteval auto constexpr_callable_help(Lambda) -> bool {
  return true;
}

consteval auto constexpr_callable_help(auto &&...) -> bool { return false; }

} // namespace detail

/**
 * @brief Detect if a function is constexpr-callable.
 *
 * \rst
 *
 * Use like:
 *
 * .. code::
 *
 *    if constexpr (is_constexpr<[]{ function_to_test() }>){
 *      // ...
 *    }
 *
 * \endrst
 */
template <auto Lambda>
concept constexpr_callable = detail::constexpr_callable_help(Lambda);

namespace detail::static_test {

inline void foo() {}

static_assert(constexpr_callable<[] {}>);

static_assert(constexpr_callable<[] {
  std::has_single_bit(1U);
}>);

static_assert(!constexpr_callable<[] {
  foo();
}>);

} // namespace detail::static_test

/**
 * @brief Forwards to ``std::is_reference_v<T>``.
 */
template <typename T>
concept reference = std::is_reference_v<T>;

/**
 * @brief Forwards to ``std::is_reference_v<T>``.
 */
template <typename T>
concept non_reference = !std::is_reference_v<T>;

/**
 * @brief Check is a type is ``void``.
 */
template <typename T>
concept is_void = std::is_void_v<T>;

/**
 * @brief Check is a type is not ``void``.
 */
template <typename T>
concept non_void = !std::is_void_v<T>;

/**
 * @brief Check if a type has ``const``, ``volatile`` or reference qualifiers.
 */
template <typename T>
concept unqualified = std::same_as<std::remove_cvref_t<T>, T>;

namespace detail {

template <typename From, typename To>
struct forward_cv;

template <unqualified From, typename To>
struct forward_cv<From, To> : std::type_identity<To> {};

template <unqualified From, typename To>
struct forward_cv<From const, To> : std::type_identity<To const> {};

template <unqualified From, typename To>
struct forward_cv<From volatile, To> : std::type_identity<To volatile> {};

template <unqualified From, typename To>
struct forward_cv<From const volatile, To> : std::type_identity<To const volatile> {};

} // namespace detail

/**
 * @brief Copy the ``const``/``volatile`` qualifiers from ``From`` to ``To``.
 */
template <non_reference From, unqualified To>
using forward_cv_t = typename detail::forward_cv<From, To>::type;

namespace detail {

template <typename T>
struct constify_ref;

template <typename T>
struct constify_ref<T &> : std::type_identity<T const &> {};

template <typename T>
struct constify_ref<T &&> : std::type_identity<T const &&> {};

} // namespace detail

/**
 * @brief Convert ``T & -> T const&`` and ``T && -> T const&&``.
 */
template <reference T>
using constify_ref_t = typename detail::constify_ref<T>::type;

/**
 * @brief True if the unqualified ``T`` and ``U`` refer to different types.
 *
 * This is useful for preventing ''T &&'' constructor/assignment from replacing the defaults.
 */
template <typename T, typename U>
concept non_converting = !std::same_as<std::remove_cvref_t<U>, std::remove_cvref_t<T>>;

/**
 * @brief True if the unqualified ``T`` and ``U`` refer to different types.
 *
 * This is useful for preventing ''T &&'' constructor/assignment from replacing the defaults.
 */
template <typename T, typename U>
concept different_from = !std::same_as<std::remove_cvref_t<U>, std::remove_cvref_t<T>>;

// ---------------- Small functions ---------------- //

/**
 * @brief Invoke a callable with the given arguments, unconditionally noexcept.
 */
template <typename... Args, std::invocable<Args...> Fn>
constexpr auto noexcept_invoke(Fn &&fun, Args &&...args) noexcept -> std::invoke_result_t<Fn, Args...> {
  return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
}

// -------------------------------- //

/**
 * @brief Transform `[a, b, c] -> [f(a), f(b), f(c)]`.
 */
template <typename T, typename F>
auto map(std::vector<T> const &from, F &&func) -> std::vector<std::invoke_result_t<F &, T const &>> {

  std::vector<std::invoke_result_t<F &, T const &>> out;

  out.reserve(from.size());

  for (auto &&item : from) {
    out.emplace_back(std::invoke(func, item));
  }

  return out;
}

/**
 * @brief Transform `[a, b, c] -> [f(a), f(b), f(c)]`.
 */
template <typename T, typename F>
auto map(std::vector<T> &&from, F &&func) -> std::vector<std::invoke_result_t<F &, T>> {

  std::vector<std::invoke_result_t<F &, T>> out;

  out.reserve(from.size());

  for (auto &&item : from) {
    out.emplace_back(std::invoke(func, std::move(item)));
  }

  return out;
}

// -------------------------------- //

/**
 * @brief Returns ``ptr`` and asserts it is non-null
 */
template <typename T>
constexpr auto non_null(T *ptr) noexcept -> T * {
  LF_ASSERT(ptr != nullptr);
  return ptr;
}

// --------------------------------- //

// -------------------------------- //

/**
 * @brief Like ``std::apply`` but reverses the argument order.
 */
template <class F, class Tuple>
constexpr auto apply_to(Tuple &&tup, F &&func)
    LF_HOF_RETURNS(std::apply(std::forward<F>(func), std::forward<Tuple>(tup)))

    // -------------------------------- //

    /**
     * @brief Cast a pointer to a byte pointer.
     */
    template <typename T>
    auto byte_cast(T *ptr) LF_HOF_RETURNS(std::bit_cast<forward_cv_t<T, std::byte> *>(ptr))

} // namespace lf::impl

#endif /* DF63D333_F8C0_4BBA_97E1_32A78466B8B7 */
