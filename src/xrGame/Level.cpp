#include "pch_script.h"
#include "xrEngine/FDemoRecord.h"
#include "xrEngine/FDemoPlay.h"
#include "xrEngine/Environment.h"
#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/x_ray.h"
#include "xrEngine/EngineThreading.h"
#include "ParticlesObject.h"
#include "Level.h"
#include "HUDManager.h"
#include "xrServer.h"
#include "NET_Queue.h"
#include "game_cl_base.h"
#include "entity_alive.h"
#include "ai_space.h"
#include "ai_debug.h"
#include "ShootingObject.h"
#include "GameTaskManager.h"
#include "Level_Bullet_Manager.h"
#include "script_process.h"
#include "script_engine.h"
#include "script_engine_space.h"
#include "team_base_zone.h"
#include "infoportion.h"
#include "patrol_path_storage.h"
#include "date_time.h"
#include "space_restriction_manager.h"
#include "seniority_hierarchy_holder.h"
#include "space_restrictor.h"
#include "client_spawn_manager.h"
#include "autosave_manager.h"
#include "ClimableObject.h"
#include "level_graph.h"
#include "mt_config.h"
#include "phcommander.h"
#include "map_manager.h"
#include "xrEngine/CameraManager.h"
#include "level_sounds.h"
#include "car.h"
#include "trade_parameters.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "MainMenu.h"
#include "xrEngine/XR_IOConsole.h"
#include "actor.h"
#include "player_hud.h"
#include "UI/UIGameTutorial.h"
#include "file_transfer.h"
#include "message_filter.h"
#include "demoplay_control.h"
#include "demoinfo.h"
#include "CustomDetector.h"
#include "xrPhysics/IPHWorld.h"
#include "xrPhysics/console_vars.h"
#include "../xrEngine/device.h"
#include "../xrServerEntities/object_factory.h"
#include "../xrServerEntities/clsid_game.h"

#include "UIGameCustom.h"
#include "ui/UIPdaWnd.h"
#include "UICursor.h"
#include "debug_renderer.h"
#include "LevelDebugScript.h"
#include "script_attachment_manager.h"
#include "script_light_inline.h"

#include "alife_simulator.h"
#include "alife_object_registry.h"

namespace
{
unsigned long long XRayLuaGCAtomicProfileClock()
{
	return static_cast<unsigned long long>(CPU::QPC());
}

void XRayReportLuaGCAtomicProfile(lua_State* lua)
{
	lua_XRayGCAtomicProfile profile{};
	if (!lua_xray_gc_atomic_profile_snapshot(lua, &profile))
		return;
	if (!mt_FrameProfile || !CPU::qpc_freq)
		return;

	const double ticks_to_ms = 1000.0 / double(CPU::qpc_freq);
	const double total_ms = profile.total_ticks * ticks_to_ms;
	if (total_ms < 10.0)
		return;

	Msg("* [Lua GC/xray-atomic] sequence=%llu total=%.2f ms "
		"phases(remark-roots/grayagain/separateudata/mmudata/weak-sweep)=%.2f/%.2f/%.2f/%.2f/%.2f ms "
		"leaf(marked/unmarked/finalized)=%llu/%llu/%llu",
		static_cast<unsigned long long>(profile.sequence), total_ms,
		profile.roots_ticks * ticks_to_ms,
		profile.grayagain_ticks * ticks_to_ms,
		profile.separateudata_ticks * ticks_to_ms,
		profile.mmudata_ticks * ticks_to_ms,
		profile.weak_sweep_ticks * ticks_to_ms,
		static_cast<unsigned long long>(profile.leaf_udata_marked),
		static_cast<unsigned long long>(profile.leaf_udata_unmarked),
		static_cast<unsigned long long>(profile.leaf_udata_finalized));
}
}

#ifdef DEBUG
#include "level_debug.h"
#include "ai/stalker/ai_stalker.h"
#include "PhysicObject.h"
#include "PHDebug.h"
#include "debug_text_tree.h"
#endif
extern ENGINE_API bool g_dedicated_server;
extern ENGINE_API BOOL	g_bootComplete;
extern CUISequencer* g_tutorial;
extern CUISequencer* g_tutorial2;

float g_cl_lvInterp = 0.1;
u32 lvInterpSteps = 0;

#ifdef SPAWN_ANTIFREEZE
BOOL spawn_antifreeze = TRUE;
BOOL spawn_antifreeze_debug = FALSE;
int spawn_antifreeze_max_per_frame = 8;
// The measured quickload decode stage is only ~55 ms and normal batches do not
// reach the safe parallel threshold. Keep the experimental path available for
// diagnostics, but do not pay its classification/allocation cost by default.
BOOL mt_load_spawn_decode = FALSE;

struct spawn_and_prefetch_events
{
	CLevel* level = nullptr;
	NET_Queue_Event* spawn_events = nullptr;
    spawn_events_data_map* spawn_events_data = nullptr;
    prefetch_event_queue* prefetch_events = nullptr;
    models_set* prefetched_models = nullptr;
    bool* closeSignal = nullptr;
    xrSRWLock* prefetch_lock = nullptr;
    bool* busy = nullptr;
	HANDLE signal = nullptr;
	HANDLE stopped = nullptr;
};

u16	GetSpawnInfo(NET_Packet& P, u16& parent_id, shared_str& section)
{
    u16 dummy16, id;
    P.r_begin(dummy16);

    shared_str s_name;
    P.r_stringZ(s_name);
    section = s_name;

    string256 temp;
    P.r_stringZ(temp);

    u8 temp_gt, s_RP;
    Fvector o_Position, o_Angle;
    u16 RespawnTime;
    P.r_u8(temp_gt/*s_gameid*/);
    P.r_u8(s_RP);
    P.r_vec3(o_Position);
    P.r_vec3(o_Angle);
    P.r_u16(RespawnTime);
    P.r_u16(id);
    P.r_u16(parent_id);

    P.r_pos = 0;
    return id;
}

void CLevel::RegisterPreparedClientSpawnResource(u16 id, u16 parent_id, const shared_str& section,
	const shared_str& actual_visual, const shared_str& ltx_visual, LPCSTR canonical_level_path)
{
	prepared_client_spawn_resource resource;
	resource.section = section;
	resource.parent_id = parent_id;
	resource.actual_visual = actual_visual;
	resource.ltx_visual = ltx_visual;
	resource.level_path = canonical_level_path ? canonical_level_path : "";
	xrCriticalSectionGuard guard(prepared_client_spawn_guard);
	prepared_client_spawn_resources[id] = std::move(resource);
}

bool CLevel::HasPreparedClientSpawnResource(u16 id)
{
	xrCriticalSectionGuard guard(prepared_client_spawn_guard);
	return prepared_client_spawn_resources.find(id) != prepared_client_spawn_resources.end();
}

bool CLevel::PublishPreparedClientSpawnResource(NET_Packet& packet)
{
	NET_Packet copy = packet;
	u16 parent_id;
	shared_str section;
	const u16 id = GetSpawnInfo(copy, parent_id, section);
	prepared_client_spawn_resource resource;
	{
		xrCriticalSectionGuard guard(prepared_client_spawn_guard);
		auto found = prepared_client_spawn_resources.find(id);
		if (found == prepared_client_spawn_resources.end())
			return false;
		resource = std::move(found->second);
		prepared_client_spawn_resources.erase(found);
	}
	if (resource.parent_id != parent_id || resource.section != section)
		return false;

	bool actual_published = false;
	if (resource.actual_visual.size())
		actual_published = ::Render->models_PrefetchPrepared(resource.actual_visual.c_str(),
			resource.level_path.c_str(), false);
	if (!actual_published && resource.ltx_visual.size() && resource.ltx_visual != resource.actual_visual)
		actual_published = ::Render->models_PrefetchPrepared(
			resource.ltx_visual.c_str(), resource.level_path.c_str(), false);
	return actual_published;
}
#endif
//-AVO

// Define a helper struct to hold the heavy data
struct ProcessNetPacket : public intrusive_base_nonatomic
{
    NET_Packet P;
};

namespace crash_saving {
    extern void(*save_impl)();
    static bool g_isSaving = false;
    int saveCountMax = 10;

    void _save_impl()
    {
        if (g_isSaving) return;
        if (saveCountMax <= 0) return;

        int saveCount = -1;
        g_isSaving = true;
        auto data = make_intrusive<ProcessNetPacket>();
        NET_Packet& net_packet = data->P;
        net_packet.w_begin(M_SAVE_GAME);

        xr_string path = "fatal_ctd_save_";
        xr_string path_mask(path);
        xr_string path_ext = ".scop";
        path_mask.append("*").append(path_ext);

        FS_FileSet fset_temp;
        FS.file_list(fset_temp, "$game_saves$", FS_ListFiles | FS_RootOnly, path_mask.c_str());

        xr_vector<FS_File> fset;
        for (auto& file : fset_temp)
        {
            fset.push_back(file);
        }
        struct {
            bool operator()(FS_File& a, FS_File& b) {
                return a.time_write > b.time_write;
            }
        } sortFilesDesc;
        std::sort(fset.begin(), fset.end(), sortFilesDesc);

        //Msg("save mask %s", path_mask.c_str());

        for (auto& file : fset)
        {
            string128 name;
            xr_strcpy(name, sizeof(name), file.name.c_str());
            xr_string name_string(name);
            name_string.erase(name_string.length() - path_ext.length());

            //Msg("found save file %s, save_name %s", name, name_string.c_str());

            try {
                //Msg("save number %s", name_string.substr(path.length()).c_str());
                int name_count = std::stoi(name_string.substr(path.length()).c_str());
                saveCount = name_count;
                break;
            }
            catch (...) {
                Msg("!error getting save number from %s", name);
            }
        }

        saveCount++;
        if (saveCount >= saveCountMax) {
            saveCount = 0;
        }

        path.append(std::to_string(saveCount));
        net_packet.w_stringZ(path.c_str());
        net_packet.w_u8(1);
        CLevel& level = Level();
        if (&level != nullptr)
        {
            level.Send(net_packet, net_flags(1));
        }

    }
}

