#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec3 gAlbedo;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 uColor;

void main()
{
    // Store fragment position in world space
    gPosition = FragPos;
    
    // Compute normal from derivatives (flat shading)
    vec3 fdx = dFdx(FragPos);
    vec3 fdy = dFdy(FragPos);
    vec3 computedNormal = normalize(cross(fdx, fdy));
    
    // Store normalized normal
    gNormal = computedNormal;
    
    // Store albedo color
    gAlbedo = uColor;
}
