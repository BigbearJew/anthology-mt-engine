// UV-authored fullscreen pass; independent of main/core/PiP viewport dimensions.
float4 main(float2 uv : TEXCOORD0, out float2 tc : TEXCOORD0) : SV_Position
{
    tc = uv;
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
