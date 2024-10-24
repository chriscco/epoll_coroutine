#pragma once
#include <coroutine>
#include <exception>
#include <utility>
#include "previous_waiter.hpp"
#include "uninitialized.hpp"

namespace co_async {

template<class T>
struct Promise {
    /**
     * 表示协程在开始时会被挂起, 直到外部代码显式恢复它
     * @return
     */
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }
    /**
     * 允许协程结束后恢复前一个协程
     * @return
     */
    auto final_suspend() noexcept {
        return Previous_awaiter(m_Previous);
    }

    void unhandled_exception() noexcept {
        m_Exception = std::current_exception();
    }

    void return_value(T &&ret) {
        m_results.putValue(std::move(ret));
    }

    void return_value(T const &ret) {
        m_results.putValue(ret);
    }

    T result() {
        if (m_Exception) [[unlikely]] {
            std::rethrow_exception(m_Exception);
        }
        return m_results.moveValue();
    }

    /**
     * 返回协程的句柄, 允许主程序通过句柄和协程交互
     * @return
     */
    auto get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> m_Previous;
    std::exception_ptr m_Exception{};
    Uninitialized<T> m_results;

    Promise &operator=(Promise &&) = delete;
};

template <>
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return Previous_awaiter(m_Previous);
    }

    void unhandled_exception() noexcept {
        m_Exception = std::current_exception();
    }

    void return_void() noexcept {}

    void result() {
        if (m_Exception) [[unlikely]] {
            std::rethrow_exception(m_Exception);
        }
    }

    auto get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> m_Previous;
    std::exception_ptr m_Exception{};

    Promise &operator=(Promise &&) = delete;
};

template<class T = void, class P = Promise<T>>
struct [[nodiscard]] Task {
    using promise_type = P;
public:
    Task(std::coroutine_handle<promise_type> routine = nullptr) noexcept : mCoroutine(routine) {}

    Task(Task &&that) noexcept : mCoroutine(that.mCoroutine) {
        that.mCoroutine = nullptr;
    }

    Task &operator=(Task &&that) noexcept {
        std::swap(mCoroutine, that.mCoroutine);
    }

    ~Task() {
        if (mCoroutine) mCoroutine.destroy();
    }

    struct Awaiter {
        std::coroutine_handle<promise_type> mCoroutine;
        bool await_ready() const noexcept {
            return false;
        };
        /**
         * 将当前协程句柄保存到 promise.m_Previous
         * 并返回 mCoroutine, 用于控制协程的恢复。
         * @param routine
         * @return
         */
        std::coroutine_handle<promise_type>
        await_suspend(std::coroutine_handle<> routine) const noexcept {
            promise_type &promise = mCoroutine.promise();
            promise.m_Previous = routine;
            return mCoroutine;
        }

        T await_resume() const {
            return mCoroutine.promise().result();
        }
    };

    /**
     * 挂起当前协程
     */
    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);
    }

    /**
     * Task到std::coroutine_handle<promise_type>的隐式转换
     * @return
     */
    operator std::coroutine_handle<promise_type>() const noexcept {
        return mCoroutine;
    }
private:
    std::coroutine_handle<promise_type> mCoroutine;
};

/**
 * 用于在事件循环中运行 Task 并等待结果
 */
template<class Loop, class T, class P>
T run_task(Loop &loop, Task<T, P> const& t) {
    auto a = t.operator co_await(); // 获取Awaiter对象a
    a.await_suspend(std::noop_coroutine()).resume(); // 将当前协程挂起并准备恢复
    while(loop.run()); // 运行直到没有更多的任务需要处理
    return a.await_resume(); // 获取协程的返回值并返回
}

/**
 * 用在并发情况下不需要等待结果时
 */
template <class T, class P>
void spawn_task(Task<T, P> const &t) {
    auto a = t.operator co_await();
    a.await_suspend(std::noop_coroutine()).resume();
}
}
