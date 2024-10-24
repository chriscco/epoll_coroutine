#pragma once

namespace co_async {
#ifndef NDEBUG
auto checkError(auto res, std::source_location const &loc =
std::source_location::current()) {
    if (res == -1) [[unlikely]] {
        throw std::system_error(errno, std::system_category(),
                                (std::string)loc.file_name() + ":" +
                                std::to_string(loc.line()));
    }
    return res;
}
#elif
auto checkError(auto res) {
    if (res == -1) [[unlikely]] {
        throw std::system_error(errno, std::system_category());
    }
    return res;
}
#endif
}