#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Math/MathParser.hpp"
#include <format>

namespace qlab::ui {

BasicMathRenderer::BasicMathRenderer(const math::MathFont& font, std::size_t cacheEntries)
    : font_(font), capacity_(cacheEntries == 0 ? 1 : cacheEntries) {}

std::string BasicMathRenderer::key(std::string_view latex, const math::MathStyle& s) {
    return std::format("{}|{:.2f}|{}", latex, s.sizePx, s.display ? 'D' : 'T');
}

Result<std::shared_ptr<const math::LayoutResult>> BasicMathRenderer::render(std::string_view latex, const math::MathStyle& style) {
    std::string k = key(latex, style);
    {
        std::lock_guard lk(mu_);
        auto it = cache_.find(k);
        if (it != cache_.end()) {
            order_.splice(order_.begin(), order_, it->second.second);
            ++hits_;
            return it->second.first;
        }
    }
    math::MathParser parser(latex);
    math::ParseOutput po = parser.parse();
    if (!po.root) return fail(ErrorCode::Ui_ + 1, "math parse produced no tree");
    auto lr = std::make_shared<math::LayoutResult>(math::layoutMath(*po.root, font_, style));
    lr->warnings.insert(lr->warnings.begin(), po.warnings.begin(), po.warnings.end());
    lr->errors = po.errors;
    if (!lr->errors.empty()) {
        Error e(ErrorCode::Ui_ + 2, "math layout failed: " + lr->errors.front());
        for (auto& w : lr->warnings) e.withNote(w);
        return std::unexpected(std::move(e));
    }
    std::lock_guard lk(mu_);
    order_.push_front(k);
    cache_[k] = {lr, order_.begin()};
    while (cache_.size() > capacity_) {
        cache_.erase(order_.back());
        order_.pop_back();
    }
    return lr;
}

void BasicMathRenderer::paint(const math::LayoutResult& lr, math::MathCanvas& canvas, double x, double baselineY) const {
    math::paintMath(lr, canvas, x, baselineY, font_);
}
std::optional<math::SymbolHit> BasicMathRenderer::hitTest(const math::LayoutResult& lr, double ox, double oy, double px, double py) const {
    return math::hitTestMath(lr, ox, oy, px, py);
}
std::size_t BasicMathRenderer::cacheSize() const { std::lock_guard lk(mu_); return cache_.size(); }
void BasicMathRenderer::clearCache() { std::lock_guard lk(mu_); cache_.clear(); order_.clear(); }

} // namespace qlab::ui
