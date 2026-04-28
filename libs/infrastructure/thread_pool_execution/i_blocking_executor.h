#pragma once

#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/task.hh>

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

class IBlockingExecutor {
public:
    virtual ~IBlockingExecutor() = default;

    virtual void submit_void(std::function<void()> task) = 0;

    template <typename Func>
    auto submit(Func&& func)
        -> seastar::future<std::invoke_result_t<Func>>
    {
        using Result = std::invoke_result_t<Func>;

        auto promise = std::make_shared<seastar::promise<Result>>();
        auto future = promise->get_future();

        auto* reactor = &seastar::engine();

        submit_void([
                        promise,
                        reactor,
                        task = std::forward<Func>(func)
        ]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    task();

                    reactor->add_urgent_task(
                        seastar::make_task([promise]() mutable {
                            promise->set_value();
                        })
                        );
                } else {
                    Result result = task();

                    reactor->add_urgent_task(
                        seastar::make_task([
                                               promise,
                                               result = std::move(result)
                    ]() mutable {
                            promise->set_value(std::move(result));
                        })
                        );
                }
            } catch (...) {
                auto eptr = std::current_exception();

                reactor->add_urgent_task(
                    seastar::make_task([promise, eptr]() mutable {
                        promise->set_exception(eptr);
                    })
                    );
            }
        });

        return future;
    }
};