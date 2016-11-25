#version 430

//!< in-variables
in vec3 passVertexColor;

//!< uniforms
layout(binding = 0, rgba32f) readonly  uniform image2D input_image;
layout(binding = 1, rgba32f) restrict uniform image2D output_image;

uniform sampler2D depthTex;
uniform sampler2D posTex;
uniform sampler2D colorTex;
uniform mat4 inverseView;
uniform mat4 rightView;
uniform mat4 projection;

// specify atomic counter to save
layout(binding = 0, offset = 0) uniform atomic_uint counter;

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main()
{
	fragColor = vec4(passVertexColor, 1.0);

//*-------  REGULAR RENDERING ENDS HERE, THIS IS WHERE IMAGE STORING COMES INTO PLAY ------*//

	// assumed "window" size
	vec2 resolution = vec2( imageSize( output_image ) );

	// which number is this fragment's invocation?
	uint cnt = atomicCounterIncrement( counter );
	float relative = float(cnt) / (resolution.x * resolution.y); // shows that invocation index relates to gl_VertexID
	
	vec4 depth = texelFetch(depthTex, ivec2(gl_FragCoord.xy),0);
	vec4 pos   = texelFetch(posTex,   ivec2(gl_FragCoord.xy),0);
	vec4 color = texelFetch(colorTex, ivec2(gl_FragCoord.xy),0);

	pos = rightView * inverseView * pos;
	pos = projection * pos;
	pos/= pos.w;

	if (depth == 1.0)
	{
		pos.xy = ((gl_FragCoord.xy / resolution) * 2.0) - 1.0;
		pos.z = 1.0;
	}

	//vec4 reprojected_color = vec4( vec3(relative),1.0);
	vec4 reprojected_color = vec4( 0.25 * color.xy + (0.75 * relative), relative, 1.0 );
	
	// reproject a point to image from 'different' view
	vec4 result_color = reprojected_color;

	ivec2 right_texelCoord = ivec2( vec2( imageSize( output_image ) ) * ( pos.xy / vec2(2.0) + vec2(0.5)) );

	vec4 current = imageLoad(output_image, right_texelCoord);
	if (current.a == 0.0)
	{
		// write into texture
		imageStore( output_image, right_texelCoord, result_color );		
	}

}
