#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;

void main()
{
    gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 o_Color;

uniform vec3 u_OutlineColor;

void main()
{
    // Selection OutlineはScene View表示用Color Attachment 0だけへ出力します。
    // Picking用R32I Attachment(location = 1)へは何も書き込まないため、
    // 直前のEntity Picking結果を壊しません。
    o_Color = vec4(u_OutlineColor, 1.0);
}