CLevel::CLevel() :
    IPureClient(Device.GetTimerGlobal())
#ifdef PROFILE_CRITICAL_SECTIONS
    , DemoCS(MUTEX_PROFILE_ID(DemoCS))
#endif
{
    PROF_EVENT("CLevel::CLevel");
    g_bDebugEvents = Core.ParamsData.test(ECoreParams::debug_ge);
    game_events = xr_new<NET_Queue_Event>();

    eChangeRP = Engine.Event.Handler_Attach("LEVEL:ChangeRP", this);
    eDemoPlay = Engine.Event.Handler_Attach("LEVEL:PlayDEMO", this);
    eChangeTrack = Engine.Event.Handler_Attach("LEVEL:PlayMusic", this);
    eEnvironment = Engine.Event.Handler_Attach("LEVEL:Environment", this);
    eEntitySpawn = Engine.Event.Handler_Attach("LEVEL:spawn", this);
    m_pBulletManager = xr_new<CBulletManager>();
    if (!g_dedicated_server)
    {
        m_map_manager = xr_new<CMapManager>();
        m_game_task_manager = xr_new<CGameTaskManager>();
    }
    m_dwDeltaUpdate = u32(fixed_step * 1000);
    m_seniority_hierarchy_holder = xr_new<CSeniorityHierarchyHolder>();
    if (!g_dedicated_server)
    {
        m_level_sound_manager = xr_new<CLevelSoundManager>();
        m_space_restriction_manager = xr_new<CSpaceRestrictionManager>();
        m_client_spawn_manager = xr_new<CClientSpawnManager>();
        m_autosave_manager = xr_new<CAutosaveManager>();
        m_debug_renderer = xr_new<CDebugRenderer>();
#ifdef DEBUG
        m_level_debug = xr_new<CLevelDebug>();
#endif
    }
    m_ph_commander = xr_new<CPHCommander>();
    m_ph_commander_scripts = xr_new<CPHCommander>();
    pObjects4CrPr.clear();
    pActors4CrPr.clear();
    g_player_hud = xr_new<player_hud>();
    g_player_hud->load_default();

#ifdef SPAWN_ANTIFREEZE
    spawn_events = xr_new<NET_Queue_Event>();
    spawn_events_data = xr_new<spawn_events_data_map>();
    prefetch_events = xr_new<prefetch_event_queue>();
    prefetched_models = xr_new<models_set>();
	prefetch_thread_signal = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	prefetch_thread_stopped = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	R_ASSERT(prefetch_thread_signal && prefetch_thread_stopped);
    auto events = new spawn_and_prefetch_events({ this, spawn_events, spawn_events_data, prefetch_events, prefetched_models,
		&closeSignal, &prefetch_lock, &spawn_prefetch_busy, prefetch_thread_signal, prefetch_thread_stopped });
    thread_spawn(ProcessPrefetchEvents, "Pre-Spawn Prefetcher Thread", 0, events);
    Msg("CLevel::CLevel() Spawn Antifreeze initialized");
#endif

    Msg("%s", Core.Params);
    //crash_saving::save_impl = crash_saving::_save_impl; // CLevel ready, we can save now
}

extern CAI_Space* g_ai_space;

CLevel::~CLevel()
{
    PROF_EVENT("CLevel::~CLevel");
    //crash_saving::save_impl = nullptr; // CLevel not available, disable crash save
    xr_delete(g_player_hud);
    delete_data(m_script_attachments);
    delete_data(hud_zones_list);
    hud_zones_list = nullptr;
    Msg("- Destroying level");
	ShutdownGameSpecificPrepare();
    Engine.Event.Handler_Detach(eEntitySpawn, this);
    Engine.Event.Handler_Detach(eEnvironment, this);
    Engine.Event.Handler_Detach(eChangeTrack, this);
    Engine.Event.Handler_Detach(eDemoPlay, this);
    Engine.Event.Handler_Detach(eChangeRP, this);
    if (physics_world())
    {
        destroy_physics_world();
        xr_delete(m_ph_commander_physics_worldstep);
    }
    // destroy PSs
    for (POIt p_it = m_StaticParticles.begin(); m_StaticParticles.end() != p_it; ++p_it)
        Particles::Details::Destroy(*p_it);
    m_StaticParticles.clear();
    // Unload sounds
    // unload prefetched sounds
    sound_registry.clear();
    // unload static sounds
    for (u32 i = 0; i < static_Sounds.size(); ++i)
    {
        static_Sounds[i]->destroy();
        xr_delete(static_Sounds[i]);
    }
    static_Sounds.clear();
    xr_delete(m_level_sound_manager);
    xr_delete(m_space_restriction_manager);
    xr_delete(m_seniority_hierarchy_holder);
    xr_delete(m_client_spawn_manager);
    xr_delete(m_autosave_manager);
    xr_delete(m_debug_renderer);
    delete_data(m_debug_render_queue);
    if (!g_dedicated_server)
        ai().script_engine().remove_script_process(ScriptEngine::eScriptProcessorLevel);

#ifdef SPAWN_ANTIFREEZE
	{
		xrSRWLockGuard g(prefetch_lock);
		closeSignal = true;
	}
	SetEvent(prefetch_thread_signal);
	R_ASSERT(WAIT_OBJECT_0 == WaitForSingleObject(prefetch_thread_stopped, INFINITE));
	CloseHandle(prefetch_thread_signal);
	CloseHandle(prefetch_thread_stopped);
	prefetch_thread_signal = nullptr;
	prefetch_thread_stopped = nullptr;
	xr_delete(spawn_events);
	xr_delete(spawn_events_data);
	xr_delete(prefetch_events);
	xr_delete(prefetched_models);
#endif

    xr_delete(game);
    xr_delete(game_events);

    xr_delete(m_pBulletManager);
    xr_delete(pStatGraphR);
    xr_delete(pStatGraphS);
    xr_delete(m_ph_commander);
    xr_delete(m_ph_commander_scripts);
    pObjects4CrPr.clear();
    pActors4CrPr.clear();
    ai().unload();
#ifdef DEBUG
    xr_delete(m_level_debug);
#endif
    xr_delete(m_map_manager);
    delete_data(m_game_task_manager);
    // here we clean default trade params
    // because they should be new for each saved/loaded game
    // and I didn't find better place to put this code in
    // XXX nitrocaster: find better place for this clean()
    CTradeParameters::clean();
    if (g_tutorial && g_tutorial->m_pStoredInputReceiver == this)
        g_tutorial->m_pStoredInputReceiver = nullptr;
    if (g_tutorial2 && g_tutorial2->m_pStoredInputReceiver == this)
        g_tutorial2->m_pStoredInputReceiver = nullptr;
    if (IsDemoPlay())
    {
        StopPlayDemo();
        if (m_reader)
        {
            FS.r_close(m_reader);
            m_reader = nullptr;
        }
    }
    xr_delete(m_msg_filter);
    xr_delete(m_demoplay_control);
    xr_delete(m_demo_info);
    if (IsDemoSave())
    {
        StopSaveDemo();
    }
    deinit_compression();
}

shared_str CLevel::name() const
{
    return map_data.m_name;
}

void CLevel::GetLevelInfo(CServerInfo* si)
{
    if (Server && game)
    {
        Server->GetServerInfo(si);
    }
}

void CLevel::PrefetchSound(LPCSTR name)
{
    // preprocess sound name
    string_path tmp;
    xr_strcpy(tmp, name);
    xr_strlwr(tmp);
    if (strext(tmp))
        *strext(tmp) = 0;
    shared_str snd_name = tmp;
    // find in registry
    SoundRegistryMapIt it = sound_registry.find(snd_name);
    // if find failed - preload sound
    if (it == sound_registry.end())
        sound_registry[snd_name].create(snd_name.c_str(), st_Effect, sg_SourceType);
}

// Game interface ////////////////////////////////////////////////////
int CLevel::get_RPID(LPCSTR /**name/**/)
{
    /*
    // Gain access to string
    LPCSTR	params = pLevel->r_string("respawn_point",name);
    if (0==params)	return -1;

    // Read data
    Fvector4	pos;
    int			team;
    sscanf		(params,"%f,%f,%f,%d,%f",&pos.x,&pos.y,&pos.z,&team,&pos.w); pos.y += 0.1f;

    // Search respawn point
    svector<Fvector4,maxRP>	&rp = Level().get_team(team).RespawnPoints;
    for (int i=0; i<(int)(rp.size()); ++i)
    if (pos.similar(rp[i],EPS_L))	return i;
    */
    return -1;
}

bool g_bDebugEvents = false;

void CLevel::cl_Process_Event(u16 dest, u16 type, NET_Packet& P)
{
    // Msg("--- event[%d] for [%d]",type,dest);
    CObject* O = Objects.net_Find(dest);
    if (0 == O)
    {
#ifdef DEBUG
        Msg("* WARNING: c_EVENT[%d] to [%d]: unknown dest", type, dest);
#endif
        return;
    }
    CGameObject* GO = smart_cast<CGameObject*>(O);
    if (!GO)
    {
#ifndef MASTER_GOLD
        Msg("! ERROR: c_EVENT[%d] : non-game-object", dest);
#endif
        return;
    }
    if (type != GE_DESTROY_REJECT)
    {
        if (type == GE_DESTROY)
        {
            Game().OnDestroy(GO);
        }
        GO->OnEvent(P, type);
    }
    else
    {
        // handle GE_DESTROY_REJECT here
        u32 pos = P.r_tell();
        u16 id = P.r_u16();
        P.r_seek(pos);
        bool ok = true;
        CObject* D = Objects.net_Find(id);
        if (0 == D)
        {
#ifndef MASTER_GOLD
            Msg("! ERROR: c_EVENT[%d] : unknown dest", id);
#endif
            ok = false;
        }
        CGameObject* GD = smart_cast<CGameObject*>(D);
        if (!GD)
        {
#ifndef MASTER_GOLD
            Msg("! ERROR: c_EVENT[%d] : non-game-object", id);
#endif
            ok = false;
        }
        GO->OnEvent(P, GE_OWNERSHIP_REJECT);
        if (ok)
        {
            Game().OnDestroy(GD);
            GD->OnEvent(P, GE_DESTROY);
        }
    }
}

