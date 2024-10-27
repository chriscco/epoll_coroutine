#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <string>
#include <chrono>
#include "co_async/debug.hpp"
#include "co_async/timer_loop.hpp"
#include "co_async/task.hpp"
#include "co_async/epoll_loop.hpp"

using namespace std::chrono_literals;

/* 处理IO事件的循环 */
co_async::EpollLoop epoll_loop;
/* 处理定时器事件 */
co_async::TimerLoop timer_loop;

inline co_async::Task<void, co_async::EpollFilePromise>
wait_file(co_async::EpollLoop &loop, int fileno, co_async::EpollEventMask events) {
    co_await co_async::EpollFileAwaiter(loop, fileno, events | EPOLLONESHOT);
}

co_async::Task<std::string> reader() {
    auto which = co_await when_any(wait_file(epoll_loop, 0, EPOLLIN),
               co_async::sleep_for(timer_loop, 1s));
    if (which.index() != 0) {
        co_return "no input over a sec";
    }
    std::string s;
    while (true) {
        char c;
        ssize_t len = read(0, &c, 1);
        if (len == -1) {
            if (errno != EWOULDBLOCK) [[unlikely]] {
                throw std::system_error(errno, std::system_category());
            }
            break;
        }
        s.push_back(c);
    }
    co_return s;
}

co_async::Task<void> async_main() {
    while (true) {
        auto s = co_await reader();
        debug(), "has input:", s;
        if (s == "quit\n") break;
    }
}

int main() {
    int attr = 1;
    ioctl(0, FIONBIO, &attr);

    auto t = async_main();
    t.mCoroutine.resume();
    while (!t.mCoroutine.done()) {
        if (auto delay = timer_loop.run()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*delay).count();
            epoll_loop.run(ms);
        } else {
            epoll_loop.run(-1);
        }
    }
    return 0;
}