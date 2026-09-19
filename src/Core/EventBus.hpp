#pragma once
// Spec 04 §4 — typed event bus: immediate publish on the caller thread, queued post drained
// on the main thread. Subscriptions are RAII (unsubscribe on destroy).
#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>
namespace qlab::core {
class EventBus;
class Subscription {
  public:
    Subscription() = default;
    Subscription(EventBus* bus, std::type_index t, std::uint64_t id)
        : bus_(bus), type_(t), id_(id) {}
    Subscription(Subscription&& o) noexcept { *this = std::move(o); }
    Subscription& operator=(Subscription&& o) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription();
    void reset();

  private:
    EventBus* bus_ = nullptr;
    std::type_index type_ = typeid(void);
    std::uint64_t id_ = 0;
};

class EventBus {
  public:
    template <class E> [[nodiscard]] Subscription subscribe(std::function<void(const E&)> fn) {
        std::lock_guard lk(mu_);
        auto id = ++nextId_;
        subs_[typeid(E)].push_back(
            {id, [fn = std::move(fn)](const std::any& a) { fn(std::any_cast<const E&>(a)); }});
        return Subscription(this, typeid(E), id);
    }
    // Deliver now on the calling thread (main thread only, spec 02 §6).
    template <class E> void publish(const E& e) { dispatch(typeid(E), std::any(e)); }
    // Queue for delivery on the next drain() (any thread).
    template <class E> void post(E e) {
        std::lock_guard lk(qmu_);
        queue_.push_back({typeid(E), std::any(std::move(e))});
    }
    void drain();
    void unsubscribe(std::type_index t, std::uint64_t id);
    std::size_t pending() const {
        std::lock_guard lk(qmu_);
        return queue_.size();
    }

  private:
    struct Sub {
        std::uint64_t id;
        std::function<void(const std::any&)> fn;
    };
    struct Queued {
        std::type_index type;
        std::any payload;
    };
    void dispatch(std::type_index t, const std::any& a);
    mutable std::mutex mu_;
    mutable std::mutex qmu_;
    std::unordered_map<std::type_index, std::vector<Sub>> subs_;
    std::vector<Queued> queue_;
    std::uint64_t nextId_ = 0;
};
} // namespace qlab::core
