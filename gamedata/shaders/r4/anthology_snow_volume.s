function normal(shader, t_base, t_second, t_detail)
    shader:begin("anthology_snow_volume", "anthology_snow_volume")
        :zb(true,true):blend(false):fog(false):aref(false,0)
    shader:dx10texture("snow_tracks", "$user$snow_tracks")
    shader:dx10sampler("smp_base")
end
