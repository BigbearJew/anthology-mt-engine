#include "stdafx.h"
#include "../xrcdb/ispatial.h"
#include "irenderable.h"
#include "ICollidable.h"
#include "RenderSurfaceOwner.h"

namespace
{
	RenderSurfaceOwnerPool<>& SurfaceOwners()
	{
		static RenderSurfaceOwnerPool<> owners;
		return owners;
	}
}

u64 GetRenderSurfaceOwnerGeneration()
{
	return SurfaceOwners().Generation();
}

void SetRenderSurfaceOwnerMainSerial(u64 serial)
{
	SurfaceOwners().SetMainSerial(serial);
}

void SetRenderSurfaceOwnerHistoryBoundary(u64 serial)
{
	SurfaceOwners().SetHistoryBoundary(serial);
}

IRenderable::IRenderable()
	: m_surfaceOwnerSlot(SurfaceOwners().Acquire()),
	  m_surfaceOwnerId(m_surfaceOwnerSlot == RenderSurfaceOwnerPool<>::Invalid ? 0.f : EncodeRenderSurfaceOwner(m_surfaceOwnerSlot))
{
	ISpatialOwner::spatial_create(g_SpatialSpace, this, STYPE_RENDERABLE);

	renderable.xform.identity();
	renderable.visual = nullptr;
	renderable.pROS = nullptr;
	renderable.pROS_Allowed = TRUE;
}

extern ENGINE_API xr_atomic_bool g_bRendering;

IRenderable::~IRenderable()
{
	VERIFY(!g_bRendering);
	Render->model_Delete(renderable.visual);
	if (renderable.pROS) Render->ros_destroy(renderable.pROS);
	renderable.visual = NULL;
	renderable.pROS = NULL;
	SurfaceOwners().Release(m_surfaceOwnerSlot);
}

IRender_ObjectSpecific* IRenderable::renderable_ROS()
{
	if (0 == renderable.pROS && renderable.pROS_Allowed) renderable.pROS = Render->ros_create(this);
	return renderable.pROS;
}
