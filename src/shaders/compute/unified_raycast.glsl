#version 430 core
// One Raycasting Shader to Rule them all!

////////////////////////////////     DEFINES      ////////////////////////////////
/*********** LIST OF POSSIBLE DEFINES ***********
	ALPHA_SCALE <float>
	AMBIENT_OCCLUSION
	ERT_THRESHOLD <float>
	EMISSION_ABSORPTION_RAW
		EMISSION_SCALE <float>
		ABSORPTION_SCALE <float>
	FIRST_HIT
	LEVEL_OF_DETAIL
	OCCLUSION_MAP
	RANDOM_OFFSET
	SCENE_DEPTH
	STEREO_SINGLE_PASS
		STEREO_SINGLE_OUTPUT
***********/

#ifndef ALPHA_SCALE
#define ALPHA_SCALE 20.0
#endif
#ifndef COLOR_SCALE
#define COLOR_SCALE 1.0
#endif

#ifdef RANDOM_OFFSET 
float rand(vec2 co) { //!< http://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
	return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}
#endif

#ifndef ERT_THRESHOLD
#define ERT_THRESHOLD 0.99
#endif

#ifdef EMISSION_ABSORPTION_RAW
	#ifndef EMISSION_SCALE
	#define EMISSION_SCALE 1.0
	#endif

	#ifndef ABSORPTION_SCALE
	#define ABSORPTION_SCALE 10.0
	#endif
#endif

#ifndef LOCAL_SIZE_X
#define LOCAL_SIZE_X 1
#endif
#ifndef LOCAL_SIZE_Y
#define LOCAL_SIZE_Y 1024
#endif

///////////////////////////////////////////////////////////////////////////////////

// specify local work group size
layout (local_size_x = LOCAL_SIZE_X, local_size_y = LOCAL_SIZE_Y) in;

layout(binding = 1, rgba8) writeonly uniform image2D color_image;
#ifdef FIRST_HIT
	layout(binding = 2, rgba8) writeonly uniform image2D fragFirst_image;
#endif

// specify atomic counter to save
//layout(binding = 0, offset = 0) uniform atomic_uint counter;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler3D volume_texture; // volume 3D integer texture sampler

#ifdef OCCLUSION_MAP
	uniform sampler2D occlusion_map;   // uvw coordinates map of occlusion map
	uniform sampler2D occlusion_clip_frustum_back;   // uvw coordinates map of occlusion map
	uniform sampler2D occlusion_clip_frustum_front;   // uvw coordinates map of occlusion map
	
	//uniform mat4 uScreenToOcclusionView; // view that produced the first hit map on which the occlusion_map is based
	//uniform mat4 uOcclusionViewToTexture; // from occlusion view to texture
#endif

#ifdef SCENE_DEPTH
	uniform sampler2D scene_depth_map;   // depth map of scene
#endif

#ifdef STEREO_SINGLE_PASS
	#ifdef STEREO_SINGLE_OUTPUT
		layout(binding = 0, rgba8) restrict uniform image2D stereo_image;
	#else
		layout(binding = 0, rgba16f) writeonly uniform image2DArray stereo_image;
		//uniform int uBlockWidth; // width of a texture block (i.e. number of textures of array) 
	#endif

	// stereo output uniforms
	uniform mat4 uTextureToProjection_r; // UVR to right view's image coordinates
	//uniform bool uPauseStereo; // if true, no write commands are executed
#endif

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;	// ray sampling step size

#ifdef LEVEL_OF_DETAIL
	uniform float uLodMaxLevel; // factor with wich depth influences sampled LoD and step size 
	uniform float uLodBegin;    // depth at which lod begins to degrade
	uniform float uLodRange;    // depth at which lod begins to degrade
#endif

// auxiliary matrices
uniform mat4 uViewToTexture;
uniform mat4 uScreenToTexture;
uniform mat4 uProjection;

#ifdef SHADOW_SAMPLING
	uniform vec3 uShadowRayDirection; // simplified: in texture space
	uniform int uShadowRayNumSteps;
#endif
///////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Struct of a volume sample point
 */
