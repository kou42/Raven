#type vertex
#version 330 core

// Picking PassではMeshの位置だけを利用します。
// 通常描画用Shaderが持つColor / UV / Normal等には依存しないため、
// MeshRendererComponentを持つEntityを共通のPicking Materialで描画できます。
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

// Color Attachment 0は通常のScene表示用RGBA8が保持しています。
// Picking Passではlocation = 1のR32I AttachmentだけへEntity Indexを書き込み、
// Scene表示用Color Attachmentを変更しません。
layout(location = 1) out int o_EntityID;

uniform int u_EntityID;

void main()
{
    o_EntityID = u_EntityID;
}
