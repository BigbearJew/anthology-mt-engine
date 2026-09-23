#ifndef IRENDERABLE_H_INCLUDED
#define IRENDERABLE_H_INCLUDED

#include "render.h"

ENGINE_API u64 GetRenderSurfaceOwnerGeneration();
ENGINE_API void SetRenderSurfaceOwnerMainSerial(u64 serial);
ENGINE_API void SetRenderSurfaceOwnerHistoryBoundary(u64 serial);

//////////////////////////////////////////////////////////////////////////
// definition ("Renderable")
class ENGINE_API IRenderable:
	public ISpatialOwner
{
private:
	const u16 m_surfaceOwnerSlot;
	const float m_surfaceOwnerId;
	IRenderable(const IRenderable&) = delete;
	IRenderable& operator=(const IRenderable&) = delete;

public:
	struct
	{
		Fmatrix xform;
		IRenderVisual* visual;
		IRender_ObjectSpecific* pROS;
		BOOL pROS_Allowed;
	} renderable;

public:
	IRenderable();
	virtual ~IRenderable();
	// Zero means no pool slot. This identity is transient, never serialized.
	float GetRenderSurfaceOwnerId() const { return m_surfaceOwnerId; }
	IRender_ObjectSpecific* renderable_ROS();
	BENCH_SEC_SCRAMBLEVTBL2
	virtual void renderable_Render(IDSGraphManager* DM) = 0;
	virtual BOOL renderable_ShadowGenerate() { return FALSE; };
	virtual BOOL renderable_ShadowReceive() { return FALSE; };

	virtual float GetHotness() { return 0.0; }			//--DSR-- HeatVision
	virtual float GetTransparency() { return 0.0; }		//--DSR-- HeatVision
	virtual float GetGlowing() { return 0.0; }			//--DSR-- SilencerOverheat

	virtual IRenderable* dcast_Renderable() override { return this; }
};

#endif // IRENDERABLE_H_INCLUDED
