#version 430

//!< in-variables
in vec3 passUVWCoord;
in vec3 passWorldPosition; // world space
in vec3 passPosition; // view space

//!< out-variables
layout(location = 0) out vec4 fragUVRCoordBack;
layout(location = 1) out vec4 fragUVRCoordFront;
layout(location = 2) out vec4 fragPosBack; // world space coords
layout(location = 3) out vec4 fragPosFront;// world space coords

#ifndef DEPTH_SCALE 
#define DEPTH_SCALE 5.0 
#endif
#ifndef DEPTH_BIAS 
#define DEPTH_BIAS 0.05
#endif

void main()
{
	if(gl_FrontFacing) // front face
	{
		fragUVRCoordFront = vec4(passUVWCoord, gl_FragCoord.z); // alpha contains fragment depth
		//fragUVRCoordFront = vec4(passUVWCoord, length(passPosition)/DEPTH_SCALE ); // alpha contains fragment (scaled) view space depth
		fragUVRCoordBack  = vec4(0.0);

		fragPosFront = vec4(passPosition, length(passPosition)/DEPTH_SCALE); 
		fragPosBack = vec4(0.0);
	}
	else // back face
	{
		fragUVRCoordBack = vec4(passUVWCoord, gl_FragCoord.z); // alpha contains fragment depth
		//fragUVRCoordBack = vec4(passUVWCoord, length(passPosition)/DEPTH_SCALE ); // alpha contains fragment (scaled) view space depth
		fragUVRCoordFront  = vec4(0.0);

		fragPosBack = vec4(passPosition, length(passPosition)/DEPTH_SCALE);
		fragPosFront = vec4(0.0);
	}
}