struct VolumeSample
{
	float value; // scalar intensity
	vec3 uvw;  // uvw coordinates
};

/**
 * @brief Struct of a volume sample point
 */
struct RaycastResult
{
	vec4 color;  // end color
	vec4 firstHit; // position of first non-zero alpha hit
	vec4 lastHit; // position of last non-zero alpha hit
};

#ifdef EMISSION_ABSORPTION_RAW
	/**
	* @brief 'transfer-function' applied to value, no premultiplied alpha
	*/
	vec4 transferFunctionRaw(float value)
	{
		// linear mapping to grayscale color [0,1]
		float rel = (value - uWindowingMinVal) / uWindowingRange;
		float clamped =	max(0.0, min(1.0, rel));
		vec4 color = texture(transferFunctionTex, clamped);
		return color;
	}
#endif

#ifdef STEREO_SINGLE_PASS
	/**
	 * @brief store color value at specified image coordinate and layer
	 */
	void storeColor(vec4 sampleColor, ivec2 texelCoord_r
	#ifndef STEREO_SINGLE_OUTPUT
	, int layerIdx
	#endif
	)
	{
		#ifdef STEREO_SINGLE_OUTPUT
			//memoryBarrierImage();
			vec4 curColor = imageLoad( stereo_image, texelCoord_r );
			
			//TODO compute segment length from ray angle to view vector (multiply with uStepSize)

			//compute accumulated value
			curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
			curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;
			vec4 result_color = curColor;

			imageStore( stereo_image, texelCoord_r, result_color );
		#else 
			imageStore( stereo_image, ivec3(texelCoord_r, layerIdx), sampleColor );
		#endif
		//memoryBarrierImage();
	}

	/** @brief reproject provided texture coordinate into right image coordinate space [0..res]*/
	vec2 reprojectCoords(vec3 curUVW)
	{
		// reproject position
		vec4 pos_r = uTextureToProjection_r * vec4(curUVW, 1.0);
		pos_r /= pos_r.w;
		pos_r.xy = ( pos_r.xy / vec2(2.0) + vec2(0.5) );

		return vec2( imageSize( stereo_image ) ) * pos_r.xy;
	}
#endif

/**
* @brief 'transfer-function' applied to value.
* @param value to be mapped to a color
* @param stepSize used for alpha scaling
*/
vec4 transferFunction(float value, float stepSize)
{
	// linear mapping to grayscale color [0,1]
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= ALPHA_SCALE * stepSize;
	color.rgb *= COLOR_SCALE * (color.a);

	return color;
}

/**
 * @brief retrieve color for a front-to-back raycast between two points in the volume
 * 
 * @param startUVW start uvw coordinates
 * @param endUVW end uvw coordinates
 * @param stepSize of ray traversal
 * @param startDepth of ray (linearized or not)
 * @param endDepth of ray (linearized or not)
 * 
 * @return RaycastResult consisiting of accumulated color, first hit and last hit
 */
