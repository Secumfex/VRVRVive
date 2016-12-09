#version 430

//!< in-variables
in vec4 passUVWCoord;
in vec3 passWorldPosition; // world space
in vec3 passPosition; // view space

//!< out-variables
layout(location = 0) out vec4 fragUVRCoordFront;
layout(location = 1) out vec4 fragDepth;
void main()
{
	fragUVRCoordFront = passUVWCoord; // alpha contains fragment (scaled) view space depth
	fragDepth = vec4(passUVWCoord.a);
}
