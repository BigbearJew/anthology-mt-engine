#pragma once

// Note:
// ZNear - always 0.0f
// ZFar - always 1.0f

//class ENGINE_API CResourceManager;
//class ENGINE_API CGammaControl;

#include "pure.h"
//#include "hw.h"
#include "../xrcore/ftimer.h"
#include "stats.h"
//#include "shader.h"
//#include "R_Backend.h"

#include "../build_config_defines.h"

#define VIEWPORT_NEAR  Device.ViewportNear //0.2f
#define R_VIEWPORT_NEAR 0.005f

#define DEVICE_RESET_PRECACHE_FRAME_COUNT 10

// demonized: toggle bone optimization
//#define OPTIMIZE_CALCULATE_BONES

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/RenderDeviceRender.h"
#include "imgui_base.h"

#ifdef INGAME_EDITOR
# include "../Include/editor/interfaces.hpp"
#endif // #ifdef INGAME_EDITOR

class engine_impl;

class IRenderDevice
{
public:
	virtual CStatsPhysics* _BCL StatPhysics() = 0;
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt) = 0;
	virtual void _BCL RemoveSeqFrame(pureFrame* f) = 0;
};

class ENGINE_API CRenderDeviceData
{
public:
	u32 dwWidth;
	u32 dwHeight;
	u32 clientWidth;
	u32 clientHeight;

	u32 dwPrecacheFrame;
	BOOL b_is_Ready;
	BOOL b_is_Active;
	BOOL b_hide_cursor;
public:

	// Engine flow-control
	u32 dwFrame;
	u64 mMainRenderSerial = 0;
	bool IsSVPRenderOnlyFrame() const;

	float fTimeDelta;
	float fTimeGlobal;
	u32 dwTimeDelta;
	u32 dwTimeGlobal;
	u32 dwTimeContinual;

	Fvector vCameraPosition;
	Fvector vCameraDirection;
	Fvector vCameraTop;
	Fvector vCameraRight;

	// demonized: Cam has fixed 83 fov, Hud is psHUD_FOV * 83.f
	Fmatrix mView;
	Fmatrix mViewHud;
	Fmatrix mViewCam;
	Fmatrix mProject;
	Fmatrix mProjectHud;
	Fmatrix mProjectCam;
	Fmatrix mFullTransform;
	Fmatrix mFullTransformHud;
	Fmatrix mFullTransformCam;

	Fmatrix mView_prev;
	Fmatrix mViewHud_prev;
	Fmatrix mViewCam_prev;
	Fmatrix mProject_prev;
	Fmatrix mProjectHud_prev;
	Fmatrix mProjectCam_prev;
	Fmatrix mFullTransform_prev;
	Fmatrix mFullTransformHud_prev;
	Fmatrix mFullTransformCam_prev;

	Fvector4 wind_anim_prev;
	Fvector4 wind_anim_saved;

	// Copies of corresponding members. Used for synchronization.
	Fvector vCameraPosition_saved;

	Fmatrix mView_saved;
	Fmatrix mProject_saved;
	Fmatrix mFullTransform_saved;
	Fmatrix mView_saved_svp;
	Fmatrix mProject_saved_svp;
	Fmatrix mViewHud_saved_main;
	Fmatrix mProjectHud_saved_main;
	Fmatrix mFullTransformHud_saved_main;
	Fmatrix mViewCam_saved_main;
	Fmatrix mProjectCam_saved_main;
	Fmatrix mFullTransformCam_saved_main;
	Fvector4 mGrassBenderPos_saved_main[16];
	Fvector4 mGrassBenderDir_saved_main[16];
	u16 mGrassBenderId_saved_main[16];
	bool mSVPCameraSaved;
	bool mMainHudCamSaved;
	u16 mMainGrassBendersValidMask;
	u32 mSVPCameraFrame;

	float fFOV;
	float fASPECT;
	float ViewportNear = 0.2f;
protected:

	u32 Timer_MM_Delta;
	CTimer_paused Timer;
	CTimer_paused TimerGlobal;

