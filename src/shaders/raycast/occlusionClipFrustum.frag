#version 430

//!< in-variables
in vec3 passUVWCoord;

//!< out-variables
layout(location = 0) out vec4 fragUVRCoordBack;
layout(location = 1) out vec4 fragUVRCoordFront;

void main()
{
	if(gl_FrontFacing) // front face
	{
		fragUVRCoordFront = vec4(passUVWCoord, gl_FragCoord.z); // alpha contains fragment depth
		fragUVRCoordBack  = vec4(0.0);
	}
	else // back face
	{
		fragUVRCoordBack = vec4(passUVWCoord, gl_FragCoord.z); // alpha contains fragment depth
		fragUVRCoordFront  = vec4(0.0);
	}
}
