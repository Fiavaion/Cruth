// Cruth core — exception-free error reporting.
//
// The core never lets an exception reach the Swift shell (Swift cannot catch
// C++ exceptions), so fallible operations return Status or Result<T>.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace cruth {

enum class ErrorCode {
    InvalidArgument,
    Io,
    Parse,
    UnsupportedFormat,
    NonManifold,
    OutOfMemory,
    Internal,
};

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};

class Status {
public:
    Status() = default;
    Status(Error e) : error_(std::move(e)), ok_(false) {}  // NOLINT(google-explicit-constructor)
    static Status ok() { return {}; }

    bool is_ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }

private:
    Error error_;
    bool ok_ = true;
};

template <typename T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
    Result(Error e) : v_(std::move(e)) {}      // NOLINT(google-explicit-constructor)

    bool is_ok() const { return std::holds_alternative<T>(v_); }
    explicit operator bool() const { return is_ok(); }

    T& value() & { return std::get<T>(v_); }
    const T& value() const& { return std::get<T>(v_); }
    T&& value() && { return std::get<T>(std::move(v_)); }
    const Error& error() const { return std::get<Error>(v_); }

private:
    std::variant<T, Error> v_;
};

}  // namespace cruth
