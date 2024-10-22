#pragma once
#include <coroutine>
namespace co_async {
    struct Previous_awaiter {
        std::coroutine_handle<> mPrevious;
        /**
         * 检查当前的 awaiter 是否可以立即继续执行
         * 它总是返回 false, 表示当前协程需要挂起, 等待外部事件(在这里是恢复到前一个协程)
         * @return false
         */
        bool await_ready() const noexcept {
            return false;
        }

        /**
         * 当前协程将被挂起, 控制权交给mPrevious
         * @param coroutine
         * @return
         */
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            return mPrevious;
        }

        /**
         * 协程恢复时被调用
         */
        void await_resume() const noexcept {}
    };
}