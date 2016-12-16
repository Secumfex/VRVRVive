#version 430

//!< in-variables
in vec2 passUV;

//!< uniforms
uniform sampler2D tex;

//!< out-variables
layout(location = 0) out vec4 fragColor;
void main()
{
	fragColor = texture(tex, passUV);
	// fragColor = vec4(passPosition,1);
}
