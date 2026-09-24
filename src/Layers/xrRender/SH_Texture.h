#ifndef SH_TEXTURE_H
#define SH_TEXTURE_H
#pragma once

#include "../../xrCore/xr_resource.h"

class ENGINE_API CAviPlayerCustom;
class CTheoraSurface;

class ECORE_API CTexture : public xr_resource_named
{
public:
	//	Since DX10 allows up to 128 unique textures, 
	//	distance between enum values should be at leas 128
	enum ResourceShaderType //	Don't change this since it's hardware-dependent
	{
		rstPixel = 0,
		//	Default texture offset
		rstVertex = D3DVERTEXTEXTURESAMPLER0,
		rstGeometry = rstVertex + 256,
		rstHull = rstGeometry + 256,
		rstDomain = rstHull + 256,
		rstCompute = rstDomain + 256,
		rstInvalid = rstCompute + 256
	};

public:
	void __stdcall apply_load(u32 stage);
	void __stdcall apply_theora(u32 stage);
	void __stdcall apply_avi(u32 stage);
	void __stdcall apply_seq(u32 stage);
	void __stdcall apply_normal(u32 stage);

	void Preload();
	void Load();
	void LoadQueued();
	void PostLoad();
	void Unload(void);
	void Bind(u32 stage)
	{
		wait_for_loading();
		bind(stage);
#if defined(USE_DX10) || defined(USE_DX11)
		Touch();
#endif
	}
	bool TryQueueLoad();
	void CancelQueuedLoad();
	bool CanLoadAsync() const;
	bool is_loaded() const;
	void wait_for_loading() const;
#if defined(USE_DX10) || defined(USE_DX11)
	enum ELoadKind : u32
	{
		LoadKindUnknown,
		LoadKindDds,
		LoadKindOgm,
		LoadKindAvi,
		LoadKindSequence,
	};
	void SetLoadSource(LPCSTR logical_name, LPCSTR resolved_path, ELoadKind kind);
	bool IsDemandOnly() const { return m_demandOnly; }
	void Touch();
	u64 ResidentBytes() const { return m_residentBytes.load(std::memory_order_relaxed); }
	u64 TrimUnused(u32 now, bool pressure);
#endif
	//	void								Apply			(u32 dwStage);

	void surface_set(ID3DBaseTexture* surf);
	ID3DBaseTexture* surface_get();
	// Swap only the already-created GPU surface state of two user render-target
	// textures. Names, flags and bind delegates remain stable, so shaders which
	// reference the canonical RT name transparently see the active SVP bank.
	void swap_surface_state(CTexture& other);

	IC BOOL isUser() { return flags.bUser; }
	IC u32 get_Width()
	{
		desc_enshure();
		return desc.Width;
	}

	IC u32 get_Height()
	{
		desc_enshure();
		return desc.Height;
	}

	void video_Sync(u32 _time) { m_play_time = _time; }
	void video_Play(BOOL looped, u32 _time = 0xFFFFFFFF);
	void video_Pause(BOOL state);
	void video_Stop();
	BOOL video_IsPlaying();

	CTexture();
	virtual ~CTexture();

#if defined(USE_DX10) || defined(USE_DX11)
	ID3DShaderResourceView* get_SRView();
#endif	//	USE_DX10

private:
	enum ELoadState : u32
	{
		LoadStateUnloaded,
		LoadStateQueued,
		LoadStateLoading,
		LoadStateLoaded,
		LoadStateUnloading,
		LoadStateFailed,
	};

	void Load(bool queued);
	bool BeginLoad(bool queued);
	void FinishLoad();
	void FailLoad();
	void ReleaseLoadedData();
	IC BOOL desc_valid() { wait_for_loading(); return pSurface==desc_cache; }
#if defined(USE_DX10) || defined(USE_DX11)
	void desc_enshure();
	void EnsureLoadedForUse();
#else
	IC void desc_enshure() { wait_for_loading(); if (!desc_valid()) desc_update(); }
#endif
	void desc_update();
#if defined(USE_DX10) || defined(USE_DX11)
	void								Apply			(u32 dwStage);
	void								ProcessStaging();
	D3D_USAGE							GetUsage();
#endif	//	USE_DX10

	//	Class data
public: //	Public class members (must be encapsulated furthur)
	struct
	{
		u32 bLoaded : 1;
		u32 bUser : 1;
		u32 seqCycles : 1;
		u64 MemoryUsage;
#if defined(USE_DX10) || defined(USE_DX11)
		u32					bLoadedAsStaging: 1;
#endif	//	USE_DX10
	} flags;
	xr_atomic_u32 loadState;
	mutable xr_atomic_u32 loadKind;

	xr_delegate<void(u32)> bind;


	CAviPlayerCustom* pAVI;
	CTheoraSurface* pTheora;
	float m_material;
	shared_str m_bumpmap;

	bool m_is_hot = false;		//--DSR-- HeatVision
	bool m_is_glowing = false;	//--DSR-- SilencerOverheat

	union
	{
		u32 m_play_time; // sync theora time
		u32 seqMSPF; // Sequence data milliseconds per frame
	};

private:
	ID3DBaseTexture* pSurface;
	// Sequence data
	xr_vector<ID3DBaseTexture*> seqDATA;

	// Description
	ID3DBaseTexture* desc_cache;
	D3D_TEXTURE2D_DESC desc;

#if defined(USE_DX10) || defined(USE_DX11)
	std::atomic<u64> m_residentBytes{0};
	xr_atomic_u32 m_loadedAt{0};
	xr_atomic_u32 m_usedAt{0};
	std::atomic<bool> m_used{false};
	std::atomic<bool> m_externalView{false};
	bool m_demandOnly = false;
	ID3DShaderResourceView*			m_pSRView;
	shared_str m_loadName;
	shared_str m_resolvedSourcePath;
	// Sequence view data
	xr_vector<ID3DShaderResourceView*>m_seqSRView;
#endif	//	USE_DX10
};

struct resptrcode_texture : public resptr_base<CTexture>
{
	void create(LPCSTR _name);
	void destroy() { _set(NULL); }
	shared_str bump_get() { return _get() ? _get()->m_bumpmap : shared_str(); }
	bool bump_exist() { return 0 != bump_get().size(); }
};

typedef resptr_core<CTexture, resptrcode_texture>
ref_texture;

#endif
