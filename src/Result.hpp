#pragma once

#include "Error.hpp"

#include <expected>
#include <type_traits>
#include <utility>

template <class T, class ErrorType>
class Result
{
public:
    using value_type = T;
    using error_type = Error<ErrorType>;

    constexpr Result() requires std::default_initializable<T> = default;
    constexpr Result(const Result&) = default;
    constexpr Result(Result&&) noexcept = default;
    constexpr Result& operator=(const Result&) = default;
    constexpr Result& operator=(Result&&) noexcept = default;

    template <typename U = T>
        requires std::constructible_from<T, U>
    constexpr Result(U&& value)
        : m_result(std::in_place, std::forward<U>(value))
    {
    }

    constexpr Result(const error_type& error)
        : m_result(std::unexpected(error))
    {
    }

    constexpr Result(error_type&& error)
        : m_result(std::unexpected(std::move(error)))
    {
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_result.has_value(); }
    [[nodiscard]] constexpr bool HasError() const noexcept { return !has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr T& value() & { return m_result.value(); }
    [[nodiscard]] constexpr const T& value() const & { return m_result.value(); }
    [[nodiscard]] constexpr T&& value() && { return std::move(m_result).value(); }
    [[nodiscard]] constexpr const T&& value() const && { return std::move(m_result).value(); }

    [[nodiscard]] constexpr T& Data() & { return value(); }
    [[nodiscard]] constexpr const T& Data() const & { return value(); }
    [[nodiscard]] constexpr T&& Data() && { return std::move(*this).value(); }
    [[nodiscard]] constexpr const T&& Data() const && { return std::move(*this).value(); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& alternative) const&
    {
        return m_result.value_or(std::forward<U>(alternative));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& alternative) &&
    {
        return std::move(m_result).value_or(std::forward<U>(alternative));
    }

    [[nodiscard]] constexpr error_type& error() & { return m_result.error(); }
    [[nodiscard]] constexpr const error_type& error() const & { return m_result.error(); }
    [[nodiscard]] constexpr error_type&& error() && { return std::move(m_result).error(); }
    [[nodiscard]] constexpr const error_type&& error() const && { return std::move(m_result).error(); }

    [[nodiscard]] constexpr error_type& Error() & { return error(); }
    [[nodiscard]] constexpr const error_type& Error() const & { return error(); }
    [[nodiscard]] constexpr error_type&& Error() && { return std::move(*this).error(); }
    [[nodiscard]] constexpr const error_type&& Error() const && { return std::move(*this).error(); }

private:
    std::expected<T, error_type> m_result;
};

template <class ErrorType>
class Result<void, ErrorType>
{
public:
    using value_type = void;
    using error_type = Error<ErrorType>;

    constexpr Result() = default;
    constexpr Result(const Result&) = default;
    constexpr Result(Result&&) noexcept = default;
    constexpr Result& operator=(const Result&) = default;
    constexpr Result& operator=(Result&&) noexcept = default;

    constexpr Result(const error_type& error)
        : m_result(std::unexpected(error))
    {
    }

    constexpr Result(error_type&& error)
        : m_result(std::unexpected(std::move(error)))
    {
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return m_result.has_value(); }
    [[nodiscard]] constexpr bool HasError() const noexcept { return !has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr void value() const { m_result.value(); }

    [[nodiscard]] constexpr error_type& error() & { return m_result.error(); }
    [[nodiscard]] constexpr const error_type& error() const & { return m_result.error(); }
    [[nodiscard]] constexpr error_type&& error() && { return std::move(m_result).error(); }
    [[nodiscard]] constexpr const error_type&& error() const && { return std::move(m_result).error(); }

    [[nodiscard]] constexpr error_type& Error() & { return error(); }
    [[nodiscard]] constexpr const error_type& Error() const & { return error(); }
    [[nodiscard]] constexpr error_type&& Error() && { return std::move(*this).error(); }
    [[nodiscard]] constexpr const error_type&& Error() const && { return std::move(*this).error(); }

private:
    std::expected<void, error_type> m_result{std::in_place};
};

#define CheckResult(function) \
    if (auto result = (function); !result) { return result; }

#define CheckResultAsError(function) \
    if (auto result = (function); !result) { return result.error(); }
