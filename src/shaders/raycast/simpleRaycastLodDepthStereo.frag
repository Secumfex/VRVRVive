#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
#ifndef ALPHA_SCALE
#define ALPHA_SCALE 20.0
#endif

#ifdef RANDOM_OFFSET 
float rand(vec2 co) { //!< http://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
	return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}
#endif

#ifndef ERT_THRESHOLD
#define ERT_THRESHOLD 0.99
#endif
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D  back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler3D volume_texture; // volume 3D integer texture sampler
//uniform isampler3D volume_texture; // volume 3D integer texture sampler

// images
layout(binding = 0, rgba16f) writeonly uniform image2DArray output_image; // output image

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size
uniform float uLodMaxLevel;  // factor with wich depth influences sampled LoD and step size 
uniform float uLodBegin;        // depth at which lod begins to degrade
uniform float uLodRange;        // depth at which lod begins to degrade

// auxiliary matrices
uniform mat4 uViewToTexture;
uniform mat4 uScreenToTexture;
uniform mat4 uProjection;

// stereo output uniforms
uniform mat4 uTextureToProjection_r; // UVR to right view's image coordinates
uniform bool uWriteStereo;
uniform int uBlockWidth;

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
* @brief 'transfer-function' applied to value.
* @param value to be mapped to a color
* @param stepSize used for alpha scaling
*/
vec4 transferFunction(int value, float stepSize)
{
	// linear mapping to grayscale color [0,1]
	float rel = (float(value) - uWindowingMinVal) / uWindowingRange;
	float clamped = max(0.0, min(1.0, rel));

	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= ALPHA_SCALE * stepSize;
	color.rgb *= (color.a);

	return color;
}

/**
 * @brief store color value at specified image coordinate and layer
 */
void reproject(vec4 sampleColor, ivec2 texelCoord_r, int layerIdx)
{
	imageStore( output_image, ivec3(texelCoord_r, layerIdx), sampleColor );
}

/** @brief reproject provided texture coordinate into right image coordinate space [0..res]*/
vec2 reprojectCoords(vec3 curPos)
{
	// reproject position
	vec4 pos_r = uTextureToProjection_r * vec4(curPos, 1.0);
	pos_r /= pos_r.w;
	pos_r.xy = ( pos_r.xy / vec2(2.0) + vec2(0.5) );

	return vec2( imageSize( output_image ) ) * pos_r.xy;
}

/**
 * @brief retrieve value for raycasting using provided parameters
 * 
 * @param startUVW start uvw coordinates
 * @param endUVW end uvw coordinates
 * @param stepSize of ray traversal
 * @param startDepth of ray (linearized or not)
 * @param endDepth of ray (linearized or not)
 * 
 * @return RaycastResult consisiting of accumulated color, first hit and last hit
 */
