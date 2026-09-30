#include "stdafx.h"
#include "uber_deffer.h"
void fix_texture_name(LPSTR fn);

#include "dxRenderDeviceRender.h"

static bool anthology_starts_with_ci(LPCSTR value, LPCSTR prefix)
{
	if (!value || !prefix)
		return false;

	while (*prefix)
	{
		if (!*value || tolower(static_cast<unsigned char>(*value)) != tolower(static_cast<unsigned char>(*prefix)))
			return false;
		++value;
		++prefix;
	}

	return true;
}

static bool anthology_is_ground_texture(LPCSTR texture)
{
	return anthology_starts_with_ci(texture, "detail\\") || anthology_starts_with_ci(texture, "grnd\\") ||
		anthology_starts_with_ci(texture, "terrain\\");
}

static bool anthology_prepare_season_texture(LPCSTR season, LPCSTR base, string512& result)
{
	if (!season || !season[0] || !base || !base[0])
	{
		result[0] = 0;
		return false;
	}

	strconcat(sizeof(result), result, "anthology_seasons\\", season, "\\", base);
	string_path resolved;
	if (FS.exist(resolved, "$game_textures$", result, ".dds"))
		return true;

	xr_strcpy(result, base);
	return false;
}

bool anthology_prepare_flora_textures(LPCSTR base, string512& green, string512& autumn, string512& dead)
{
	const bool levelDetails = xr_strcmp(base, "build_details") == 0;
	if (levelDetails)
	{
		xr_strcpy(green, "build_details_green");
		xr_strcpy(autumn, "build_details_autumn");
		xr_strcpy(dead, "build_details_dead");
	}
	else
	{
		strconcat(sizeof(green), green, "anthology_seasons\\green\\", base);
		strconcat(sizeof(autumn), autumn, "anthology_seasons\\autumn\\", base);
		strconcat(sizeof(dead), dead, "anthology_seasons\\dead\\", base);
	}

	string_path resolved;
	LPCSTR textureRoot = levelDetails ? "$level$" : "$game_textures$";
	const bool hasGreen = FS.exist(resolved, textureRoot, green, ".dds");
	const bool hasAutumn = FS.exist(resolved, textureRoot, autumn, ".dds");
	const bool hasDead = FS.exist(resolved, textureRoot, dead, ".dds");

	if (!hasGreen)
		xr_strcpy(green, base);
	if (!hasAutumn)
		xr_strcpy(autumn, base);
	if (!hasDead)
		xr_strcpy(dead, base);

	if (strstr(Core.Params, "-season_diagnostics"))
	{
		static std::atomic<unsigned> logged{0};
		if (logged.fetch_add(1, std::memory_order_relaxed) < 160)
			Msg("* [season/flora] base=%s green=%u autumn=%u dead=%u level=%u", base,
				unsigned(hasGreen), unsigned(hasAutumn), unsigned(hasDead), unsigned(levelDetails));
	}
	return hasGreen || hasAutumn || hasDead;
}

static bool anthology_route_flora_shader(LPCSTR vs, LPCSTR& ps, LPCSTR base,
    string512& green, string512& autumn, string512& dead)
{
    const bool branch = xr_strcmp(ps, "anthology_branch") == 0 || xr_strcmp(ps, "anthology_branch_atoc") == 0;
    const bool grass = xr_strcmp(ps, "anthology_grass") == 0 || xr_strcmp(ps, "anthology_grass_atoc") == 0;
    const bool seasonal = branch || xr_strcmp(ps, "anthology_flora") == 0 || xr_strcmp(ps, "anthology_flora_atoc") == 0 ||
        xr_strcmp(ps, "anthology_grass") == 0 || xr_strcmp(ps, "anthology_grass_atoc") == 0;
    const bool tree = xr_strcmp(vs, "tree") == 0 || xr_strcmp(vs, "tree_s") == 0 || xr_strcmp(vs, "tree_branch") == 0;
    const bool basePass = xr_strcmp(ps, "base") == 0 || xr_strcmp(ps, "base_atoc") == 0;
    const bool treePass = xr_strcmp(ps, "tree_branch") == 0 || xr_strcmp(ps, "tree_branch_atoc") == 0 ||
        xr_strcmp(ps, "tree_atoc") == 0 || xr_strcmp(ps, "tree_s_atoc") == 0;
    const bool surv = anthology_starts_with_ci(base, "dex_team\\surv\\veg\\");
    const bool oakLeaf = xr_strcmp(base, "trees\\trees_oak1leafdif") == 0;
    if (!seasonal && !((tree || surv || oakLeaf) && basePass) && !(tree && treePass))
        return false;

    const bool atoc = strstr(ps, "_atoc") != nullptr;
    // Detail atlases such as Dead City's ccon\\cconv1.5 have no seasonal DDS.
    // Keep the grass adapter: its base-texture fallback still supports snow.
    if (!anthology_prepare_flora_textures(base, green, autumn, dead) && !grass)
    {
        if (seasonal)
            ps = branch ? (atoc ? "tree_branch_atoc" : "tree_branch") : (atoc ? "base_atoc" : "base");
        return false;
    }

    if (!seasonal)
    {
        // Keep SSFX's branch PS paired with its vertex shader, including motion vectors.
        if (xr_strcmp(vs, "tree_branch") == 0)
            ps = atoc ? "anthology_branch_atoc" : "anthology_branch";
        else
            ps = atoc ? "anthology_flora_atoc" : "anthology_flora";
    }
    return true;
}

