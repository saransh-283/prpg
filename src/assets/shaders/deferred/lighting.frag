#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D shadowMap;
uniform sampler2D gDepth;

uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform mat4 lightSpaceMatrix;

// Ambient (hemisphere) lighting
uniform float ambientIntensity;
uniform float ambientMin;
uniform vec3 ambientSkyColor;
uniform vec3 ambientGroundColor;

// Shadow intensity control
uniform float shadowStrength;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Outside shadow map frustum => no shadow.
    if (projCoords.z <= 0.0 || projCoords.z >= 1.0 || projCoords.x <= 0.0 || projCoords.x >= 1.0 || projCoords.y <= 0.0 || projCoords.y >= 1.0)
        return 0.0;
    
    // Get closest depth value from light's perspective
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    
    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    
    // Check whether current frag pos is in shadow with bias to reduce shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    
    // PCF (Percentage-Closer Filtering) for softer shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    
    // Keep areas outside shadow frustum illuminated
    // (already handled by early return)
    
    return shadow;
}

void main()
{
    // If this pixel has no geometry, skip lighting so the skybox can render cleanly.
    float depth = texture(gDepth, TexCoords).r;
    if (depth >= 1.0 - 1e-6)
    {
        FragColor = vec4(0.0);
        return;
    }

    // Retrieve data from G-buffer
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 Normal = texture(gNormal, TexCoords).rgb;
    vec3 Albedo = texture(gAlbedo, TexCoords).rgb;

    // Defensive normalize: prevent NaNs from propagating into specular/shadows.
    float nLen = length(Normal);
    if (nLen < 1e-4)
        Normal = vec3(0.0, 1.0, 0.0);
    else
        Normal = Normal / nLen;
    
    // Ambient lighting (hemisphere: sky above, ground below)
    // Keeps non-sun-facing surfaces from going nearly black.
    float hemi = clamp(Normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientColor = mix(ambientGroundColor, ambientSkyColor, hemi);
    vec3 ambient = (ambientIntensity * ambientColor + vec3(max(ambientMin, 0.0))) * Albedo;
    
    // Directional light (sun)
    vec3 lightDir = normalize(-sunDirection);
    float diff = max(dot(Normal, lightDir), 0.0);
    vec3 diffuse = diff * sunColor * sunIntensity * Albedo;
    
    // Specular lighting (simple Blinn-Phong)
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(Normal, halfwayDir), 0.0), 32.0);
    vec3 specular = spec * sunColor * 0.3;
    
    // Calculate shadow
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    float shadow = ShadowCalculation(fragPosLightSpace, Normal, lightDir);
    
    // Combine lighting (apply shadow only to diffuse and specular, not ambient)
    float shadowFactor = 1.0 - clamp(shadowStrength, 0.0, 1.0) * shadow;
    vec3 lighting = ambient + shadowFactor * (diffuse + specular);
    
    FragColor = vec4(lighting, 1.0);
}
