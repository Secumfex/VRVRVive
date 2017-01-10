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
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler3D volume_texture; // volume 3D integer texture sampler
//uniform isampler3D volume_texture; // volume 3D integer texture sampler

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size

uniform mat4 uProjection;
uniform mat4 uViewToTexture; 
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragDepth; // depth from layers 1 - 4
layout(location = 1) out vec4 fragColor1;
layout(location = 2) out vec4 fragColor2;
layout(location = 3) out vec4 fragColor3;
layout(location = 4) out vec4 fragColor4;
// depth buffer holds layer 0 depth (rgba is assumed vec4(0) anyway)

/**
 * @brief Struct of a volume sample point
 */
struct VolumeSample
{
	float value; // scalar intensity
	vec3 uvw;    // uvw coordinates
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
* @brief 'transfer-function' applied to value
* @return mapped color corresponding to value
*/
vec4 transferFunction(float value)
{
	// linear mapping to grayscale color [0,1]
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= ALPHA_SCALE * uStepSize;
	color.rgb *= (color.a);

	return color;
}

/**
* @brief returns Values for Emission (no premultiplied alpha color)
*/
vec4 transferFunctionEmissionAbsorption(float value)
{	
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));

	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= ALPHA_SCALE * uStepSize;
	return color;
}

/**
* @brief returns only the alpha value (transmittance) of 'transfer-function' applied to value
* @return mapped alpha corresponding to value
*/
float transferFunctionAlphaOnly(float value)
{	
	// linear mapping to grayscale color [0,1]
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	return ALPHA_SCALE * uStepSize * texture(transferFunctionTex, clamped).a;
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
RaycastResult raycast(vec3 startUVW, vec3 endUVW, float stepSize, float startDistance, float endDistance)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // necessary parametric steps to get from start to end

	RaycastResult result;
	result.color = vec4(0);
	result.firstHit = vec4(startUVW, endDistance);
	result.lastHit = vec4(endUVW, endDistance);

	////////////////// FIRST PASS: Compute Normalization constant /////////////////
	// Transmission k( /inf ) used for normalization
	float T_norm = 0.0;

	float t = 0.01;
	#ifdef RANDOM_OFFSET 
	t = t * 2.0 * rand(passUV);
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		float curDistance = mix(startDistance, endDistance, t);

		float sampleAlpha = transferFunctionAlphaOnly( texture(volume_texture, curUVW).r );
		T_norm = (1.0 - T_norm) * sampleAlpha + T_norm;

		// first hit? (implicitly: d_0)
		if (result.firstHit.a > curDistance && sampleAlpha > 0.0)
		{
			result.firstHit.rgb = curUVW;
			result.firstHit.a = curDistance;
		} 

		// early ray-termination
		if (T_norm > 0.99)
		{
			break;
		}

		t += parameterStepSize;
	}

	//////////////////// SECOND PASS: PERFORM EMISSION ABSORBTION and write to layers//////////////////////
	vec4 curColor = vec4(0);
	vec3 startUVW_ = result.firstHit.rgb; // skip ahead, because we can
	//parameterStepSize = stepSize / length(endUVW - startUVW_); // necessary parametric steps to get from start to end
	
	int currentLayer = 0; // to identify fbo output to write to
	int numLayers = 4;    // total number of Layers
	float currentLayerThreshold = (float(currentLayer) + 0.5) / float(numLayers); // y_i
	float lastDistance = result.firstHit.a; // distance where last layer ended

	t = 0.00;
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		float curDistance = mix(startDistance, endDistance, t);

		// retrieve current sample
		VolumeSample curSample;
		curSample.value = texture(volume_texture, curUVW).r;
		curSample.uvw   = curUVW;

		vec4 sampleColor = transferFunctionEmissionAbsorption(curSample.value);
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a   = (1.0 - curColor.a) * sampleColor.a     + curColor.a;

		// reached y_i?
		if ( (curColor.a * T_norm) > currentLayerThreshold) // inspect relative to total value
		{
			// compute the Beer-Lambert equivalent values
			float z   = curDistance - lastDistance;
			float T_i = exp( -curColor.a * z );
			float A   = -log(T_i) / z;
		}

		t += parameterStepSize;
	}

	// return emission absorbtion result
	result.color = curColor;
	return result;
}

void main()
{
	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	if (uvwStart.a == 0.0) { discard; } //invalid pixel

	// linearize depth
	float startDistance = abs(getViewCoord(vec3(passUV, uvwStart.a)).z);
	float endDistance   = abs(getViewCoord(vec3(passUV, uvwEnd.a)).z);

	// EA-raycasting
	RaycastResult raycastResult = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize    			// sampling step size
		, startDistance
		, endDistance
		);

	// final color
	fragColor1 = raycastResult.color;

	if (raycastResult.firstHit.a > 0.0)
	{
		vec4 firstHitProjected = uProjection * inverse(uViewToTexture) * vec4(raycastResult.firstHit.xyz, 1.0);
		gl_FragDepth = max( (firstHitProjected.z / firstHitProjected.w) * 0.5 + 0.5, 0.0 ); // ndc to depth
	}
}
