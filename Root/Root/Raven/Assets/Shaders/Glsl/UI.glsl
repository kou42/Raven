#type vertex
#version 330 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;

uniform vec2 u_ViewportSize;

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    // Raven UIは左上原点のpixel座標を採用します。
    // OpenGL NDCは中央原点かつY上向きなので、ここで[-1, 1]へ変換しYを反転します。
    vec2 normalized = a_Position / u_ViewportSize;
    vec2 ndc = vec2(
        normalized.x * 2.0 - 1.0,
        1.0 - normalized.y * 2.0);

    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(ndc, 0.0, 1.0);
}

#type fragment
#version 330 core

in vec4 v_Color;
in vec2 v_TexCoord;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform int u_UseTexture;

void main()
{
    // SolidRectとImageでShaderを共有します。
    // Textureを使わないCommandではsample自体を行わず、従来のColor描画を維持します。
    if (u_UseTexture != 0)
    {
        FragColor = texture(u_Texture, v_TexCoord) * v_Color;
    }
    else
    {
        FragColor = v_Color;
    }
}
