#version 330 core

in vec2 vUV;

uniform sampler2D uSDF;
uniform vec3 uColor;

out vec4 FragColor;

void main()
{
    // Read distance
    float d = texture(uSDF, vUV).r;

    // If inside is dark, invert
    d = 1.0 - d;

    // Distance-based AA
    float w = fwidth(d);

    // Edge at 0.5
    float alpha = smoothstep(0.5 - w, 0.5 + w, d);

    FragColor = vec4(uColor, alpha);
}
