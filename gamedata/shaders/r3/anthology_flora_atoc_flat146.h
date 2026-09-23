// Coverage-only pass: stock filtering with the visible season's alpha.
float4 main(p_flat I) : SV_Target
{
    const float style = anthology_flora_style.x;
    float4 D;
    if (style >= 5.5f)
        D = s_base.Sample(smp_linear, I.tcdh);
    else if (style < 1.5f)
        D = s_base_green.Sample(smp_linear, I.tcdh);
    else if (style < 2.5f)
        D = s_base_autumn.Sample(smp_linear, I.tcdh);
    else
        D = s_base_dead.Sample(smp_linear, I.tcdh);
    D.w = (D.w - def_aref * 0.5f) / (1 - def_aref * 0.5f);
    return D;
}
