#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <string>
#include "co_async/debug.hpp"
#include "co_async/timer_loop.hpp"
#include "co_async/task.hpp"
#include "co_async/epoll_loop.hpp"

co_async::EpollLoop g_loop;

inline co_async::Task<void, co_async::EpollFilePromise>
wait_file(co_async::EpollLoop &loop, int fileno, co_async::EpollEventMask events) {
    co_await co_async::EpollFileAwaiter(loop, fileno, events);
}

co_async::Task<std::string> reader() {
    co_await wait_file(g_loop, 0, EPOLLIN);
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
        g_loop.run();
    }
    return 0;
}