#pragma once
// Spec 20 §1/§6 — fallback LaTeX renderer with an LRU layout cache. Never throws.
#include "Core/Error.hpp"
#include "UI/Math/MathLayout.hpp"
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace qlab::ui {

class BasicMathRenderer {
  public:
    explicit BasicMathRenderer(const math::MathFont& font, std::size_t cacheEntries = 512);

    // Parse + layout. Errors are structural failures only (never for unknown commands).
    Result<std::shared_ptr<const math::LayoutResult>> render(std::string_view latex,
                                                             const math::MathStyle& style);
    void paint(const math::LayoutResult& lr, math::MathCanvas& canvas, double x,
               double baselineY) const;
    std::optional<math::SymbolHit> hitTest(const math::LayoutResult& lr, double originX,
                                           double baselineY, double px, double py) const;

    std::size_t cacheSize() const;
    std::size_t cacheHits() const { return hits_; }
    void clearCache();

  private:
    static std::string key(std::string_view latex, const math::MathStyle& s);
    const math::MathFont& font_;
    std::size_t capacity_;
    mutable std::mutex mu_;
    std::list<std::string> order_; // most recent at front
    std::unordered_map<std::string, std::pair<std::shared_ptr<const math::LayoutResult>,
                                              std::list<std::string>::iterator>>
        cache_;
    std::size_t hits_ = 0;
};

} // namespace qlab::ui
