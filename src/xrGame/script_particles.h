////////////////////////////////////////////////////////////////////////////
//	Module 		: script_particles.h
//	Created 	: 27.07.2004
//  Modified 	: 27.07.2004
//	Author		: Alexander Maximchuk
//	Description : XRay Script particles class
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "../xrScripts/script_export_space.h"
#include "../xrParticles/stdafx.h"
#include "../xrParticles/ParticlesObject.h"

// refs
class CObjectAnimator;
class CScriptParticles;

class CScriptParticlesCustom: 
	public CParticlesObject
{
public:
	CObjectAnimator*			m_animator;
	virtual						~CScriptParticlesCustom();
	CScriptParticles* m_owner;
								CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName);
	virtual void				shedule_Update		(u32 dt);

	void						LoadPath			(LPCSTR caPathName);
	void						StartPath			(bool looped);
	void						StopPath			();
	void						PausePath			(bool val);
	virtual void				PSI_internal_delete	();
	virtual void				PSI_destroy			();
			void				remove_owner		();
};

class CScriptParticles
{
    Fmatrix m_transform;
public:
    void SetDirection(const Fvector& direction);
    void SetOrientation(float yaw, float pitch, float roll);
    void SetPosition(const Fvector& position);
    void SetHudMode(bool hud);
    Fvector LastPosition() const { return m_transform.c; }
	xr_shared_ptr<CScriptParticlesCustom> m_particles;
								CScriptParticles	(LPCSTR caParticlesName);
	virtual						~CScriptParticles	();

	void						Play				(bool hud = false);
	void						PlayAtPos			(const Fvector &pos, bool hud = false);
	void						Stop				();
	void						StopDeffered		();

	bool						IsPlaying			() const;
	bool						IsLooped			() const;

	void						MoveTo				(const Fvector &pos, const Fvector& vel);

	void						LoadPath			(LPCSTR caPathName);
	void						StartPath			(bool looped);
	void						StopPath			();
	void						PausePath			(bool val);

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

#include "script_particles_inline.h"