#pragma once
#include <variant>
#include <string>
#include <optional>
#include <functional>
#include <stdexcept>

// Error type for Result
struct Error {
    int code;
    std::string message;
    std::string details;

    Error() : code(0) {}
    Error(int c, const std::string& msg) : code(c), message(msg) {}
    Error(int c, const std::string& msg, const std::string& det)
        : code(c), message(msg), details(det) {}

    std::string toString() const {
        std::string s = "Error " + std::to_string(code) + ": " + message;
        if (!details.empty()) {
            s += " (" + details + ")";
        }
        return s;
    }

    // Common error codes
    static constexpr int NetworkError = 1000;
    static constexpr int ParseError = 2000;
    static constexpr int ValidationError = 3000;
    static constexpr int NotFoundError = 4000;
    static constexpr int TimeoutError = 5000;
    static constexpr int RateLimitError = 6000;
    static constexpr int AuthError = 7000;
    static constexpr int InternalError = 8000;

    // Factory methods
    static Error network(const std::string& msg) {
        return Error(NetworkError, "Network error", msg);
    }

    static Error parse(const std::string& msg) {
        return Error(ParseError, "Parse error", msg);
    }

    static Error validation(const std::string& msg) {
        return Error(ValidationError, "Validation error", msg);
    }

    static Error notFound(const std::string& what) {
        return Error(NotFoundError, "Not found", what);
    }

    static Error timeout(const std::string& op) {
        return Error(TimeoutError, "Timeout", op);
    }

    static Error rateLimit(const std::string& msg) {
        return Error(RateLimitError, "Rate limit exceeded", msg);
    }

    static Error auth(const std::string& msg) {
        return Error(AuthError, "Authentication error", msg);
    }

    static Error internal(const std::string& msg) {
        return Error(InternalError, "Internal error", msg);
    }
};

// Rust-style Result type: either a value or an error
template<typename T>
class Result {
private:
    std::variant<T, Error> data_;
    bool isOk_;

public:
    // Success constructor
    Result(const T& value) : data_(value), isOk_(true) {}
    Result(T&& value) : data_(std::move(value)), isOk_(true) {}

    // Error constructor
    Result(const Error& error) : data_(error), isOk_(false) {}
    Result(Error&& error) : data_(std::move(error)), isOk_(false) {}

    // Check if result is successful
    bool isOk() const { return isOk_; }
    bool isError() const { return !isOk_; }

    // Explicit conversion to bool
    explicit operator bool() const { return isOk_; }

    // Get value (throws if error)
    T& value() {
        if (!isOk_) {
            throw std::runtime_error(std::get<Error>(data_).toString());
        }
        return std::get<T>(data_);
    }

    const T& value() const {
        if (!isOk_) {
            throw std::runtime_error(std::get<Error>(data_).toString());
        }
        return std::get<T>(data_);
    }

    // Get value or default
    T valueOr(const T& defaultValue) const {
        return isOk_ ? std::get<T>(data_) : defaultValue;
    }

    T valueOr(T&& defaultValue) const {
        return isOk_ ? std::get<T>(data_) : std::move(defaultValue);
    }

    // Get error (undefined behavior if ok)
    const Error& error() const {
        return std::get<Error>(data_);
    }

    // Map: transform the value if ok
    template<typename F>
    auto map(F&& f) const -> Result<decltype(f(std::declval<T>()))> {
        using U = decltype(f(std::declval<T>()));
        if (isOk_) {
            return Result<U>(f(std::get<T>(data_)));
        } else {
            return Result<U>(std::get<Error>(data_));
        }
    }

    // FlatMap/AndThen: chain operations that return Result
    template<typename F>
    auto andThen(F&& f) const -> decltype(f(std::declval<T>())) {
        using ResultType = decltype(f(std::declval<T>()));
        if (isOk_) {
            return f(std::get<T>(data_));
        } else {
            return ResultType(std::get<Error>(data_));
        }
    }

    // MapError: transform the error if error
    Result<T> mapError(std::function<Error(const Error&)> f) const {
        if (isOk_) {
            return *this;
        } else {
            return Result<T>(f(std::get<Error>(data_)));
        }
    }

    // OrElse: provide alternative if error
    Result<T> orElse(std::function<Result<T>(const Error&)> f) const {
        if (isOk_) {
            return *this;
        } else {
            return f(std::get<Error>(data_));
        }
    }

    // Match: pattern matching
    template<typename OkF, typename ErrF>
    auto match(OkF&& okFn, ErrF&& errFn) const
        -> decltype(okFn(std::declval<T>())) {
        if (isOk_) {
            return okFn(std::get<T>(data_));
        } else {
            return errFn(std::get<Error>(data_));
        }
    }

    // Convert to optional (discards error)
    std::optional<T> toOptional() const {
        if (isOk_) {
            return std::get<T>(data_);
        }
        return std::nullopt;
    }

    // Static factory methods
    static Result<T> ok(const T& value) {
        return Result<T>(value);
    }

    static Result<T> ok(T&& value) {
        return Result<T>(std::move(value));
    }

    static Result<T> err(const Error& error) {
        return Result<T>(error);
    }

    static Result<T> err(int code, const std::string& msg) {
        return Result<T>(Error(code, msg));
    }
};

// Specialization for void
template<>
class Result<void> {
private:
    std::optional<Error> error_;

public:
    Result() : error_(std::nullopt) {}
    Result(const Error& error) : error_(error) {}

    bool isOk() const { return !error_.has_value(); }
    bool isError() const { return error_.has_value(); }
    explicit operator bool() const { return isOk(); }

    const Error& error() const { return *error_; }

    static Result<void> ok() {
        return Result<void>();
    }

    static Result<void> err(const Error& error) {
        return Result<void>(error);
    }
};

// Helper macros for error propagation (similar to Rust's ?)
#define TRY(expr) \
    ({ \
        auto _result = (expr); \
        if (_result.isError()) { \
            return _result.error(); \
        } \
        _result.value(); \
    })

// Combine multiple results
template<typename T>
Result<std::vector<T>> combineResults(const std::vector<Result<T>>& results) {
    std::vector<T> values;
    for (const auto& r : results) {
        if (r.isError()) {
            return Result<std::vector<T>>(r.error());
        }
        values.push_back(r.value());
    }
    return Result<std::vector<T>>(values);
}

// Try-catch wrapper
template<typename T, typename F>
Result<T> tryExecute(F&& f) {
    try {
        return Result<T>::ok(f());
    } catch (const std::exception& e) {
        return Result<T>::err(Error::internal(e.what()));
    }
}

template<typename F>
Result<void> tryExecuteVoid(F&& f) {
    try {
        f();
        return Result<void>::ok();
    } catch (const std::exception& e) {
        return Result<void>::err(Error::internal(e.what()));
    }
}
