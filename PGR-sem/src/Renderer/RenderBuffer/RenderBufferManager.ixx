module;
#include "pgr.h"
#include <vector>
#include <unordered_map>
#include <utility>

export module RenderBufferManager;

import Logger;
import BlitShader;

// ── Named buffers known to the rendering pipeline ─────────────────────────

export enum class ENamedBuffer : uint32_t {
    // G-buffer
    Position   = 0,   
    Normal     = 1,   
    AlbedoSpec = 2,   
    MotionVec  = 3,   
    MetallicAO = 4,   
    Emissive   = 5,   
    Depth      = 6,   

    // Lighting/scene output
    SceneColor = 7,  

    // Shadow map 
    ShadowMap  = 8,

    // Post-process ping-pong
    PingPong0  = 9, 
    PingPong1  = 10,

    // Blit target
    Blit       = 11, 

    Count
};

/**
 * Helper function returning texture unit for the given named buffer
 * @param buffer Named buffer
 * @return texture unit for the given buffer
 */
export int getNamedBufferTextureUnit(ENamedBuffer buffer)
{
    return static_cast<int>(buffer); 
}

export struct BufferDesc {
    GLenum internalFormat = GL_RGBA16F;
    GLenum format         = GL_RGBA;
    GLenum dataType       = GL_FLOAT;

    int    fixedWidth     = 0;   
    int    fixedHeight    = 0;

    GLenum minFilter      = GL_NEAREST;
    GLenum magFilter      = GL_NEAREST;
    GLenum wrapS          = GL_CLAMP_TO_EDGE;
    GLenum wrapT          = GL_CLAMP_TO_EDGE;

    bool   depthCompare   = false;      
    float  borderColor[4] = {0,0,0,0};
    
    static BufferDesc rgb32F() {
        return {GL_RGB32F, GL_RGB, GL_FLOAT};
    }
    static BufferDesc rgba16F(GLenum filter = GL_NEAREST) {
        BufferDesc d;
        d.internalFormat = GL_RGBA16F;
        d.format         = GL_RGBA;
        d.dataType       = GL_FLOAT;
        d.minFilter = d.magFilter = filter;
        return d;
    }
    static BufferDesc rgba8(GLenum filter = GL_NEAREST) {
        BufferDesc d;
        d.internalFormat = GL_RGBA8;
        d.format         = GL_RGBA;
        d.dataType       = GL_UNSIGNED_BYTE;
        d.minFilter = d.magFilter = filter;
        return d;
    }
    static BufferDesc rg16F() {
        return {GL_RG16F, GL_RG, GL_FLOAT};
    }
    static BufferDesc rg8() {
        return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
    }
    static BufferDesc rgb16F() {
        return {GL_RGB16F, GL_RGB, GL_FLOAT};
    }
    static BufferDesc depth24() {
        return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT};
    }
    
    static BufferDesc shadowMap(int size) {
        BufferDesc d;
        d.internalFormat    = GL_DEPTH_COMPONENT24;
        d.format            = GL_DEPTH_COMPONENT;
        d.dataType          = GL_FLOAT;
        d.fixedWidth        = size;
        d.fixedHeight       = size;
        d.minFilter         = GL_LINEAR;
        d.magFilter         = GL_LINEAR;
        d.wrapS             = GL_CLAMP_TO_BORDER;
        d.wrapT             = GL_CLAMP_TO_BORDER;
        d.depthCompare      = true;
        d.borderColor[0]    = d.borderColor[1] = d.borderColor[2] = d.borderColor[3] = 1.f;
        return d;
    }
};

export class RenderBufferManager {
    struct Entry {
        BufferDesc  desc;
        GLuint      texture   = 0;
        GLuint      fbo       = 0;  
        bool        ready     = false;
        const char* debugName = nullptr;
    };

    std::unordered_map<uint32_t, Entry> entries;
    int screenW = 0, screenH = 0;

    static uint32_t key(ENamedBuffer b) { return static_cast<uint32_t>(b); }

    bool allocTexture(Entry& e, int w, int h);
    bool ensureFBO(Entry& e);
    void freeEntry(Entry& e);

public:
    /**
     * Register new buffer
     * @param name Buffer name
     * @param debugName name used for debug
     * @param desc buffer format
     */
    void declare(ENamedBuffer name, const char* debugName, BufferDesc desc);

    /**
     * Creates all declared buffers
     * @param w Width
     * @param h Height
     * @return True on success, false otherwise
     */
    [[nodiscard]] bool init(int w, int h);


    void resize(int w, int h);

    void shutdown();

  
    /**
     * Binds buffer at given texture unit
     * @param name Buffer name
     * @param textureUnit Texture unit to bind to
     * @return True on success, false otherwise
     */
    [[nodiscard]] bool useBuffer(ENamedBuffer name, GLuint textureUnit) const;

    /**
     * Retunrs raw texture handle for given buffer
     * @param name Buffer name
     * @return Raw texture handle
     */
    [[nodiscard]] GLuint getHandle(ENamedBuffer name) const;
    
    [[nodiscard]] bool   isReady(ENamedBuffer name)   const;

    
    [[nodiscard]] bool setOutput(ENamedBuffer name);

    /**
     * Copies buffer from src to dst, using blit shader
     * @param src -> source buffer 
     * @param dst -> target buffer
     * @return true on success, false otherwise
     */
    [[nodiscard]] bool copyBuffer(ENamedBuffer src, ENamedBuffer dst, const BlitShader& blitShader, GLuint quadVAO);


    [[nodiscard]] GLuint buildFramebuffer(
        std::vector<std::pair<ENamedBuffer, GLenum>> colorAttachments,
        ENamedBuffer depthBuffer = ENamedBuffer::Count) const;
    
    ~RenderBufferManager() { shutdown(); }
};
