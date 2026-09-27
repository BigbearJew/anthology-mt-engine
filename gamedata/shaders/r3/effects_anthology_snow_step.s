function normal(shader, t_base, t_second, t_detail)
    shader:begin('effects_wallmark', 'anthology_snow_step')
        :blend(true, blend.destcolor, blend.srccolor)
        :zb(true, false)
    shader:dx10texture('s_tread', 'anthology\\snow_boot_tread')
    shader:dx10sampler('smp_base')
    shader:dx10color_write_enable(true, true, true, false)
end