RaycastResult raycast(vec3 startUVW, vec3 endUVW, float stepSize, float startDepth, float endDepth
#ifdef STEREO_SINGLE_PASS
	#ifndef STEREO_SINGLE_OUTPUT
	, int layerIdx 
	#endif
#endif
)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
	
	RaycastResult result;
	result.color = vec4(0);
	result.firstHit = vec4(0.0);
	result.lastHit = vec4(endUVW, endDepth);
	vec4 curColor = vec4(0);

	#ifdef STEREO_SINGLE_PASS
		vec4 segmentColor_r = vec4(0);
		ivec2 texelCoord_r = ivec2( reprojectCoords( startUVW ) );
	#endif

	// traverse ray front to back rendering
	float t = 0.0;
	#ifdef RANDOM_OFFSET 
		t = 0.002 * 2.0 * rand( vec2(gl_GlobalInvocationID.xy) );
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		
		float curDepth = mix( startDepth, endDepth, t);

		VolumeSample curSample;
		curSample.uvw   = curUVW;
		
		#ifdef LEVEL_OF_DETAIL
			float curLod = min( max(0.0, min(1.0, ((curDepth - uLodBegin) / uLodRange) ) ) * uLodMaxLevel, float( textureQueryLevels( volume_texture ) - 1) ); // bad approximation, but idc for now
			float curStepSize = stepSize * pow(2.0, curLod);
			parameterStepSize = curStepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
			curSample.value = textureLod(volume_texture, curUVW, curLod).r;
		#else
			float curStepSize = stepSize;
			curSample.value = texture(volume_texture, curUVW).r;
		#endif

		// retrieve current sample
		#ifdef EMISSION_ABSORPTION_RAW
			float distanceStepSize = parameterStepSize * (endDepth - startDepth); // distance of a step
			vec4 sampleEmissionAbsorption = transferFunctionRaw( curSample.value );
			sampleEmissionAbsorption.rgb = sampleEmissionAbsorption.rgb * EMISSION_SCALE;
			sampleEmissionAbsorption.a = pow(sampleEmissionAbsorption.a * ABSORPTION_SCALE, 2.0); // to scale a bit within real numbers
			float sampleAlpha = 1.0 - exp( - sampleEmissionAbsorption.a * distanceStepSize );

			vec4 sampleColor = vec4(sampleEmissionAbsorption.rgb * (sampleAlpha), sampleAlpha);
		#else
			vec4 sampleColor = transferFunction(curSample.value, curStepSize );
		#endif

		t += parameterStepSize; // update running variable, then decide whether to shade or skip sample

		if (sampleColor.a < 0.00001) {
			#ifdef STEREO_SINGLE_PASS
			// reproject Coords, check whether image coords changed
			ivec2 curTexelCoord_r = ivec2( reprojectCoords( curUVW ) );
			if ( curTexelCoord_r.x > texelCoord_r.x ) //changed
			{
				// reproject color, then reset segment color
				for(int i = texelCoord_r.x; i < curTexelCoord_r.x; i++)
				{
					storeColor( segmentColor_r, ivec2(i, texelCoord_r.y)
						#ifndef STEREO_SINGLE_OUTPUT
						, layerIdx 
						#endif
					);
				}

				// update/reset for next segment
				texelCoord_r = curTexelCoord_r;
				segmentColor_r = vec4(0);
			}
			#endif
			continue; 
		} // skip invisible voxel

		#ifdef AMBIENT_OCCLUSION
			float occlusion = 0.0;	
			float numSamples = 8.0;
			for (int i = -1; i <= 1; i+= 2) {	for (int j = -1; j <= 1; j+= 2) { for (int k = -1; k <= 1; k+= 2)
			{	
				vec3 ao_uvw = curUVW + ( vec3(float(i), float(j), float(k)) ) * ( 2.0 * curStepSize );

				#ifdef LEVEL_OF_DETAIL
					float ao_value = textureLod(volume_texture, ao_uvw, curLod).r;
				#else
					float ao_value = texture(volume_texture, ao_uvw).r;
				#endif
			
				#ifdef EMISSION_ABSORPTION_RAW
					occlusion += 1.0 - exp( - (pow(transferFunctionRaw( ao_value ).a * ABSORPTION_SCALE, 2.0) ) * distanceStepSize );
				#else
					occlusion += transferFunction(ao_value, curStepSize ).a;
				#endif
			}}}
			occlusion -= sampleColor.a; // to remove "self-occlusion"

			sampleColor.rgb *= max(0.0, min( 1.0, 1.0 - (occlusion / numSamples)));
		#endif

		#ifdef SHADOW_SAMPLING
			float shadow = 0.0;
			for (int i = 1; i < uShadowRayNumSteps; i++)
			{	
				#ifdef LEVEL_OF_DETAIL
					float shadow_lod = max(1.0, min(uLodMaxLevel, curLod + 0.5 ) );
					float shadow_stepSize = stepSize * pow(2.0, shadow_lod + 1.0);
					vec3 shadow_sampleUVW = curUVW + uShadowRayDirection * ( float(i) * shadow_stepSize );
					float shadow_value = textureLod(volume_texture, shadow_sampleUVW, shadow_lod).r;
				#else
					float shadow_stepSize = curStepSize * 2.5;
					vec3 shadow_sampleUVW = curUVW + uShadowRayDirection * ( float(i) * shadow_stepSize );
					float shadow_value = texture(volume_texture, shadow_sampleUVW).r;
				#endif
			
				#ifdef EMISSION_ABSORPTION_RAW
					float shadow_parameterStepSize = shadow_stepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
					float shadow_distanceStepSize = shadow_parameterStepSize * (endDepth - startDepth); // distance of a step
					float shadow_alpha = (1.0 - exp( - (pow(transferFunctionRaw( shadow_value ).a * ABSORPTION_SCALE, 2.0) ) * shadow_distanceStepSize  * 2.0 ));
				#else
					float shadow_alpha = transferFunction(shadow_value, curStepSize ).a;
				#endif
				
				shadow = (1.0 - shadow) * shadow_alpha + shadow;
			}
			
			sampleColor.rgb *= max(0.25, min( 1.0, 1.0 - shadow));
		#endif

		#ifdef STEREO_SINGLE_PASS
			// reproject Coords, check whether image coords changed
			ivec2 curTexelCoord_r = ivec2( reprojectCoords( curUVW ) );
			if ( curTexelCoord_r.x > texelCoord_r.x ) //changed
			{
				// reproject color, then reset segment color
				for(int i = texelCoord_r.x; i < curTexelCoord_r.x; i++)
				{
					storeColor( segmentColor_r, ivec2(i, texelCoord_r.y)
						#ifndef STEREO_SINGLE_OUTPUT
						, layerIdx 
						#endif
					);
				}

				// update/reset for next segment
				texelCoord_r = curTexelCoord_r;
				segmentColor_r = vec4(0);
			}

			// accumulate segment colors
			segmentColor_r.rgb = (1.0 - segmentColor_r.a) * (sampleColor.rgb) + segmentColor_r.rgb;
			segmentColor_r.a = (1.0 - segmentColor_r.a) * sampleColor.a + segmentColor_r.a;
		#endif

		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;

		#ifdef FIRST_HIT// first hit?
			if (result.firstHit.a == 0.0 && sampleColor.a > 0.001)
			{
				result.firstHit.rgb = curUVW;
				result.firstHit.a = curDepth;
			} 
		#endif

		// early ray-termination
		if (curColor.a > ERT_THRESHOLD)
		{
			break;
		}
	}

	#ifdef STEREO_SINGLE_PASS
	storeColor( segmentColor_r, texelCoord_r // reproject last state of segmentColor
		#ifndef STEREO_SINGLE_OUTPUT
		, layerIdx 
		#endif
	);
	#endif

	// return emission absorption result
	result.color = curColor;
	return result;
}

