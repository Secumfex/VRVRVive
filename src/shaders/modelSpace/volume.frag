#version 430

// in-variables
in vec2 passImageCoord;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D  back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform isampler3D volume_texture; // volume 3D integer texture sampler

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound
//uniform float uWindowingMaxVal; // windowing upper bound

// ray traversal related uniforms
uniform float uRayParamStart;  // constrained sampling parameter intervall start
uniform float uRayParamEnd;	// constrained sampling parameter intervall end
uniform float uStepSize;		// ray sampling step size

/********************    EXPERIMENTAL PARAMETERS      ***********************/ 
uniform int  uMinValThreshold; // minimal value threshold for sample to be considered; deceeding values will be ignored  
uniform int  uMaxValThreshold; // maximal value threshold for sample to be considered; exceeding values will be ignored
/****************************************************************************/

//int rdm[36] = { 29, 35, 9, 3,18,23,25,10, 4, 5,19, 8,21, 6,26,16, 7,34,31,24, 1,27,12, 2,32,20,17,30,11,15,33,22,13,28, 0,14 };
//int rdm[16] = { 2, 1, 7, 6, 4, 10, 3, 0, 5, 11, 9, 14, 12, 13, 8, 15 };
//int rdm[9] = { 6,4,1,0,3,5,7,8,2 };
//uniform int uActivePixelIdx;

///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor;

/**
 * @brief Struct of a volume sample point
 */
struct VolumeSample
{
	int value; // scalar intensity
	vec3 uvw;  // uvw coordinates
};

/**
* @brief 'transfer-function' applied to value at a given distance to Camera.
* shifts towards one color or the other
* @param value to be mapped to a color
* @param depth parameter to shift towards front or back color
*
* @return mapped color corresponding to value at provided depth
*/
vec4 transferFunction(int value)
{
	// linear mapping to grayscale color [0,1]
	float rel = (float(value) - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= 10.0 * uStepSize;
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
vec4 raycast(vec3 startUVW, vec3 endUVW, float stepSize, int minValueThreshold, int maxValueThreshold)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // necessary parametric steps to get from start to end

	vec4 curColor = vec4(0);

	// traverse ray front to back rendering
	for (float t = 0.0; t < 1.0 + (0.5 * parameterStepSize); t += parameterStepSize)
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		
		// retrieve current sample
		VolumeSample curSample;
		curSample.value = texture(volume_texture, curUVW).r;
		curSample.uvw   = curUVW;

		/// experimental: ignore values exceeding or deceeding some thresholds
		if ( curSample.value > maxValueThreshold || curSample.value < minValueThreshold)
		{
			continue;
		}

		vec4 sampleColor = transferFunction(curSample.value);
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;
	}

	// return emission absorbtion result
	return curColor;
}


/**
 * @brief shifts the relative value closer to 0.5, based on provided distance
 * @param relVal the arbitrary, relative value in [0,1] to be mapped
 * @param dist distance to be used as mixing parameter
 * 
 * @return mapped value with decreased contrast
 */
float contrastAttenuationLinear(float relVal, float dist)
{	
	return  mix(relVal, 0.5, dist);
}

/**
 * @brief shifts the relative value closer to 0.5, based on provided distance. Alternative to above.
 * @param relVal the arbitrary, relative value in [0,1] to be mapped
 * @param dist distance to be used as mixing parameter, squared
 * 
 * @return mapped value with decreased contrast
 */
float contrastAttenuationSquared(float relVal, float dist)
{	
	float squaredDist = dist*dist;
	return  mix(relVal, 0.5, squaredDist);
}

void main()
{
	//// For fun: progressive sampling
	//int activePixelIdx = rdm[ (uActivePixelIdx % 9) ];
	//int thisPixelIdx = 0;
	//thisPixelIdx =  int(gl_FragCoord.x) % 3;
	//thisPixelIdx += (int(gl_FragCoord.y) % 3) * 3;
	//if ( thisPixelIdx != activePixelIdx )
	//{
	//	discard;
	//} 

	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passImageCoord );
	vec4 uvwEnd   = texture( back_uvw_map,  passImageCoord );

	// apply offsets to start and end of ray
	uvwStart.rgb = mix (uvwStart.rgb, uvwEnd.rgb, uRayParamStart);
	uvwEnd.rgb   = mix( uvwStart.rgb, uvwEnd.rgb, uRayParamEnd);

	// EA-raycasting
	vec4 color = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize,    			// sampling step size
		uMinValThreshold,	 // min value threshold 
		uMaxValThreshold);   // max value threshold
	
	// final color
	fragColor = color;
}
