#ifndef	SHADOW_H
#define SHADOW_H

#include "common.h"

//uniform	sampler	s_smap	: register(ps,s0);	// 2D/cube shadowmap
//Texture2D<float>	s_smap;		// 2D/cube shadowmap
//	Used for RGBA texture too ?!
Texture2D	s_smap : register(ps,t0);		// 2D/cube shadowmap

Texture2D<float> s_smap_minmax;		// 2D/cube shadowmap
#include "gather.ps"

SamplerComparisonState smp_smap;	//	Special comare sampler
sampler smp_jitter;

uniform float4 ssfx_shadow_bias;

Texture2D jitter0;
Texture2D jitter1;
Texture2D jitterMipped;

#ifndef USE_ULTRA_SHADOWS
	#define	KERNEL	0.6f
#else
	#define	KERNEL	1.0f
#endif

#define PCSS_PIXEL int(4)
#define PCSS_STEP int(2)
#define PCSS_PIXEL_MIN float(1.0)
#define PCSS_SUN_WIDTH float(150.0)

float modify_light( float light )
{
   return ( light > 0.7 ? 1.0 : lerp( 0.0, 1.0, saturate( light / 0.7 ) ) ); 
}

//////////////////////////////////////////////////////////////////////////////////////////
// hardware + PCF
//////////////////////////////////////////////////////////////////////////////////////////
float sample_hw_pcf (float4 tc,float4 shift)
{
	static const float 	ts = KERNEL / float(SMAP_size);

	tc.xyz 	/= tc.w;
	tc.xy 	+= shift.xy * ts;

	return s_smap.SampleCmpLevelZero( smp_smap, tc.xy, tc.z).x;
}

float shadow_hw( float4 tc )
{
  	float	s0		= sample_hw_pcf( tc, float4( -1, -1, 0, 0) );
  	float	s1		= sample_hw_pcf( tc, float4( +1, -1, 0, 0) );
  	float	s2		= sample_hw_pcf( tc, float4( -1, +1, 0, 0) );
  	float	s3		= sample_hw_pcf( tc, float4( +1, +1, 0, 0) );

	return	(s0+s1+s2+s3)/4.h;
}


#ifdef SM_MINMAX
bool cheap_reject( float3 tc, inout bool full_light ) 
{
   float4 plane0  = sm_minmax_gather( tc.xy, int2( -1,-1 ) );
   float4 plane1  = sm_minmax_gather( tc.xy, int2(  1,-1 ) );
   float4 plane2  = sm_minmax_gather( tc.xy, int2( -1, 1 ) );
   float4 plane3  = sm_minmax_gather( tc.xy, int2(  1, 1 ) );
   bool plane     = all( ( plane0 >= (0).xxxx ) * ( plane1 >= (0).xxxx ) * ( plane2 >= (0).xxxx ) * ( plane3 >= (0).xxxx ) );

   [flatten] if( !plane ) // if there are no proper plane equations in the support region
   {
      bool no_plane  = all( ( plane0 < (0).xxxx ) * ( plane1 < (0).xxxx ) * ( plane2 < (0).xxxx ) * ( plane3 < (0).xxxx ) );
      float4 z       = ( tc.z - 0.0005 ).xxxx;
      bool reject    = all( ( z > -plane0 ) * ( z > -plane1 ) * ( z > -plane2 ) * ( z > -plane3 ) ); 
      [flatten] if( no_plane && reject )
      {
         full_light = false;
         return true;
      }
      else
      {
         return false;
      }
   }
   else // plane equation detected
   {
      // compute corrected z for texel pos
      static const float scale = float( SMAP_size / 4 );
      float2 fc  = frac( tc.xy * scale );
      float  z   = lerp( lerp( plane0.y, plane1.x, fc.x ), lerp( plane2.z, plane3.w, fc.x ), fc.y );

      // do minmax test with new z
      full_light = ( ( tc.z - 0.0001 ) <= z );

      return true; 
   }
}

//Sunshafts
float shadow_dx10_1_sunshafts( float4 tc, float2 pos2d ) 
{
   float3 t         = tc.xyz / tc.w;
   float minmax     = s_smap_minmax.SampleLevel( smp_nofilter, t, 0 ).x;
   bool   umbra     = ( ( minmax.x < 0 ) && ( t.z > -minmax.x ) );

   [branch] if( umbra )
   {
      return 0.0;
   }
   else
   {
      return shadow_hw( tc ); 
   }
}
#endif	//	SM_MINMAX



	//PCSS shadows
