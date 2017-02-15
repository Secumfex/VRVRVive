#version 330

/*
* Simple fragmentshader that can modifie the alpha value of the fragcolor. 
*/

//!< in-variable
in vec3 passPosition;

//!< uniforms
#ifdef ARRAY_TEXTURE
uniform sampler2DArray tex;
uniform float layer;
#else
uniform sampler2D tex;
#endif

#ifdef LEVEL_OF_DETAIL
uniform float level;
#endif

uniform float transparency;

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	vec4 texColor =
	#ifdef LEVEL_OF_DETAIL
		textureLod(tex,
	#else
		texture(tex,
	#endif
	#ifdef ARRAY_TEXTURE
		vec3(passPosition.xy, layer)
	#else
		passPosition.xy
	#endif
	#ifdef LEVEL_OF_DETAIL
		, level
	#endif
	);		

	// #ifdef ARRAY_TEXTURE
	// vec4 texColor = textureLod(tex, vec3(passPosition.xy,level), 4.0);
	// #else
	// vec4 texColor = textureLod(tex, passPosition.xy, 4.0);
	// #endif

	//!< fragcolor gets transparency by uniform
    fragColor = vec4(texColor.rgb, texColor.a * (1.0 - transparency));
}