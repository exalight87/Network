#pragma once
#include "Error.hpp"
#include <optional>
#include <assert.h>

template <class T, class ErrorType>
struct Result 
{
	std::optional<Error<ErrorType>> error;
	std::optional<T> data;

	Result() = default;
	Result(Error<ErrorType> iError) : error(iError) {};
	Result(T iData) : data(std::move( iData )) {};

	constexpr bool HasError() const { return error.has_value(); };
	constexpr T&& Data() { assert(data.has_value());  return std::move(data.value()); }
	constexpr T DataOr( T val ) { return data.value_or(val); }
	constexpr Error<ErrorType> Error() { return error.value(); }

	operator bool() { return !error.has_value(); };
};

template <class ErrorType>
struct Result<void, ErrorType>
{
	std::optional<Error<ErrorType>> error;

	Result() = default;
	Result(Error<ErrorType> iError) : error(iError) {};

	constexpr bool HasError() const { return error.has_value(); }
	Error< ErrorType > Error() { return error.value(); }
	constexpr operator bool() { return !error.has_value(); };
};

#define CheckResult(function) \
	if(auto result = function; !result) { return result; };

#define CheckResultAsError(function) \
	if(auto result = function; !result) { return result.Error(); };