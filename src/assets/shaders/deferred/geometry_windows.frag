#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec3 gAlbedo;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 uColor;

void main()
{
    gPosition = FragPos;

    vec3 fdx = dFdx(FragPos);
    vec3 fdy = dFdy(FragPos);
    vec3 computedNormal = normalize(cross(fdx, fdy));
    if (!gl_FrontFacing) {
        computedNormal = -computedNormal;
    }
    gNormal = computedNormal;

    gAlbedo = uColor;
}
