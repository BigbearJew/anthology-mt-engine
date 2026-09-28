#include "common.h"
#include "screenspace_mvectors.h"
#include "anthology_snow_field.h"
struct snow_input { float4 p:POSITION; float3 n:NORMAL; float4 data:TEXCOORD0; };
struct snow_output {
    float4 hpos:SV_Position;
    float3 world:TEXCOORD0;
    float3 view:TEXCOORD1;
    float4 current:TEXCOORD2;
    float4 previous:TEXCOORD3;
    float cover:TEXCOORD4;
    float3 normal:TEXCOORD5;
};
snow_output main(snow_input I)
{
    snow_output O;
    float4 P=I.p;
    float depth=snow_field_depth(P.xz)*I.data.x*I.data.z*I.data.w;
    P.y += depth + .003;
    O.cover=saturate(depth/.035);
    O.normal=I.n;
    O.world=P.xyz;
    O.view=mul(m_V,P).xyz;
    O.current=mul(m_VP,P);
    P.y=I.data.y;
    O.previous=mul(m_vp_prev,P);
    O.hpos=O.current;
    O.hpos.xy=ssfx_taa_jitter(O.hpos);
    return O;
}