//AVO: used by SPAWN_ANTIFREEZE (by alpet, edited by demonized)
#ifdef SPAWN_ANTIFREEZE
bool CLevel::PostponedSpawnFind(u16 id, const NET_Event& E) const
{
    auto data = make_intrusive<ProcessNetPacket>();
    NET_Packet& P = data->P;
    E.implication(P);
    return PostponedSpawnFind(id, P);
}

bool CLevel::PostponedSpawnFind(u16 id, NET_Packet& P) const
{
    u16 parent_id;
    shared_str section;
    return id == GetSpawnInfo(P, parent_id, section);
}

bool CLevel::PostponedSpawn(u16 id)
{
    PROF_EVENT("ProcessGameEvents PostponedSpawn");

    xrSRWLockGuard g(prefetch_lock, true);
    auto queue = prefetch_events;
    auto it = std::find_if(queue->begin(), queue->end(), [id, this](prefetch_event& E) { return PostponedSpawnFind(id, E.p); });

    auto& queue2 = spawn_events->queue;
    auto it2 = std::find_if(queue2.begin(), queue2.end(), [id, this](const NET_Event& E) { return PostponedSpawnFind(id, E); });
    return it != queue->end() || it2 != queue2.end();
}

int CLevel::GetSpawnEventPriority(const NET_Event& e) const
{
    if (e.ID == M_EVENT)
        return 0;

    if (e.ID == M_SPAWN) {
        auto data = make_intrusive<ProcessNetPacket>();
        NET_Packet& P = data->P;
        e.implication(P);

        u16 parent_id = 0;
        shared_str section;
        GetSpawnInfo(P, parent_id, section);
        if (parent_id < 0xFFFF)
            return 1;

        return 2;
    }

    return 0;
}

bool CLevel::SpawnEventCompare(const NET_Event& a, const NET_Event& b) const
{
    return GetSpawnEventPriority(a) > GetSpawnEventPriority(b);
}

// demonized: If called manually, be aware of ProcessPrefetchEvents thread, which may modify spawn_events queue at the same time, maybe fix later
void CLevel::SortSpawnEventsQueue()
{
    xrSRWLockGuard g(prefetch_lock);
    auto& queue = spawn_events->queue;
    std::stable_sort(queue.begin(), queue.end(), [this](const NET_Event& a, const NET_Event& b) { return SpawnEventCompare(a, b); });
}

void CLevel::ProcessPrefetchEvents(void* args)
{
    auto events = reinterpret_cast<spawn_and_prefetch_events*>(args);
	auto level = events->level;
    auto spawn_events = events->spawn_events;
    auto spawn_events_data = events->spawn_events_data;
    auto prefetch_events = events->prefetch_events;
    auto prefetched_models = events->prefetched_models;
    auto closeSignal = events->closeSignal;
    auto prefetch_lock = events->prefetch_lock;
    auto busy = events->busy;
	auto signal = events->signal;
	auto stopped = events->stopped;

    while (true)
    {
        WaitForSingleObject(signal, INFINITE); // wait for prefetch queue event to be signaled

        PROF_EVENT("ProcessPrefetchEvents")
            prefetch_event_queue saved_prefetch_events;
		bool close = false;
        {
            xrSRWLockGuard g(prefetch_lock);
			close = *closeSignal;
			if (!close)
			{
				if (prefetch_events->empty())
				{
					if (spawn_antifreeze_debug) Msg("[ProcessPrefetchEvents] called, but prefetch_events queue is empty");
				}
				else
				{
					if (spawn_antifreeze_debug) Msg("[ProcessPrefetchEvents] started, queue size %d", prefetch_events->size());
					saved_prefetch_events.swap(*prefetch_events); // move the events to temp queue, so we can continue processing prefetch_events in the main thread
					*busy = true;
				}
				ResetEvent(signal);
			}
        }

		if (close)
		{
			if (spawn_antifreeze_debug) Msg("[ProcessPrefetchEvents] closeSignal received, destroying thread");
			delete events;
			SetEvent(stopped);
			return;
		}

		if (saved_prefetch_events.empty())
			continue;

		constexpr size_t publish_chunk_size = 32;
		prefetch_event_queue ready_prefetch_events;
		ready_prefetch_events.reserve(publish_chunk_size);
		u32 published_events = 0;

		auto publish_ready_events = [&]()
		{
			if (ready_prefetch_events.empty())
				return;

			xrSRWLockGuard g(prefetch_lock);
			for (auto& ready : ready_prefetch_events)
			{
				const u16 id = ready.id;
				spawn_events->insert(ready.p);
				spawn_events_data->emplace(id, std::move(ready));
				++published_events;
			}
			ready_prefetch_events.clear();
		};

		for (auto& E : saved_prefetch_events)
		{
			// Connection spawns already have worker-built blueprints. Recreating the
			// same visual here made the blueprint useless and committed renderer/Lua
			// state from this background thread. Runtime spawns keep the legacy path.
			if (!level->HasPreparedClientSpawnResource(E.id))
			{
				for (const auto& model : E.models)
				{
					bool not_prefetched = false;
					{
						xrSRWLockGuard g(prefetch_lock, true);
						not_prefetched = prefetched_models->find(model) == prefetched_models->end();
					}
					if (!not_prefetched)
						continue;

					::Render->models_PrefetchOne(model.c_str(), false);
					xrSRWLockGuard g(prefetch_lock);
					prefetched_models->insert(model);
				}
			}

			// Keep the original server order, but expose completed objects in small
			// chunks. The old all-or-nothing batch made geometry and NPCs appear late
			// even when their models were already ready.
			ready_prefetch_events.emplace_back(std::move(E));
			if (ready_prefetch_events.size() >= publish_chunk_size)
				publish_ready_events();
		}
		publish_ready_events();

		{
			xrSRWLockGuard g(prefetch_lock);
			*busy = false;

			if (spawn_antifreeze_debug)
				Msg("[ProcessPrefetchEvents] finished, published %u, spawn_events queue size %d",
					published_events, spawn_events->queue.size());
        }
    }
}

