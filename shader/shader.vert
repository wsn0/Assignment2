#version 330 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

out vec2 TexCoord;
out vec3 vNormal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(pos, 1.0);
    FragPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
}