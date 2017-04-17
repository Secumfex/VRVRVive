#version 330

////////////////////////////////     DEFINES      ////////////////////////////////
/*********** LIST OF POSSIBLE DEFINES ***********
	ABSOLUTE_ERROR
	DISTRIBUTE_ALPHA //!< to encode fragColor as RGB-Image: adds ALPHA-Error to RGB channels, sets alpha to 1
	DSSIM_ERROR
	SQUARE_ERROR
***********/

//!< in-variable
in vec3 passPosition;

//!< uniforms
uniform sampler2D tex1;
uniform sampler2D tex2;

//!< out-variables
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragAvg;

#ifdef DSSIM_ERROR
	vec4 computeDSSIM( ivec2 coord )
	{
		vec4 avg1 = vec4(0.0);
		vec4 avg2 = vec4(0.0);
		vec4 var1 = vec4(0.0);
		vec4 var2 = vec4(0.0);
		vec4 cov  = vec4(0.0);
		const float dynR = 1.0;
		const float k1 = 0.01;
		const float k2 = 0.03;
		const int wX = 8;
		const int wY = 8;
		const float c1 = (k1 * dynR) * (k1 * dynR);
		const float c2 = (k2 * dynR) * (k2 * dynR);

		// sample window, compute average
		for (int i = 0; i < wY; i++) // row
		{	
			for( int j = 0; j < wX; j++) // col
			{
				avg1 += texelFetch( tex1, coord + ivec2(j,i), 0 );
				avg2 += texelFetch( tex2, coord + ivec2(j,i), 0 );
			}
		}
		avg1 /= float(wX * wY);
		avg2 /= float(wX * wY);

		// sample window again, compute variances
		for (int i = 0; i < wY; i++) // row
		{	
			for( int j = 0; j < wX; j++) // col
			{
				vec4 dev1 = (texelFetch( tex1, coord + ivec2(j,i), 0 )) - avg1;
				vec4 dev2 = (texelFetch( tex2, coord + ivec2(j,i), 0 )) - avg2;
				var1 += dev1 * dev1;
				var2 += dev2 * dev2;
				cov  += dev1 * dev2;
			}
		}
		var1 /= vec4(wX * wY);
		var2 /= vec4(wX * wY);
		cov  /= vec4(wX * wY);

		vec4 ssim  = (2.0 * avg1 * avg2 + c1) * (2.0 * cov + c2);
			 ssim /= (avg1 * avg1 + avg2 * avg2 + c1) * (var1 + var2 + c2);
		
		vec4 dssim = (1.0 - ssim) / 2.0;
		return dssim;
	}
#endif

void main() 
{
	vec4 texColor1 = texture(tex1, passPosition.xy);
	vec4 texColor2 = texture(tex2, passPosition.xy);

	vec4 diff = texture(tex2, passPosition.xy) - texture(tex1, passPosition.xy);

	vec4 error = diff;

	#ifdef ABSOLUTE_ERROR
		error = abs(diff);
	#endif

	#ifdef SQUARE_ERROR
		error = diff * diff;
	#endif

	// #ifdef SSIM_ERROR
	// 	error = ;
	// #endif

	#ifdef DSSIM_ERROR
		error = computeDSSIM( ivec2( vec2(textureSize( tex1, 0 )) * passPosition.xy + vec2(0.5) ) );
	#endif

	vec4 errorColor = error;
	float errorAvg = (errorColor.r + errorColor.g + errorColor.b + errorColor.a) / 4.0;

	#ifdef DISTRIBUTE_ALPHA
		error.rgb += error.a;
		error.a = 1.0;
	#endif

	//!< fragcolor gets transparency by uniform
    fragColor = errorColor;
    fragAvg = vec4(errorAvg);
}