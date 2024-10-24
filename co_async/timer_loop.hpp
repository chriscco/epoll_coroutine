#pragma once
#include "rbtree.hpp"
#include "task.hpp"
#include <optional>
#include <chrono>
namespace co_async {

struct SleepUntilPromise : RbTree<SleepUntilPromise>::RbNode, Promise<void> {
    /* 储存计时器的过期时间点 */
    std::chrono::system_clock::time_point mExpireTime;

    auto get_return_object() {
        return std::coroutine_handle<SleepUntilPromise>::from_promise(*this);
    }

    SleepUntilPromise &operator=(SleepUntilPromise &&) = delete;

    /**
     * 比较两个对象的过期时间, 方便红黑树排序
     * @return
     */
    friend bool operator<(SleepUntilPromise const &lhs,
                          SleepUntilPromise const &rhs) noexcept {
        return lhs.mExpireTime < rhs.mExpireTime;
    }
};

struct TimerLoop {
    RbTree<SleepUntilPromise> m_RBTimer;

    bool hasEvent() const noexcept {
        return !m_RBTimer.empty();
    }

    /**
     * 加入到红黑树中, 注册一个新的定时器
     * @param promise
     */
    void addTimer(SleepUntilPromise &promise) {
        m_RBTimer.insert(promise);
    }

    /**
     * 负责检查定时器是否到期
     * 循环遍历红黑树, 检查当前时间与定时器的过期时间
     * 如果过期时间到达, 则从树中删除相应的 promise, 并恢复对应的协程
     * 如果没有定时器到期, 则返回下一个定时器的剩余时间, 方便调度
     */
    std::optional<std::chrono::system_clock::duration> run() {
        while (!m_RBTimer.empty()) {
            auto now = std::chrono::system_clock::now();
            auto &promise = m_RBTimer.front();
            if (promise.mExpireTime < now) {
                m_RBTimer.erase(promise);
                std::coroutine_handle<SleepUntilPromise>::from_promise(promise)
                .resume();
            } else {
                return promise.mExpireTime - now;
            }
        }
        return std::nullopt;
     }

     TimerLoop& operator=(TimerLoop&&) = delete;
};

struct SleepAwaiter {
private:
    TimerLoop &mLoop;
    std::chrono::system_clock::time_point mExpireTime;

public:
    SleepAwaiter(TimerLoop &mLoop, const std::chrono::system_clock::time_point &mExpireTime) :
            mLoop(mLoop), mExpireTime(mExpireTime) {}

    using ClockType = std::chrono::system_clock;

    bool await_ready() const noexcept { return false; }

    /**
     * 在协程挂起时被调用
     * 它将过期时间设置到对应的 SleepUntilPromise 对象中
     * 并将该对象添加到 TimerLoop 中
     * 当协程被挂起时它会注册一个定时器, 以便在到达指定时间后恢复执行。
     * @param coroutine
     */
    void await_suspend(std::coroutine_handle<SleepUntilPromise> coroutine) {
        auto& promise = coroutine.promise();
        promise.mExpireTime = mExpireTime;
        mLoop.addTimer(promise);
    }
};

template<class Clock, class Duration>
inline Task<void, SleepUntilPromise>
sleep_until(TimerLoop& loop, std::chrono::time_point<Clock, Duration> expireTime) {
    co_await SleepAwaiter(loop,
              std::chrono::time_point_cast<SleepAwaiter::ClockType::duration>(expireTime));
}

template<class Rep, class Period>
inline Task<void, SleepUntilPromise>
sleep_for(TimerLoop& loop, std::chrono::duration<Rep, Period> duration) {
    auto d = std::chrono::duration_cast<SleepAwaiter::ClockType::duration>(duration);
    if (d.count() > 0) {
        co_await SleepAwaiter(loop, SleepAwaiter::ClockType::now() + d);
    }
}
}