static const float2 poissonDisk[32] = {
	float2(0.0617981, 0.07294159),
	float2(0.6470215, 0.7474022),
	float2(-0.5987766, -0.7512833),
	float2(-0.693034, 0.6913887),
	float2(0.6987045, -0.6843052),
	float2(-0.9402866, 0.04474335),
	float2(0.8934509, 0.07369385),
	float2(0.1592735, -0.9686295),
	float2(-0.05664673, 0.995282),
	float2(-0.1203411, -0.1301079),
	float2(0.1741608, -0.1682285),
	float2(-0.09369049, 0.3196758),
	float2(0.185363, 0.3213367),
	float2(-0.1493771, -0.3147511),
	float2(0.4452095, 0.2580113),
	float2(-0.1080467, -0.5329178),
	float2(0.1604507, 0.5460774),
	float2(-0.4037193, -0.2611179),
	float2(0.5947998, -0.2146744),
	float2(0.3276062, 0.9244621),
	float2(-0.6518704, -0.2503952),
	float2(-0.3580975, 0.2806469),
	float2(0.8587891, 0.4838005),
	float2(-0.1596546, -0.8791054),
	float2(-0.3096867, 0.5588146),
	float2(-0.5128918, 0.1448544),
	float2(0.8581337, -0.424046),
	float2(0.1562584, -0.5610626),
	float2(-0.7647934, 0.2709858),
	float2(-0.3090832, 0.9020988),
	float2(0.3935608, 0.4609676),
	float2(0.3929337, -0.5010948),
};

	//Quality tokens --Fine
#if !defined(SUN_QUALITY) || SUN_QUALITY == 0
	#define PCSS_NUM_SAMPLES int(1)
#elif SUN_QUALITY==1
	#define PCSS_NUM_SAMPLES int(8)
#elif SUN_QUALITY==2
	#define PCSS_NUM_SAMPLES int(12)
#elif SUN_QUALITY==3
	#define PCSS_NUM_SAMPLES int(20)
#elif (SUN_QUALITY==4 || SUN_QUALITY==5)
	#define PCSS_NUM_SAMPLES int(32)
#endif

float shadow_pcss_og( float4 tc )
{
	// - Small modification to fix flickering and black squares.
	// - Added a extra performance option with lower SUN_QUALITY settings.
	// - Extended the blocker search from 3x3 to 4x4 for better results.
	// https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders/

	tc.xyz /= tc.w;

#if SUN_QUALITY > 3 // Blocker search ( Penumbra ) and filter

	int3 uv = int3(tc.xy * float(SMAP_size), 0);
	float zBlock = tc.z - 0.0001;
	float avgBlockerDepth = 0.0;
	float blockerCount = 0.0;

	[unroll] 
	for( int row = -PCSS_PIXEL; row <= PCSS_PIXEL; row += PCSS_STEP )
	{
		[unroll] 
		for( int col = -PCSS_PIXEL; col <= PCSS_PIXEL; col += PCSS_STEP )
		{
			float shadowMapDepth = s_smap.Load( uv, int2( col, row ) ).x;
			float b1 = ( shadowMapDepth < zBlock ) ? 1.0 : 0.0;
			blockerCount += b1;
			avgBlockerDepth += shadowMapDepth * b1;
		}
	}

	if(blockerCount < 1)
		return 1.0;

	avgBlockerDepth /= blockerCount;
	float fRatio = saturate( ( ( tc.z - avgBlockerDepth ) * PCSS_SUN_WIDTH ) / avgBlockerDepth );
	fRatio *= fRatio;
	fRatio = max(PCSS_PIXEL_MIN, fRatio * float(PCSS_PIXEL)) / float(SMAP_size);

	float s = 0.0;
	[unroll] 
	for( uint i = 0; i < PCSS_NUM_SAMPLES; ++i )
	{
		float2 offset = poissonDisk[i] * fRatio;
		s += s_smap.SampleCmpLevelZero( smp_smap, tc.xy + offset, tc.z ).x;
	}
	return s / PCSS_NUM_SAMPLES;

#else // No blocker search ( Penumbra ), just filter


	float fRatio = 4.0f / float(SMAP_size);

	float s = 0.0;
	[unroll] 
	for( uint i = 0; i < PCSS_NUM_SAMPLES; ++i )
	{
		float2 offset = poissonDisk[i] * fRatio;
		float test = s_smap.SampleCmpLevelZero( smp_smap, tc.xy + offset, tc.z ).x;
		s += test;
	}

	return s / PCSS_NUM_SAMPLES;
	

#endif

}




