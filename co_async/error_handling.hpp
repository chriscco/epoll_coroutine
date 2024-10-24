#pragma once

namespace co_async {
#ifndef NDEBUG
/* C语言错误码转换成C++错误码 */
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
/* C语言错误码转换成C++错误码 */
auto checkError(auto res) {
    if (res == -1) [[unlikely]] {
        throw std::system_error(errno, std::system_category());
    }
    return res;
}
#endif
}