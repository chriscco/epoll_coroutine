#pragma once
#include <coroutine>
namespace co_async {
    struct Previous_awaiter {
        std::coroutine_handle<> mPrevious;
        /**
         * 当前协程将被挂起, 控制权交给mPrevious
         * @return
         */
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            return mPrevious;
        }

        void await_resume() const noexcept {}
    };
}