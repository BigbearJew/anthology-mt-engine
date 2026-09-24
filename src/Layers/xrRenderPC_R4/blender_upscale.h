#pragma once

class CBlender_upscale final : public IBlender
{
	bool m_auxiliary;
public:
    explicit CBlender_upscale(bool auxiliary = false);
    LPCSTR getComment() override { return "INTERNAL: Anthology upscale present"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override;
};
