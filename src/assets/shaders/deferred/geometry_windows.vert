#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // Placeholder; actual face normal computed in fragment shader.
    Normal = vec3(0.0, 1.0, 0.0);

    gl_Position = projection * view * worldPos;
}
