#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
/*********** LIST OF POSSIBLE DEFINES ***********
	ALPHA_SCALE <float>
	AMBIENT_OCCLUSION
		AMBIENT_OCCLUSION_SCALE <float>
	CULL_PLANES
	COLOR_SCALE <float>
	CUBEMAP_SAMPLING
		CUBEMAP_STRENGTH <float>
	ERT_THRESHOLD <float>
	EMISSION_ABSORPTION_RAW
		EMISSION_SCALE <float>
		ABSORPTION_SCALE <float>
	FIRST_HIT
	LEVEL_OF_DETAIL
	RANDOM_OFFSET
	SCENE_DEPTH
	SHADOW_SAMPLING
		SHADOW_SCALE <float>
***********/

#ifndef ALPHA_SCALE
#define ALPHA_SCALE 20.0
#endif
#ifndef COLOR_SCALE
#define COLOR_SCALE 1.0
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

#ifdef AMBIENT_OCCLUSION
	#ifndef AMBIENT_OCCLUSION_SCALE
	#define AMBIENT_OCCLUSION_SCALE 2.0
	#endif

	#ifndef AMBIENT_OCCLUSION_RADIUS
	#define AMBIENT_OCCLUSION_RADIUS 2.0
	#endif
#endif

#ifdef SHADOW_SAMPLING
	#ifndef SHADOW_SCALE
	#define SHADOW_SCALE 1.0
	#endif
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

#ifdef CUBEMAP_SAMPLING
	uniform samplerCube cube_map;
	uniform int uNumSamples;
	#ifndef CUBEMAP_STRENGTH
		uniform float uCubemapStrength;
	#endif
	#ifndef RANDOM_OFFSET
	float rand(vec2 co) { //!< http://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
		return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
	}
	#endif
#endif

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

#ifdef LEVEL_OF_DETAIL
	uniform float uLodMaxLevel; // factor with wich depth influences sampled LoD and step size 
	uniform float uLodBegin;    // depth at which lod begins to degrade
	uniform float uLodRange;    // depth at which lod begins to degrade
#endif

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size

uniform mat4 uProjection;
uniform mat4 uScreenToView;
uniform mat4 uScreenToTexture;

#ifdef SHADOW_SAMPLING
	uniform vec3 uShadowRayDirection; // simplified: in texture space
	uniform int uShadowRayNumSteps;
#endif

#ifdef CULL_PLANES
	uniform vec3 uCullMin; // min UVR
	uniform vec3 uCullMax; // max UVR
#endif
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

