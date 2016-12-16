#version 430

#ifdef RANDOM_OFFSET 
float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}
#endif

// in-variables
in vec2 passUV;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler2D occlusion_map;   // uvw coordinates map of occlusion map
uniform sampler3D volume_texture; // volume 3D integer texture sampler
uniform sampler2D scene_depth_map;   // depth map of scene

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size
uniform float uLodDepthScale;  // factor with wich depth influences sampled LoD and step size 
uniform float uLodBias;        // depth at which lod begins to degrade

uniform mat4 uViewToTexture;
uniform mat4 uScreenToTexture;
uniform mat4 uProjection;
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragFirstHit;

/**
 * @brief Struct of a volume sample point
 */
struct VolumeSample
{
	int value; // scalar intensity
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

/**
* @brief 'transfer-function' applied to value at a given distance to Camera.
* shifts towards one color or the other
* @param value to be mapped to a color
* @param depth parameter to shift towards front or back color
*
* @return mapped color corresponding to value at provided depth
*/
vec4 transferFunction(int value, float stepSize)
{
	// linear mapping to grayscale color [0,1]
	float rel = (float(value) - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= 20.0 * stepSize;
	color.rgb *= (color.a);

	return color;
}

/**
 * @brief retrieve value for a maximum intensity projection	
 * 
 * @param startUVW start uvw coordinates
 * @param endUVW end uvw coordinates
 * @param stepSize of ray traversal
 * @param minValueThreshold to ignore values when deceeded (experimental parameter)
 * @param maxValueThreshold to ignore values when exceeded (experimental parameter)
 * 
 * @return sample point in volume, holding value and uvw coordinates
 */
RaycastResult raycast(vec3 startUVW, vec3 endUVW, float stepSize, float startDepth, float endDepth)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
	
	RaycastResult result;
	result.color = vec4(0);
	result.firstHit = vec4(0.0);
	result.lastHit = vec4(endUVW, endDepth);
	vec4 curColor = vec4(0);

	// traverse ray front to back rendering
	float t = 0.01;
	#ifdef RANDOM_OFFSET 
		t = t * 2.0 * rand(passUV);
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		
		float curDepth = mix( startDepth, endDepth, t);
		float curLod = max(0.0, uLodDepthScale * (curDepth - uLodBias)); // bad approximation, but idc for now
		float curStepSize = stepSize * pow(2.0, curLod);
		parameterStepSize = curStepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)

		// retrieve current sample
		VolumeSample curSample;
		curSample.value = int( textureLod(volume_texture, curUVW, curLod).r );
		curSample.uvw   = curUVW;

		vec4 sampleColor = transferFunction(curSample.value, curStepSize );
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;

		// early ray-termination
		if (curColor.a > 0.99)
		{
			break;
		}
		
		// first hit?
		if (result.firstHit.a == 0.0 && sampleColor.a > 0.001)
		{
			result.firstHit.rgb = mix( startUVW, endUVW, t - (3.0 * parameterStepSize)); // move towards camera a little bit
			result.firstHit.a = curDepth;
		} 

		t += parameterStepSize;
	}

	result.color = curColor;
	// return emission absorbtion result
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
	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	if (uvwEnd.a == 0.0) { 
		discard; 
	} //invalid pixel
	
	if( uvwStart.a == 0.0 ) // only back-uvws visible (camera inside volume)
	{
		uvwStart = uScreenToTexture * vec4(passUV, 0, 1); // clamp to near plane
		uvwStart.a = 0.0;
	}

	// check uvw coords against occlusion map
	vec4 uvwOcclusion = texture(occlusion_map, passUV);
	if (uvwOcclusion.x < 1.0 && uvwOcclusion.x < uvwEnd.a && uvwOcclusion.x > uvwStart.a) // found starting point in front of back face but in back of front face
	{
		// compute uvw from depth value
		uvwStart.xyz = (uViewToTexture * getViewCoord( vec3(passUV, uvwOcclusion.x) ) ).xyz;
		uvwStart.a = uvwOcclusion.x;
	}

	// check uvw coords against scene depth map, 
	float scene_depth = texture(scene_depth_map, passUV).x;
	if (scene_depth < uvwStart.a) 
	{
		gl_FragDepth = uvwStart.a;
		return;
	}
	if (scene_depth < uvwEnd.a)
	{
		float endDepth = max(scene_depth, uvwStart.a);
		uvwEnd = uViewToTexture * getViewCoord( vec3(passUV, endDepth ) );
		uvwEnd.a = endDepth;
	}

	// linearize depth //?
	//float startDistance = abs(getViewCoord(vec3(passUV,uvwStart.a)).z);
	//float endDistance   = abs(getViewCoord(vec3(passUV,uvwEnd.a)).z);
	float startDistance = uvwStart.a;
	float endDistance   = uvwEnd.a;

	// EA-raycasting
	RaycastResult raycastResult = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize    			// sampling step size
		, startDistance
		, endDistance
		);

	// final color
	fragColor = raycastResult.color;

	if (raycastResult.firstHit.a > 0.0)
	{
		fragFirstHit.xyz = raycastResult.firstHit.xyz; // uvw coords
		vec4 firstHitProjected = uProjection * inverse(uViewToTexture) * vec4( raycastResult.firstHit.xyz, 1.0);
		fragFirstHit.a = max( (firstHitProjected.z / firstHitProjected.w) * 0.5 + 0.5, 0.0 ); // ndc to depth
		
		gl_FragDepth = fragFirstHit.a;
	}
	else
	{
		fragFirstHit = uvwEnd;
		gl_FragDepth = 1.0;
	}
}
