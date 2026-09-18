#pragma once
// Spec 18 §10 — GL error checking. In QXL_DEV builds GL_CHECK drains glGetError after a call.
#include <string_view>
namespace qlab::gfx {
// Returns number of errors drained; logs each with the call site.
int drainGlErrors(std::string_view where);
}
#if defined(QXL_DEV)
#define GL_CHECK(call) do { call; ::qlab::gfx::drainGlErrors(#call); } while (0)
#else
#define GL_CHECK(call) do { call; } while (0)
#endif
