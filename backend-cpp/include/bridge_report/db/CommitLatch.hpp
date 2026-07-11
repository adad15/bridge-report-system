#pragma once

#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace bridge_report::db {

class CommitLatch {
public:
    CommitLatch();
    std::function<void(bool)> callback() const;
    bool wait(std::chrono::milliseconds timeout = std::chrono::seconds(30)) const;

private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool completed{false};
        bool committed{false};
    };
    std::shared_ptr<State> state_;
};

}  // namespace bridge_report::db
