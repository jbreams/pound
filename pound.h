#pragma once

#include <system_error>
#include <type_traits>
#include <utility>

#include "fmt/format.h"
#include "nonstd/string_view.hpp"
#include "tl/optional.hpp"

using namespace nonstd::string_view_literals;
using namespace fmt::literals;

namespace stdx {
template <typename T>
using optional = ::tl::optional<T>;
using nullopt_t = ::tl::nullopt_t;
static constexpr nullopt_t nullopt{nullopt_t::do_not_use{}, nullopt_t::do_not_use{}};

using string_view = ::nonstd::string_view;
}  // namespace stdx

// This little hack lets use use stdx::string_view with fmt.
namespace nonstd {
namespace sv_lite {
constexpr fmt::string_view to_string_view(const string_view& val) noexcept {
    return fmt::string_view(val.data(), val.size());
}
}  // namespace sv_lite
}  // namespace nonstd

class PoundException : public std::exception {
public:
    const char* what() const noexcept override {
        return _errorStr.c_str();
    }

    const stdx::optional<std::error_code>& systemError() const {
        return _systemError;
    }

    explicit PoundException(std::string val) : _errorStr(std::move(val)) {}
    explicit PoundException(std::string val, std::error_code err)
        : _systemError(std::move(err)), _errorStr(std::move(val)) {}

private:
    stdx::optional<std::error_code> _systemError;
    std::string _errorStr;
};

struct Position {
    Position(size_t row_, size_t column_) : row(row_), column(column_) {}
    Position() = default;

    size_t row = 0;
    size_t column = 0;

    auto toTuple() const {
        return std::make_tuple(column, row);
    }

    bool operator==(const Position& other) {
        return row == other.row && column == other.column;
    }
};

enum class Direction { Up, Down, Left, Right };

#define POUND_NON_COPYABLE(className)     \
    className(const className&) = delete; \
    className& operator=(const className&) = delete
#define POUND_NON_MOVABLE(className) \
    className(className&&) = delete; \
    className& operator=(className&&) = delete
#define POUND_NON_COPYABLE_NON_MOVABLE(className) \
    POUND_NON_COPYABLE(className);                \
    POUND_NON_MOVABLE(className)