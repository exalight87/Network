#pragma once
#include <string>
#include <source_location>

enum class DefaultErrorType {
	NotSpecialized,
	NotSupported,
	InvalidArgument,
	CreationFailed,
	LoadingFileFailed,
	NullObject,
	AlreadyRunning,
	AlreadyCreated,
	NotFound,
};

enum class HttpServerError {
	NotSpecialized,
	CloseRequested
};

template<class ErrorType>
struct Error {
	ErrorType type;
	std::string message;
	std::source_location location;

	Error(ErrorType t, const std::string& msg, std::source_location loc = std::source_location::current()) : type(t), message(msg), location(loc) {};
	constexpr std::string GetFormatedError();
};

template<class ErrorType>
constexpr inline std::string Error< ErrorType >::GetFormatedError()
{
	return "Error [code: " + std::to_string(static_cast<int>(type)) + "] in " + std::string(location.file_name()) + ":" + std::string(location.function_name()) + ":" + std::to_string(location.line()) + " with the message : \n" + message;
}