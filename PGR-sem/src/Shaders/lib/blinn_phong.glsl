

vec3 blinnPhong(vec3 lightDir, vec3 lightColor,
                vec3 normal,   vec3 viewDir,
                vec3 albedo,   float specInt,
                float shininess, float metallic) {
    float diff = max(dot(normal, lightDir), 0.0);
    vec3  halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    vec3  specColor = mix(vec3(1.0), albedo, metallic);
    float kD = (1.0 - specInt) * (1.0 - metallic);
    return kD * diff * lightColor * albedo
         + specInt * spec * lightColor * specColor;
}