// demonized: If called manually, be aware of ProcessPrefetchEvents thread, which may modify spawn_events queue at the same time, maybe fix later
void CLevel::ProcessSpawnEvents()
{
    PROF_EVENT("ProcessSpawnEvents");

	xr_vector<NET_Event> events_to_process;
    spawn_events_data_map spawn_events_data_copy;
	{
		xrSRWLockGuard g(prefetch_lock);
		if (!spawn_events->queue.empty())
		{
			// Loading must still drain the complete client-spawn queue before the
			// player gets control. During normal gameplay the worker can publish a
			// large prepared chunk at once, but committing that whole chunk here
			// creates a main-thread hitch. Keep the rest queued for later frames.
			const bool smooth_runtime_publication = g_bootComplete &&
				(!pApp || !pApp->LoadSessionActive()) && spawn_antifreeze_max_per_frame > 0;
			if (smooth_runtime_publication &&
				spawn_events->queue.size() > static_cast<size_t>(spawn_antifreeze_max_per_frame))
			{
				const size_t batch_size = static_cast<size_t>(spawn_antifreeze_max_per_frame);
				events_to_process.reserve(batch_size);
				for (size_t index = 0; index < batch_size; ++index)
					events_to_process.emplace_back(std::move(spawn_events->queue[index]));
				spawn_events->queue.erase(spawn_events->queue.begin(),
					spawn_events->queue.begin() + batch_size);
			}
			else
				events_to_process.swap(spawn_events->queue);
		}

		const bool queue_was_partially_drained = !spawn_events->queue.empty();
        if (!spawn_events_data->empty() && !queue_was_partially_drained)
        {
            spawn_events_data_copy.swap(*spawn_events_data);
        }
		else if (!spawn_events_data->empty())
		{
			// Keep blueprint/model data for deferred events. Move only the map
			// entries belonging to the batch selected above.
			auto packet_data = make_intrusive<ProcessNetPacket>();
			for (const NET_Event& event : events_to_process)
			{
				NET_Packet& packet = packet_data->P;
				event.implication(packet);
				u16 parent_id = u16(-1);
				shared_str section;
				const u16 object_id = GetSpawnInfo(packet, parent_id, section);
				auto data_it = spawn_events_data->find(object_id);
				if (data_it == spawn_events_data->end())
					continue;
				spawn_events_data_copy.emplace(object_id, std::move(data_it->second));
				spawn_events_data->erase(data_it);
			}
		}
	}

	struct decoded_spawn
	{
		CSE_Abstract* entity = nullptr;
		shared_str section;
		CLASS_ID clsid = 0;
		u16 object_id = u16(-1);
		u16 parent_id = u16(-1);
		u64 decode_ticks = 0;
		bool parallel_safe = false;
		bool configuration_matches = true;
	};

	xr_vector<decoded_spawn> decoded;
	bool use_parallel_decode = mt_load_spawn_decode && pApp && pApp->LoadSessionActive() &&
		events_to_process.size() >= 64;
	if (use_parallel_decode)
	{
		decoded.resize(events_to_process.size());
		auto packet_data = make_intrusive<ProcessNetPacket>();
		for (u32 index = 0; index < events_to_process.size(); ++index)
		{
			NET_Packet& packet = packet_data->P;
			events_to_process[index].implication(packet);
			decoded_spawn& item = decoded[index];
			item.object_id = GetSpawnInfo(packet, item.parent_id, item.section);
			if (!pSettings->section_exist(item.section.c_str()) ||
				!pSettings->line_exist(item.section.c_str(), "class"))
				continue;

			item.clsid = pSettings->r_clsid(item.section.c_str(), "class");
			string16 clsid_text;
			CLSID2TEXT(item.clsid, clsid_text);
			const bool ai_entity = !strncmp(clsid_text, "AI_", 3);
			// Script-created server entities enter the single Lua VM. Actor/AI
			// constructors can use the live ALife registry and the process-wide RNG,
			// while custom_data can enter the ALife config registry. Keep all of them
			// on the owner thread; workers get native data-only server entities.
			item.parallel_safe = object_factory().server_object_parallel_safe(item.clsid) &&
				item.clsid != CLSID_OBJECT_ACTOR && !ai_entity &&
				!pSettings->line_exist(item.section.c_str(), "custom_data");
		}

		const u64 parallel_started_at = CPU::QPC();
		try
		{
			xr_parallel_for(0u, static_cast<u32>(decoded.size()), [&](u32 index)
			{
				decoded_spawn& item = decoded[index];
				if (!item.parallel_safe)
					return;

				NET_Packet packet;
				events_to_process[index].implication(packet);
				u16 message;
				packet.r_begin(message);
				shared_str packet_section;
				packet.r_stringZ(packet_section);
				if (packet_section != item.section)
					return;

				const u64 started_at = CPU::QPC();
				item.entity = F_entity_Create(item.section.c_str(), item.clsid);
				if (!item.entity)
					return;
				item.entity->Spawn_Read(packet);
				if (item.entity->s_flags.is(M_SPAWN_UPDATE))
					item.entity->UPDATE_Read(packet);
				item.configuration_matches = item.entity->match_configuration();
				item.decode_ticks = CPU::QPC() - started_at;
			});
		}
		catch (...)
		{
			for (decoded_spawn& item : decoded)
				if (item.entity)
					F_entity_Destroy(item.entity);
			decoded.clear();
			use_parallel_decode = false;
			Msg("! [client-spawn/decode] worker preparation failed; using owner-thread fallback");
		}

		if (use_parallel_decode)
		{
			u32 prepared_count = 0;
			u32 owner_fallback_count = 0;
			for (const decoded_spawn& item : decoded)
			{
				prepared_count += item.entity != nullptr;
				owner_fallback_count += item.entity == nullptr;
			}
			const double wall_ms = double(CPU::QPC() - parallel_started_at) * 1000.0 / double(CPU::qpc_freq);
			Msg("* [client-spawn/decode] parallel prepared=%u owner-fallback=%u wall=%.2f ms",
				prepared_count, owner_fallback_count, wall_ms);
		}
	}

	// NET_Packet is 16 KiB. Reuse one owner-thread packet for the serial spawn
	// loop instead of allocating and freeing it once per event.
	auto packet_data = make_intrusive<ProcessNetPacket>();
	for (u32 event_index = 0; event_index < events_to_process.size(); ++event_index)
	{
		const NET_Event& E = events_to_process[event_index];
		u16 ID, dest, type;
		NET_Packet& P = packet_data->P;
		ID = E.ID;
		dest = E.destination;
		type = E.type;
		E.implication(P);

		u16 parent_id;
		shared_str section;
		u16 obj_id;
		decoded_spawn* prepared = use_parallel_decode ? &decoded[event_index] : nullptr;
		if (prepared)
		{
			parent_id = prepared->parent_id;
			section = prepared->section;
			obj_id = prepared->object_id;
		}
		else
			obj_id = GetSpawnInfo(P, parent_id, section);

		if (spawn_antifreeze_debug) Msg("[ProcessSpawnEvents] spawning section %s, obj_id %d, parent_id %d, event_id %d", section.c_str(), obj_id, parent_id, dest);

		// demonized: If item is II_BOLT class - go through anyway
		if (pSettings->line_exist(section.c_str(), "class") && strstr(pSettings->r_string(section.c_str(), "class"), "II_BOLT") != nullptr)
		{
			// demonized: this is a sin, but its an easy way
			goto spawn;
		}

        // If the object was in alife, but now its absent, skip it
        auto spawn_data_it = spawn_events_data_copy.find(obj_id);
        if (spawn_data_it != spawn_events_data_copy.end())
        {
            if (spawn_data_it->second.hasAlifeObject)
            {
                auto obj = ai().alife().objects().object(obj_id);
				if (!obj)
				{
					if (spawn_antifreeze_debug) Msg("![ProcessSpawnEvents] object was in alife, but now is not, do not spawn, section %s, obj_id %d, parent_id %d, event_id %d", section.c_str(), obj_id, parent_id, dest);
					if (prepared && prepared->entity)
						F_entity_Destroy(prepared->entity);
					continue;
				}
            }
        }        

		// If there is a parent of this object, check if its still in alife
		if (parent_id != 0xffff)
		{
			auto parent_obj = ai().alife().objects().object(parent_id);
			if (!parent_obj || !parent_obj->m_bOnline)
			{
				if (spawn_antifreeze_debug) Msg("![ProcessSpawnEvents] parent object is not in alife, do not spawn, section %s, obj_id %d, parent_id %d, event_id %d", section.c_str(), obj_id, parent_id, dest);
				if (prepared && prepared->entity)
					F_entity_Destroy(prepared->entity);
				continue;
			}
		}

		// Model publication can enter renderer Lua shader lookup. It must stay
		// on the owner thread and immediately precede the original spawn.
		if (spawn_data_it != spawn_events_data_copy.end() && !HasPreparedClientSpawnResource(obj_id))
		{
			for (const xr_string& model : spawn_data_it->second.models)
			{
				bool needs_prefetch = false;
				{
					xrSRWLockGuard g(prefetch_lock);
					needs_prefetch = prefetched_models->insert(model).second;
				}
				if (needs_prefetch)
					::Render->models_PrefetchOne(model.c_str(), false);
			}
		}

	spawn:
		u16 dummy16;
		P.r_begin(dummy16);
		pApp->LoadSessionRecordClientEvent(true, 0, 0, P.B.data, P.B.count);
		if (prepared && prepared->entity)
		{
			if (prepared->configuration_matches)
				cl_Process_Spawn(P, prepared->entity, prepared->section, prepared->decode_ticks);
			else
				F_entity_Destroy(prepared->entity);
			prepared->entity = nullptr;
		}
		else
			cl_Process_Spawn(P);
	}

	for (decoded_spawn& item : decoded)
		if (item.entity)
			F_entity_Destroy(item.entity);
}
#endif

