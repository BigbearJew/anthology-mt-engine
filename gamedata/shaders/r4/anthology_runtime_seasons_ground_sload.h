#ifndef SLOAD_H
#define SLOAD_H

// This file intentionally uses the stock SLOAD_H include guard.  Season
// ground wrappers include it before the active deffer shader, so only matched
// detail/grnd materials receive the runtime Dead Autumn texture selection.

#include "common.h"

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5), 
								               float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct	surface_bumped
{
	float4	base;
	float3	normal;
	float	gloss;
	float	height;

};

// Ground materials use separate Dead Autumn and Winter diffuse resources.
// Winter keeps the stock support maps because XRay bump/detail channels must
// match the original material; foreign support maps can make roads black.
Texture2D s_base_dead;
Texture2D s_bump_dead;
Texture2D s_bumpX_dead;
Texture2D s_detail_dead;
Texture2D s_detailBump_dead;
Texture2D s_detailBumpX_dead;
Texture2D s_base_winter;
Texture2D s_bump_winter;
Texture2D s_bumpX_winter;
Texture2D s_detail_winter;
Texture2D s_detailBump_winter;
Texture2D s_detailBumpX_winter;

uniform float4 anthology_flora_style;
#ifndef ANTHOLOGY_WINTER_OBJECTS
#define ANTHOLOGY_SNOW_SINGLE_GROUND
#include "anthology_snow_ground.h"
#endif

bool anthology_ground_dead_enabled()
{
#ifdef ANTHOLOGY_WINTER_OBJECTS
    return false;
#else
    return anthology_flora_style.x >= 2.5f && anthology_flora_style.x < 3.5f;
#endif
}

bool anthology_ground_winter_enabled()
{
#ifdef ANTHOLOGY_WINTER_OBJECTS
    // Architecture and props switch only in Winter, not Late Autumn.
    return anthology_flora_style.x >= 3.5f && anthology_flora_style.x < 4.5f;
#else
    return anthology_flora_style.x >= 3.5f && anthology_flora_style.x < 5.5f;
#endif
}

float4 tbase( float2 tc )
{
    if (anthology_ground_winter_enabled())
        return s_base_winter.Sample(smp_base, tc);
#ifdef ANTHOLOGY_WINTER_OBJECTS
    return s_base.Sample(smp_base, tc);
#else
    return anthology_ground_dead_enabled() ?
        s_base_dead.Sample(smp_base, tc) : s_base.Sample(smp_base, tc);
#endif
}

float4 anthology_ground_bump(float2 tc)
{
    // Seasonal diffuse maps may come from a different material pack. Keep the
    // compiled map's own normal/gloss texture so its channel layout remains
    // valid and does not create dark Dead Autumn patches.
    return s_bump.Sample(smp_base, tc);
}

float4 anthology_ground_bump_x(float2 tc)
{
    return s_bumpX.Sample(smp_base, tc);
}

float4 anthology_ground_bump_x_level(float2 tc, float lod)
{
    return s_bumpX.SampleLevel(smp_base, tc, lod);
}

float4 anthology_ground_detail(float2 tc)
{
#ifdef ANTHOLOGY_WINTER_OBJECTS
    return s_detail.Sample(smp_base, tc);
#else
    if (anthology_ground_winter_enabled())
        return s_detail.Sample(smp_base, tc);
    return anthology_ground_dead_enabled() ?
        s_detail_dead.Sample(smp_base, tc) : s_detail.Sample(smp_base, tc);
#endif
}

float4 anthology_ground_detail_bump(float2 tc)
{
    return s_detailBump.Sample(smp_base, tc);
}

