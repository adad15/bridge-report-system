#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {

CommitLatch::CommitLatch() : state_(std::make_shared<State>()) {}

std::function<void(bool)> CommitLatch::callback() const {
    const auto state = state_;
    return [state](bool committed) {
        {
            std::lock_guard lock(state->mutex);
            state->completed = true;
            state->committed = committed;
        }
        state->changed.notify_all();
    };
}

bool CommitLatch::wait(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(state_->mutex);
    if (!state_->changed.wait_for(lock, timeout, [this] { return state_->completed; })) {
        return false;
    }
    return state_->committed;
}

}  // namespace bridge_report::db
