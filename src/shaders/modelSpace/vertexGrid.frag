#version 330

//!< in-variables
in vec3 passVertexColor;

//!< uniforms

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main()
{
	fragColor = vec4(passVertexColor, 1.0);
}
