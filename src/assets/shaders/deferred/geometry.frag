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

    // Compute face normal from screen-space derivatives (flat shading).
    vec3 fdx = dFdx(FragPos);
    vec3 fdy = dFdy(FragPos);
    vec3 computedNormal = normalize(cross(fdx, fdy));

    // Ensure the normal always points toward the camera regardless of
    // the original triangle winding order.  gl_FrontFacing is false for
    // CW-wound triangles (in screen space); flipping the normal for
    // those gives correct lighting on meshes with mixed winding.
    if (!gl_FrontFacing) {
        computedNormal = -computedNormal;
    }

    // Store normalized normal
    gNormal = computedNormal;

    // Store albedo color
    gAlbedo = uColor;
}
