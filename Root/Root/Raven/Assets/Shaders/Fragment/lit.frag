#version 330 core

in vec3 v_Color;
in vec2 v_TexCoord;
in vec3 v_WorldNormal;

out vec4 FragColor;

uniform vec4 u_BaseColorFactor;
uniform sampler2D u_BaseColorTexture;
uniform int u_HasBaseColorTexture;
uniform int u_AlphaMaskEnabled;
uniform float u_AlphaCutoff;
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

    // glTFのbaseColorはFactorとTextureの積です。
    // Texture未指定Materialも同じShaderを使えるよう、明示Flagで白Texture相当へfallbackします。
    vec4 textureColor = vec4(1.0);
    if (u_HasBaseColorTexture != 0)
    {
        textureColor = texture(u_BaseColorTexture, v_TexCoord);
    }

    vec4 baseColor = vec4(v_Color, 1.0) * u_BaseColorFactor * textureColor;

    // Masked MaterialはBlendせずOpaque PassでDepthを書き込みます。
    // cutoff未満のFragmentだけを破棄することで、葉・フェンス等のcutout形状でも
    // Opaqueと同じDepth整合性を維持しながら透過部分だけを除外できます。
    if (u_AlphaMaskEnabled != 0 && baseColor.a < u_AlphaCutoff)
    {
        discard;
    }

    vec3 ambient = baseColor.rgb * u_AmbientIntensity;
    vec3 diffuse = baseColor.rgb * u_LightColor * (nDotL * u_LightIntensity);

    FragColor = vec4(ambient + diffuse, baseColor.a);
}
