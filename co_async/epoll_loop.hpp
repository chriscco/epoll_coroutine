#pragma once
#include <coroutine>
#include <optional>
#include <vector>
#include <span>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include "error_handling.hpp"

namespace co_async {

using EpollEventMask = std::uint32_t;

struct EpollFilePromise : Promise<void> {
    auto get_return_object() {
        return std::coroutine_handle<EpollFilePromise>::from_promise(*this);
    }
    EpollFilePromise& operator=(EpollFilePromise&&) = delete;
    inline ~EpollFilePromise();
    struct EpollFileAwaiter* m_epollAwaiter{};
};

struct EpollLoop {
private:
    int m_epoll = checkError(epoll_create(0));
    size_t m_count = 0;
    struct epoll_event m_buffer[64];
    std::vector<std::coroutine_handle<>> m_queue;
public:
    inline bool addListener(EpollFilePromise& promise, int control);
    inline void removeListener(int Fileno);
    inline bool run(std::optional<std::chrono::system_clock::duration> timeout = std::nullopt);

    bool hasEvent() {
        return m_count != 0;
    }

    ~EpollLoop() {
        close(m_epoll);
    }

    EpollLoop &operator=(EpollLoop &&) = delete;
};

struct EpollFileAwaiter {
    EpollFileAwaiter(EpollLoop& loop, int fileno, EpollEventMask event) :
            m_loop(loop), fileno(fileno), m_events(event){};

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<EpollFilePromise> coroutine) {
        auto &promise = coroutine.promise();
        promise.m_epollAwaiter = this;
        if (!m_loop.addListener(promise, control)) {
            promise.m_epollAwaiter = nullptr;
            coroutine.resume();
        }
    }

    EpollEventMask await_resume() const noexcept {
        return m_resumeEvents;
    }

    EpollLoop& m_loop;
    int control = EPOLL_CTL_ADD;
    int fileno;
    EpollEventMask m_events;
    EpollEventMask m_resumeEvents;
};

EpollFilePromise::~EpollFilePromise() {
    if (m_epollAwaiter) {
        m_epollAwaiter->m_loop.removeListener(m_epollAwaiter->fileno);
    }
}

bool EpollLoop::addListener(EpollFilePromise& promise, int control) {
    struct epoll_event event{};
    event.events = promise.m_epollAwaiter->m_events;
    event.data.ptr = &promise;
    int res = epoll_ctl(m_epoll, control, promise.m_epollAwaiter->fileno, &event);
    if (res == -1) return false;
    else if (control == EPOLL_CTL_ADD) m_count++;
    return true;
}

void EpollLoop::removeListener(int Fileno) {
    checkError(epoll_ctl(m_epoll, EPOLL_CTL_DEL, Fileno, nullptr));
    --m_count;
}

bool EpollLoop::run(std::optional<std::chrono::system_clock::duration> timeout) {
    /* 逐个恢复挂起的协程 */
    while (!m_queue.empty()) {
        auto task = m_queue.back();
        m_queue.pop_back();
        task.resume();
    }
    if (m_count == 0) return false;
    int timeoutMS = -1;
    if (timeout) {
        timeoutMS = std::chrono::duration_cast<std::chrono::milliseconds>(*timeout).count();
    }
    /* 等待事件发生 */
    int res = checkError(epoll_wait(m_epoll, m_buffer, std::size(m_buffer), timeoutMS));
    /* 将发生的事件信息写入相应的 promise.m_epollAwaiter->m_resumeEvents */
    for (int i = 0; i < res; i++) {
        auto &event = m_buffer[i];
        auto &promise = *(EpollFilePromise *) event.data.ptr;
        promise.m_epollAwaiter->m_resumeEvents = event.events;
    }
    /* 恢复所有相关的协程 */
    for (int i = 0; i < res; i++) {
        auto &event = m_buffer[i];
        auto &promise = *(EpollFilePromise *) event.data.ptr;
        std::coroutine_handle<EpollFilePromise>::from_promise(promise).resume();
    }
    return true;
}

/**
 * 管理异步文件描述符
 */
struct [[nodiscard]] AsyncFile {
public:
    /* -1表示没有有效的文件描述符 */
    AsyncFile() : m_fileNo(-1) {}

    explicit AsyncFile(int fileNo) noexcept : m_fileNo(fileNo) {}

    AsyncFile(AsyncFile &&that) noexcept : m_fileNo(that.m_fileNo) {
        that.m_fileNo = -1;
    }

    AsyncFile &operator=(AsyncFile &&that) noexcept {
        std::swap(m_fileNo, that.m_fileNo);
        return *this;
    }

    ~AsyncFile() {
        if (m_fileNo != -1) close(m_fileNo);
    }

    int fileNo() const noexcept {
        return m_fileNo;
    }

    int releaseOwnership() noexcept {
        int ret = m_fileNo;
        m_fileNo = -1;
        return ret;
    }

    void setNonblock() const {
        int attr = 1;
        checkError(ioctl(fileNo(), FIONBIO, &attr));
    }
private:
    int m_fileNo;
};

#if 0
/**
 *
 * @param loop 事件循环, 在一个循环中不断地检查注册的文件描述符的状态
 * @param file 要监听的文件描述符
 * @param events 要等待的事件类型(如可读、可写)
 * @return
 */
inline Task<EpollEventMask, EpollFilePromise>
wait_file_event(EpollLoop& loop, AsyncFile& file, EpollEventMask events) {
        co_return co_await EpollFileAwaiter(loop, file.fileNo(), events);
}
/**
 * 同步读取文件内容到提供的缓冲区
 * @param file 要读取的文件
 * @param buffer 连续的一段内存区间, 类似于一个轻量级的只读数组容器
 * @return
 */
inline size_t readFileSync(AsyncFile& file, std::span<char> buffer) {
    return checkErrorNonBlock(
            read(file.fileNo(), buffer.data(), buffer.size())
    );
}
/**
 * 同步写入数据到文件
 * @param file 要读取的文件
 * @param buffer 要写入的数据缓冲区
 * @return
 */
inline size_t writeFileSync(AsyncFile& file, std::span<char const> buffer) {
    return checkErrorNonBlock(
            write(file.fileNo(), buffer.data(), buffer.size()));
}

inline Task<size_t> read_file(EpollLoop& loop, AsyncFile& file,
                              std::span<char> buffer) {
    co_await wait_file_event(loop, file, EPOLLIN | EPOLLRDHUP);
    auto len = readFileSync(file, buffer);
    co_return len;
}

inline Task<size_t> write_file(EpollLoop& loop, AsyncFile& file,
                               std::span<char const> buffer) {
    co_await wait_file_event(loop, file, EPOLLIN | EPOLLRDHUP);
    auto len = writeFileSync(file, buffer);
    co_return len;
}
#endif
}