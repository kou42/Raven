#version 330 core

in vec3 v_Color;

out vec4 FragColor;

uniform vec3 u_Tint;
uniform float u_Alpha;

void main()
{
    FragColor = vec4(v_Color * u_Tint, u_Alpha);
}