vec4 validate(vec4 vec)
{
	vec4 result = vec;
	if ( isnan(result.r) ) { result.r = 0.0; }
	if ( isnan(result.g) ) { result.g = 0.0; }
	if ( isnan(result.b) ) { result.b = 0.0; }
	if ( isnan(result.a) ) { result.a = 0.0; }
	return result;
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
	color.rgb *= COLOR_SCALE * (color.a);

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

vec4 beerLambertColorTransmissionToEmissionAbsorption(vec3 C, float T, float z)
{
	// compute Absorption
	float A = -log( T ) / z; // invert equation to to get A

	// compute Emission
	vec3 E = C / (1.0 - T); // invert equation to to get E

	return vec4(E, A);
}


vec4 beerLambertEmissionAbsorptionToColorTransmission(vec3 E, float A, float z)
{
	float T = exp( -A * z);

	vec3 C = E * (1.0 - T);

	return vec4(C, T);
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
	result.firstHit = vec4(endUVW, endDistance);
	result.lastHit = vec4(endUVW, endDistance);

	//////////////////// Compute Emission Absorption coefficients and write to layers//////////////////////
	vec4 segmentColor = vec4(0);
	float curAlpha = 0.0; // keep track of kappa(d)
	vec4 curColor = vec4(0.0); // the raycasting result, for fun
	
	// declare some output variables (to get rid of if-else)
	float numLayers = 4.0;
	vec4  layerColor[4] =  vec4[4](0.0);
	float layerDepth[4] = float[4](0.0);
	float layerThresholds[4] = float[4](0.0);

	layerDepth[0] = startDistance; //entry
	layerDepth[1] = startDistance + 0.33 * ( endDistance - startDistance ); 
	layerDepth[2] = startDistance + 0.66 * ( endDistance - startDistance );
	layerDepth[3] = endDistance;

	// arbitrary thresholds
	layerThresholds[0] = 0.001;
	layerThresholds[1] = 0.05;
	layerThresholds[2] = 0.50;
	layerThresholds[3] = 999.0; // infinity

	float lastDistance = startDistance; // distance where last layer ended d_(i-1)
	float lastNonZero = 0.0;

	int currentLayer = 0; // to identify fbo output to write to

	float t = 0.001;
	#ifdef RANDOM_OFFSET 
	t = 0.002 * 2.0 * rand(passUV);
	#endif
	while( t < 1.0 + (0.5 * parameterStepSize) )
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		float curDistance = mix(startDistance, endDistance, t); // distance on ray

		#ifdef CULL_PLANES // lazy 
			if ( any( lessThan(curUVW, uCullMin) ) ||  any( greaterThan(curUVW, uCullMax) ) ) // outside bounds?
			{
				t += parameterStepSize;
				continue; // skip sample
			}
		#endif

		// retrieve current sample
		VolumeSample curSample;
		curSample.uvw   = curUVW;

		#ifdef LEVEL_OF_DETAIL
			float curLod = max(0.0, min(1.0, ((curDistance - uLodBegin) / uLodRange) ) ) * uLodMaxLevel; // bad approximation, but idc for now
			float curStepSize = stepSize * pow(2.0, curLod);
			parameterStepSize = curStepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
			distanceStepSize = parameterStepSize * (endDistance - startDistance); // distance of a step
			curSample.value = textureLod(volume_texture, curUVW, curLod).r;
		#else
			float curStepSize = stepSize;
			curSample.value = texture(volume_texture, curUVW).r;
		#endif

		// retrieve current sample
		#ifdef EMISSION_ABSORPTION_RAW
			vec4 sampleEmissionAbsorption = transferFunctionRaw( curSample.value );
			sampleEmissionAbsorption.rgb = sampleEmissionAbsorption.rgb * EMISSION_SCALE;
			sampleEmissionAbsorption.a = pow(sampleEmissionAbsorption.a * ABSORPTION_SCALE, 2.0); // to scale a bit within real numbers
			float sampleAlpha = 1.0 - exp( - sampleEmissionAbsorption.a * distanceStepSize );

			vec4 sampleColor = vec4(sampleEmissionAbsorption.rgb * (sampleAlpha), sampleAlpha);
		#else
			vec4 sampleColor = transferFunction(curSample.value, curStepSize );
		#endif

		t += parameterStepSize; // update running variable, then decide whether to shade or skip sample

		if (sampleColor.a < 0.00001) { continue; } // skip invisible voxel

		#ifdef AMBIENT_OCCLUSION
			float occlusion = 0.0;	
			float numSamples = 8.0;
			for (int i = -1; i <= 1; i+= 2) {	for (int j = -1; j <= 1; j+= 2) { for (int k = -1; k <= 1; k+= 2)
			{	
				vec3 ao_uvw = curUVW + ( vec3(float(i), float(j), float(k)) ) * ( (AMBIENT_OCCLUSION_RADIUS/1.414) * curStepSize );

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

			// top bottom left right front back
			numSamples += 6.0;
			for (int i = -1; i <= 1; i+= 2)
			{	
				vec3 ao_uvwX = curUVW + ( vec3(float(i), 0.0, 0.0) ) * ( AMBIENT_OCCLUSION_RADIUS * curStepSize );
				vec3 ao_uvwY = curUVW + ( vec3(0.0, float(i), 0.0) ) * ( AMBIENT_OCCLUSION_RADIUS * curStepSize );
				vec3 ao_uvwZ = curUVW + ( vec3(0.0, 0.0, float(i)) ) * ( AMBIENT_OCCLUSION_RADIUS * curStepSize );

				#ifdef LEVEL_OF_DETAIL
					float ao_valueX = textureLod(volume_texture, ao_uvwX, curLod).r;
					float ao_valueY = textureLod(volume_texture, ao_uvwY, curLod).r;
					float ao_valueZ = textureLod(volume_texture, ao_uvwZ, curLod).r;
				#else
					float ao_valueX = texture(volume_texture, ao_uvwX).r;
					float ao_valueY = texture(volume_texture, ao_uvwY).r;
					float ao_valueZ = texture(volume_texture, ao_uvwZ).r;
				#endif
			
				#ifdef EMISSION_ABSORPTION_RAW
					occlusion += 1.0 - exp( - (pow(transferFunctionRaw( ao_valueX ).a * ABSORPTION_SCALE, 2.0) ) * distanceStepSize );
					occlusion += 1.0 - exp( - (pow(transferFunctionRaw( ao_valueY ).a * ABSORPTION_SCALE, 2.0) ) * distanceStepSize );
					occlusion += 1.0 - exp( - (pow(transferFunctionRaw( ao_valueZ ).a * ABSORPTION_SCALE, 2.0) ) * distanceStepSize );
				#else
					occlusion += transferFunction(ao_valueX, curStepSize ).a;
					occlusion += transferFunction(ao_valueY, curStepSize ).a;
					occlusion += transferFunction(ao_valueZ, curStepSize ).a;
				#endif
			}

			occlusion -= sampleColor.a; // to remove "self-occlusion"
			occlusion *= AMBIENT_OCCLUSION_SCALE;

			sampleColor.rgb *= max(0.0, min( 1.0, 1.0 - (occlusion / numSamples)));
		#endif

		// shadow ray sampling
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
					float shadow_distanceStepSize = shadow_parameterStepSize * (endDistance - startDistance); // distance of a step
					float shadow_alpha = (1.0 - exp( - (pow(transferFunctionRaw( shadow_value ).a * ABSORPTION_SCALE, 2.0) ) * shadow_distanceStepSize  * 2.0 ));
				#else
					float shadow_alpha = transferFunction(shadow_value, curStepSize ).a;
				#endif
				
				shadow = (1.0 - shadow) * shadow_alpha + shadow;
			}

			shadow *= SHADOW_SCALE;
			
			sampleColor.rgb *= max(0.25, min( 1.0, 1.0 - shadow));
		#endif

		#ifdef CUBEMAP_SAMPLING
			vec4 cubemapColor = vec4(0.0);
			int num_opacity_samples = 4;
			for (int i = 0; i <  max(uNumSamples/ num_opacity_samples, 1); i++)
			{
				vec2 seed = vec2(i);
				vec3 cubemapSampleDir = normalize( vec3( 
					2.0 * rand(curUVW.xy + passUV + seed) - 1.0,
					2.0 * rand(curUVW.yz + passUV + seed) - 1.0, 
					2.0 * rand(curUVW.zx + passUV + seed) - 1.0 
					) );
				//cubemapSampleDir = normalize( vec3(curUVW.x, 1.0 - curUVW.z, curUVW.y) * 2.0 - 1.0);
				
				vec4 cubemapSampleColor = vec4(0.0);
				//cubemapSampleColor = texture(cube_map, cubemapSampleDir);
				//cubemapSampleColor = max(vec4(0), (cubemapSampleColor - 0.1) * 5.0);
				cubemapSampleColor = max(vec4(0), (vec4(cubemapSampleDir,0)) * 3.0);

				float shadow = 0.0;
				for (int j = 1; j <= min(uNumSamples - (i * num_opacity_samples), num_opacity_samples); j++)
				{
					vec3 opacitySampleUVW = curUVW + curStepSize * 1.0 * pow(2.0,float(j)) * vec3(cubemapSampleDir.x, (cubemapSampleDir.z), -cubemapSampleDir.y);
					
					#ifdef CULL_PLANES // lazy 
					if ( any( lessThan(opacitySampleUVW, uCullMin) ) ||  any( greaterThan(opacitySampleUVW, uCullMax) ) ) // outside bounds?
					{
						continue; // skip sample
					}
					#endif
					
					float shadow_alpha = transferFunction( 
						texture(volume_texture, opacitySampleUVW ).r, 
						curStepSize * 1.0 * pow(2.0,float(j))).a;
				
					shadow = (1.0 - shadow) * shadow_alpha + shadow;
				}
				shadow *= 0.9;

				float cubemapSampleStrength = 1.0 - shadow;
				cubemapSampleColor *= cubemapSampleStrength;
				cubemapColor += cubemapSampleColor;
				//cubemapColor += vec4(cubemapSampleDir,0.0) * cubemapSampleStrength;
			}
			cubemapColor /= float(max( uNumSamples / num_opacity_samples, 1));
			sampleColor.rgb = mix( sampleColor.rgb, cubemapColor.rgb * sampleColor.a,
			#ifdef CUBEMAP_STRENGTH
				CUBEMAP_STRENGTH
			#else
				uCubemapStrength
			#endif
			);
		#endif

		// update segment color
		segmentColor.rgb = (1.0 - segmentColor.a) * (sampleColor.rgb) + segmentColor.rgb;
		segmentColor.a   = (1.0 - segmentColor.a) * sampleColor.a     + segmentColor.a;
		
		//update kappa(d)
		curAlpha = (1.0 - curAlpha) * sampleColor.a + curAlpha;

		// reached y_i?
		float currentLayerThreshold = layerThresholds[currentLayer]; // y_i
		if ( (curAlpha ) >= currentLayerThreshold ) // F(d) >= y_i ? 
		{		
			//<<<< compute the Beer-Lambert equivalent values
			vec4 emissionAbsorption = beerLambertColorTransmissionToEmissionAbsorption(segmentColor.rgb, 1.0 - segmentColor.a, abs(curDistance - lastDistance));

			// DEBUG the full raycasting result
			vec4 colorTransmission = beerLambertEmissionAbsorptionToColorTransmission(emissionAbsorption.rgb, emissionAbsorption.a, abs(curDistance - lastDistance));
			curColor.rgb = (1.0 - curColor.a) * (colorTransmission.rgb) + curColor.rgb;
			curColor.a =  (1.0 - curColor.a) * (1.0 - colorTransmission.a) + curColor.a;

			//<<<< write to layer
			layerColor[currentLayer] = emissionAbsorption;
			layerDepth[currentLayer] = curDistance;

			//<<<< reset and update for next segment
			segmentColor = vec4(0.0);
			currentLayer = currentLayer + 1; // i+1
			lastDistance = curDistance; // d_(i)
		}
	}

	// make sure last layer gets filled
	layerDepth[currentLayer] = endDistance - ( 3.0 - float(currentLayer) ) * distanceStepSize;
	vec4 emissionAbsorption = beerLambertColorTransmissionToEmissionAbsorption(
		segmentColor.rgb,
		 1.0 - segmentColor.a,
		 abs(layerDepth[currentLayer] - lastDistance)
		 );
	layerColor[currentLayer] = emissionAbsorption;

	//<<<< write to layer

	// DEBUG the full raycasting result
	curColor.rgb = (1.0 - curColor.a) * (segmentColor.rgb) + curColor.rgb;
	curColor.a =  (1.0 - curColor.a) * segmentColor.a + curColor.a;

	//<<<< write to outputs
	fragColor1 = layerColor[0];
	fragDepth.x = layerDepth[0];

	fragColor2 = layerColor[1];
	fragDepth.y = layerDepth[1];

	fragColor3 = layerColor[2];
	fragDepth.z = layerDepth[2];

	fragColor4 = layerColor[3];
	fragDepth.w = layerDepth[3];

	//====== check for NaN! ===========//
	fragColor1 = validate(fragColor1);
	fragColor2 = validate(fragColor2);
	fragColor3 = validate(fragColor3);
	fragColor4 = validate(fragColor4);
	//=================================//

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

	if (uvwEnd.a == 0.0) {
		discard;
	} //invalid pixel

	if (uvwStart.a == 0.0) // only back-uvws visible (camera inside volume)
	{
		uvwStart.xyz = ( uScreenToTexture * vec4(passUV, 0, 1)).xyz; // clamp to near plane
	}
	// linearize depth
	float startDistance = length(getViewCoord(vec3(passUV, uvwStart.a)).xyz);
	float endDistance   = length(getViewCoord(vec3(passUV, uvwEnd.a)).xyz);

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