void CLevel::ProcessGameEvents()
{
	PROF_EVENT("ProcessGameEvents");

	xr_vector<NET_Event> events_to_process;
	xr_vector<prefetch_event> events_to_prefetch;
	{
		xrSRWLockGuard g(prefetch_lock);
		if (!game_events->queue.empty())
		{
			events_to_process.swap(game_events->queue);
		}
	}

	// NET_Packet is 16 KiB and this queue contains thousands of entries during
	// a save load. Keep one packet for the serial owner-thread loop; postponed
	// prefetch entries still receive their own durable packet copy below.
	auto packet_data = make_intrusive<ProcessNetPacket>();

	// Game events
	{
		for (auto it = events_to_process.begin(); it != events_to_process.end(); )
		{
			PROF_EVENT("ProcessGameEvents game_events queue");
			u16 ID = it->ID;
			u16 dest = it->destination;
			u16 type = it->type;

			auto& P = packet_data->P;
			it->implication(P);

//AVO: spawn antifreeze implementation, originally by alpet, reritten by demonized
#ifdef SPAWN_ANTIFREEZE
			if (spawn_antifreeze && g_bootComplete)
			{
				// Postpone M_EVENT for postponed spawns
				//if (M_EVENT == ID && PostponedSpawn(dest))
				//{
				//	game_events->insert(P);
				//	Msg("[ProcessGameEvents] postponed M_EVENT, object in prefetch queue: obj_id %d", dest);
				//	it = game_events->queue.erase(it); // remove current event
				//	continue;
				//}

				// add to prefetch_events queue for postponed spawn
				if (M_SPAWN == ID)
				{
					PROF_EVENT("ProcessGameEvents M_SPAWN");

					u16 parent_id;
					shared_str section;
					u16 obj_id = GetSpawnInfo(P, parent_id, section);

					static auto isValidToPrefetch = [](u16 parent_id, shared_str& section, u16 obj_id, NET_Packet& P) {
						if (pSettings->line_exist("spawn_antifreeze_ignore", section))
						{
							return false;
						}

						bool valid = true;

						if (pSettings->line_exist(section.c_str(), "class"))
						{
							auto c = pSettings->r_string(section.c_str(), "class");

							// Do not prefetch fake missiles of a weapon
							valid &= strstr(c, "G_RPG7") == nullptr;
							valid &= strstr(c, "G_FAKE") == nullptr;

							// Do not prefetch helicopters
							valid &= strstr(c, "C_HLCP") == nullptr;
						}

						return valid;
					};

					if (isValidToPrefetch(parent_id, section, obj_id, P))
					{
						models_set models;

						static auto safe_insert = [](models_set& models, LPCSTR model) {
							if (model)
							{
								string_path modelWithoutExtension;
								xr_strcpy(modelWithoutExtension, model);
								xr_strlwr(modelWithoutExtension);
								if (strext(modelWithoutExtension)) *strext(modelWithoutExtension) = 0;

								if (xr_strcmp(modelWithoutExtension, "") != 0 && xr_strcmp(modelWithoutExtension, ".ogf") != 0)
									models.insert(modelWithoutExtension);
							}
						};

						// Insert visual from ltx
						if (pSettings->line_exist(section, "visual"))
						{
							safe_insert(models, pSettings->r_string(section, "visual"));
						}

						// Corpse visual
						/*if (pSettings->line_exist(section, "corpse_visual"))
						{
							safe_insert(models, pSettings->r_string(section, "corpse_visual"));
						}*/

						// Bloodsucker visual
						/*if (pSettings->line_exist(section, "Predator_Visual"))
						{
							safe_insert(models, pSettings->r_string(section, "Predator_Visual"));
						}*/

						auto obj = ai().alife().objects().object(obj_id);

						// Actual visual from alife object
						if (obj && obj->visual())
						{
							safe_insert(models, obj->visual()->get_visual());
						}

						if (!models.empty())
						{
							prefetch_event E;
							E.p = P;
							E.models = std::move(models);
							E.id = obj_id;
							E.hasAlifeObject = obj != nullptr;
							events_to_prefetch.push_back(std::move(E));
							++it;
							continue;
						}
					}					
				}
			}
#endif
//-AVO
			it++; // Move to next event
			switch (ID)
			{
			case M_SPAWN:
				{
					PROF_EVENT("ProcessGameEvents M_SPAWN");

#ifdef SPAWN_ANTIFREEZE
					if (spawn_antifreeze_debug)
					{
						u16 parent_id;
						shared_str section;
						u16 obj_id = GetSpawnInfo(P, parent_id, section);
						Msg("[ProcessGameEvents] M_SPAWN: section %s, obj_id %d, parent_id %d, event_id %d", section.c_str(), obj_id, parent_id, dest);
					}
#endif

					u16 dummy16;
					P.r_begin(dummy16);
					pApp->LoadSessionRecordClientEvent(true, 0, 0, P.B.data, P.B.count);
					cl_Process_Spawn(P);
					break;
				}
			case M_EVENT:
				{
					PROF_EVENT("ProcessGameEvents M_EVENT");
					pApp->LoadSessionRecordClientEvent(false, dest, type, P.B.data, P.B.count);
					cl_Process_Event(dest, type, P);
					break;
				}
			case M_MOVE_PLAYERS:
				{
					PROF_EVENT("ProcessGameEvents M_MOVE_PLAYERS");
					u8 Count = P.r_u8();
					for (u8 i = 0; i < Count; i++)
					{
						u16 ID = P.r_u16();
						Fvector NewPos, NewDir;
						P.r_vec3(NewPos);
						P.r_vec3(NewDir);
						CActor* OActor = smart_cast<CActor*>(Objects.net_Find(ID));
						if (0 == OActor)
							break;
						OActor->MoveActor(NewPos, NewDir);
					}
					// This response is rare; allocate its large packet only for the
					// message type that actually needs it.
					auto response_data = make_intrusive<ProcessNetPacket>();
					auto& PRespond = response_data->P;
					PRespond.w_begin(M_MOVE_PLAYERS_RESPOND);
					Send(PRespond, net_flags(TRUE, TRUE));
					break;
				}
			case M_STATISTIC_UPDATE:
				{
					PROF_EVENT("ProcessGameEvents M_STATISTIC_UPDATE");
					if (GameID() != eGameIDSingle)
						Game().m_WeaponUsageStatistic->OnUpdateRequest(&P);
					break;
				}
			case M_FILE_TRANSFER:
				{
					PROF_EVENT("ProcessGameEvents M_FILE_TRANSFER");
					if (m_file_transfer) // in case of net_Stop
						m_file_transfer->on_message(&P);
					break;
				}
			case M_GAMEMESSAGE:
				{
					PROF_EVENT("ProcessGameEvents M_GAMEMESSAGE");
					Game().OnGameMessage(P);
					break;
				}
			default:
				{
					VERIFY(0);
					break;
				}
			}
		}
	}

#ifdef SPAWN_ANTIFREEZE
	if (!events_to_prefetch.empty())
	{
		xrSRWLockGuard g(prefetch_lock);
		prefetch_events->insert(prefetch_events->end(),
			std::make_move_iterator(events_to_prefetch.begin()),
			std::make_move_iterator(events_to_prefetch.end()));
		SetEvent(prefetch_thread_signal);
	}
#endif

	if (OnServer() && GameID() != eGameIDSingle)
		Game().m_WeaponUsageStatistic->Send_Check_Respond();
}

#ifdef DEBUG_MEMORY_MANAGER
extern Flags32 psAI_Flags;
extern float debug_on_frame_gather_stats_frequency;

struct debug_memory_guard
{
    inline debug_memory_guard()
    {
        mem_alloc_gather_stats(!!psAI_Flags.test(aiDebugOnFrameAllocs));
        mem_alloc_gather_stats_frequency(debug_on_frame_gather_stats_frequency);
    }
};
#endif

void CLevel::MakeReconnect()
{
	if (!Engine.Event.Peek("KERNEL:disconnect"))
	{
		pApp->LoadSessionExpectReconnect();
		Engine.Event.Defer("KERNEL:disconnect");
		char const* server_options = nullptr;
		char const* client_options = nullptr;
		if (m_caServerOptions.c_str())
		{
			server_options = xr_strdup(*m_caServerOptions);
		}
		else
		{
			server_options = xr_strdup("");
		}
		if (m_caClientOptions.c_str())
		{
			client_options = xr_strdup(*m_caClientOptions);
		}
		else
		{
			client_options = xr_strdup("");
		}
		Engine.Event.Defer("KERNEL:start", size_t(server_options), size_t(client_options));
	}
}

BOOL mt_ph_commander = FALSE;
BOOL mt_TaskManager = FALSE;
void CLevel::OnFrame()
{
	PROF_EVENT("CLevel::OnFrame()");
	const bool measure_detail = mt_FrameProfile && mt_FrameProfileDetailed && !Device.dwPrecacheFrame;
	const u64 level_frame_started_at = measure_detail ? CPU::QPC() : 0;
	u64 phase_started_at = level_frame_started_at;
	u64 prefix_ticks = 0;
	u64 network_ticks = 0;
	u64 tasks_ticks = 0;
	u64 inherited_ticks = 0;
	u64 post_ticks = 0;
	u64 scripts_ticks = 0;
	u64 sounds_ticks = 0;
	u64 gc_ticks = 0;
	u64 attachments_ticks = 0;

    // demonized: update wallmarks before rendering
    ::Render->update_Wallmarks();

#ifdef DEBUG_MEMORY_MANAGER
    debug_memory_guard __guard__;
#endif
#ifdef DEBUG
    DBG_RenderUpdate();
#endif
	Fvector temp_vector;
	m_feel_deny.feel_touch_update(temp_vector, 0.f);
	if (GameID() != eGameIDSingle)
		psDeviceFlags.set(rsDisableObjectsAsCrows, true);
	else
		psDeviceFlags.set(rsDisableObjectsAsCrows, false);
	// commit events from bullet manager from prev-frame
	Device.Statistic->TEST0.Begin();
	BulletManager().CommitEvents();
	Device.Statistic->TEST0.End();
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		prefix_ticks = now - phase_started_at;
		phase_started_at = now;
	}
	// Client receive
	if (net_isDisconnected())
	{
		if (OnClient() && GameID() != eGameIDSingle)
		{
#ifdef DEBUG
            Msg("--- I'm disconnected, so clear all objects...");
#endif
			ClearAllObjects();
		}
		Engine.Event.Defer("kernel:disconnect");
		return;
	}
	else
	{
		const bool measure_client_spawn = pApp->LoadSessionActive() && pApp->LoadSessionPrecacheStarted();
		if (measure_client_spawn)
			pApp->LoadSessionPhaseBegin(LoadSessionClientSpawn);
		Device.Statistic->netClient1.Begin();
		ClientReceive();
		Device.Statistic->netClient1.End();

		ProcessGameEvents();
#ifdef SPAWN_ANTIFREEZE
		{
			bool queueEmpty = false;
			{
				xrSRWLockGuard g(prefetch_lock);
				queueEmpty = spawn_events->queue.empty();
			}
			if (!queueEmpty)
			{
				SortSpawnEventsQueue();
				ProcessSpawnEvents();
			}
		}
#endif
		if (measure_client_spawn)
			pApp->LoadSessionPhaseEnd(LoadSessionClientSpawn);
	}

	const auto load_queues_drained = [this]()
	{
		if (!net_msg_Empty() || !Objects.destroy_queues_empty())
			return false;
#ifdef SPAWN_ANTIFREEZE
		xrSRWLockGuard g(prefetch_lock, true);
		return game_events->queue.empty() && game_spawn_queue.empty() && spawn_events->queue.empty() &&
			prefetch_events->empty() && spawn_events_data->empty() && !spawn_prefetch_busy;
#else
		return game_events->queue.empty() && game_spawn_queue.empty();
#endif
	};

	const bool control_ready = g_dedicated_server ||
		(CurrentControlEntity() != nullptr && (GameID() != eGameIDSingle || g_actor != nullptr));
	bool queues_drained = load_queues_drained();
	if (!g_dedicated_server && pApp->LoadSessionActive() && pApp->LoadSessionPrecacheStarted() &&
		!Device.dwPrecacheFrame && g_loading_events.empty() && bReady && control_ready && queues_drained)
	{
		pApp->LoadSessionPhaseBegin(LoadSessionResourceWait);
		Device.m_pRender->ResourcesDeferredUpload();
		pApp->LoadSessionPhaseEnd(LoadSessionResourceWait);
		queues_drained = load_queues_drained();
	}
	if (pApp->LoadSessionActive() && !Device.dwPrecacheFrame && g_loading_events.empty() &&
		bReady && control_ready && queues_drained)
		DumpClientSpawnProfile();
	pApp->LoadSessionTryFinish(bReady, control_ready, queues_drained);
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		network_ticks = now - phase_started_at;
		phase_started_at = now;
	}

	if (m_bNeed_CrPr)
		make_NetCorrectionPrediction();
	if (!g_dedicated_server)
	{
		if (g_mt_config.test(mtMap))
			Device.add_to_seq_parallel(
				xr_make_delegate(m_map_manager, &CMapManager::Update), "map.update");
		else
			MapManager().Update();

		if (!mt_TaskManager && Device.dwPrecacheFrame == 0)
			GameTaskManager().UpdateTasks();
	}
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		tasks_ticks = now - phase_started_at;
		phase_started_at = now;
	}

	// Inherited update
	inherited::OnFrame();
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		inherited_ticks = now - phase_started_at;
		phase_started_at = now;
	}
	// Draw client/server stats
	if (!g_dedicated_server && psDeviceFlags.test(rsStatistic))
	{
		CGameFont* F = UI().Font().pFontDI;
		if (!psNET_direct_connect)
		{
			if (IsServer())
			{
				const IServerStatistic* S = Server->GetStatistic();
				F->SetHeightI(0.015f);
				F->OutSetI(0.0f, 0.5f);
				F->SetColor(D3DCOLOR_XRGB(0, 255, 0));
				F->OutNext("IN:  %4d/%4d (%2.1f%%)", S->bytes_in_real, S->bytes_in,
				           100.f * float(S->bytes_in_real) / float(S->bytes_in));
				F->OutNext("OUT: %4d/%4d (%2.1f%%)", S->bytes_out_real, S->bytes_out,
				           100.f * float(S->bytes_out_real) / float(S->bytes_out));
				F->OutNext("client_2_sever ping: %d", net_Statistic.getPing());
				F->OutNext("SPS/Sended : %4d/%4d", S->dwBytesPerSec, S->dwBytesSended);
				F->OutNext("sv_urate/cl_urate : %4d/%4d", psNET_ServerUpdate, psNET_ClientUpdate);
				F->SetColor(D3DCOLOR_XRGB(255, 255, 255));
				struct net_stats_functor
				{
					xrServer* m_server;
					CGameFont* F;

					void operator()(IClient* C)
					{
						m_server->UpdateClientStatistic(C);
						F->OutNext("0x%08x: P(%d), BPS(%2.1fK), MRR(%2d), MSR(%2d), Retried(%2d), Blocked(%2d)",
						           //Server->game->get_option_s(*C->Name,"name",*C->Name),
						           C->ID.value(),
						           C->stats.getPing(),
						           float(C->stats.getBPS()), // /1024,
						           C->stats.getMPS_Receive(),
						           C->stats.getMPS_Send(),
						           C->stats.getRetriedCount(),
						           C->stats.dwTimesBlocked);
					}
				};
				net_stats_functor tmp_functor;
				tmp_functor.m_server = Server;
				tmp_functor.F = F;
				Server->ForEachClientDo(tmp_functor);
			}
			if (IsClient())
			{
				IPureClient::UpdateStatistic();
				F->SetHeightI(0.015f);
				F->OutSetI(0.0f, 0.5f);
				F->SetColor(D3DCOLOR_XRGB(0, 255, 0));
				F->OutNext("client_2_sever ping: %d", net_Statistic.getPing());
				F->OutNext("sv_urate/cl_urate : %4d/%4d", psNET_ServerUpdate, psNET_ClientUpdate);
				F->SetColor(D3DCOLOR_XRGB(255, 255, 255));
				F->OutNext("BReceivedPs(%2d), BSendedPs(%2d), Retried(%2d), Blocked(%2d)",
				           net_Statistic.getReceivedPerSec(),
				           net_Statistic.getSendedPerSec(),
				           net_Statistic.getRetriedCount(),
				           net_Statistic.dwTimesBlocked);
#ifdef DEBUG
                if (!pStatGraphR)
                {
                    pStatGraphR = xr_new<CStatGraph>();
                    pStatGraphR->SetRect(50, 700, 300, 68, 0xff000000, 0xff000000);
                    //m_stat_graph->SetGrid(0, 0.0f, 10, 1.0f, 0xff808080, 0xffffffff);
                    pStatGraphR->SetMinMax(0.0f, 65536.0f, 1000);
                    pStatGraphR->SetStyle(CStatGraph::stBarLine);
                    pStatGraphR->AppendSubGraph(CStatGraph::stBarLine);
                }
                pStatGraphR->AppendItem(float(net_Statistic.getBPS()), 0xff00ff00, 0);
                F->OutSet(20.f, 700.f);
                F->OutNext("64 KBS");
#endif
			}
		}
	}
	else
	{
#ifdef DEBUG
        if (pStatGraphR)
            xr_delete(pStatGraphR);
#endif
	}