	//AVO: 
	CTimer frame_timer; //TODO: ïðîâåðèòü, íå äóáëèðóåòñÿ-ëè ñõîæèé òàéìåð (alpet)
	//-AVO

public:

	// Registrators
	CRegistrator<pureRender> seqRender;
	CRegistrator<pureAppActivate> seqAppActivate;
	CRegistrator<pureAppDeactivate> seqAppDeactivate;
	CRegistrator<pureAppStart> seqAppStart;
	CRegistrator<pureAppEnd> seqAppEnd;
	CRegistrator<pureFrame> seqFrame;
	CRegistrator<pureScreenResolutionChanged> seqResolutionChanged;

	HWND m_hWnd;
	// CStats* Statistic;
};

class ENGINE_API CRenderDeviceBase :
	public IRenderDevice,
	public CRenderDeviceData
{
public:
};

// refs
#include "SvpTemporalSchedule.h"

class ENGINE_API CRenderDevice : public CRenderDeviceBase
{
public:
	class ENGINE_API CSecondVPParams //--#SM+#-- +SecondVP+
	{
		bool isActive = false; // Oeaa aeoeaaoee ?aiaa?a ai aoi?ie au?ii?o
		bool isThermal = false;
		bool isTextureReady = false;
		int thermalMode = 0;
		u8 frameDelay = 2;  // Ia eaeii eaa?a n iiiaioa i?ioeiai ?aiaa?a ai aoi?ie au?ii?o iu ia?i?i iiaue
						  //(ia ii?ao auou iaiuoa 2 - ea?aue aoi?ie eaa?, ?ai aieuoa oai aieaa ieceee FPS ai aoi?ii au?ii?oa)
		u16 ownerId = u16(-1);
		int qualityPreset = -1;
		SvpTemporalSchedule temporalSchedule;
		Fvector capturedPosition = {0.f, 0.f, 0.f};
		Fvector capturedDirection = {0.f, 0.f, 1.f};
		Fvector capturedTop = {0.f, 1.f, 0.f};
		Fmatrix capturedView;
		Fmatrix capturedProjection;
		float capturedFov = 0.f;
		int capturedNvg = 0;
		int capturedQuality = 100;
		u32 capturedTime = 0;
		u32 capturedFrame = u32(-1);

	public:
		bool isCamReady = false; // Oeaa aioiaiinoe eaia?u (FOV, iiceoey, e o.i) e ?aiaa?o aoi?iai au?ii?oa

		IC bool IsSVPActive() { return isActive; }
		void SetSVPActive(bool bState);
		bool    IsSVPFrame();
		IC bool IsSVPTextureReady() const { return isTextureReady; }
		void MarkSVPTextureReady();
		IC const Fmatrix& GetSVPCapturedView() const { return capturedView; }
		IC const Fmatrix& GetSVPCapturedProjection() const { return capturedProjection; }
		IC u32 GetSVPCaptureTime() const { return capturedTime; }
		IC u32 GetSVPCaptureFrame() const { return capturedFrame; }
		void InvalidateSVPContent();
		void SetSVPOwner(u16 id)
		{
			if (ownerId != id)
			{
				ownerId = id;
				InvalidateSVPContent();
			}
		}
		void SetSVPQualityPreset(int preset)
		{
			clamp<int>(preset, 0, 3);
			if (qualityPreset != preset)
			{
				qualityPreset = preset;
				InvalidateSVPContent();
			}
		}
		IC bool IsSVPThermal() const { return isThermal; }
		IC void SetSVPThermal(bool bState)
		{
			if (isThermal != bState)
				InvalidateSVPContent();
			isThermal = bState;
		}
		IC int GetSVPThermalMode() const { return thermalMode; }
		IC void SetSVPThermalMode(int mode)
		{
			clamp<int>(mode, 0, 1);
			if (thermalMode != mode)
				InvalidateSVPContent();
			thermalMode = mode;
		}
		IC u8 GetSVPFrameDelay() { return frameDelay; }
		void  SetSVPFrameDelay(u8 iDelay)
		{
			clamp<u8>(iDelay, 2, u8(-1));
			if (frameDelay != iDelay)
				InvalidateSVPContent();
			frameDelay = iDelay;
		}
	};	
	
private:
	// Main objects used for creating and rendering the 3D scene
	u32 m_dwWindowStyle;
	RECT m_rcWindowBounds;
	RECT m_rcWindowClient;

