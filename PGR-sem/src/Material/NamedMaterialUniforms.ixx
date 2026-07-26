module;
export module NamedMaterialUniforms;


export namespace NamedMaterialUniforms {
    constexpr const char* Albedo            = "uAlbedo";
    constexpr const char* EmissiveColor     = "uEmissiveColor";
    constexpr const char* EmissiveIntensity = "uEmissiveIntensity";
    constexpr const char* SpecularIntensity = "uSpecularIntensity";
    constexpr const char* Shininess         = "uShininess";
    constexpr const char* Metallic          = "uMetallic";
    constexpr const char* Roughness         = "uRoughness";
    constexpr const char* AlphaThreshold    = "uAlphaThreshold";
    constexpr const char* Emissive          = "uEmissive";

    constexpr const char* AlbedoTexture        = "uAlbedoTexture";
    constexpr const char* UseTexture           = "uUseTexture";
    constexpr const char* NormalTexture        = "uNormalTexture";
    constexpr const char* UseNormalMap         = "uUseNormalMap";
    constexpr const char* MetallicTexture      = "uMetallicTexture";
    constexpr const char* UseMetallicMap       = "uUseMetallicMap";
    constexpr const char* AOTexture            = "uAOTexture";
    constexpr const char* UseAOMap             = "uUseAOMap";
    constexpr const char* EmissiveTexture      = "uEmissiveTexture";
    constexpr const char* UseEmissiveTexture   = "uUseEmissiveTexture";
}
