#pragma once
#include <coroutine>
#include <optional>
#include <vector>
#include "error_handling.hpp"

namespace co_async {

using EpollEventMask = std::uint32_t;

struct EpollFilePromise : Promise<EpollEventMask> {
    std::coroutine_handle<> get() override {
        return std::coroutine_handle<EpollFilePromise>::from_promise(*this);
    }
    EpollFilePromise& operator=(EpollFilePromise&&) = delete;
    ~EpollFilePromise();
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
    static bool await_ready() noexcept { return false; }

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
        auto &promise = *(EpollFilePromise *)event.data.ptr;
        promise.m_epollAwaiter->m_resumeEvents = event.events;
    }
    /* 恢复所有相关的协程 */
    for (int i = 0; i < res; i++) {
        auto &event = m_buffer[i];
        auto &promise = *(EpollFilePromise *)event.data.ptr;
        std::coroutine_handle<EpollFilePromise>::from_promise(promise).resume();
    }
    return true;
}

}