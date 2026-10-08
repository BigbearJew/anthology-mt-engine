////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "script_particles.h"
#include "../Include/xrRender/ParticleCustom.h"
#include "../xrEngine/ObjectAnimator.h"
#include "../xrEngine/IGame_Persistent.h"

CScriptParticlesCustom::CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName):
	CParticlesObject(caParticlesName,FALSE,true)
{
//	CScriptParticlesCustom* self = this;
//	Msg							("CScriptParticlesCustom: 0x%08x",*(int*)&self);
	m_owner						= owner;
	m_animator					= 0;
}

//XRCORE_API		xr_delegate< void () >	g_verify_stalkers;

CScriptParticlesCustom::~CScriptParticlesCustom()
{
//	CScriptParticlesCustom* self = this;
//	Msg							("~CScriptParticlesCustom: 0x%08x",*(int*)&self);
//	if ( g_verify_stalkers )
//		g_verify_stalkers		();

	xr_delete					(m_animator);

//	if ( g_verify_stalkers )
//		g_verify_stalkers		();
}

void CScriptParticlesCustom::PSI_internal_delete()
{
	if (m_owner)
		m_owner->m_particles = nullptr;

	CParticlesObject::PSI_destroy();
}

void CScriptParticlesCustom::PSI_destroy()
{
	if (m_owner)
		m_owner->m_particles = nullptr;

	CParticlesObject::PSI_destroy();
}

void CScriptParticlesCustom::shedule_Update(u32 _dt)
{
	CParticlesObject::shedule_Update(_dt);
	if (m_animator){
		float dt				= float(_dt)/1000.f; 
		Fvector prev_pos		= m_animator->XFORM().c;
		m_animator->Update		(dt);
		Fvector vel;
		vel.sub					(m_animator->XFORM().c,prev_pos).div(dt);
		UpdateParent			(m_animator->XFORM(),vel);
	}
}
void CScriptParticlesCustom::LoadPath(LPCSTR caPathName)
{
	if (!m_animator) m_animator	= new CObjectAnimator();
	if ((0==m_animator->Name())||(0!=xr_strcmp(m_animator->Name(),caPathName))){
		m_animator->Clear		();
		m_animator->Load		(caPathName);
	}
}
void CScriptParticlesCustom::StartPath(bool looped)
{
	VERIFY						(m_animator);
	m_animator->Play			(looped);
}
void CScriptParticlesCustom::PausePath(bool val)
{
	VERIFY						(m_animator);
	m_animator->Pause			(val);
}

void CScriptParticlesCustom::StopPath()
{
	VERIFY						(m_animator);
	m_animator->Stop			();
}

void CScriptParticlesCustom::remove_owner	()
{
	R_ASSERT					(m_owner);
	m_owner						= 0;
}

CScriptParticles::CScriptParticles(LPCSTR caParticlesName)
{
    m_transform.identity();
	m_particles = xr_make_shared<CScriptParticlesCustom>(this, caParticlesName);
	g_pGamePersistent->ps_active_deffer.push_back(m_particles);
}

CScriptParticles::~CScriptParticles()
{
	if(m_particles)
	{
		// destroy particles
		m_particles->remove_owner	();

		if (!m_particles->IsLooped() && m_particles->IsPlaying())
			m_particles->SetAutoRemove(true);
		else
			m_particles->PSI_destroy();

		m_particles					= 0;
	}
}

void CScriptParticles::Play(bool hud)
{
    if (!m_particles) return;
    SetHudMode(hud);
    m_particles->Play(hud);
}

void CScriptParticles::PlayAtPos(const Fvector& position, bool hud)
{
    if (!m_particles || !_valid(position)) return;
    m_transform.translate_over(position);
    m_particles->UpdateParent(m_transform, Fvector().set(0, 0, 0));
    Play(hud);
    m_particles->UpdateParent(m_transform, Fvector().set(0, 0, 0));
}

void CScriptParticles::SetHudMode(bool hud)
{
    if (!m_particles || !m_particles->renderable.visual) return;
    if (auto* visual = m_particles->renderable.visual->dcast_ParticleCustom()) visual->SetHudMode(hud);
}

void CScriptParticles::SetDirection(const Fvector& direction)
{
    if (!m_particles || !_valid(direction) || direction.square_magnitude() < EPS_S) return;
    Fmatrix transform;
    transform.identity();
    transform.k.set(direction).normalize();
    Fvector::generate_orthonormal_basis_normalized(transform.k, transform.j, transform.i);
    transform.translate_over(m_transform.c);
    m_transform = transform;
    m_particles->UpdateParent(m_transform, Fvector().set(0, 0, 0));
}

void CScriptParticles::SetOrientation(float yaw, float pitch, float roll)
{
    if (!m_particles || !_valid(yaw) || !_valid(pitch) || !_valid(roll)) return;
    const Fvector position = m_transform.c;
    m_transform.setHPB(yaw, pitch, roll);
    m_transform.translate_over(position);
    m_particles->SetXFORM(m_transform);
}

void CScriptParticles::SetPosition(const Fvector& position)
{
    if (!m_particles || !_valid(position)) return;
    m_transform.translate_over(position);
    m_particles->SetXFORM(m_transform);
}

void CScriptParticles::Stop		()
{
	VERIFY						(m_particles);
	m_particles->Stop			(FALSE);
}

void CScriptParticles::StopDeffered()
{
	VERIFY						(m_particles);
	m_particles->Stop			(TRUE);
}

void CScriptParticles::MoveTo	(const Fvector &pos, const Fvector& vel)
{
	VERIFY						(m_particles);
    if (!_valid(pos) || !_valid(vel)) return;
    m_transform.translate_over(pos);
    m_particles->UpdateParent(m_transform, vel);
}

bool CScriptParticles::IsPlaying() const
{
	VERIFY						(m_particles);
	return m_particles->IsPlaying();
}

bool CScriptParticles::IsLooped	() const
{
	VERIFY						(m_particles);
	return m_particles->IsLooped();
}

void CScriptParticles::LoadPath(LPCSTR caPathName)
{
	VERIFY						(m_particles);
	m_particles->LoadPath		(caPathName);
}
void CScriptParticles::StartPath(bool looped)
{
	m_particles->StartPath		(looped);
}
void CScriptParticles::StopPath	()
{
	m_particles->StopPath		();
}
void CScriptParticles::PausePath(bool val)
{
	m_particles->PausePath		(val);
}
