#version 430

//!< in-variables
layout(location = 0) in vec4 pos; //-1 .. 1
layout(location = 1) in vec2 uv; // 0..1

//!< uniforms
uniform sampler2D depth_map;

uniform mat4 uProjection;
uniform mat4 uViewOld;
uniform mat4 uViewNew;

#ifdef STEREO_SINGLE_PASS
	uniform mat4 uViewOld_eye; //old view of current warping eye (equal to uViewOld for left view)
#endif
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
	vec4 worldPos = inverse(uViewOld) * getViewCoord(vec3(uv, depth));
	vec4 position = uProjection * uViewNew * worldPos;

	#ifdef STEREO_SINGLE_PASS
		// uViewOld: old left view, uViewNew: current right view, uViewOld_eye: old view of current eye,
		// reproject world position to right view
		vec4 uv_r = uProjection * uViewOld_eye * worldPos; // assume projection is the same
		uv_r /= uv_r.w;
		uv_r = (uv_r * 0.5) + 0.5;
		passUV  = uv_r.xy;
	#else
		passUV  = uv;
	#endif

	if (position.z < -position.w){ position.z = -position.w; }
	gl_Position = position;
}
