#version 450

// VulkanSceneTriangleDemo::Vertexとlocation/型を一致させます。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

// 各DrawのModel-View-Projection行列。C++側のcolumn-major float[16]と一致させます。
layout(push_constant) uniform MeshTransform
{
    mat4 ClipTransform;
} meshTransform;

void main()
{
    gl_Position = meshTransform.ClipTransform * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUV = inUV;
}