float4 anthology_ground_detail_bump_x(float2 tc)
{
    return s_detailBumpX.Sample(smp_base, tc);
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

static const float fParallaxStartFade = 8.0f;
static const float fParallaxStopFade = 12.0f;

void UpdateTC( inout p_bumped I)
{
	if (I.position.z < fParallaxStopFade)
	{
		const float maxSamples = 25;
		const float minSamples = 5;
		const float fParallaxOffset = -0.013;

		float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
									 I.M1.y, I.M2.y, I.M3.y,
									 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);
		
		//	Calculate number of steps
		float nNumSteps = lerp( maxSamples, minSamples, eye.z );

		float	fStepSize			= 1.0 / nNumSteps;
		float2	vDelta				= eye.xy * fParallaxOffset*1.2;
		float2	vTexOffsetPerStep	= fStepSize * vDelta;

		//	Prepare start data for cycle
		float2	vTexCurrentOffset	= I.tcdh;
		float	fCurrHeight			= 0.0;
		float	fCurrentBound		= 1.0;

		for( int i=0; i<nNumSteps; ++i )
		{
			if (fCurrHeight < fCurrentBound)
			{	
				vTexCurrentOffset += vTexOffsetPerStep;		
				fCurrHeight = anthology_ground_bump_x_level(vTexCurrentOffset.xy, 0).a;
				fCurrentBound -= fStepSize;
			}
		}

/*
		[unroll(25)]	//	Doesn't work with [loop]
		for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
		{
			vTexCurrentOffset += vTexOffsetPerStep;		
			fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
		}
*/
		//	Reconstruct previouse step's data
		vTexCurrentOffset -= vTexOffsetPerStep;
		float fPrevHeight = anthology_ground_bump_x(vTexCurrentOffset.xy).a;

		//	Smooth tc position between current and previouse step
		float	fDelta2 = ((fCurrentBound + fStepSize) - fPrevHeight);
		float	fDelta1 = (fCurrentBound - fCurrHeight);
		float	fParallaxAmount = (fCurrentBound * fDelta2 - (fCurrentBound + fStepSize) * fDelta1 ) / ( fDelta2 - fDelta1 );
		float	fParallaxFade 	= smoothstep(fParallaxStopFade, fParallaxStartFade, I.position.z);
		float2	vParallaxOffset = vDelta * ((1- fParallaxAmount )*fParallaxFade);
		float2	vTexCoord = I.tcdh + vParallaxOffset;
	
		//	Output the result
		I.tcdh = vTexCoord;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC( inout p_bumped I)
{
	float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								 I.M1.y, I.M2.y, I.M3.y,
								 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);
								 
	float	height	= anthology_ground_bump_x(I.tcdh).w;	//
			//height  /= 2;
			//height  *= 0.8;
			height	= height*(parallax.x) + (parallax.y);	//
	float2	new_tc  = I.tcdh + height * normalize(eye);	//

	//	Output the result
	I.tcdh.xy	= new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC( inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

surface_bumped sload_i( p_bumped I)
{
	surface_bumped	S;
   
	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= anthology_ground_bump(I.tcdh);		// IN:	normal.gloss
	float4 	NuE	= anthology_ground_bump_x(I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzy + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.w;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
	float4 NDetail		= anthology_ground_detail_bump(I.tcdbump);
	float4 NDetailX		= anthology_ground_detail_bump_x(I.tcdbump);
	S.gloss				= S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	S.normal			+= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)

	float4 detail		= anthology_ground_detail(I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
	float4 detail		= anthology_ground_detail(I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * 2;

#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload_i( p_bumped I, float2 pixeloffset )
{
	surface_bumped	S;
   
   // apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
   I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= anthology_ground_bump(I.tcdh);		// IN:	normal.gloss
	float4 	NuE	= anthology_ground_bump_x(I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzyx + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.w;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail		= anthology_ground_detail_bump(I.tcdbump);
	float4 NDetailX		= anthology_ground_detail_bump_x(I.tcdbump);
	S.gloss				= S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	S.normal			+= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)

	float4 detail		= anthology_ground_detail(I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail		= anthology_ground_detail(I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * 2;
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload ( p_bumped I)
{
      surface_bumped      S   = sload_i	(I);
	//	S.normal.z			*=	0.5;		//. make bump twice as contrast (fake, remove me if possible)
      return              S;
}

surface_bumped sload ( p_bumped I, float2 pixeloffset )
{
      surface_bumped      S   = sload_i	(I, pixeloffset );
	//	S.normal.z			*=	0.5;		//. make bump twice as contrast (fake, remove me if possible)
      return              S;
}

#endif
