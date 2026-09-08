#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;

void main()
{
    // Texture / Normal / Lightingを一切参照せず、現在のRenderer Camera/Model規約だけで
    // GeometryがRasterize経路まで到達できるかを切り分けます。
    gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
}
