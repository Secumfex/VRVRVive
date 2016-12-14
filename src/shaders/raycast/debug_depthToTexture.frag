#version 430

// in-variables
in vec2 passUV;

// textures
uniform sampler2D depth_texture; // the depth texture

////////////////////////////////     UNIFORMS      ////////////////////////////////
uniform mat4 uProjection;    // used for unprojection
uniform mat4 uViewToTexture; // used for texture space transformation
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor; // a human-readable color

/** 
*   @param screenPos screen space position in [0..1]
**/
vec4 getViewCoord( vec3 screenPos )
{
	vec4 unProject = inverse(uProjection) * vec4( (screenPos * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;
	return unProject;
}

void main()
{
	float depth = texture( depth_texture, passUV ).x;
	
	if (depth == 1.0) // far plane, don't bother 
	{
		discard; 
	}

	vec4 posView = getViewCoord( vec3(passUV, depth) );
	fragColor = uViewToTexture * (posView);
}
