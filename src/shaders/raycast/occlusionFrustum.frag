#version 430

//!< in-variables
in vec3 passUVWCoord;
in vec3 passWorldPosition; // world space
in vec3 passPosition; // view space

//!< out-variables
layout(location = 0) out vec4 fragUVRCoordFront;
void main()
{
	fragUVRCoordFront = vec4(passUVWCoord, gl_FragCoord.z); // alpha contains fragment screen space depth
}
