//---------------------------------------------------------------------------
#include "stdafx.h"
#include "GameMtlLib.h"
#ifndef _EDITOR
void DestroySounds(SoundVec&);
void CreateSounds(SoundVec&, LPCSTR);
void CreatePSs(PSVec&, LPCSTR);
void CreateMarks(IWallMarkArray*, LPCSTR);
#endif
CGameMtlLibrary GMLib;

CGameMtlLibrary::CGameMtlLibrary()
{
    material_index = 0;
    material_pair_index = 0;
    material_count = 0;
    PGMLib = &GMLib;
}

SGameMtl* CGameMtlLibrary::GetMaterialByIdx(u16 idx)
{
    if (idx >= materials.size())
    {
        if (idx != 65535)
            Msg("Material [%d] not found in library! ", (int)idx);
        return materials[0];
    }

    return materials[idx];
}

void SGameMtl::Load(IReader& fs)
{
	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_MAIN));
	ID						= fs.r_u32();
    fs.r_stringZ			(m_Name);

    if (fs.find_chunk(GAMEMTL_CHUNK_DESC)){
		fs.r_stringZ		(m_Desc);
    }
    
	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FLAGS));
    Flags.assign			(fs.r_u32());

	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_PHYSICS));
    fPHFriction				= fs.r_float();
    fPHDamping				= fs.r_float();
    fPHSpring				= fs.r_float();
    fPHBounceStartVelocity 	= fs.r_float();
    fPHBouncing				= fs.r_float();

	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FACTORS));
    fShootFactor			= fs.r_float();
    fBounceDamageFactor		= fs.r_float();
    fVisTransparencyFactor	= fs.r_float();
    fSndOcclusionFactor		= fs.r_float();


	if(fs.find_chunk(GAMEMTL_CHUNK_FACTORS_MP))
	    fShootFactorMP	    = fs.r_float();
    else
	    fShootFactorMP	    = fShootFactor;

	if(fs.find_chunk(GAMEMTL_CHUNK_FLOTATION))
	    fFlotationFactor	= fs.r_float();

    if(fs.find_chunk(GAMEMTL_CHUNK_INJURIOUS))
    	fInjuriousSpeed		= fs.r_float();
    
	if(fs.find_chunk(GAMEMTL_CHUNK_DENSITY))
    	fDensityFactor		= fs.r_float();
}