#ifdef DEBUG
    g_pGamePersistent->Environment().m_paused = m_bEnvPaused;
#endif
	g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(),
	                                             game->GetEnvironmentGameTimeFactor());
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		post_ticks = now - phase_started_at;
		phase_started_at = now;
	}
	if (!mt_ph_commander)
	{
		PROF_EVENT("m_ph_commander");
		const bool measure_precache = pApp && pApp->LoadSessionMeasurePrecache();
		const u64 script_started_at = measure_precache ? CPU::QPC() : 0;
		ai().script_engine().script_process(ScriptEngine::eScriptProcessorLevel)->update();

		m_ph_commander->update();
		m_ph_commander_scripts->update();
		if (measure_precache)
			pApp->LoadSessionRecordPrecacheUpdate(0, 0, CPU::QPC() - script_started_at);
	}
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		scripts_ticks = now - phase_started_at;
		phase_started_at = now;
	}

	// update static sounds
	if (!g_dedicated_server)
	{
		if (g_mt_config.test(mtLevelSounds))
		{
			Device.add_to_seq_parallel(xr_make_delegate(
				m_level_sound_manager, &CLevelSoundManager::Update), "level.sound_manager");
		}
		else
			m_level_sound_manager->Update();
	}
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		sounds_ticks = now - phase_started_at;
		phase_started_at = now;
	}

	// defer LUA-GC-STEP
	if (!g_dedicated_server)
		script_gc();
	if (measure_detail)
	{
		const u64 now = CPU::QPC();
		gc_ticks = now - phase_started_at;
		phase_started_at = now;
	}
	if (pStatGraphR)
	{
		static float fRPC_Mult = 10.0f;
		static float fRPS_Mult = 1.0f;
		pStatGraphR->AppendItem(float(m_dwRPC) * fRPC_Mult, 0xffff0000, 1);
		pStatGraphR->AppendItem(float(m_dwRPS) * fRPS_Mult, 0xff00ff00, 0);
	}

	for (auto& pair : m_script_attachments)
		pair.second->Update();

	if (measure_detail)
	{
		attachments_ticks = CPU::QPC() - phase_started_at;
		const u64 total_ticks = CPU::QPC() - level_frame_started_at;
		struct SLevelFrameDetailProfile
		{
			u32 frames = 0;
			u64 total = 0;
			u64 prefix = 0;
			u64 network = 0;
			u64 tasks = 0;
			u64 inherited = 0;
			u64 post = 0;
			u64 scripts = 0;
			u64 sounds = 0;
			u64 gc = 0;
			u64 attachments = 0;
			u64 max_total = 0;
			u64 max_tasks = 0;
			u64 max_inherited = 0;
			u64 max_scripts = 0;
			u64 max_gc = 0;
		};
		static SLevelFrameDetailProfile profile;
		++profile.frames;
		profile.total += total_ticks;
		profile.prefix += prefix_ticks;
		profile.network += network_ticks;
		profile.tasks += tasks_ticks;
		profile.inherited += inherited_ticks;
		profile.post += post_ticks;
		profile.scripts += scripts_ticks;
		profile.sounds += sounds_ticks;
		profile.gc += gc_ticks;
		profile.attachments += attachments_ticks;
		profile.max_total = std::max(profile.max_total, total_ticks);
		profile.max_tasks = std::max(profile.max_tasks, tasks_ticks);
		profile.max_inherited = std::max(profile.max_inherited, inherited_ticks);
		profile.max_scripts = std::max(profile.max_scripts, scripts_ticks);
		profile.max_gc = std::max(profile.max_gc, gc_ticks);
		if (profile.frames >= 300)
		{
			const double average_ms = 1000.0 / (double(CPU::qpc_freq) * double(profile.frames));
			const double ticks_to_ms = 1000.0 / double(CPU::qpc_freq);
			Msg("* [mt-frame/level] avg(total/prefix/net/tasks/inherited/post)=%.2f/%.2f/%.2f/%.2f/%.2f/%.2f ms",
				profile.total * average_ms, profile.prefix * average_ms,
				profile.network * average_ms, profile.tasks * average_ms,
				profile.inherited * average_ms, profile.post * average_ms);
			Msg("* [mt-frame/level] avg(scripts/sounds/gc/attachments)=%.2f/%.2f/%.2f/%.2f ms max(total/tasks/inherited/scripts/gc)=%.2f/%.2f/%.2f/%.2f/%.2f ms",
				profile.scripts * average_ms, profile.sounds * average_ms,
				profile.gc * average_ms, profile.attachments * average_ms,
				profile.max_total * ticks_to_ms, profile.max_tasks * ticks_to_ms,
				profile.max_inherited * ticks_to_ms, profile.max_scripts * ticks_to_ms,
				profile.max_gc * ticks_to_ms);
			profile = {};
		}
	}
}

int psLUA_GCSTEP = 300;
// Restore the measured V81 feed rate. Atomic itself remains stock and
// indivisible; feeding propagation promptly keeps the write-barrier/grayagain
// interval short without holding an unfinished collector state across motion.
int psLua_ParallelGCStep = 76;
extern BOOL psLua_ParallelGC;
extern BOOL psLua_ParallelGC_debug;
extern int psLua_ParallelGC_CallAmount;
extern int psLua_ParallelGC_BudgetUs;
extern BOOL psLua_ParallelGC_Adaptive;
extern int psLua_ParallelGC_FrameBudgetUs;
extern int psLua_ParallelGC_PostLoadDelayMs;
int psLua_ParallelGCPause = 200;
int psLua_ParallelGCStepMul = 200;

void CLevel::script_gc()
{
	if (mt_ph_commander)
	{
		PROF_EVENT("m_ph_commander");
		ai().script_engine().script_process(ScriptEngine::eScriptProcessorLevel)->update();

		m_ph_commander->update();
		m_ph_commander_scripts->update();
	}

	// The parallel mode is executed by GameThread only after seqFrameMT has
	// completed. This owner-thread fallback remains for the regular mode.
	if (!(psLua_ParallelGC && Device.LuaGC))
	{
		PROF_EVENT("CLevel::script_gc");
		const u64 profile_started_at = XRay::Engine::BeginLuaGCTaskProfile();
		lua_State* const lua = ai().script_engine().lua();
		lua_gc(lua, LUA_GCSTEP, psLUA_GCSTEP);
		XRayReportLuaGCAtomicProfile(lua);
		XRay::Engine::EndLuaGCTaskProfile(profile_started_at);
	}
	
}