void uber_deffer(CBlender_Compile& C, bool hq, LPCSTR _vspec, LPCSTR _pspec, BOOL _aref, LPCSTR _detail_replace,
                 bool DO_NOT_FINISH, bool DO_NOT_WRITE)
{
	string512 anthologyGreen = {};
	string512 anthologyAutumn = {};
	string512 anthologyDead = {};
	string512 anthologyGroundDead = {};
	string512 anthologyGroundWinter = {};
	string512 anthologyGroundBump = {};
	string512 anthologyGroundBumpX = {};
	string512 anthologyGroundDetail = {};
	string512 anthologyGroundDetailBump = {};
	string512 anthologyGroundDetailBumpX = {};
	string512 anthologyGroundWinterBump = {};
	string512 anthologyGroundWinterBumpX = {};
	string512 anthologyGroundWinterDetail = {};
	string512 anthologyGroundWinterDetailBump = {};
	string512 anthologyGroundWinterDetailBumpX = {};
	bool anthologyFloraTextures = false;
	bool anthologyGroundTextures = false;
	bool anthologyWinterObjects = false;
	string512 anthologyWinterObject = {};
	anthologyFloraTextures = anthology_route_flora_shader(
		_vspec, _pspec, C.L_textures[0].c_str(), anthologyGreen, anthologyAutumn, anthologyDead);
	if (!anthologyFloraTextures)
	{
		const bool baseATOC = xr_strcmp(_pspec, "base_atoc") == 0;
		if ((baseATOC || xr_strcmp(_pspec, "base") == 0) &&
			anthology_is_ground_texture(C.L_textures[0].c_str()))
		{
			const bool hasDeadGround = anthology_prepare_season_texture(
				"dead", C.L_textures[0].c_str(), anthologyGroundDead);
			const bool hasWinterGround = anthology_prepare_season_texture(
				"winter", C.L_textures[0].c_str(), anthologyGroundWinter);
			if (!hasWinterGround)
				xr_strcpy(anthologyGroundWinter, anthologyGroundDead);
			// Single-texture soil/ground decals need the same winter surface as
			// four-layer terrain, including textures without a seasonal DDS.
			anthologyGroundTextures = true;
			if (anthologyGroundTextures)
				_pspec = baseATOC ? "anthology_ground_atoc" : "anthology_ground";
		}
		else if (baseATOC || xr_strcmp(_pspec, "base") == 0)
		{
			anthologyWinterObjects = anthology_prepare_season_texture(
				"winter_objects", C.L_textures[0].c_str(), anthologyWinterObject);
			if (anthologyWinterObjects)
				_pspec = baseATOC ? "anthology_winter_object_atoc" : "anthology_winter_object";
		}
	}

	// Uber-parse
	string256 fname = {}, fnameA = {}, fnameB = {};
	xr_strcpy(fname, *C.L_textures[0]); //. andy if (strext(fname)) *strext(fname)=0;
	fix_texture_name(fname);
	ref_texture _t;
	_t.create(fname);
	bool bump = _t.bump_exist();

	// detect lmap
	bool lmap = true;
	if (C.L_textures.size() < 3) lmap = false;
	else
	{
		pcstr tex = C.L_textures[2].c_str();
		if (tex[0] == 'l' && tex[1] == 'm' && tex[2] == 'a' && tex[3] == 'p') lmap = true;
		else lmap = false;
	}


	string256 ps, vs, dt;
	strconcat(sizeof(vs), vs, "deffer_", _vspec, lmap ? "_lmh" : "");
	strconcat(sizeof(ps), ps, "deffer_", _pspec, lmap ? "_lmh" : "");
	xr_strcpy(dt, sizeof(dt), _detail_replace ? _detail_replace : (C.detail_texture ? C.detail_texture : ""));

	// detect detail bump
	string256 texDetailBump = {'\0'};
	string256 texDetailBumpX = {'\0'};
	bool bHasDetailBump = false;
	if (C.bDetail_Bump)
	{
		LPCSTR detail_bump_texture = DEV->m_textures_description.GetBumpName(dt).c_str();
		//	Detect and use detail bump
		if (detail_bump_texture)
		{
			bHasDetailBump = true;
			xr_strcpy(texDetailBump, sizeof(texDetailBump), detail_bump_texture);
			xr_strcpy(texDetailBumpX, sizeof(texDetailBumpX), detail_bump_texture);
			xr_strcat(texDetailBumpX, "#");
		}
	}

	if (_aref)
	{
		xr_strcat(ps, "_aref");
	}

	if (!bump)
	{
		fnameA[0] = fnameB[0] = 0;
		xr_strcat(vs, "_flat");
		xr_strcat(ps, "_flat");
		if (hq && (C.bDetail_Diffuse || C.bDetail_Bump))
		{
			xr_strcat(vs, "_d");
			xr_strcat(ps, "_d");
		}
	}
	else
	{
		xr_strcpy(fnameA, _t.bump_get().c_str());
		strconcat(sizeof(fnameB), fnameB, fnameA, "#");
		xr_strcat(vs, "_bump");
		if (hq && C.bUseSteepParallax)
		{
			xr_strcat(ps, "_steep");
		}
		else
		{
			xr_strcat(ps, "_bump");
		}
		if (hq && (C.bDetail_Diffuse || C.bDetail_Bump))
		{
			xr_strcat(vs, "_d");
			if (bHasDetailBump)
				xr_strcat(ps, "_db"); //	bump & detail & hq
			else
				xr_strcat(ps, "_d");
		}
	}

	// Resolve seasonal support maps only after both bump/flat paths initialize the names.
	if (anthologyGroundTextures)
	{
		anthology_prepare_season_texture("dead", fnameA, anthologyGroundBump);
		anthology_prepare_season_texture("dead", fnameB, anthologyGroundBumpX);
		anthology_prepare_season_texture("dead", dt, anthologyGroundDetail);
		anthology_prepare_season_texture("dead", texDetailBump, anthologyGroundDetailBump);
		anthology_prepare_season_texture("dead", texDetailBumpX, anthologyGroundDetailBumpX);
		if (!anthologyGroundBump[0]) xr_strcpy(anthologyGroundBump, anthologyGroundDead);
		if (!anthologyGroundBumpX[0]) xr_strcpy(anthologyGroundBumpX, anthologyGroundDead);
		if (!anthologyGroundDetail[0]) xr_strcpy(anthologyGroundDetail, anthologyGroundDead);
		if (!anthologyGroundDetailBump[0]) xr_strcpy(anthologyGroundDetailBump, anthologyGroundDead);
		if (!anthologyGroundDetailBumpX[0]) xr_strcpy(anthologyGroundDetailBumpX, anthologyGroundDead);
		if (!anthology_prepare_season_texture("winter", fnameA, anthologyGroundWinterBump))
			xr_strcpy(anthologyGroundWinterBump, anthologyGroundBump);
		if (!anthology_prepare_season_texture("winter", fnameB, anthologyGroundWinterBumpX))
			xr_strcpy(anthologyGroundWinterBumpX, anthologyGroundBumpX);
		if (!anthology_prepare_season_texture("winter", dt, anthologyGroundWinterDetail))
			xr_strcpy(anthologyGroundWinterDetail, anthologyGroundDetail);
		if (!anthology_prepare_season_texture("winter", texDetailBump, anthologyGroundWinterDetailBump))
			xr_strcpy(anthologyGroundWinterDetailBump, anthologyGroundDetailBump);
		if (!anthology_prepare_season_texture("winter", texDetailBumpX, anthologyGroundWinterDetailBumpX))
			xr_strcpy(anthologyGroundWinterDetailBumpX, anthologyGroundDetailBumpX);
	}

	// HQ
	if (bump && hq)
	{
		xr_strcat(vs, "-hq");
		xr_strcat(ps, "-hq");
	}

	// Uber-construct
#if defined(USE_DX10) || defined(USE_DX11)
#	ifdef USE_DX11
	if (bump && hq && RImplementation.o.dx11_enable_tessellation && C.TessMethod != 0)
	{
		char hs[256], ds[256]; // = "DX11\\tess", ds[256] = "DX11\\tess";
		char params[256] = "(";

		if (C.TessMethod == CBlender_Compile::TESS_PN || C.TessMethod == CBlender_Compile::TESS_PN_HM)
		{
			RImplementation.addShaderOption("TESS_PN", "1");
			xr_strcat(params, "TESS_PN,");
		}

		if (C.TessMethod == CBlender_Compile::TESS_HM || C.TessMethod == CBlender_Compile::TESS_PN_HM)
		{
			RImplementation.addShaderOption("TESS_HM", "1");
			xr_strcat(params, "TESS_HM,");
		}

		if (lmap)
		{
			RImplementation.addShaderOption("USE_LM_HEMI", "1");
			xr_strcat(params, "USE_LM_HEMI,");
		}

		if (C.bDetail_Diffuse)
		{
			RImplementation.addShaderOption("USE_TDETAIL", "1");
			xr_strcat(params, "USE_TDETAIL,");
		}

		if (C.bDetail_Bump)
		{
			RImplementation.addShaderOption("USE_TDETAIL_BUMP", "1");
			xr_strcat(params, "USE_TDETAIL_BUMP,");
		}

		xr_strcat(params, ")");

		strconcat(sizeof(vs), vs, "deffer_", _vspec, "_bump", params);
		strconcat(sizeof(ps), ps, "deffer_", _pspec, _aref ? "_aref" : "", "_bump", params);
		strconcat(sizeof(hs), hs, "DX11\\tess", params);
		strconcat(sizeof(ds), ds, "DX11\\tess", params);

		VERIFY(strstr(vs, "bump")!=0);
		VERIFY(strstr(ps, "bump")!=0);
		C.r_TessPass(vs, hs, ds, "null", ps, FALSE);
		RImplementation.clearAllShaderOptions();
		u32 stage = C.r_dx10Sampler("smp_bump_ds");
		if (stage != -1)
		{
			C.i_dx10Address(stage, D3DTADDRESS_WRAP);
			C.i_dx10FilterAnizo(stage, TRUE);
		}
		if (ps_r2_ls_flags_ext.test(R2FLAGEXT_WIREFRAME))
			C.R().SetRS(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
		C.r_dx10Texture("s_tbump", fnameA);
		C.r_dx10Texture("s_tbumpX", fnameB); // should be before base bump
		if (bHasDetailBump)
		{
			C.r_dx10Texture("s_tdetailBumpX", texDetailBumpX);
		}
	}
	else
#	endif
	if (DO_NOT_WRITE) C.r_Pass(vs, ps, FALSE, TRUE, FALSE);
	else C.r_Pass(vs, ps, FALSE);
	//C.r_Sampler		("s_base",		C.L_textures[0],	false,	D3DTADDRESS_WRAP,	D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
	//C.r_Sampler		("s_bumpX",		fnameB,				false,	D3DTADDRESS_WRAP,	D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);	// should be before base bump
	//C.r_Sampler		("s_bump",		fnameA,				false,	D3DTADDRESS_WRAP,	D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
	//C.r_Sampler		("s_bumpD",		dt,					false,	D3DTADDRESS_WRAP,	D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
	//C.r_Sampler		("s_detail",	dt,					false,	D3DTADDRESS_WRAP,	D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
	C.r_dx10Texture("s_base", C.L_textures[0]);
	if (anthologyWinterObjects)
		C.r_dx10Texture("s_base_winter", anthologyWinterObject);
	if (anthologyFloraTextures)
	{
		C.r_dx10Texture("s_base_green", anthologyGreen);
		C.r_dx10Texture("s_base_autumn", anthologyAutumn);
		C.r_dx10Texture("s_base_dead", anthologyDead);
		C.r_dx10Texture("s_snow_tree", "anthology_seasons\\detail_snow_ground");
	}
	if (anthologyGroundTextures)
	{
		C.r_dx10Texture("s_base_dead", anthologyGroundDead);
		C.r_dx10Texture("s_bump_dead", anthologyGroundBump);
		C.r_dx10Texture("s_bumpX_dead", anthologyGroundBumpX);
		C.r_dx10Texture("s_detail_dead", anthologyGroundDetail);
		C.r_dx10Texture("s_detailBump_dead", anthologyGroundDetailBump);
		C.r_dx10Texture("s_detailBumpX_dead", anthologyGroundDetailBumpX);
		C.r_dx10Texture("s_base_winter", anthologyGroundWinter);
		C.r_dx10Texture("s_bump_winter", anthologyGroundWinterBump);
		C.r_dx10Texture("s_bumpX_winter", anthologyGroundWinterBumpX);
		C.r_dx10Texture("s_detail_winter", anthologyGroundWinterDetail);
		C.r_dx10Texture("s_detailBump_winter", anthologyGroundWinterDetailBump);
		C.r_dx10Texture("s_detailBumpX_winter", anthologyGroundWinterDetailBumpX);
	}
	C.r_dx10Texture("s_bumpX", fnameB); // should be before base bump
	C.r_dx10Texture("s_bump", fnameA);
	C.r_dx10Texture("s_bumpD", dt);
	C.r_dx10Texture("s_detail", dt);
	if (bHasDetailBump)
	{
		C.r_dx10Texture("s_detailBump", texDetailBump);
		C.r_dx10Texture("s_detailBumpX", texDetailBumpX);
	}
	C.r_dx10Sampler("smp_base");
	C.r_dx10Sampler("smp_linear");
	if (lmap)
	{
		//C.r_Sampler("s_hemi",	C.L_textures[2],	false,	D3DTADDRESS_CLAMP,	D3DTEXF_LINEAR,		D3DTEXF_NONE,	D3DTEXF_LINEAR);
		C.r_dx10Texture("s_hemi", C.L_textures[2]);
		C.r_dx10Sampler("smp_rtlinear");
	}
#else	//	USE_DX10
	if (DO_NOT_WRITE) C.r_Pass(vs, ps, FALSE, TRUE, FALSE);
	else C.r_Pass(vs, ps, FALSE);
	VERIFY(C.L_textures[0].size());
	if (bump)
	{
		VERIFY2(xr_strlen(fnameB), C.L_textures[0].c_str());
		VERIFY2(xr_strlen(fnameA), C.L_textures[0].c_str());
	}
	if (bHasDetailBump)
	{
		VERIFY2(xr_strlen(texDetailBump), C.L_textures[0].c_str());
		VERIFY2(xr_strlen(texDetailBumpX), C.L_textures[0].c_str());
	}
	C.r_Sampler("s_base", C.L_textures[0], false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
	            D3DTEXF_ANISOTROPIC);
	C.r_Sampler("s_bumpX", fnameB, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC);
	// should be before base bump
	C.r_Sampler("s_bump", fnameA, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC);
	C.r_Sampler("s_bumpD", dt, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC);
	C.r_Sampler("s_detail", dt, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC);
	if (bHasDetailBump)
	{
		C.r_Sampler("s_detailBump", texDetailBump, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
		            D3DTEXF_ANISOTROPIC);
		C.r_Sampler("s_detailBumpX", texDetailBumpX, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
		            D3DTEXF_ANISOTROPIC);
	}
	if (lmap)C.r_Sampler("s_hemi", C.L_textures[2], false, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_NONE,
	                     D3DTEXF_LINEAR);
#endif	//	USE_DX10

	if (!DO_NOT_FINISH) C.r_End();
}

#ifdef USE_DX11
void uber_shadow(CBlender_Compile& C, LPCSTR _vspec)
{
	// Uber-parse
	string256 fname, fnameA, fnameB;
	xr_strcpy(fname, *C.L_textures[0]); //. andy if (strext(fname)) *strext(fname)=0;
	fix_texture_name(fname);
	ref_texture _t;
	_t.create(fname);
	bool bump = _t.bump_exist();

	// detect lmap
	bool lmap = true;
	if (C.L_textures.size() < 3) lmap = false;
	else
	{
		pcstr tex = C.L_textures[2].c_str();
		if (tex[0] == 'l' && tex[1] == 'm' && tex[2] == 'a' && tex[3] == 'p') lmap = true;
		else lmap = false;
	}


	string256 vs, dt;
	xr_strcpy(dt, sizeof(dt), C.detail_texture ? C.detail_texture : "");

	// detect detail bump
	string256 texDetailBump = {'\0'};
	string256 texDetailBumpX = {'\0'};
	bool bHasDetailBump = false;
	if (C.bDetail_Bump)
	{
		LPCSTR detail_bump_texture = DEV->m_textures_description.GetBumpName(dt).c_str();
		//	Detect and use detail bump
		if (detail_bump_texture)
		{
			bHasDetailBump = true;
			xr_strcpy(texDetailBump, sizeof(texDetailBump), detail_bump_texture);
			xr_strcpy(texDetailBumpX, sizeof(texDetailBumpX), detail_bump_texture);
			xr_strcat(texDetailBumpX, "#");
		}
	}


	if (!bump)
	{
		fnameA[0] = fnameB[0] = 0;
	}
	else
	{
		xr_strcpy(fnameA, _t.bump_get().c_str());
		strconcat(sizeof(fnameB), fnameB, fnameA, "#");
	}

	if (bump && RImplementation.o.dx11_enable_tessellation && C.TessMethod != 0)
	{
		char hs[256], ds[256]; // = "DX11\\tess", ds[256] = "DX11\\tess";
		char params[256] = "(";

		if (C.TessMethod == CBlender_Compile::TESS_PN || C.TessMethod == CBlender_Compile::TESS_PN_HM)
		{
			RImplementation.addShaderOption("TESS_PN", "1");
			xr_strcat(params, "TESS_PN,");
		}

		if (C.TessMethod == CBlender_Compile::TESS_HM || C.TessMethod == CBlender_Compile::TESS_PN_HM)
		{
			RImplementation.addShaderOption("TESS_HM", "1");
			xr_strcat(params, "TESS_HM,");
		}

		if (lmap)
		{
			RImplementation.addShaderOption("USE_LM_HEMI", "1");
			xr_strcat(params, "USE_LM_HEMI,");
		}

		if (C.bDetail_Diffuse)
		{
			RImplementation.addShaderOption("USE_TDETAIL", "1");
			xr_strcat(params, "USE_TDETAIL,");
		}

		if (C.bDetail_Bump)
		{
			RImplementation.addShaderOption("USE_TDETAIL_BUMP", "1");
			xr_strcat(params, "USE_TDETAIL_BUMP,");
		}

		xr_strcat(params, ")");

		strconcat(sizeof(vs), vs, "deffer_", _vspec, "_bump", params);
		strconcat(sizeof(hs), hs, "DX11\\tess", params);
		strconcat(sizeof(ds), ds, "DX11\\tess_shadow", params);

		C.r_TessPass(vs, hs, ds, "null", "dumb", FALSE,TRUE,TRUE,FALSE);
		RImplementation.clearAllShaderOptions();
		C.r_dx10Texture("s_base", C.L_textures[0]);
		C.r_dx10Texture("s_bumpX", fnameB); // should be before base bump
		C.r_dx10Texture("s_bump", fnameA);
		if (bHasDetailBump)
		{
			C.r_dx10Texture("s_detailBump", texDetailBump);
			C.r_dx10Texture("s_detailBumpX", texDetailBumpX);
		}
		u32 stage = C.r_dx10Sampler("smp_bump_ds");
		if (stage != -1)
		{
			C.i_dx10Address(stage, D3DTADDRESS_WRAP);
			C.i_dx10FilterAnizo(stage, TRUE);
		}
		if (ps_r2_ls_flags_ext.test(R2FLAGEXT_WIREFRAME))
			C.R().SetRS(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	}
	else
		C.r_Pass("shadow_direct_base", "dumb", FALSE,TRUE,TRUE,FALSE);
}
#endif
