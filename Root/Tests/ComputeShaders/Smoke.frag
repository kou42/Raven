#version 450
layout(set = 0, binding = 0) uniform sampler3D volume;
layout(location = 0) out vec4 color;
void main()
{
    int i = int(gl_FragCoord.x);
    vec3 uvw = (vec3(i & 1, (i >> 1) & 1, i >> 2) + 0.5) / 2.0;
    color = vec4(texture(volume, uvw).r, 0, 0, 1);
}