//http://the-witness.net/news/2013/09/shadow-mapping-summary-part-1/
//https://github.com/TheRealMJP
float shadow_pcss(float4 tc)
{
	tc.xyz *= 1.0 / tc.w;
	tc.z -= 0.000085; //static bias

    float2 shadow_res = float2(SMAP_size, 1.0 / SMAP_size);

	float2 uv = tc.xy * shadow_res.x;

    float2 base_uv = floor(uv.xy + 0.5);
    float2 st = (uv.xy + 0.5 - base_uv.xy);

    base_uv -= float2(0.5, 0.5);
    base_uv *= shadow_res.y;

	float sum = 0.0;

//Low quality
#if SUN_QUALITY == 0
	float uw0 = (3.0 - 2.0 * st.x);
	float uw1 = (1.0 + 2.0 * st.x);

	float u0 = (2.0 - st.x) / uw0 - 1.0;
	float u1 = st.x / uw1 + 1;

	float vw0 = (3.0 - 2.0 * st.y);
	float vw1 = (1.0 + 2.0 * st.y);

	float v0 = (2.0 - st.y) / vw0 - 1.0;
	float v1 = st.y / vw1 + 1.0;

	sum += uw0 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v0) * shadow_res.y, tc.z);
	sum += uw1 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v0) * shadow_res.y, tc.z);
	sum += uw0 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v1) * shadow_res.y, tc.z);
	sum += uw1 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v1) * shadow_res.y, tc.z);

	return sum * 0.0625;

//Medium quality
#elif SUN_QUALITY == 1

	float uw0 = (4.0 - 3.0 * st.x);
	float uw1 = 7.0;
	float uw2 = (1.0 + 3.0 * st.x);

	float u0 = (3.0 - 2.0 * st.x) / uw0 - 2;
	float u1 = (3.0 + st.x) / uw1;
	float u2 = st.x / uw2 + 2;

	float vw0 = (4.0 - 3.0 * st.y);
	float vw1 = 7.0;
	float vw2 = (1.0 + 3.0 * st.y);

	float v0 = (3.0 - 2.0 * st.y) / vw0 - 2.0;
	float v1 = (3.0 + st.y) / vw1;
	float v2 = st.y / vw2 + 2.0;

	sum += uw0 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v0) * shadow_res.y, tc.z);
	sum += uw1 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v0) * shadow_res.y, tc.z);
	sum += uw2 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v0) * shadow_res.y, tc.z);

	sum += uw0 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v1) * shadow_res.y, tc.z);
	sum += uw1 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v1) * shadow_res.y, tc.z);
	sum += uw2 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v1) * shadow_res.y, tc.z);

	sum += uw0 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v2) * shadow_res.y, tc.z);
	sum += uw1 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v2) * shadow_res.y, tc.z);
	sum += uw2 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v2) * shadow_res.y, tc.z);

	return sum * 0.00694444444;

