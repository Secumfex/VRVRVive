#version 430

//!< in-variables
layout(location = 0) in vec4 pos;
layout(location = 1) in vec2 uv;

//!< out-variables
out PointData {
	vec3 pos;
	vec2 uv;
} PointGeom;

void main() {
	PointGeom.pos = pos.xyz;
	PointGeom.uv  = uv;
	gl_Position = vec4(pos.xy, 0, 1);
}