	//u32 Timer_MM_Delta;
	//CTimer_paused Timer;
	//CTimer_paused TimerGlobal;
	CTimer TimerMM;

	void _Create(LPCSTR shName);
	void _Destroy(BOOL bKeepTextures);
	void _SetupStates();
public:
	// HWND m_hWnd;
	LRESULT MsgProc(HWND, UINT, WPARAM, LPARAM);

	// u32 dwFrame;
	// u32 dwPrecacheFrame;
	u32 dwPrecacheTotal;

	// u32 dwWidth, dwHeight;
	float fWidth_2, fHeight_2;
	// BOOL b_is_Ready;
	// BOOL b_is_Active;
	void OnWM_Activate(WPARAM wParam, LPARAM lParam);
public:
	//ref_shader m_WireShader;
	//ref_shader m_SelectionShader;

	IRenderDeviceRender* m_pRender;

	BOOL m_bNearer;

	void SetNearer(BOOL enabled)
	{
		if (enabled && !m_bNearer)
		{
			m_bNearer = TRUE;
			mProject._43 -= EPS_L;
		}
		else if (!enabled && m_bNearer)
		{
			m_bNearer = FALSE;
			mProject._43 += EPS_L;
		}
		m_pRender->SetCacheXform(mView, mProject);
		//R_ASSERT(0);
		// TODO: re-implement set projection
		//RCache.set_xform_project (mProject);
	}

	void DumpResourcesMemoryUsage() { m_pRender->ResourcesDumpMemoryUsage(); }
public:
	// Registrators
	//CRegistrator <pureRender > seqRender;
	// CRegistrator <pureAppActivate > seqAppActivate;
	// CRegistrator <pureAppDeactivate > seqAppDeactivate;
	// CRegistrator <pureAppStart > seqAppStart;
	// CRegistrator <pureAppEnd > seqAppEnd;
	//CRegistrator <pureFrame > seqFrame;
	CRegistrator<pureFrame> seqFrameMT;
	CRegistrator<pureDeviceReset> seqDeviceReset;
	xr_vector<xr_delegate<void()>> seqParallel;
	// Static diagnostic labels stay aligned with seqParallel. Keeping the label
	// as LPCSTR avoids shared_str allocations for every NPC task on every frame.
	xr_vector<LPCSTR> seqParallelNames;

	// ForserX: Pre-Render sequence
	xr_vector<xr_delegate<void()>> seqParallelRender;
	xr_vector<xr_delegate<void()>> seqParallelBeforRender;

	xr_delegate<void()> ParticleWorkerCallback;
	xr_delegate<void()> ModelDefferClear;

	xr_atomic_bool isRendering{false};

	// LuaGC
	int LuaGCCount;
	bool LuaGCDone;
    xr_delegate<int()> LuaGC;
    xr_delegate<void()> LuaGCFull;
    xr_delegate<void()> LuaGCDebug;

	// Dependent classes
	//CResourceManager* Resources;

	CStats* Statistic;

	// Engine flow-control
	//float fTimeDelta;
	//float fTimeGlobal;
	//u32 dwTimeDelta;
	//u32 dwTimeGlobal;
	//u32 dwTimeContinual;

	// Cameras & projection
	//Fvector vCameraPosition;
	//Fvector vCameraDirection;
	//Fvector vCameraTop;
	//Fvector vCameraRight;

	//Fmatrix mView;
	//Fmatrix mProject;
	//Fmatrix mFullTransform;

	Fmatrix mInvView;
	Fmatrix mInvProject;
	Fmatrix mInvProjectHud;
	Fmatrix mInvFullTransform;

	CSecondVPParams m_SecondViewport;	//--#SM+#-- +SecondVP+

