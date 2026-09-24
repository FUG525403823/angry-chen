Shader "Ac/Emblem"
{
    Properties
    {
        _EmissiveTint ("Emissive Tint", Color) = (0.4784314, 0.8196079, 1, 1)
        _EmissiveIntensity ("Emissive Intensity", Range(0, 2)) = 0.2
    }

    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "IgnoreProjector" = "True" "RenderPipeline" = "UniversalPipeline" }

        Pass
        {
            Name "Emblem"
            ZWrite Off
            ZTest LEqual
            Cull Off
            Blend SrcAlpha One

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            CBUFFER_START(UnityPerMaterial)
            half4 _EmissiveTint;
            half _EmissiveIntensity;
            CBUFFER_END

            struct Attributes
            {
                float4 positionOS : POSITION;
                half4 color : COLOR;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                half4 color : COLOR;
            };

            Varyings vert(Attributes input)
            {
                Varyings output;
                output.positionCS = TransformObjectToHClip(input.positionOS.xyz);
                output.color = input.color;
                return output;
            }

            half4 frag(Varyings input) : SV_Target
            {
                half3 rgb = input.color.rgb * _EmissiveTint.rgb * _EmissiveIntensity;
                return half4(rgb, input.color.a);
            }
            ENDHLSL
        }
    }
}
