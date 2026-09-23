#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_Source.h"


#include "../xrCore/RuntimeSeason.h"

void CSoundRender_Emitter::refresh_season_source(bool force)
{
    const bool winter = anthology::runtime_season() == 4;
    if (!force && anthology_winter_mode == winter)
        return;
    anthology_winter_mode = winter;
    // Queued multi-part sounds keep their authored stream/tail accounting.
    if (!owner_data || owner_data->fn_attached[0].size() || owner_data->fn_attached[1].size())
        return;
    CSoundRender_Source* next = SoundRender->i_season_source(source(), winter);
    if (!next || next == source())
        return;

    // Close the old decoder/OpenAL buffers before replacing the source.
    if (target)
        SoundRender->i_stop(this);
    owner_data->handle = next;
    owner_data->dwBytesTotal = next->bytes_total();
    owner_data->fTimeTotal = next->length_sec();
    m_cur_handle_cursor = 0;
    set_cursor(0);
    p_source.base_volume = next->m_fBaseVolume;
    bRewind = FALSE;
    fTimeStarted = SoundRender->fTimer_Value;
    fTimeToStop = fTimeStarted + get_length_sec() / psSpeedOfSound;

    // Preserve owner, position, user volume/range, pause and 2D/3D flags.
    // Loops restart through the normal FSM, which opens the new decoder.
    if (m_current_state == stPlayingLooped || m_current_state == stSimulatingLooped)
        m_current_state = stStartingLooped;
    else if (m_current_state == stPlaying || m_current_state == stSimulating)
        m_current_state = stStarting;
}

void CSoundRender_Emitter::start(ref_sound* _owner, BOOL _loop, float delay)
{
	starting_delay = delay;

	VERIFY(_owner);
	owner_data = _owner->_p;
	VERIFY(owner_data);
    refresh_season_source(true);
	//	source					= (CSoundRender_Source*)owner_data->handle;
	p_source.position.set(0, 0, 0);
	const CSoundRender_Source* authored = source()->anthology_original ? source()->anthology_original : source();
	p_source.min_distance = authored->m_fMinDist; // DS3D_DEFAULTMINDISTANCE;
	p_source.max_distance = authored->m_fMaxDist; // 300.f;
	p_source.base_volume = source()->m_fBaseVolume; // 1.f
	p_source.volume = 1.f; // 1.f
	set_frequency(1.f);
	p_source.max_ai_distance = authored->m_fMaxAIDist; // 300.f;

	if (fis_zero(delay, EPS_L))
	{
		m_current_state = _loop ? stStartingLooped : stStarting;
	}
	else
	{
		m_current_state = _loop ? stStartingLoopedDelayed : stStartingDelayed;
		fTimeToPropagade = SoundRender->Timer.GetElapsed_sec();
	}
	bStopping = FALSE;
	bRewind = FALSE;
}

void CSoundRender_Emitter::i_stop()
{
	bRewind = FALSE;
	if (target) SoundRender->i_stop(this);
	if (owner_data)
	{
		Event_ReleaseOwner();
		VERIFY(this==owner_data->feedback);
		owner_data->feedback = NULL;
		owner_data = NULL;
	}
	m_current_state = stStopped;
}

void CSoundRender_Emitter::stop(BOOL bDeffered)
{
	if (bDeffered) bStopping = TRUE;
	else i_stop();
}

void CSoundRender_Emitter::rewind()
{
    refresh_season_source(true);
	bStopping = FALSE;

	float fTime = SoundRender->Timer.GetElapsed_sec();
	float fDiff = fTime - fTimeStarted;
	fTimeStarted += fDiff;
	fTimeToStop += fDiff;
	fTimeToPropagade = fTime;

	set_cursor(0);
	bRewind = TRUE;
}

void CSoundRender_Emitter::pause(BOOL bVal, int id)
{
	if (bVal)
	{
		if (0 == iPaused) iPaused = id;
	}
	else
	{
		if (id == iPaused)iPaused = 0;
	}
}

void CSoundRender_Emitter::cancel()
{
	// Msg		("- %10s : %3d[%1.4f] : %s","cancel",dbg_ID,priority(),source->fname);
	switch (m_current_state)
	{
	case stPlaying:
		// switch to: SIMULATE
		m_current_state = stSimulating; // switch state
		SoundRender->i_stop(this);
		break;
	case stPlayingLooped:
		// switch to: SIMULATE
		m_current_state = stSimulatingLooped; // switch state
		SoundRender->i_stop(this);
		break;
	default:
		FATAL("Non playing ref_sound forced out of render queue");
		break;
	}
}
