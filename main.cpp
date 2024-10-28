#include "co_async/debug.hpp"
#include "co_async/timer_loop.hpp"
#include "co_async/task.hpp"
#include "co_async/epoll_loop.hpp"
#include "co_async/async_loop.hpp"

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <string>
#include <chrono>
#include <fcntl.h>
#include <termios.h>


/* timeout内没有输入时会输出信息 */
#define WHEN_ANY
#define TEST_MAIN 1

using namespace std::chrono_literals;

[[gnu::constructor]] static void disable_canon() {
    struct termios tc;
    tcgetattr(STDIN_FILENO, &tc);
    tc.c_lflag &= ~ICANON;
    tc.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tc);
}

/* 处理IO事件的循环 */
co_async::EpollLoop epoll_loop;
/* 处理定时器事件 */
co_async::TimerLoop timer_loop;
/* 同时处理TimerLoop和EpollLoop */
co_async::AsyncLoop async_loop;

/**
 * 等待特定文件描述符（fileno）上的 IO 事件
 * 使用 EpollFileAwaiter 来挂起协程, 直到文件描述符上有数据可读(或事件发生)
 * events | EPOLLONESHOT 表示这个事件在触发后只会被监听一次
 * @param loop
 * @param fileno
 * @param events uint32_t
 * @return
 */
inline co_async::Task<void, co_async::EpollFilePromise>
wait_file(co_async::EpollLoop &loop, int fileno, co_async::EpollEventMask events) {
    co_await co_async::EpollFileAwaiter(loop, fileno, events | EPOLLONESHOT);
}

co_async::Task<std::string> read_string(co_async::EpollLoop& loop, co_async::AsyncFile& file) {
    /* @code{EPOLLET} 只要buffer中仍存在内容就会持续读写 */
    co_await co_async::wait_file_event(loop, file, EPOLLIN | EPOLLET);
    std::string s;
    size_t chunk = 15;
    while (true) {
        char c;
        size_t exist = s.size();
        s.append(chunk, 0);
        std::span<char> buffer(s.data() + exist, chunk);
        auto len = co_async::readFileSync(file, buffer);
        if (len != chunk) {
            s.resize(exist + len);
            break;
        }
        if (chunk < 65536) chunk *= 3;
    }
    co_return s;
}

co_async::Task<void> async_main() {
    int file_in = co_async::checkError(open("/dev/stdin", O_RDONLY | O_NONBLOCK));
    co_async::AsyncFile file(STDIN_FILENO);
    while (true) {
        auto res = co_await co_async::when_any(read_string(async_loop, file),
                                       co_async::sleep_for(async_loop, 1s));
        debug(), "Receives Input: ", res;
    }
}

/**
 * co_await: 挂起协程, 等待异步操作完成
 * co_return: 结束协程并返回Task<T>类型
 * @return
 */
int main() {
#if !TEST_MAIN
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
#else
    co_async::run_task(async_loop, async_main());
#endif
    return 0;
}