	//float fFOV;
	//float fASPECT;

	CRenderDevice()
		:
		m_pRender(0)
#ifdef INGAME_EDITOR
        , m_editor_module(0),
        m_editor_initialize(0),
        m_editor_finalize(0),
        m_editor(0),
        m_engine(0)
#endif // #ifdef INGAME_EDITOR
#ifdef PROFILE_CRITICAL_SECTIONS
        
#endif // #ifdef PROFILE_CRITICAL_SECTIONS
	{
		m_hWnd = NULL;
		b_is_Active = FALSE;
		b_is_Ready = FALSE;
		b_hide_cursor = FALSE;
		Timer.Start();
		m_bNearer = FALSE;
		
		m_SecondViewport.SetSVPActive(false);
		m_SecondViewport.SetSVPThermal(false);
		m_SecondViewport.SetSVPThermalMode(0);
		m_SecondViewport.SetSVPFrameDelay(2);
		m_SecondViewport.isCamReady = false;
		mSVPCameraSaved = false;
		mMainHudCamSaved = false;
		mMainGrassBendersValidMask = 0;
		mSVPCameraFrame = 0;
	};

	void Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason);
	bool Paused();

	// Scene control
	void PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input);
	BOOL Begin();
	void Clear();
	void End();
	void FrameMove();

	void overdrawBegin();
	void overdrawEnd();

	//Console Screenshot
	void Screenshot();

	// Mode control
	void DumpFlags();
	IC CTimer_paused* GetTimerGlobal() { return &TimerGlobal; }
	u32 TimerAsync() { return TimerGlobal.GetElapsed_ms(); }
	u32 TimerAsync_MMT() { return TimerMM.GetElapsed_ms() + Timer_MM_Delta; }

	// Creation & Destroying
	void ConnectToRender();
	void Create(void);
	void Run(void);
	void Destroy(void);
	void Reset(bool precache = true);

	bool ChangeOutputMonitor(HMONITOR hTargetMon);

	void Initialize(void);
	void ShutDown(void);

