#type vertex
#version 330 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec4 a_Color;

uniform vec2 u_ViewportSize;

out vec4 v_Color;

void main()
{
    // Raven UIは左上原点のpixel座標を採用します。
    // OpenGL NDCは中央原点かつY上向きなので、ここで[-1, 1]へ変換しYを反転します。
    vec2 normalized = a_Position / u_ViewportSize;
    vec2 ndc = vec2(
        normalized.x * 2.0 - 1.0,
        1.0 - normalized.y * 2.0);

    v_Color = a_Color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}

#type fragment
#version 330 core

in vec4 v_Color;
out vec4 FragColor;

void main()
{
    FragColor = v_Color;
}
