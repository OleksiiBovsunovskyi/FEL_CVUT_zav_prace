module;
#include "pgr.h"
#include <string>
#include <vector>
#include <utility>

module RenderBufferManager;

import BlitShader;


bool RenderBufferManager::allocTexture(Entry& e, int w, int h) {
    const int tw = e.desc.fixedWidth  ? e.desc.fixedWidth  : w;
    const int th = e.desc.fixedHeight ? e.desc.fixedHeight : h;

    if (!e.texture)
        glGenTextures(1, &e.texture);

    glBindTexture(GL_TEXTURE_2D, e.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)e.desc.internalFormat,
                 tw, th, 0, e.desc.format, e.desc.dataType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)e.desc.minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)e.desc.magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     (GLint)e.desc.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     (GLint)e.desc.wrapT);

    if (e.desc.depthCompare) {
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, e.desc.borderColor);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    e.ready = true;
    return true;
}

bool RenderBufferManager::ensureFBO(Entry& e) {
    if (e.fbo) return true;   // already exists; texture ID is stable across resize

    glGenFramebuffers(1, &e.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);

    const bool isDepth =
        e.desc.internalFormat == GL_DEPTH_COMPONENT24   ||
        e.desc.internalFormat == GL_DEPTH_COMPONENT32F  ||
        e.desc.internalFormat == GL_DEPTH24_STENCIL8;

    if (isDepth) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, e.texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, e.texture, 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        logError(std::string("RenderBufferManager: FBO incomplete for '") +
                 (e.debugName ? e.debugName : "?") + "'");
        glDeleteFramebuffers(1, &e.fbo);
        e.fbo = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void RenderBufferManager::freeEntry(Entry& e) {
    if (e.fbo)     { glDeleteFramebuffers(1, &e.fbo);  e.fbo     = 0; }
    if (e.texture) { glDeleteTextures(1, &e.texture);  e.texture = 0; }
    e.ready = false;
}

void RenderBufferManager::declare(ENamedBuffer name, const char* debugName, BufferDesc desc) {
    Entry e;
    e.desc      = desc;
    e.debugName = debugName;
    entries[key(name)] = std::move(e);
}

bool RenderBufferManager::init(int w, int h) {
    screenW = w; screenH = h;
    bool ok = true;
    for (auto& [k, e] : entries) {
        if (!allocTexture(e, w, h)) {
            logError(std::string("RenderBufferManager::init — failed to allocate '") +
                     (e.debugName ? e.debugName : "?") + "'");
            ok = false;
        }
    }
    return ok;
}

void RenderBufferManager::resize(int w, int h) {
    screenW = w; screenH = h;
    for (auto& [k, e] : entries) {
        if (e.desc.fixedWidth == 0)   
            allocTexture(e, w, h);    
    }
}

void RenderBufferManager::shutdown() {
    for (auto& [k, e] : entries)
        freeEntry(e);
    entries.clear();
}

bool RenderBufferManager::useBuffer(ENamedBuffer name, GLuint textureUnit) const {
    auto it = entries.find(key(name));
    if (it == entries.end()) {
        logError(std::string("RenderBufferManager::useBuffer — buffer #") +
                 std::to_string(key(name)) + " was never declared");
        return false;
    }
    const Entry& e = it->second;
    if (!e.ready) {
        logError(std::string("RenderBufferManager::useBuffer — '") +
                 (e.debugName ? e.debugName : "?") +
                 "' is not ready (was init() called?)");
        return false;
    }
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, e.texture);
    return true;
}

bool RenderBufferManager::setOutput(ENamedBuffer name) {
    auto it = entries.find(key(name));
    if (it == entries.end()) {
        logError(std::string("RenderBufferManager::setOutput — buffer #") +
                 std::to_string(key(name)) + " was never declared");
        return false;
    }
    Entry& e = it->second;
    if (!e.ready) {
        logError(std::string("RenderBufferManager::setOutput — '") +
                 (e.debugName ? e.debugName : "?") + "' is not ready");
        return false;
    }
    if (!ensureFBO(e)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
    return true;
}

GLuint RenderBufferManager::buildFramebuffer(
    std::vector<std::pair<ENamedBuffer, GLenum>> colorAttachments,
    ENamedBuffer depthBuffer) const
{
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    std::vector<GLenum> drawBuffers;
    for (auto& [buf, attachment] : colorAttachments) {
        GLuint tex = getHandle(buf);
        if (!tex) {
            logError(std::string("RenderBufferManager::buildFramebuffer — '") +
                     std::to_string(static_cast<uint32_t>(buf)) + "' not ready");
            glDeleteFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return 0;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex, 0);
        drawBuffers.push_back(attachment);
    }

    if (depthBuffer != ENamedBuffer::Count) {
        GLuint depth = getHandle(depthBuffer);
        if (depth)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_2D, depth, 0);
    }

    if (!drawBuffers.empty())
        glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data());

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        logError("RenderBufferManager::buildFramebuffer — FBO incomplete");
        glDeleteFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

GLuint RenderBufferManager::getHandle(ENamedBuffer name) const {
    auto it = entries.find(key(name));
    if (it == entries.end() || !it->second.ready) return 0;
    return it->second.texture;
}

bool RenderBufferManager::isReady(ENamedBuffer name) const {
    auto it = entries.find(key(name));
    return it != entries.end() && it->second.ready;
}

bool RenderBufferManager::copyBuffer(ENamedBuffer src, ENamedBuffer dst, const BlitShader& blitShader, GLuint quadVAO) {
    GLuint srcTex = getHandle(src);
    if (!srcTex) {
        logError("RenderBufferManager::copyBuffer — src buffer not ready");
        return false;
    }
    if (!setOutput(dst)) {
        logError("RenderBufferManager::copyBuffer — failed to bind dst FBO");
        return false;
    }
    blitShader.blit(srcTex, quadVAO);
    return true;
}
