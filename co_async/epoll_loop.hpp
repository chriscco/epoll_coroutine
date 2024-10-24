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
}

}