// demonized: bind LuaGC call to be available in device.cpp
bool CLevel::Load(u32 dwNum)
{
    inherited::Load(dwNum);
	lua_State* const lua = ai().script_engine().lua();
	lua_xray_gc_atomic_profile_configure(lua, &XRayLuaGCAtomicProfileClock);
	const int old_pause = lua_gc(lua, LUA_GCSETPAUSE, psLua_ParallelGCPause);
	const int old_step_mul = lua_gc(lua, LUA_GCSETSTEPMUL, psLua_ParallelGCStepMul);
	Msg("* [Lua GC] incremental profile pause=%d (was %d), stepmul=%d (was %d)",
		psLua_ParallelGCPause, old_pause, psLua_ParallelGCStepMul, old_step_mul);
	Msg("* [Lua GC/v86] V84 cadence retained; borrowed luabind userdata uses incremental leaf cleanup: step=%d calls=%d budget=%d us adaptive=%d frame-budget=%d us postload=%d ms",
		psLua_ParallelGCStep, psLua_ParallelGC_CallAmount, psLua_ParallelGC_BudgetUs,
		psLua_ParallelGC_Adaptive, psLua_ParallelGC_FrameBudgetUs,
		psLua_ParallelGC_PostLoadDelayMs);
    Msg("Device.LuaGC bind");
    Device.LuaGC.bind(&CLevel::LuaGC);
    Device.LuaGCFull.bind(&CLevel::LuaGCFull);
    Device.LuaGCDebug.bind(&CLevel::LuaGCDebug);
    return true;
}

// demonized: called from Device, via Device.LuaGC pointer
int CLevel::LuaGC()
{
	lua_State* const lua = ai().script_engine().lua();
	const int result = lua_gc(lua, LUA_GCSTEP, psLua_ParallelGCStep);
	XRayReportLuaGCAtomicProfile(lua);
	return result;
}
void CLevel::LuaGCFull()
{
	lua_State* const lua = ai().script_engine().lua();
	lua_gc(lua, LUA_GCCOLLECT, 0);
	XRayReportLuaGCAtomicProfile(lua);
}
void CLevel::LuaGCDebug()
{
    static int mem_kb = 0;
    mem_kb = lua_gc(ai().script_engine().lua(), LUA_GCCOUNT, 0);
    Msg("[Lua] CLevel::LuaGCDebug mem_kb %llu, times performed %d", mem_kb, Device.LuaGCCount);
}

#ifdef DEBUG_PRECISE_PATH
void test_precise_path();
#endif

#ifdef DEBUG
extern Flags32 dbg_net_Draw_Flags;
#endif

extern void draw_wnds_rects();
extern bool use_reshade;
extern void render_reshade_effects();

extern int ps_r4_hdr10_pda; // NOTE: this is a hack to avoid double HDR tonemapping the PDA

void CLevel::OnRender()
{
	// PDA
	if (game && CurrentGameUI() && &CurrentGameUI()->GetPdaMenu() != nullptr)
	{
		CUIPdaWnd* pda = &CurrentGameUI()->GetPdaMenu();
		if (psActorFlags.test(AF_3D_PDA) && pda->IsShown())
		{
			ps_r4_hdr10_pda = 1; // !!! HACK !!!

			pda->Draw();
			CUICursor* cursor = &UI().GetUICursor();

			if (cursor)
			{
				static bool need_reset;
				bool is_top = CurrentGameUI()->TopInputReceiver() == pda;

				if (pda->IsEnabled() && is_top && !Console->bVisible)
				{
					if (need_reset)
					{
						need_reset = false;
						pda->ResetCursor();
					}

					Frect &pda_border = pda->m_cursor_box;
					Fvector2 cursor_pos = cursor->GetCursorPosition();

					if (!pda_border.in(cursor_pos))
					{
						clamp(cursor_pos.x, pda_border.left, pda_border.right);
						clamp(cursor_pos.y, pda_border.top, pda_border.bottom);
						cursor->SetUICursorPosition(cursor_pos);
					}

					Fvector2 cursor_pos_dif;
					cursor_pos_dif.set(cursor_pos);
					cursor_pos_dif.sub(pda->last_cursor_pos);
					pda->last_cursor_pos.set(cursor_pos);
					pda->MouseMovement(cursor_pos_dif.x, cursor_pos_dif.y);
				}
				else
					need_reset = true;

				if (is_top)
					cursor->OnRender();
			}
			Render->RenderToTarget(Render->rtPDA);

			ps_r4_hdr10_pda = 0;
		}

		if (Actor() && Actor()->m_bDelayDrawPickupItems)
		{
			Actor()->m_bDelayDrawPickupItems = false;
			Actor()->DrawPickupItems();
		}
	}

	inherited::OnRender();
	if (!game)
		return;
	Game().OnRender();
	BulletManager().Render();

	// Update the visible lens only after CameraManager has applied a real PiP
	// camera. On the activation frame UpdateSecondVP runs after cam_Update, so a
	// cadence hit can otherwise publish one main-FOV frame into the scope.
	if (Device.m_SecondViewport.IsSVPFrame() && Device.m_SecondViewport.isCamReady)
	{
		Render->RenderToTarget(Render->rtSVP);
		Device.m_SecondViewport.MarkSVPTextureReady();
	}
	// The hidden lens image is complete. Screen UI/reshade are discarded here;
	// MT UI also belongs to the main simulation tick, not this render-only pass.
	if (Device.m_SecondViewport.IsSVPFrame())
		return;

	if (use_reshade)
		render_reshade_effects();

	HUD().RenderUI();

	ScriptDebugRender();

#ifdef DEBUG
    draw_wnds_rects();
    physics_world()->OnRender();
#endif
#ifdef DEBUG
    if (ai().get_level_graph())
        ai().level_graph().render();
#ifdef DEBUG_PRECISE_PATH
    test_precise_path();
#endif
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(Level().CurrentEntity());
    if (stalker)
        stalker->OnRender();
    if (bDebug)
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* _O = Level().Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(_O);
            if (stalker)
                stalker->OnRender();
            CCustomMonster* monster = smart_cast<CCustomMonster*>(_O);
            if (monster)
                monster->OnRender();
            CPhysicObject* physic_object = smart_cast<CPhysicObject*>(_O);
            if (physic_object)
                physic_object->OnRender();
            CSpaceRestrictor* space_restrictor = smart_cast<CSpaceRestrictor*>(_O);
            if (space_restrictor)
                space_restrictor->OnRender();
            CClimableObject* climable = smart_cast<CClimableObject*>(_O);
            if (climable)
                climable->OnRender();
            CTeamBaseZone* team_base_zone = smart_cast<CTeamBaseZone*>(_O);
            if (team_base_zone)
                team_base_zone->OnRender();
            if (GameID() != eGameIDSingle)
            {
                CInventoryItem* pIItem = smart_cast<CInventoryItem*>(_O);
                if (pIItem)
                    pIItem->OnRender();
            }
            if (dbg_net_Draw_Flags.test(dbg_draw_skeleton)) //draw skeleton
            {
                CGameObject* pGO = smart_cast<CGameObject*>	(_O);
                if (pGO && pGO != Level().CurrentViewEntity() && !pGO->H_Parent())
                {
                    if (pGO->Position().distance_to_sqr(Device.vCameraPosition) < 400.0f)
                    {
                        pGO->dbg_DrawSkeleton();
                    }
                }
            }
        }
        //  [7/5/2005]
        if (Server && Server->game) Server->game->OnRender();
        //  [7/5/2005]
        ObjectSpace.dbgRender();
        UI().Font().pFontStat->OutSet(170, 630);
        UI().Font().pFontStat->SetHeight(16.0f);
        UI().Font().pFontStat->SetColor(0xffff0000);
        if (Server)
            UI().Font().pFontStat->OutNext("Client Objects:      [%d]", Server->GetEntitiesNum());
        UI().Font().pFontStat->OutNext("Server Objects:      [%d]", Objects.o_count());
        UI().Font().pFontStat->OutNext("Interpolation Steps: [%d]", Level().GetInterpolationSteps());
        if (Server)
        {
            UI().Font().pFontStat->OutNext("Server updates size: [%d]", Server->GetLastUpdatesSize());
        }
        UI().Font().pFontStat->SetHeight(8.0f);
    }
#endif
	debug_renderer().render();
#ifdef DEBUG
    if (bDebug)
    {
        DBG().draw_object_info();
        DBG().draw_text();
        DBG().draw_level_info();
    }
    DBG().draw_debug_text();
    if (psAI_Flags.is(aiVision))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;
            stalker->dbg_draw_vision();
        }
    }

    if (psAI_Flags.test(aiDrawVisibilityRays))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;
            stalker->dbg_draw_visibility_rays();
        }
    }
#endif
}

void CLevel::ScriptDebugRender()
{
	if (!m_debug_render_queue.size())
		return;

	bool hasVisibleObj = false;
	auto it = m_debug_render_queue.begin();
	auto it_e = m_debug_render_queue.end();
	for (; it != it_e; ++it)
	{
		DBG_ScriptObject* obj = (*it).second;
		if (obj->m_visible) {
			hasVisibleObj = true;
			obj->Render();
		}
	}

	// demonized: fix of showing console window when there are no visible gizmos 
	if (hasVisibleObj)
		DRender->OnFrameEnd();
}

script_attachment* CLevel::add_attachment(LPCSTR name, script_attachment* att)
{
	R_ASSERT(att);
	remove_child(name, true);
	m_script_attachments.emplace(mk_pair(name, att));
	return att;
}

