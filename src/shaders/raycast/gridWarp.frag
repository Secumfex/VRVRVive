#version 430

//!< in-variables
in vec2 passUV;
in vec3 passPosition;

//!< uniforms
uniform sampler2D tex;
uniform vec4 color;

//!< out-variables
layout(location = 0) out vec4 fragColor;
void main()
{
	fragColor = texture(tex, passUV);
	fragColor = (1.0 - fragColor.a) * color + fragColor;
	// fragColor = vec4(passPosition,1);
}
