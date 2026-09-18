#pragma once
// Spec 18 §2 — shader programs: GLSL 410 source with `#include "file"` (relative to
// Assets/Shaders) and `#define` permutations; uniform setters; UBO block binding.
#include "Core/Error.hpp"
#include "Graphics/GlObjects.hpp"
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace qlab::gfx {

struct ShaderDesc {
    std::string name;                       // for logs
    std::filesystem::path vertex;           // paths relative to shaderRoot(), or absolute
    std::filesystem::path fragment;
    std::filesystem::path geometry;         // optional
    std::vector<std::string> defines;       // "FOO", "BAR 2"
};

std::filesystem::path shaderRoot();         // Assets/Shaders
void setShaderRoot(std::filesystem::path p);

// Expands #include lines recursively; returns the flattened source. Pure (no GL).
Result<std::string> preprocessShader(const std::string& source, const std::filesystem::path& dir,
                                     const std::vector<std::string>& defines, int depth = 0);
Result<std::string> preprocessShaderFile(const std::filesystem::path& path,
                                         const std::vector<std::string>& defines);

class ShaderProgram : public GlHandle {
public:
    ShaderProgram() = default;
    ~ShaderProgram() override { release(); }
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;

    static Result<ShaderProgram> fromFiles(const ShaderDesc& d);
    static Result<ShaderProgram> fromSource(const std::string& name, const std::string& vs,
                                            const std::string& fs, const std::string& gs = "");

    void use() const;
    GLint uniform(const std::string& name) const; // cached lookup, -1 if absent
    void set(const std::string& n, int v) const;
    void set(const std::string& n, float v) const;
    void set(const std::string& n, const glm::vec2& v) const;
    void set(const std::string& n, const glm::vec3& v) const;
    void set(const std::string& n, const glm::vec4& v) const;
    void set(const std::string& n, const glm::mat4& v) const;
    void set(const std::string& n, const glm::mat3& v) const;
    void setUnsigned(const std::string& n, unsigned v) const;
    // Binds the uniform block `blockName` to `binding` (no-op if the block is absent).
    void bindUniformBlock(const std::string& blockName, GLuint binding) const;
    const std::string& name() const { return name_; }
protected:
    void release() override;
private:
    static Result<GLuint> compileStage(GLenum type, const std::string& src, const std::string& name);
    std::string name_;
    mutable std::map<std::string, GLint> cache_;
};

} // namespace qlab::gfx
