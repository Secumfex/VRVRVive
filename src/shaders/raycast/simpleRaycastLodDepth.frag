#version 430

// in-variables
in vec2 passUV;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D  back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler3D volume_texture; // volume 3D integer texture sampler
//uniform isampler3D volume_texture; // volume 3D integer texture sampler

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size
uniform float uLodDepthScale;  // factor with wich depth influences sampled LoD and step size 
uniform float uLodBias;        // depth at which lod begins to degrade
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
vec4 transferFunction(int value, float stepSize)
{
	// linear mapping to grayscale color [0,1]
	float rel = (float(value) - uWindowingMinVal) / uWindowingRange;
	float clamped =	max(0.0, min(1.0, rel));
	
	vec4 color = texture(transferFunctionTex, clamped);
	color.a *= 10.0 * stepSize;
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
vec4 raycast(vec3 startUVW, vec3 endUVW, float stepSize, float startDepth, float endDepth)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // parametric step size (scaled to 0..1)
	
	vec4 curColor = vec4(0);

	// traverse ray front to back rendering
	float t = 0.01;
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
		
		t += parameterStepSize;
	}

	// return emission absorbtion result
	return curColor;
}

void main()
{
	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	if (uvwStart.a == 0.0) { discard; } //invalid pixel

	// EA-raycasting
	vec4 color = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize    			// sampling step size
		, uvwStart.a
		, uvwEnd.a
		);

	// final color
	fragColor = color;// * 0.8 + 0.2 * uvwStart; //debug
}