RaycastResult raycast(vec3 startUVW, vec3 endUVW, float stepSize, float startDepth, float endDepth, int layerIdx)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)

	RaycastResult result;
	result.color = vec4(0);
	result.firstHit = vec4(0.0);
	result.lastHit = vec4(endUVW, endDepth);
	vec4 curColor = vec4(0);
	vec4 segmentColor_r = vec4(0);
	ivec2 texelCoord_r = ivec2(reprojectCoords(startUVW));

	// traverse ray front to back rendering
	float t = 0.01;
	#ifdef RANDOM_OFFSET 
		t = t * 2.0 * rand(passUV);
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);

		float curDepth = mix(startDepth, endDepth, t);
		float curLod = max(0.0, min(1.0, ((curDepth - uLodBegin) / uLodRange))) * uLodMaxLevel;
		float curStepSize = stepSize * pow(2.0, curLod);
		parameterStepSize = curStepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)

		// retrieve current sample
		VolumeSample curSample;
		curSample.value = int( textureLod(volume_texture, curUVW, curLod).r );
		curSample.uvw   = curUVW;

		vec4 sampleColor = transferFunction(curSample.value, curStepSize);
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;
		
		// first hit?
		if (result.firstHit.a == 0.0 && sampleColor.a > 0.001)
		{
			result.firstHit.rgb = curUVW;
			result.firstHit.a = curDepth;
		}

		
		//TODO reproject Coords, check whether image coords changed
		ivec2 curTexelCoord_r = ivec2( reprojectCoords( curUVW ) );
		if ( curTexelCoord_r != texelCoord_r ) //changed
		{
			// reproject color, then reset segment color
			if (
			 uWriteStereo
			 //&& segmentColor_r.a > 0.005 // don't bother if almost invisible
			 )
			{
				for(int i = texelCoord_r.x; i < curTexelCoord_r.x; i++)
				{
					reproject( segmentColor_r, ivec2(i, texelCoord_r.y) , layerIdx );
				}
			}

			// update/reset for next segment
			texelCoord_r = curTexelCoord_r;
			segmentColor_r = vec4(0);
		}
		

		// accumulate segment colors
		segmentColor_r.rgb = (1.0 - segmentColor_r.a) * (sampleColor.rgb) + segmentColor_r.rgb;
		segmentColor_r.a = (1.0 - segmentColor_r.a) * sampleColor.a + segmentColor_r.a;

		// early ray-termination
		if (curColor.a > ERT_THRESHOLD)
		{
			break;
		}

		t += parameterStepSize;
	}

	// make sure to reproject the last state of segmentColor
	if(uWriteStereo)
	{
		reproject( segmentColor_r, ivec2( texelCoord_r ), layerIdx);
	}
	
	// return emission absorbtion result
	result.color = curColor;
	return result;
}

/**
*   @param screenPos screen space position in [0..1]
**/
vec4 getViewCoord(vec3 screenPos)
{
	vec4 unProject = inverse(uProjection) * vec4((screenPos * 2.0) - 1.0, 1.0);
	unProject /= unProject.w;

	return unProject;
}

void main()
{
	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	if (uvwEnd.a == 0.0) //invalid pixel
	{
		discard;
	}

	if (uvwStart.a == 0.0) // only back-uvws visible (camera inside volume)
	{
		uvwStart = uScreenToTexture * vec4(passUV, 0, 1); // clamp to near plane
		uvwStart.a = 0.0;
	}

	// this ray's write-to layer idx
	//float textureWidth = float( textureSize( front_uvw_map, 0 ).x );
	int layerIdx = (uBlockWidth - ( int(gl_FragCoord.x-0.5) % uBlockWidth ) - 1 );

	// linearize depth
	float startDistance = abs(getViewCoord(vec3(passUV, uvwStart.a)).z);
	float endDistance = abs(getViewCoord(vec3(passUV, uvwEnd.a)).z);

	// EA-raycasting
	RaycastResult raycastResult = raycast(
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize    			// sampling step size
		, startDistance         // (linearized) start depth
		, endDistance           // (linearized) end depth
		, layerIdx				// write-to layer idx
		);

	// final color
	fragColor = raycastResult.color;

	if (raycastResult.firstHit.a > 0.0)
	{
		fragFirstHit.xyz = raycastResult.firstHit.xyz; // uvw coords
		vec4 firstHitProjected = uProjection * inverse(uViewToTexture) * vec4(raycastResult.firstHit.xyz, 1.0);
		fragFirstHit.a = max((firstHitProjected.z / firstHitProjected.w) * 0.5 + 0.5, 0.0); // ndc to depth

		gl_FragDepth = fragFirstHit.a;
	}
	else
	{
		fragFirstHit = uvwEnd;
		gl_FragDepth = 1.0;
	}

	// DEBUG reproject
	 //reproject(fragColor, uvwStart.xyz);
	 //reproject(vec4(float(layerIdx)/32), ivec2(reprojectCoords(uvwStart.xyz)), layerIdx);
}