public:
	void time_factor(const float& time_factor)
	{
		Timer.time_factor(time_factor);
		TimerGlobal.time_factor(time_factor);
	}

	IC const float& time_factor() const
	{
		VERIFY(Timer.time_factor() == TimerGlobal.time_factor());
		return (Timer.time_factor());
	}

	Fvector& hud_to_world(Fvector& v, const Fmatrix& p)
	{
		mView.transform_tiny(v);
		p.transform_tiny(v);

		v.z -= ViewportNear;

		mInvProject.transform_tiny(v);
		mInvView.transform_tiny(v);

		return v;
	}

	Fvector& hud_to_world(Fvector& v)
	{
		return hud_to_world(v, mProjectHud);
	}

	Fvector& hud_to_world_dir(Fvector& v, const Fmatrix& p)
	{
		mView.transform_dir(v);
		p.transform_dir(v);

		mInvProject.transform_dir(v);
		mInvView.transform_dir(v);

		return v;
	}

	Fvector& hud_to_world_dir(Fvector& v)
	{
		return hud_to_world_dir(v, mProjectHud);
	}

	Fmatrix& hud_to_world(Fmatrix& m, const Fmatrix& p)
	{
		hud_to_world(m.c, p);
		hud_to_world_dir(m.i, p).normalize();
		hud_to_world_dir(m.j, p).normalize();
		hud_to_world_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& hud_to_world(Fmatrix& m)
	{
		return hud_to_world(m, mProjectHud);
	}

	Fvector& world_to_hud(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_tiny(v);
		mInvProject.transform_tiny(v);

		v.z += ViewportNear;

		p.transform_tiny(v);
		mView.transform_tiny(v);
		return v;
	}

	Fvector& world_to_hud(Fvector& v)
	{
		return world_to_hud(v, mProjectHud);
	}

	Fvector& world_to_hud_dir(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_dir(v);
		mInvProject.transform_dir(v);

		p.transform_dir(v);
		mView.transform_dir(v);

		return v;
	}

	Fvector& world_to_hud_dir(Fvector& v)
	{
		return world_to_hud_dir(v, mProjectHud);
	}

	Fmatrix& world_to_hud(Fmatrix& m, const Fmatrix& p)
	{
		world_to_hud(m.c, p);
		world_to_hud_dir(m.i, p).normalize();
		world_to_hud_dir(m.j, p).normalize();
		world_to_hud_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& world_to_hud(Fmatrix& m)
	{
		return world_to_hud(m, mProjectHud);
	}

	// Multi-threading
	xr_task_group secondary_tasks;

	ICF void add_to_seq_parallel(const xr_delegate<void()>& delegate, LPCSTR name)
	{
		seqParallel.push_back(delegate);
		seqParallelNames.push_back(name ? name : "legacy");
	}

	ICF void remove_from_seq_parallel(const xr_delegate<void()>& delegate)
	{
		xr_vector<xr_delegate<void()>>::iterator I = std::find(
			seqParallel.begin(),
			seqParallel.end(),
			delegate
		);
		if (I != seqParallel.end())
		{
			const size_t index = static_cast<size_t>(I - seqParallel.begin());
			seqParallel.erase(I);
			if (index < seqParallelNames.size())
				seqParallelNames.erase(seqParallelNames.begin() + index);
		}
	}

	//AVO: elapsed famed counter (by alpet)
	IC u32 frame_elapsed()
	{
		return frame_timer.GetElapsed_ms();
	}

	// demonized: Perceivable distance depending on FOV, so that objects will behave normal in binoculars
	IC float GetPerceivedDist(const Fvector& p, float* real_dist = nullptr)
	{
		float dist = vCameraPosition.distance_to(p);
		float fov_rad = deg2rad(fFOV);
		float perceived_dist = dist * tanf(fov_rad * 0.5f);
		if (real_dist) *real_dist = dist;
		return perceived_dist;
	}

	IC float CalcSSADynamic(const Fvector& C, float R)
	{
		Fvector4 v_res1, v_res2;
		mFullTransform.transform(v_res1, C);
		mFullTransform.transform(v_res2, Fvector(C).mad(vCameraRight, R));
		return v_res1.sub(v_res2).magnitude();
	}

public:
	void xr_stdcall on_idle();
	bool xr_stdcall on_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	void message_loop();
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt);
	virtual void _BCL RemoveSeqFrame(pureFrame* f);
	virtual CStatsPhysics* _BCL StatPhysics() { return Statistic; }

private:
	xr_imgui::ide m_imgui;

public:
	xr_imgui::ide& imgui() { return m_imgui; }
	bool imgui_shown() const { return m_imgui.is_shown(); }
#ifdef INGAME_EDITOR
public:
    IC editor::ide* editor() const { return m_editor; }

private:
    void initialize_editor();
    void message_loop_editor();

private:
    typedef editor::initialize_function_ptr initialize_function_ptr;
    typedef editor::finalize_function_ptr finalize_function_ptr;

private:
    HMODULE m_editor_module;
    initialize_function_ptr m_editor_initialize;
    finalize_function_ptr m_editor_finalize;
    editor::ide* m_editor;
    engine_impl* m_engine;
#endif // #ifdef INGAME_EDITOR
};

extern ENGINE_API CRenderDevice Device;
extern ENGINE_API CRenderDevice* DevicePtr;

#ifndef _EDITOR
#define RDEVICE Device
#else
#define RDEVICE EDevice
#endif

#ifdef ECO_RENDER
extern ENGINE_API float refresh_rate;
#endif // ECO_RENDER

extern ENGINE_API bool g_bBenchmark;

typedef xr_delegate<bool()> LOADING_EVENT;
extern ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

class ENGINE_API CLoadScreenRenderer : public pureRender
{
public:
	CLoadScreenRenderer();
	void start(bool b_user_input);
	void stop();
	virtual void OnRender();

	bool b_registered;
	bool b_need_user_input;

	bool IsActive() const {
		return b_registered;
	}
};

extern ENGINE_API CLoadScreenRenderer load_screen_renderer;
