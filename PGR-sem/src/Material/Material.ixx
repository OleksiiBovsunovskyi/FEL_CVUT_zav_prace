module;
#include "pgr.h"
#include <filesystem>

#include <cstddef>
export module Material;
export import RenderMode;


export struct MaterialUBO {
    glm::vec4 albedo;        
    glm::vec3 emissiveColor; 
    float emissiveIntensity; 
    float specularIntensity; 
    float shininess;         
    float metallic;          
    float roughness;         
    float alphaThreshold;    
    int   emissive;          
    int   useTexture;        
    int   useNormalMap;      
    int   useORMMap;         
    int   _pad2;             
    int   useEmissiveTexture;
    int   _pad;              
};
static_assert(sizeof(MaterialUBO) == 80);
static_assert(offsetof(MaterialUBO, emissiveIntensity) == 28);
static_assert(offsetof(MaterialUBO, useEmissiveTexture) == 72);

export class Material {

    mutable GLuint materialUBOHandle = 0;

    GLuint albedoTexture   = 0;
    GLuint normalTexture   = 0;
    GLuint ormTexture      = 0;  // R=AO, G=roughness, B=metallic
    GLuint specularTexture = 0;
    GLuint emissiveTexture = 0;

    //Constants if no texture set
    glm::vec4 albedo{1.0f};
    glm::vec3 emissiveColor{0.0f};
    float emissiveIntensity = 1.0f;
    float specularIntensity = 0.5f;
    float shininess         = 32.0f;
    float metallic          = 0.0f;
    float roughness         = 0.1f;
    
    float alphaThreshold    = 0.0f; 
    bool  blendAlpha        = false;
    bool  emissive          = false;

public:
    Material() = default;
    ~Material();

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    
    bool loadTextureFromMemory(const void* data, int bytes);
    bool loadNormalTextureFromMemory(const void* data, int bytes);
    bool loadORMTextureFromMemory(const void* ormData, int ormBytes, const void* aoData, int aoBytes);
    bool loadEmissiveTextureFromMemory(const void* data, int bytes);

    void bindUniforms(GLuint shader) const;

    void setAlbedo(const glm::vec4& col)        { albedo = col; }
    void setEmissiveColor(const glm::vec3& col) { emissiveColor = col; }
    void setEmissiveIntensity(float v)          { emissiveIntensity = v; }
    void setSpecularIntensity(float v)          { specularIntensity = v; }
    void setShininess(float v)                  { shininess = v; }
    void setMetallic(float v)                   { metallic = v; }
    void setRoughness(float v)                  { roughness = v; }
    void setAlphaThreshold(float v)             { alphaThreshold = v; }
    void setEmissive(bool v)                    { emissive = v; }

    [[nodiscard]] glm::vec4 getAlbedo()           const { return albedo; }
    [[nodiscard]] glm::vec3 getEmissiveColor()    const { return emissiveColor; }
    [[nodiscard]] float     getEmissiveIntensity() const { return emissiveIntensity; }
    [[nodiscard]] float     getSpecularIntensity() const { return specularIntensity; }
    [[nodiscard]] float     getShininess()         const { return shininess; }
    [[nodiscard]] float     getMetallic()          const { return metallic; }
    [[nodiscard]] float     getRoughness()         const { return roughness; }
    [[nodiscard]] float     getAlphaThreshold()    const { return alphaThreshold; }
    [[nodiscard]] bool      isEmissive()           const { return emissive; }
    [[nodiscard]] bool      hasTexture()           const { return albedoTexture != 0; }
    bool transparent = false;
    bool isTransparent() const { return transparent; }
    void setTransparent(bool t) { transparent = t; }

    /**
     * Loads material from the assimp scene
     * @param scene Assimp source scene
     * @param mat Assimp source material
     * @param meshMode Mesh render mode
     * @param staticMeshMode StaticMesh render mode
     * @return tru on success
     */
    bool loadFromAssimp(const aiScene* scene, const aiMaterial* mat, RenderMode& meshMode, RenderMode staticMeshMode);

};