script_attachment* CLevel::get_attachment(LPCSTR name)
{
	if (m_script_attachments.size())
	{
		auto& att = m_script_attachments.find(name);
		if (att != m_script_attachments.end())
			return att->second;
	}

	return nullptr;
}

void CLevel::remove_child(LPCSTR name, bool destroy)
{
	script_attachment* attachment = get_attachment(name);
	if (!attachment)
		return;

	if (destroy)
		xr_delete(attachment);

	m_script_attachments.erase(name);
}

void CLevel::remove_attachment(script_attachment* child)
{
	if (!child) return;
	if (m_script_attachments.size())
	{
		script_attachment* attachment = get_attachment(child->GetName());
		if (!attachment || attachment != child)
			return;

		remove_child(child->GetName(), true);
	}
}

void CLevel::iterate_attachments(::luabind::functor<bool> functor)
{
	if (!m_script_attachments.size())
		return;

	for (auto& pair : m_script_attachments)
		if (functor(pair.first.c_str(), pair.second) == true)
			return;
}

void CLevel::OnEvent(EVENT E, u64 P1, u64 /**P2/**/)
{
	if (E == eEntitySpawn)
	{
		char Name[128];
		Name[0] = 0;
		sscanf(LPCSTR(P1), "%s", Name);
		Level().g_cl_Spawn(Name, 0xff, M_SPAWN_OBJECT_LOCAL, Fvector().set(0, 0, 0));
	}
	else if (E == eChangeRP && P1)
	{
	}
	else if (E == eDemoPlay && P1)
	{
		char* name = (char*)P1;
		string_path RealName;
		xr_strcpy(RealName, name);
		xr_strcat(RealName, ".xrdemo");
		Cameras().AddCamEffector(xr_new<CDemoPlay>(RealName, 1.3f, 0));
	}
	else if (E == eChangeTrack && P1)
	{
		// int id = atoi((char*)P1);
		// Environment->Music_Play(id);
	}
	else if (E == eEnvironment)
	{
		// int id=0; float s=1;
		// sscanf((char*)P1,"%d,%f",&id,&s);
		// Environment->set_EnvMode(id,s);
	}
}

void CLevel::AddObject_To_Objects4CrPr(CGameObject* pObj)
{
	if (!pObj)
		return;
	for (CGameObject* obj : pObjects4CrPr)
	{
		if (obj == pObj)
			return;
	}
	pObjects4CrPr.push_back(pObj);
}

void CLevel::AddActor_To_Actors4CrPr(CGameObject* pActor)
{
	if (!pActor)
		return;
	if (!smart_cast<CActor*>(pActor)) return;
	for (CGameObject* act : pActors4CrPr)
	{
		if (act == pActor)
			return;
	}
	pActors4CrPr.push_back(pActor);
}

void CLevel::RemoveObject_From_4CrPr(CGameObject* pObj)
{
	if (!pObj)
		return;
	auto objIt = std::find(pObjects4CrPr.begin(), pObjects4CrPr.end(), pObj);
	if (objIt != pObjects4CrPr.end())
	{
		pObjects4CrPr.erase(objIt);
	}
	auto aIt = std::find(pActors4CrPr.begin(), pActors4CrPr.end(), pObj);
	if (aIt != pActors4CrPr.end())
	{
		pActors4CrPr.erase(aIt);
	}
}

void CLevel::make_NetCorrectionPrediction()
{
	m_bNeed_CrPr = false;
	m_bIn_CrPr = true;
	u64 NumPhSteps = physics_world()->StepsNum();
	physics_world()->StepsNum() -= m_dwNumSteps;
	if (ph_console::g_bDebugDumpPhysicsStep && m_dwNumSteps > 10)
	{
		Msg("!!!TOO MANY PHYSICS STEPS FOR CORRECTION PREDICTION = %d !!!", m_dwNumSteps);
		m_dwNumSteps = 10;
	}
	physics_world()->Freeze();
	//setting UpdateData and determining number of PH steps from last received update
	for (CGameObject* obj : pObjects4CrPr)
	{
		if (!obj)
			continue;
		obj->PH_B_CrPr();
	}
	//first prediction from "delivered" to "real current" position
	//making enought PH steps to calculate current objects position based on their updated state
	for (u32 i = 0; i < m_dwNumSteps; i++)
	{
		physics_world()->Step();

		for (CGameObject* act : pActors4CrPr)
		{
			if (!act || act->CrPr_IsActivated())
				continue;
			act->PH_B_CrPr();
		}
	}
	for (CGameObject* obj : pObjects4CrPr)
	{
		if (!obj)
			continue;
		obj->PH_I_CrPr();
	}
	if (!InterpolationDisabled())
	{
		for (u32 i = 0; i < lvInterpSteps; i++) //second prediction "real current" to "future" position
		{
			physics_world()->Step();
		}
		for (CGameObject* obj : pObjects4CrPr)
		{
			if (!obj)
				continue;
			obj->PH_A_CrPr();
		}
	}
	physics_world()->UnFreeze();
	physics_world()->StepsNum() = NumPhSteps;
	m_dwNumSteps = 0;
	m_bIn_CrPr = false;
	pObjects4CrPr.clear();
	pActors4CrPr.clear();
}

u32 CLevel::GetInterpolationSteps()
{
	return lvInterpSteps;
}

void CLevel::UpdateDeltaUpd(u32 LastTime)
{
	u32 CurrentDelta = LastTime - m_dwLastNetUpdateTime;
	if (CurrentDelta < m_dwDeltaUpdate)
		CurrentDelta = iFloor(float(m_dwDeltaUpdate * 10 + CurrentDelta) / 11);
	m_dwLastNetUpdateTime = LastTime;
	m_dwDeltaUpdate = CurrentDelta;
	if (0 == g_cl_lvInterp)
		ReculcInterpolationSteps();
	else if (g_cl_lvInterp > 0)
	{
		lvInterpSteps = iCeil(g_cl_lvInterp / fixed_step);
	}
}

void CLevel::ReculcInterpolationSteps()
{
	lvInterpSteps = iFloor(float(m_dwDeltaUpdate) / (fixed_step * 1000));
	if (lvInterpSteps > 60)
		lvInterpSteps = 60;
	if (lvInterpSteps < 3)
		lvInterpSteps = 3;
}

bool CLevel::InterpolationDisabled()
{
	return g_cl_lvInterp < 0;
}

void CLevel::SetNumCrSteps(u32 NumSteps)
{
	m_bNeed_CrPr = true;
	if (m_dwNumSteps > NumSteps)
		return;
	m_dwNumSteps = NumSteps;
	if (m_dwNumSteps > 1000000)
	{
		VERIFY(0);
	}
}

ALife::_TIME_ID CLevel::GetStartGameTime()
{
	return (game->GetStartGameTime());
}

ALife::_TIME_ID CLevel::GetGameTime()
{
	return (game->GetGameTime());
}

ALife::_TIME_ID CLevel::GetEnvironmentGameTime()
{
	return (game->GetEnvironmentGameTime());
}

u8 CLevel::GetDayTime()
{
	u32 dummy32, hours;
	GetGameDateTime(dummy32, dummy32, dummy32, hours, dummy32, dummy32, dummy32);
	VERIFY(hours < 256);
	return u8(hours);
}

float CLevel::GetGameDayTimeSec()
{
	return (float(s64(GetGameTime() % (24 * 60 * 60 * 1000))) / 1000.f);
}

u32 CLevel::GetGameDayTimeMS()
{
	return (u32(s64(GetGameTime() % (24 * 60 * 60 * 1000))));
}

float CLevel::GetEnvironmentGameDayTimeSec()
{
	return (float(s64(GetEnvironmentGameTime() % (24 * 60 * 60 * 1000))) / 1000.f);
}

void CLevel::GetGameDateTime(u32& year, u32& month, u32& day, u32& hours, u32& mins, u32& secs, u32& milisecs)
{
	split_time(GetGameTime(), year, month, day, hours, mins, secs, milisecs);
}

float CLevel::GetGameTimeFactor()
{
	return (game ? game->GetGameTimeFactor() : 1.0f);
}

void CLevel::SetGameTimeFactor(const float fTimeFactor)
{
	game->SetGameTimeFactor(fTimeFactor);
}

void CLevel::SetGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor)
{
	game->SetGameTimeFactor(GameTime, fTimeFactor);
}

void CLevel::SetEnvironmentGameTimeFactor(u64 const& GameTime, float const& fTimeFactor)
{
	if (!game)
		return;
	game->SetEnvironmentGameTimeFactor(GameTime, fTimeFactor);
}

bool CLevel::IsServer()
{
	if (!Server || IsDemoPlayStarted())
		return false;
	return true;
}

bool CLevel::IsClient()
{
	if (IsDemoPlayStarted())
		return true;
	if (Server)
		return false;
	return true;
}

void CLevel::OnAlifeSimulatorUnLoaded()
{
	MapManager().ResetStorage();
	GameTaskManager().ResetStorage();
	delete_data(m_debug_render_queue);
}

void CLevel::OnAlifeSimulatorLoaded()
{
	MapManager().ResetStorage();
	GameTaskManager().ResetStorage();
	delete_data(m_debug_render_queue);
}

void CLevel::OnSessionTerminate(LPCSTR reason)
{
	MainMenu()->OnSessionTerminate(reason);
}

u32 GameID()
{
	return Game().Type();
}

CZoneList* CLevel::create_hud_zones_list()
{
	hud_zones_list = xr_new<CZoneList>();
	hud_zones_list->clear();
	return hud_zones_list;
}

bool CZoneList::feel_touch_contact(CObject* O)
{
	TypesMapIt it = m_TypesMap.find(O->cNameSect());
	bool res = (it != m_TypesMap.end());
	CCustomZone* pZone = smart_cast<CCustomZone*>(O);
	if (pZone && !pZone->IsEnabled())
	{
		res = false;
	}
	return res;
}

CZoneList::CZoneList()
{
}

CZoneList::~CZoneList()
{
	clear();
	destroy();
}
