#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;
layout(location = 2) in vec2 a_Texcord;
layout(location = 3) in vec3 a_Normal;

out vec3 v_Color;
out vec2 v_TexCoord;
out vec3 v_WorldNormal;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_Texcord;

    // 非一様Scaleを含む地形/Propでも法線方向を正しく維持するため、
    // Model行列の逆転置行列でLocal NormalをWorld Spaceへ変換します。
    mat3 normalMatrix = transpose(inverse(mat3(u_Model)));
    v_WorldNormal = normalize(normalMatrix * a_Normal);

    gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
}
