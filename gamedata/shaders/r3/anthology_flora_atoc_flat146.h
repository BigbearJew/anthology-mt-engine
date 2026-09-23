float4 main(p_flat I) : SV_Target
{
    float4 D = tbase(I.tcdh);
    D.w = (D.w - def_aref * 0.5f) / (1 - def_aref * 0.5f);
    return D;
}
