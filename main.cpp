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

/**
 * 等待特定文件描述符（fileno）上的 IO 事件
 * 使用 EpollFileAwaiter 来挂起协程, 直到文件描述符上有数据可读(或事件发生)
 * events | EPOLLONESHOT 表示这个事件在触发后只会被监听一次
 * @param loop
 * @param fileno
 * @param events
 * @return
 */
inline co_async::Task<void, co_async::EpollFilePromise>
wait_file(co_async::EpollLoop &loop, int fileno, co_async::EpollEventMask events) {
    co_await co_async::EpollFileAwaiter(loop, fileno, events | EPOLLONESHOT);
}

/**
 * 负责从标准输入(文件描述符 0)读取字符
 * 使用 when_any, 它同时等待两个事件: 从输入读取数据或在 1 秒内超时
 * @return
 */
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

/**
 * co_await: 挂起协程, 等待异步操作完成
 * co_return: 结束协程并返回Task<T>类型
 * @return
 */
int main() {
    /* 非阻塞模式 */
    int attr = 1;
    ioctl(0, FIONBIO, &attr);

    auto t = async_main();
    t.mCoroutine.resume();
    while (!t.mCoroutine.done()) {
        /**
         * timer_loop.run() 返回一个可选的延迟, 如果有延迟就传递这个延迟给 epoll_loop.run(ms)
         * 如果没有延迟，则 epoll_loop.run(-1) 会阻塞, 直到有 I/O 事件发生
         */
        if (auto delay = timer_loop.run()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*delay).count();
            epoll_loop.run(ms);
        } else {
            epoll_loop.run(-1);
        }
    }
    return 0;
}