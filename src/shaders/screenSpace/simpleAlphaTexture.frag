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

uniform float transparency;

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	#ifdef ARRAY_TEXTURE
	vec4 texColor = texture(tex, vec3(passPosition.xy, layer));
	#else
	vec4 texColor = texture(tex, passPosition.xy);
	#endif

	//!< fragcolor gets transparency by uniform
    fragColor = vec4(texColor.rgb, texColor.a * (1.0 - transparency));
}