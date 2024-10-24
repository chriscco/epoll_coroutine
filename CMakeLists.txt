cmake_minimum_required(VERSION 3.22.1)
project(epoll_coroutine)

set(CMAKE_CXX_STANDARD 20)

add_executable(epoll_coroutine
        main.cpp
        co_async/debug.hpp
        co_async/task.hpp
        co_async/timer_loop.hpp)