/** 
*   @param screenPos screen space position in [0..1]
**/
vec4 getViewCoord( vec3 screenPos )
{
	vec4 unProject = inverse(uProjection) * vec4( (screenPos * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;
	
	return unProject;
}

void main()
{
	//uint cnt = atomicCounterIncrement( counter ); // number of invocation
	//int x = int(cnt) / imageSize( color_image ).y;// x coordinate 
	//int y = int(cnt) % imageSize( color_image ).y;    // y coordinate
	//ivec2 index = ivec2(x,y);
	ivec2 index = ivec2( gl_GlobalInvocationID.xy );

	ivec2 imageCoord = ivec2( imageSize( color_image ).x - index.x, index.y ); // right to left

	vec2 passUV = ( vec2( imageCoord ) + vec2(0.5) ) / vec2( imageSize( color_image ) );

	vec4 fragColor = vec4(0.0);
	vec4 fragFirstHit = vec4(0.0);
	float fragDepth = 1.0;

	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	if (uvwEnd.a == 0.0) { 
		return; 
	} //invalid pixel
	
	if( uvwStart.a == 0.0 ) // only back-uvws visible (camera inside volume)
	{
		uvwStart.xyz = (uScreenToTexture * vec4(passUV, 0, 1)).xyz; // clamp to near plane
	}

	#ifdef OCCLUSION_MAP
		vec4 clipFrustumFront = texture(occlusion_clip_frustum_front, passUV);
		clipFrustumFront.rgb = (uViewToTexture * getViewCoord( vec3(passUV, clipFrustumFront.a) ) ).xyz;
		vec4 firstHit = vec4(0.0);
		if ( clipFrustumFront.a > uvwStart.a ) // there is unknown volume space between proxy geom and frustum map
		{
			RaycastResult raycastResult = raycast( 
			uvwStart.rgb, 			// ray start
			clipFrustumFront.rgb,   // ray end
			uStepSize    			// sampling step size
			, length( getViewCoord(vec3(passUV, uvwStart.a) ).xyz)
			, length( getViewCoord(vec3(passUV, min(clipFrustumFront.a, uvwEnd.a) ) ).xyz)
			);

			fragColor = raycastResult.color;
			firstHit = raycastResult.firstHit;
		}

		//vec4 clipFrustumBack = texture(occlusion_clip_frustum_back, passUV);
		// not as important...

		// check uvw coords against occlusion map
		vec4 uvwOcclusion = texture(occlusion_map, passUV);
		if (uvwOcclusion.x < 1.0 && uvwOcclusion.x < uvwEnd.a && uvwOcclusion.x > uvwStart.a) // found starting point in front of back face but in back of front face
		{
			// compute uvw from depth value
			uvwStart.xyz = (uViewToTexture * getViewCoord( vec3(passUV, uvwOcclusion.x) ) ).xyz;
			uvwStart.a = uvwOcclusion.x;
		}
	#endif

	#ifdef SCENE_DEPTH
		// check uvw coords against scene depth map, 
		float scene_depth = texture(scene_depth_map, passUV).x;
		if (scene_depth < uvwStart.a) // fully occluded
		{
			fragDepth = uvwStart.a;
			return;
		}
		if (scene_depth < uvwEnd.a)
		{
			float endDepth = max(scene_depth, uvwStart.a);
			uvwEnd = uViewToTexture * getViewCoord( vec3(passUV, endDepth ) );
			uvwEnd.a = endDepth;
		}
	#endif

	// linearize depth
	float startDistance = length(getViewCoord(vec3(passUV,uvwStart.a)).xyz);
	float endDistance   = length(getViewCoord(vec3(passUV,uvwEnd.a)).xyz);
	//float startDistance = uvwStart.a;
	//float endDistance   = uvwEnd.a;

	#ifdef STEREO_SINGLE_PASS
		#ifndef STEREO_SINGLE_OUTPUT
		ivec3 texSize = imageSize(stereo_image);
		int layerIdx = texSize.z - ( int(passUV.x * texSize.x) % texSize.z ) - 1;
		#endif
	#endif
	
	// EA-raycasting
	RaycastResult raycastResult = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize    			// sampling step size
		, startDistance
		, endDistance
		#ifdef STEREO_SINGLE_PASS
			#ifndef STEREO_SINGLE_OUTPUT
				, layerIdx
			#endif
		#endif
		);

	// final color (front to back)
	fragColor.rgb = (1.0 - fragColor.a) * (raycastResult.color.rgb) + fragColor.rgb;
	fragColor.a   = (1.0 - fragColor.a) * raycastResult.color.a + fragColor.a;

	#ifdef FIRST_HIT
		#ifdef OCCLUSION_MAP
		if (firstHit.a != 0.0) // first hit happened even before raycasting within occlusion frustum
		{
			raycastResult.firstHit = firstHit;
		}
		#endif
		if (raycastResult.firstHit.a > 0.0)
		{
			fragFirstHit.xyz = raycastResult.firstHit.xyz; // uvw coords
			vec4 firstHitProjected = uProjection * inverse(uViewToTexture) * vec4( raycastResult.firstHit.xyz, 1.0);
			fragFirstHit.a = max( (firstHitProjected.z / firstHitProjected.w) * 0.5 + 0.5, 0.0 ); // ndc to depth
			
			fragDepth = max(fragFirstHit.a, 0.0);
		}
		else
		{
			fragFirstHit = uvwEnd;
			fragDepth = 1.0;
		}

		imageStore( fragFirst_image, imageCoord, vec4(fragDepth) );
	#endif

	imageStore( color_image, imageCoord, fragColor );
}
