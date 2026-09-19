#include "Core/EventBus.hpp"
namespace qlab::core {
Subscription& Subscription::operator=(Subscription&& o) noexcept {
    reset();
    bus_ = o.bus_;
    type_ = o.type_;
    id_ = o.id_;
    o.bus_ = nullptr;
    o.id_ = 0;
    return *this;
}
Subscription::~Subscription() {
    reset();
}
void Subscription::reset() {
    if (bus_ && id_)
        bus_->unsubscribe(type_, id_);
    bus_ = nullptr;
    id_ = 0;
}
void EventBus::unsubscribe(std::type_index t, std::uint64_t id) {
    std::lock_guard lk(mu_);
    auto it = subs_.find(t);
    if (it == subs_.end())
        return;
    std::erase_if(it->second, [&](const Sub& s) { return s.id == id; });
}
void EventBus::dispatch(std::type_index t, const std::any& a) {
    std::vector<Sub> copy;
    {
        std::lock_guard lk(mu_);
        auto it = subs_.find(t);
        if (it == subs_.end())
            return;
        copy = it->second;
    }
    for (auto& s : copy)
        s.fn(a);
}
void EventBus::drain() {
    std::vector<Queued> q;
    {
        std::lock_guard lk(qmu_);
        q.swap(queue_);
    }
    for (auto& e : q)
        dispatch(e.type, e.payload);
}
} // namespace qlab::core
