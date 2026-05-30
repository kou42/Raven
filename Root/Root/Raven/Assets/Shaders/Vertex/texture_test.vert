#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;
layout(location = 2) in vec2 a_TexCord;

out vec3 v_Color;
out vec2 v_TexCord;

void main()
{
    v_TexCord = a_TexCord;
    v_Color = a_Color;
    gl_Position = vec4(a_Position, 1.0);
}