void CGameMtlLibrary::Load()
{
	string_path			name;
	if (!FS.exist(name,	_game_data_,GAMEMTL_FILENAME)){
        Msg("! Can't find game material file: %s",name);
    	return;
    }

    R_ASSERT			(material_pairs.empty());
    R_ASSERT			(materials.empty());

	IReader*	F		= FS.r_open(name);
    IReader& fs			= *F;

    R_ASSERT(fs.find_chunk(GAMEMTLS_CHUNK_VERSION));
    u16 version			= fs.r_u16();
    if (GAMEMTL_CURRENT_VERSION!=version){
        Log				("CGameMtlLibrary: invalid version. Library can't load.");
		FS.r_close		(F);
    	return;
    }

    R_ASSERT(fs.find_chunk(GAMEMTLS_CHUNK_AUTOINC));
    material_index		= fs.r_u32();
    material_pair_index	= fs.r_u32();

    materials.clear		();
    material_pairs.clear();

    IReader* OBJ 		= fs.open_chunk(GAMEMTLS_CHUNK_MTLS);
    if (OBJ) {
        u32				count;
        for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count,O)) {
        	SGameMtl*	M = new SGameMtl ();
	        M->Load		(*O);
        	materials.push_back(M);
        }
        OBJ->close		();
    }

    OBJ 				= fs.open_chunk(GAMEMTLS_CHUNK_MTLS_PAIR);
    if (OBJ){
        u32				count;
        for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count,O)) {
        	SGameMtlPair* M	= new SGameMtlPair (this);
	        M->Load		(*O);
        	material_pairs.push_back(M);
        }
        OBJ->close		();
    }


    // Monolith material extensions use normal CInifile/DLTX inheritance.
    // Append new IDs without changing any index from the binary library.
    string_path material_file;
    if (FS.exist(material_file, _game_data_, "materials\\materials.ltx"))
    {
        CInifile ini(material_file, TRUE);
        int next_id = -1;
        for (auto* material : materials) next_id = std::max(next_id, material->ID);
        u32 added = 0;
        for (const auto* section : ini.sections())
        {
            auto found = GetMaterialIt(section->Name.c_str());
            SGameMtl* material;
            if (found == materials.end())
            {
                R_ASSERT2(materials.size() < GAMEMTL_NONE_IDX, "Too many game materials");
                material = new SGameMtl();
                material->ID = ++next_id;
                material->m_Name = section->Name;
                materials.push_back(material);
                ++added;
            }
            else material = *found;
            LPCSTR name = section->Name.c_str();
            if (ini.line_exist(name, "desc")) material->m_Desc = ini.r_string(name, "desc");
            if (ini.line_exist(name, "flag_breakable")) material->Flags.set(SGameMtl::flBreakable, ini.r_bool(name, "flag_breakable"));
            if (ini.line_exist(name, "flag_bounceable")) material->Flags.set(SGameMtl::flBounceable, ini.r_bool(name, "flag_bounceable"));
            if (ini.line_exist(name, "flag_skidmark")) material->Flags.set(SGameMtl::flSkidmark, ini.r_bool(name, "flag_skidmark"));
            if (ini.line_exist(name, "flag_bloodmark")) material->Flags.set(SGameMtl::flBloodmark, ini.r_bool(name, "flag_bloodmark"));
            if (ini.line_exist(name, "flag_climable")) material->Flags.set(SGameMtl::flClimable, ini.r_bool(name, "flag_climable"));
            if (ini.line_exist(name, "flag_passable")) material->Flags.set(SGameMtl::flPassable, ini.r_bool(name, "flag_passable"));
            if (ini.line_exist(name, "flag_dynamic")) material->Flags.set(SGameMtl::flDynamic, ini.r_bool(name, "flag_dynamic"));
            if (ini.line_exist(name, "flag_liquid")) material->Flags.set(SGameMtl::flLiquid, ini.r_bool(name, "flag_liquid"));
            if (ini.line_exist(name, "flag_suppress_shadows")) material->Flags.set(SGameMtl::flSuppressShadows, ini.r_bool(name, "flag_suppress_shadows"));
            if (ini.line_exist(name, "flag_suppress_wallmarks")) material->Flags.set(SGameMtl::flSuppressWallmarks, ini.r_bool(name, "flag_suppress_wallmarks"));
            if (ini.line_exist(name, "flag_actor_obstacle")) material->Flags.set(SGameMtl::flActorObstacle, ini.r_bool(name, "flag_actor_obstacle"));
            if (ini.line_exist(name, "flag_bullet_no_ricochet")) material->Flags.set(SGameMtl::flNoRicoshet, ini.r_bool(name, "flag_bullet_no_ricochet"));
            if (ini.line_exist(name, "flag_injurious")) material->Flags.set(SGameMtl::flInjurious, ini.r_bool(name, "flag_injurious"));
            if (ini.line_exist(name, "flag_shootable")) material->Flags.set(SGameMtl::flShootable, ini.r_bool(name, "flag_shootable"));
            if (ini.line_exist(name, "flag_transparent")) material->Flags.set(SGameMtl::flTransparent, ini.r_bool(name, "flag_transparent"));
            if (ini.line_exist(name, "flag_slowdown")) material->Flags.set(SGameMtl::flSlowDown, ini.r_bool(name, "flag_slowdown"));
            if (ini.line_exist(name, "friction")) material->fPHFriction = ini.r_float(name, "friction");
            if (ini.line_exist(name, "damping")) material->fPHDamping = ini.r_float(name, "damping");
            if (ini.line_exist(name, "spring")) material->fPHSpring = ini.r_float(name, "spring");
            if (ini.line_exist(name, "bounce_start_velocity")) material->fPHBounceStartVelocity = ini.r_float(name, "bounce_start_velocity");
            if (ini.line_exist(name, "bouncing")) material->fPHBouncing = ini.r_float(name, "bouncing");
            if (ini.line_exist(name, "shoot_factor")) material->fShootFactor = ini.r_float(name, "shoot_factor");
            if (ini.line_exist(name, "shoot_factor_mp")) material->fShootFactorMP = ini.r_float(name, "shoot_factor_mp");
            if (ini.line_exist(name, "bounce_damage_factor")) material->fBounceDamageFactor = ini.r_float(name, "bounce_damage_factor");
            if (ini.line_exist(name, "vis_transparency_factor")) material->fVisTransparencyFactor = ini.r_float(name, "vis_transparency_factor");
            if (ini.line_exist(name, "sound_occlusion_factor")) material->fSndOcclusionFactor = ini.r_float(name, "sound_occlusion_factor");
            if (ini.line_exist(name, "flotation_factor")) material->fFlotationFactor = ini.r_float(name, "flotation_factor");
            if (ini.line_exist(name, "injurious_factor")) material->fInjuriousSpeed = ini.r_float(name, "injurious_factor");
            if (ini.line_exist(name, "density_factor")) material->fDensityFactor = ini.r_float(name, "density_factor");
        }
        Msg("* [IX-Ray materials] added=%u total=%u", added, u32(materials.size()));
    }
