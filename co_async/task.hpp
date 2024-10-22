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
}