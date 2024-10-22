#include <coroutine>
#include "previous_waiter.hpp"
#include <exception>

namespace co_async {

template<class T>
class Promise {
private:
    std::coroutine_handle<> mPrevious;
    std::exception_ptr mException{};
public:
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
        return Previous_awaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    void result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
        }
    }

    /**
     * 返回协程的句柄, 允许主程序通过句柄和协程交互
     * @return
     */
    auto get() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }
    Promise &operator=(Promise &&) = delete;
};

template<class T = void, class P = Promise<T>>
struct Task {
private:
    using promise_type = P;
    std::coroutine_handle<promise_type> mCoroutine;
public:
    Task(std::coroutine_handle<promise_type> routine = nullptr) : mCoroutine(routine) {}

    Task(Task &&that) noexcept : mCoroutine(that.mCoroutine) {
        that.mCoroutine = nullptr;
    }

    Task &operator=(Task &&that) noexcept {
        std::swap(mCoroutine, that.mCoroutine);
    }

    ~Task() {
        if (mCoroutine)
            mCoroutine.destroy();
    }

    struct Awaiter {
    private:
        std::coroutine_handle<promise_type> mCoroutine;
    public:
        bool await_ready() const noexcept {
            return false;
        };

        /**
         * 将当前协程句柄保存到 promise.mPrevious
         * 并返回 mCoroutine, 用于控制协程的恢复。
         * @param routine
         * @return
         */
        std::coroutine_handle<promise_type>
        await_suspended(std::coroutine_handle<promise_type> routine) const noexcept {
            promise_type &promise = mCoroutine.promise();
            promise.mPrevious = routine;
            return mCoroutine;
        }

        T await_resume() const {
            return mCoroutine.promise().result();
        }
    };

};

}
