#version 430

// in-variables
in vec2 passUV;

// textures
uniform sampler1D transferFunctionTex;
uniform sampler2D  back_uvw_map;   // uvw coordinates map of back  faces
uniform sampler2D front_pos_map;   // uvw coordinates map of front faces
uniform sampler2D  back_pos_map;   // uvw coordinates map of back  faces
uniform sampler2D front_uvw_map;   // uvw coordinates map of front faces
uniform sampler3D volume_texture; // volume 3D integer texture sampler
//uniform isampler3D volume_texture; // volume 3D integer texture sampler

// images
layout(binding = 0, rgba16f) restrict uniform image2DArray output_image;

////////////////////////////////     UNIFORMS      ////////////////////////////////
// color mapping related uniforms 
uniform float uWindowingRange;  // windowing value range
uniform float uWindowingMinVal; // windowing lower bound

// ray traversal related uniforms
uniform float uStepSize;		// ray sampling step size
uniform bool uWriteStereo;		// ray sampling step size
uniform int uBlockWidth;

// for reprojection
uniform mat4 viewprojection_r; // viewprojection of right view
// uniform mat4 bias_r; // bias matrix (projects into range 0..1)

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
 * @param curPosition to reproject into right view
 * @param sampleColor to accumulate into right view
 */
void reproject(vec4 sampleColor, ivec2 texelCoord_r, int layerIdx)
{
	//read old value
	vec4 curColor = imageLoad( output_image, ivec3(texelCoord_r, layerIdx) );

	//TODO compute segment length from ray angle to view vector (multiply with uStepSize)

	//compute accumulated value
	curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
	curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;
	vec4 result_color = curColor;

	//result_color = curColor + vec4( float(layerIdx)/64.0 ); // DEBUG

	// write into texture
	imageStore( output_image, ivec3(texelCoord_r, layerIdx), result_color );
}

vec2 reprojectCoords(vec3 curPos)
{
	// reproject position
	vec4 pos_r = viewprojection_r * vec4(curPos, 1.0);
	pos_r /= pos_r.w;
	pos_r.xy = ( pos_r.xy / vec2(2.0) + vec2(0.5) );

	return vec2( imageSize( output_image ) ) * pos_r.xy;
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
vec4 raycast(vec3 startUVW, vec3 endUVW, float stepSize, vec3 startPos, vec3 endPos, int layerIdx)
{
	float parameterStepSize = stepSize / length(endUVW - startUVW); // necessary parametric steps to get from start to end

	vec4 curColor = vec4(0);
	vec4 segmentColor_r = vec4(0);
	ivec2 texelCoord_r = ivec2( reprojectCoords( startPos ) );

	// traverse ray front to back rendering
	for (float t = 0.01; t < 1.0 + (0.5 * parameterStepSize); t += parameterStepSize)
	{
		vec3 curUVW = mix( startUVW, endUVW, t);
		vec3 curPos = mix( startPos, endPos, t);

		// retrieve current sample
		VolumeSample curSample;
		curSample.value = int( texture(volume_texture, curUVW).r );
		curSample.uvw   = curUVW;

		vec4 sampleColor = transferFunction(curSample.value);
		curColor.rgb = (1.0 - curColor.a) * (sampleColor.rgb) + curColor.rgb;
		curColor.a = (1.0 - curColor.a) * sampleColor.a + curColor.a;

		// reproject Coords, check whether image coords changed
		ivec2 curTexelCoord_r = ivec2( reprojectCoords( curPos ) );
		if ( curTexelCoord_r != texelCoord_r ) //changed
		{
			// reproject color, then reset segment color

			if (
			 uWriteStereo
			 //&& segmentColor_r.a > 0.005
			 ) // don't bother if nearly invisible
			{
				reproject( segmentColor_r, texelCoord_r, layerIdx );
			}
			texelCoord_r = curTexelCoord_r;
			segmentColor_r = vec4(0); // reset for next segment
		}

		// accumulate segment colors
		segmentColor_r.rgb = (1.0 - segmentColor_r.a) * (sampleColor.rgb) + segmentColor_r.rgb;
		segmentColor_r.a = (1.0 - segmentColor_r.a) * sampleColor.a + segmentColor_r.a;

		// early ray-termination
		if (curColor.a > 0.99)
		{
			break;
		}
	}

	// make sure to reproject the last state of segmentColor
	if(uWriteStereo)
	{
		reproject( segmentColor_r, ivec2( texelCoord_r ), layerIdx);
	}
	
	// return emission absorbtion result
	return curColor;
}

void main()
{
	// define ray start and end points in volume
	vec4 uvwStart = texture( front_uvw_map, passUV );
	vec4 uvwEnd   = texture( back_uvw_map,  passUV );

	// TODO this can be skipped, by reprojection using the depth value from .a cannel of uvw map
	vec4 posStart = texture( front_pos_map, passUV );
	vec4 posEnd   = texture( back_pos_map,  passUV );

	if (uvwStart.a == 0.0) { discard; } //invalid pixel

	float textureWidth = float( textureSize( front_uvw_map, 0 ).x );
	int layerIdx = (uBlockWidth - ( int(passUV.x * textureWidth) % uBlockWidth ) - 1 );

	// EA-raycasting
	vec4 color = raycast( 
		uvwStart.rgb, 			// ray start
		uvwEnd.rgb,   			// ray end
		uStepSize,    			// sampling step size
		posStart.xyz,
		posEnd.xyz,
		layerIdx
		);

	// final color
	fragColor = color;// * 0.5 + 0.5 * vec4(uBlockWidth); //debug

	// DEBUG reproject
	// reproject(fragColor, posStart.xyz);
}
