#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
#ifndef ALPHA_SCALE
#define ALPHA_SCALE 20.0
#endif

#ifndef EMISSION_SCALE
#define EMISSION_SCALE 1.0
#endif

#ifndef ABSORPTION_SCALE
#define ABSORPTION_SCALE 10.0
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
uniform mat4 uScreenToView; 
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragDepth; // depth from layers 1 - 4
layout(location = 1) out vec4 fragColor1;
layout(location = 2) out vec4 fragColor2;
layout(location = 3) out vec4 fragColor3;
layout(location = 4) out vec4 fragColor4;
layout(location = 5) out vec4 fragColorDebug;
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

/**
* @brief 'transfer-function' applied to value
* @return mapped color corresponding to value
*/
vec4 transferFunction(float value, float stepSize)
{
	// linear mapping to grayscale color [0,1]
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= ALPHA_SCALE * stepSize;
	color.rgb *= (color.a);

	return color;
}

/**
* @brief returns only the alpha value (transmittance) of 'transfer-function' applied to value
* @return mapped alpha corresponding to value
*/
float transferFunctionAlphaOnly(float value, float stepSize)
{	
	// linear mapping to grayscale color [0,1]
	float rel = (value - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	return ALPHA_SCALE * stepSize * texture(transferFunctionTex, clamped).a;
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

vec4 beerLambertColorToEmissionAbsorption(vec3 C, float T, float z)
{
	// compute Absorption
	float A = -log( T ) / z; // invert equation to to get A

	// compute Emission
	vec3 E = C / T; // invert equation to to get E

	return vec4(E, A);
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
	float distanceStepSize = parameterStepSize * (endDistance - startDistance); // distance of a step

	RaycastResult result;
	result.color = vec4(0);
	result.firstHit = vec4(startUVW, endDistance);
	result.lastHit = vec4(endUVW, endDistance);

	////////////////// FIRST PASS: Compute Normalization constant /////////////////
	// Transmission kappa( /inf ) used for normalization
	float maxAlpha = 0.0;

	float t = 0.001;
	#ifdef RANDOM_OFFSET 
	t = t * 2.0 * rand(passUV);
	#endif
	while (t < 1.0 + (0.5 * parameterStepSize))
	{
		vec3 curUVW = mix(startUVW, endUVW, t);
		float curDistance = mix(startDistance, endDistance, t);

		float sampleAbsorption = transferFunctionRaw(texture(volume_texture, curUVW).r).a; // scale to 0..10, then pow
		sampleAbsorption = pow(sampleAbsorption * ABSORPTION_SCALE, 2.0);

		float sampleTransmittance = 1.0 - exp( - sampleAbsorption * distanceStepSize );
		float sampleAlpha = sampleTransmittance;

		maxAlpha = (1.0 - maxAlpha) * sampleAlpha + maxAlpha;

		// first hit? (implicitly: d_0)
		if (result.firstHit.a > curDistance && sampleAlpha > 0.0)
		{
			result.firstHit.rgb = curUVW;
			result.firstHit.a = curDistance;
		}

		// early ray-termination
		if (maxAlpha > 0.99)
		{
			break;
		}

		t += parameterStepSize;
	}

	//////////////////// SECOND PASS: Compute Emission Absorbtion coefficients and write to layers//////////////////////
	vec4 segmentColor = vec4(0);
	float curAlpha = 0.0f; // keep track of kappa(d)
	vec4 curColor = vec4(0.0); // the raycasting result, for fun
	
	// declare some output variables (to get rid of if-else)
	vec4 layerColor[4] = vec4[4](0.0);
	float layerDepth[4] = float[4](0.0);

	// TODO debug
	//vec3 startUVW_ = result.firstHit.rgb; // skip ahead, because we can
	//parameterStepSize = stepSize / length(endUVW - startUVW_); // necessary parametric steps to get from start to end
	
	float lastDistance = result.firstHit.a; // distance where last layer ended d_(i-1)
	float lastAlpha = 0.0;
	int currentLayer = 1; // to identify fbo output to write to
	int numLayers = 4;    // total number of Layers (excluding first depth layer d_0)

	float currentLayerThreshold = ( float(currentLayer) ) / float(numLayers); // y_i

	t = 0.001;
	#ifdef RANDOM_OFFSET 
	t = t * 2.0 * rand(passUV);
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		float curDistance = mix(startDistance, endDistance, t); // distance on ray

		// retrieve current sample
		VolumeSample curSample;
		curSample.value = texture(volume_texture, curUVW).r;
		curSample.uvw   = curUVW;

		vec4 sampleEmissionAbsorption = transferFunctionRaw(texture(volume_texture, curUVW).r);
		sampleEmissionAbsorption.rgb = sampleEmissionAbsorption.rgb * EMISSION_SCALE;
		sampleEmissionAbsorption.a = pow(sampleEmissionAbsorption.a * ABSORPTION_SCALE, 2.0); // to scale a bit within real numbers

		float sampleTransmittance = 1.0 - exp( - sampleEmissionAbsorption.a * distanceStepSize );
		vec4 sampleColor = vec4(sampleEmissionAbsorption.rgb * (sampleTransmittance), sampleTransmittance);

		// update segment color
		segmentColor.rgb = (1.0 - segmentColor.a) * (sampleColor.rgb) + segmentColor.rgb;
		segmentColor.a   = (1.0 - segmentColor.a) * sampleColor.a     + segmentColor.a;
		
		//update kappa(d)
		curAlpha = (1.0 - curAlpha) * sampleColor.a + curAlpha;

		// reached y_i?
		if ( (curAlpha / maxAlpha) >= currentLayerThreshold) // F(d) = kappa(d) / kappa( /inf )   >= y_i ? 
		{
			//<<<< compute the Beer-Lambert equivalent values
			vec4 emissionAbsorption = beerLambertColorToEmissionAbsorption(segmentColor.rgb, segmentColor.a - lastAlpha, curDistance - lastDistance);

			//<<<< write to layer
			layerColor[currentLayer - 1] = emissionAbsorption;
			layerDepth[currentLayer - 1] = curDistance;

			//<<<< reset and update for next segment
			segmentColor = vec4(0.0);
			currentLayer = currentLayer + 1; // i+1
			currentLayerThreshold = ( float(currentLayer) ) / float(numLayers);
			lastDistance = curDistance; // d_(i)
		}

		// DEBUG the full raycasting result
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = curAlpha;

		t += parameterStepSize;
	}

	// make sure last layer gets filled
	if (layerDepth[3] == 0.0)
	{
		vec4 emissionAbsorption = beerLambertColorToEmissionAbsorption(segmentColor.rgb, segmentColor.a - lastAlpha, endDistance - lastDistance);

		//<<<< write to layer
		layerColor[3] = emissionAbsorption;
		layerDepth[3] = endDistance;
	}

	//<<<< write to outputs
	fragColor1 = layerColor[0];
	fragDepth.x = layerDepth[0];

	fragColor2 = layerColor[1];
	fragDepth.y = layerDepth[1];

	fragColor3 = layerColor[2];
	fragDepth.z = layerDepth[2];

	fragColor4 = layerColor[3];
	fragDepth.w = layerDepth[3];

	result.color = curColor; //DEBUG
	return result;
}

float distanceToDepth(vec2 uv, float dist)
{
	vec4 pView = uScreenToView * vec4(passUV, 0.0, 1.0);
	vec4 projected = uProjection * vec4( normalize(pView.xyz) * dist, 1.0 );
	projected.z = projected.z / projected.w;
	return (0.5 * projected.z) + 0.5;
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

	// DEBUG
	fragColorDebug = raycastResult.color;

	// first hit texture
	if (raycastResult.firstHit.a > 0.0)
	{
		//vec4 firstHitProjected = uProjection * inverse(uViewToTexture) * vec4(raycastResult.firstHit.xyz, 1.0);
		//gl_FragDepth = max( (firstHitProjected.z / firstHitProjected.w) * 0.5 + 0.5, 0.0 ); // ndc to depth
		
		gl_FragDepth = distanceToDepth(passUV, raycastResult.firstHit.a);
		//fragColorDebug = vec4( distanceToDepth(passUV, raycastResult.firstHit.a) );
	}
}
