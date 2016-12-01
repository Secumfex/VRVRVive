#version 330

/*
* Simple fragmentshader that can modifie the alpha value of the fragcolor. 
*/

//!< in-variable
in vec3 passPosition;

//!< uniforms
uniform vec4 color;
uniform float blendColor;
uniform sampler2D tex;

//!< SLOW warping
uniform mat4 oldView;
uniform mat4 newView;
uniform mat4 projection; //used for both

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	// assume position is on farPlane (z := 1)
	vec4 pos = vec4( ( (passPosition.xy * 2.0) - 1.0), 1.0, 1.0);
	vec4 reprojected = inverse(projection) * pos;

	reprojected /= reprojected.w; //view new
	reprojected = projection * oldView * inverse(newView) * reprojected; //proj old
	
	reprojected /= reprojected.w; //proj old
	reprojected.xy = (reprojected.xy / 2.0) + 0.5;

	vec4 texColor = texture(tex, reprojected.xy);
    fragColor = fragColor = mix(color, texColor, blendColor);
}