#ifndef _EDITOR
    if (FS.exist(material_file, _game_data_, "materials\\material_pairs.ltx"))
    {
        CInifile ini(material_file, TRUE);
        auto pair_key = [](int a, int b) { return (u64(u32(std::min(a, b))) << 32) | u32(std::max(a, b)); };
        xr_map<u64, SGameMtlPair*> pairs;
        int next_id = -1;
        for (auto* pair : material_pairs)
        {
            pairs[pair_key(pair->mtl0, pair->mtl1)] = pair;
            next_id = std::max(next_id, pair->ID);
        }
        u32 added = 0;
        for (const auto* section : ini.sections())
        {
            LPCSTR name = section->Name.c_str();
            const xr_string names(name);
            const auto delimiter = names.find('@');
            if (delimiter == xr_string::npos || names.find('@', delimiter + 1) != xr_string::npos)
            {
                Msg("! [IX-Ray materials] invalid pair: %s", name);
                continue;
            }
            const u32 first = GetMaterialID(names.substr(0, delimiter).c_str());
            const u32 second = GetMaterialID(names.substr(delimiter + 1).c_str());
            if (first == GAMEMTL_NONE_ID || second == GAMEMTL_NONE_ID)
            {
                Msg("! [IX-Ray materials] pair references unknown material: %s", name);
                continue;
            }
            const auto key = pair_key(first, second);
            auto& pair = pairs[key];
            if (!pair)
            {
                pair = new SGameMtlPair(this);
                pair->ID = ++next_id;
                pair->SetPair(first, second);
                material_pairs.push_back(pair);
                ++added;
            }
            auto replace_sounds = [&](LPCSTR key, SoundVec& target, u32 flag)
            {
                if (!ini.line_exist(name, key)) return;
                DestroySounds(target);
                target.clear();
                LPCSTR value = ini.r_string(name, key);
                if (value && *value) CreateSounds(target, value);
                pair->OwnProps.set(flag, TRUE);
            };
            replace_sounds("breaking_sounds", pair->BreakingSounds, SGameMtlPair::flBreakingSounds);
            replace_sounds("step_sounds", pair->StepSounds, SGameMtlPair::flStepSounds);
            replace_sounds("collide_sounds", pair->CollideSounds, SGameMtlPair::flCollideSounds);
            if (ini.line_exist(name, "collide_particles"))
            {
                pair->CollideParticles.clear();
                LPCSTR value = ini.r_string(name, "collide_particles");
                if (value && *value) CreatePSs(pair->CollideParticles, value);
                pair->OwnProps.set(SGameMtlPair::flCollideParticles, TRUE);
            }
            if (ini.line_exist(name, "collide_marks"))
            {
                pair->m_pCollideMarks->clear();
                LPCSTR value = ini.r_string(name, "collide_marks");
                if (value && *value) CreateMarks(&*pair->m_pCollideMarks, value);
                pair->OwnProps.set(SGameMtlPair::flCollideMarks, TRUE);
            }
        }
        Msg("* [IX-Ray material pairs] added=%u total=%u", added, u32(material_pairs.size()));
    }
#endif

	material_count		= (u32)materials.size();
    material_pairs_rt.resize(material_count*material_count,0);
    for (GameMtlPairIt p_it=material_pairs.begin(); material_pairs.end() != p_it; ++p_it){
		SGameMtlPair* S	= *p_it;
    	int idx0		= GetMaterialIdx(S->mtl0)*material_count+GetMaterialIdx(S->mtl1);
    	int idx1		= GetMaterialIdx(S->mtl1)*material_count+GetMaterialIdx(S->mtl0);
	    material_pairs_rt[idx0]=S;
	    material_pairs_rt[idx1]=S;
    }

	FS.r_close		(F);
}

#ifdef DEBUG
LPCSTR SGameMtlPair::dbg_Name()
{
	static string256 nm;
	SGameMtl* M0 = GMLib.GetMaterialByID(GetMtl0());
	SGameMtl* M1 = GMLib.GetMaterialByID(GetMtl1());
	xr_sprintf(nm,sizeof(nm),"Pair: %s - %s",*M0->m_Name,*M1->m_Name);
	return nm;
}
#endif