//High/ultra/extreme quality
#else

	float uw0 = (5.0 * st.x - 6.0);
	float uw1 = (11.0 * st.x - 28.0);
	float uw2 = -(11.0 * st.x + 17.0);
	float uw3 = -(5.0 * st.x + 1.0);

	float u0 = (4.0 * st.x - 5.0) / uw0 - 3.0;
	float u1 = (4.0 * st.x - 16.0) / uw1 - 1.0;
	float u2 = -(7.0 * st.x + 5.0) / uw2 + 1.0;
	float u3 = -st.x / uw3 + 3.0;

	float vw0 = (5.0 * st.y - 6.0);
	float vw1 = (11.0 * st.y - 28.0);
	float vw2 = -(11.0 * st.y + 17.0);
	float vw3 = -(5.0 * st.y + 1.0);

	float v0 = (4.0 * st.y - 5.0) / vw0 - 3.0;
	float v1 = (4.0 * st.y - 16.0) / vw1 - 1.0;
	float v2 = -(7.0 * st.y + 5.0) / vw2 + 1.0;
	float v3 = -st.y / vw3 + 3.0;

	sum += uw0 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v0) * shadow_res.y, tc.z);
	sum += uw1 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v0) * shadow_res.y, tc.z);
	sum += uw2 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v0) * shadow_res.y, tc.z);
	sum += uw3 * vw0 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u3, v0) * shadow_res.y, tc.z);

	sum += uw0 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v1) * shadow_res.y, tc.z);
	sum += uw1 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v1) * shadow_res.y, tc.z);
	sum += uw2 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v1) * shadow_res.y, tc.z);
	sum += uw3 * vw1 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u3, v1) * shadow_res.y, tc.z);

	sum += uw0 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v2) * shadow_res.y, tc.z);
	sum += uw1 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v2) * shadow_res.y, tc.z);
	sum += uw2 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v2) * shadow_res.y, tc.z);
	sum += uw3 * vw2 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u3, v2) * shadow_res.y, tc.z);

	sum += uw0 * vw3 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u0, v3) * shadow_res.y, tc.z);
	sum += uw1 * vw3 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u1, v3) * shadow_res.y, tc.z);
	sum += uw2 * vw3 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u2, v3) * shadow_res.y, tc.z);
	sum += uw3 * vw3 * s_smap.SampleCmpLevelZero(smp_smap, base_uv + float2(u3, v3) * shadow_res.y, tc.z);

	return sum * 0.00036982248;
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////
//	D24X8+PCF
//////////////////////////////////////////////////////////////////////////////////////////

float4 test (float4 tc, float2 offset)
{
	tc.xyz 	/= tc.w;
	tc.xy 	+= offset;
	return s_smap.SampleCmpLevelZero( smp_smap, tc.xy, tc.z).x;
}

half 	shadowtest_sun 	(float4 tc, float4 tcJ)			// jittered sampling
{
	half4	r;
	const 	float 	scale 	= (0.7/float(SMAP_size));

	float2 	tc_J	= frac(tc.xy/tc.w*SMAP_size/4.0 )*0.5;
	float4	J0		= jitter0.Sample(smp_jitter,tc_J)*scale;

	const float k = 0.5/float(SMAP_size);
	r.x 	= test 	(tc, J0.xy+half2(-k,-k)).x;
	r.y 	= test 	(tc, J0.wz+half2( k,-k)).y;
	r.z		= test	(tc,-J0.xy+half2(-k, k)).z;
	r.w		= test	(tc,-J0.wz+half2( k, k)).x;

	return	dot(r,1.0/4.0);
}

float shadow_hw_hq( float4 tc )
{
#ifdef SM_MINMAX
   bool   full_light = false; 
   bool   cheap_path = cheap_reject( tc.xyz / tc.w, full_light );

   [branch] if( cheap_path )
   {
      [branch] if( full_light == true )
         return 1.0;
      else
         return sample_hw_pcf( tc, (0).xxxx ); 
   }
   else
   {
      return shadow_pcss(tc);
   }
#else //	SM_MINMAX
      return shadow_pcss(tc);
#endif //	SM_MINMAX
}

float shadow( float4 tc ) 
{
#ifdef USE_ULTRA_SHADOWS
	#ifdef SM_MINMAX
		return modify_light( shadow_hw_hq( tc ) ); 
	#else
		return shadow_hw_hq( tc ); 
	#endif
#else
	return shadow_pcss(tc);
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////
// testbed

float shadow_rain(float4 tc, float2 tcJ)			// jittered sampling
{
	float4	r;

	const 	float 	scale 	= (4.0/float(SMAP_size));

	float4	J0 	= jitter0.Sample( smp_linear, tcJ )*scale;
	float4	J1 	= jitter1.Sample( smp_linear, tcJ )*scale;

	r.x 	= test 	(tc,J0.xy).x;
	r.y 	= test 	(tc,J0.wz).y;
	r.z		= test	(tc,J1.xy).z;
	r.w		= test	(tc,J1.wz).x;
	return	dot(r,1.0/4.0);
}

//////////////////////////////////////////////////////////////////////////////////////////
#ifdef  USE_SUNMASK	
float3x4 m_sunmask;	// ortho-projection
float sunmask( float4 P )
{
	float2 		tc	= mul( m_sunmask, P );		//
	return 		s_lmap.SampleLevel( smp_linear, tc, 0 ).w;	//Hemi map - ambient occlusion	
}
#else
float sunmask( float4 P ) { return 1.0; }		// 
#endif
//////////////////////////////////////////////////////////////////////////////////////////
uniform float4x4	m_shadow;

#endif
