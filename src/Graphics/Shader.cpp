#include "Graphics/Shader.hpp"
#include "Core/Json.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Graphics/GlCheck.hpp"
#include <format>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>

namespace qlab::gfx {
namespace {
std::filesystem::path& rootRef() {
    static std::filesystem::path p = core::assetDir() / "Shaders";
    return p;
}
} // namespace

std::filesystem::path shaderRoot() {
    return rootRef();
}
void setShaderRoot(std::filesystem::path p) {
    rootRef() = std::move(p);
}

Result<std::string> preprocessShader(const std::string& source, const std::filesystem::path& dir,
                                     const std::vector<std::string>& defines, int depth) {
    if (depth > 16)
        return fail(ErrorCode::Gfx_ + 1, "shader #include nesting too deep");
    std::istringstream in(source);
    std::string line, out;
    bool versionSeen = false;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.starts_with("#version")) {
            out += line + "\n";
            versionSeen = true;
            if (depth == 0)
                for (auto& d : defines)
                    out += "#define " + d + "\n";
            continue;
        }
        if (trimmed.starts_with("#include")) {
            auto q1 = trimmed.find('"');
            auto q2 = trimmed.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos)
                return fail(ErrorCode::Gfx_ + 2, "malformed #include: " + line);
            std::filesystem::path inc = dir / trimmed.substr(q1 + 1, q2 - q1 - 1);
            auto text = core::readTextFile(inc);
            if (!text)
                return fail(ErrorCode::Gfx_ + 3, "cannot read shader include " + inc.string());
            auto sub = preprocessShader(*text, inc.parent_path(), defines, depth + 1);
            if (!sub)
                return sub;
            out += *sub;
            continue;
        }
        out += line + "\n";
    }
    if (depth == 0 && !versionSeen) {
        std::string pre = "#version 410 core\n";
        for (auto& d : defines)
            pre += "#define " + d + "\n";
        out = pre + out;
    }
    return out;
}

Result<std::string> preprocessShaderFile(const std::filesystem::path& path,
                                         const std::vector<std::string>& defines) {
    std::filesystem::path p = path.is_absolute() ? path : shaderRoot() / path;
    auto text = core::readTextFile(p);
    if (!text)
        return fail(ErrorCode::Gfx_ + 3, "cannot read shader " + p.string());
    return preprocessShader(*text, p.parent_path(), defines, 0);
}

Result<GLuint> ShaderProgram::compileStage(GLenum type, const std::string& src,
                                           const std::string& name) {
    GLuint sh = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(len > 0 ? len : 1), '\0');
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        glDeleteShader(sh);
        const char* stage = type == GL_VERTEX_SHADER     ? "vertex"
                            : type == GL_FRAGMENT_SHADER ? "fragment"
                                                         : "geometry";
        QXL_LOG_ERROR(Gfx, "shader '{}' {} stage failed:\n{}", name, stage, log);
        return fail(ErrorCode::Gfx_ + 4, std::format("shader '{}' {} stage: {}", name, stage, log));
    }
    return sh;
}

Result<ShaderProgram> ShaderProgram::fromSource(const std::string& name, const std::string& vs,
                                                const std::string& fs, const std::string& gs) {
    auto v = compileStage(GL_VERTEX_SHADER, vs, name);
    if (!v)
        return std::unexpected(v.error());
    auto f = compileStage(GL_FRAGMENT_SHADER, fs, name);
    if (!f) {
        glDeleteShader(*v);
        return std::unexpected(f.error());
    }
    GLuint g = 0;
    if (!gs.empty()) {
        auto gr = compileStage(GL_GEOMETRY_SHADER, gs, name);
        if (!gr) {
            glDeleteShader(*v);
            glDeleteShader(*f);
            return std::unexpected(gr.error());
        }
        g = *gr;
    }
    ShaderProgram p;
    p.name_ = name;
    p.id_ = glCreateProgram();
    glAttachShader(p.id_, *v);
    glAttachShader(p.id_, *f);
    if (g)
        glAttachShader(p.id_, g);
    glLinkProgram(p.id_);
    glDeleteShader(*v);
    glDeleteShader(*f);
    if (g)
        glDeleteShader(g);
    GLint ok = 0;
    glGetProgramiv(p.id_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p.id_, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(len > 0 ? len : 1), '\0');
        glGetProgramInfoLog(p.id_, len, nullptr, log.data());
        QXL_LOG_ERROR(Gfx, "shader '{}' link failed:\n{}", name, log);
        return fail(ErrorCode::Gfx_ + 5, std::format("shader '{}' link: {}", name, log));
    }
    return p;
}

Result<ShaderProgram> ShaderProgram::fromFiles(const ShaderDesc& d) {
    auto vs = preprocessShaderFile(d.vertex, d.defines);
    if (!vs)
        return std::unexpected(vs.error());
    auto fs = preprocessShaderFile(d.fragment, d.defines);
    if (!fs)
        return std::unexpected(fs.error());
    std::string gs;
    if (!d.geometry.empty()) {
        auto g = preprocessShaderFile(d.geometry, d.defines);
        if (!g)
            return std::unexpected(g.error());
        gs = *g;
    }
    return fromSource(d.name.empty() ? d.vertex.string() : d.name, *vs, *fs, gs);
}

void ShaderProgram::use() const {
    glUseProgram(id_);
}
void ShaderProgram::release() {
    if (id_)
        glDeleteProgram(id_);
    id_ = 0;
}
GLint ShaderProgram::uniform(const std::string& name) const {
    auto it = cache_.find(name);
    if (it != cache_.end())
        return it->second;
    GLint loc = glGetUniformLocation(id_, name.c_str());
    cache_[name] = loc;
    return loc;
}
void ShaderProgram::set(const std::string& n, int v) const {
    glUniform1i(uniform(n), v);
}
void ShaderProgram::set(const std::string& n, float v) const {
    glUniform1f(uniform(n), v);
}
void ShaderProgram::set(const std::string& n, const glm::vec2& v) const {
    glUniform2fv(uniform(n), 1, glm::value_ptr(v));
}
void ShaderProgram::set(const std::string& n, const glm::vec3& v) const {
    glUniform3fv(uniform(n), 1, glm::value_ptr(v));
}
void ShaderProgram::set(const std::string& n, const glm::vec4& v) const {
    glUniform4fv(uniform(n), 1, glm::value_ptr(v));
}
void ShaderProgram::set(const std::string& n, const glm::mat4& v) const {
    glUniformMatrix4fv(uniform(n), 1, GL_FALSE, glm::value_ptr(v));
}
void ShaderProgram::set(const std::string& n, const glm::mat3& v) const {
    glUniformMatrix3fv(uniform(n), 1, GL_FALSE, glm::value_ptr(v));
}
void ShaderProgram::setUnsigned(const std::string& n, unsigned v) const {
    glUniform1ui(uniform(n), v);
}
void ShaderProgram::bindUniformBlock(const std::string& blockName, GLuint binding) const {
    GLuint idx = glGetUniformBlockIndex(id_, blockName.c_str());
    if (idx != GL_INVALID_INDEX)
        glUniformBlockBinding(id_, idx, binding);
}
} // namespace qlab::gfx
