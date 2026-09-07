#version 330 core

in vec3 v_Color;
in vec2 v_TexCoord;
in vec3 v_WorldNormal;

out vec4 FragColor;

uniform vec3 u_Tint;
uniform float u_Alpha;
uniform vec3 u_LightDirection;
uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform float u_AmbientIntensity;

void main()
{
    vec3 normal = normalize(v_WorldNormal);

    // u_LightDirectionは「光が進むWorld方向」と定義します。
    // SurfaceからLightへ向かうLambert計算では反転した方向を使用します。
    vec3 toLight = normalize(-u_LightDirection);
    float nDotL = max(dot(normal, toLight), 0.0);

    vec3 baseColor = v_Color * u_Tint;
    vec3 ambient = baseColor * u_AmbientIntensity;
    vec3 diffuse = baseColor * u_LightColor * (nDotL * u_LightIntensity);

    FragColor = vec4(ambient + diffuse, u_Alpha);
}
