#version 430

//!< in-variables
layout(location = 0) in vec4 pos; //-1 .. 1
layout(location = 1) in vec2 uv; // 0..1

//!< uniforms
uniform sampler2D depth_map;

uniform mat4 uProjection;
uniform mat4 uViewOld;
uniform mat4 uViewNew;

//!< out-variables
out vec2 passUV;

/** 
*   @param screenPos screen space position in [0..1]
**/
vec4 getViewCoord( vec3 screenPos )
{
	vec4 unProject = inverse(uProjection) * vec4( (screenPos * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;

	return unProject;
}

void main() {
	float depth = texture(depth_map, uv).x;
	vec4 position = uProjection * uViewNew * inverse(uViewOld) * getViewCoord(vec3(uv, depth));

	passUV  = uv;
	gl_Position = position;
}
