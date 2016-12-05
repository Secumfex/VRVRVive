#version 330

/*
* This shader renders something on a screenfilling quad. 
* The renderpass needs a quad as VAO.
*/

//!< in-variables
in vec4 pos;

uniform vec4 uViewport;   // current viewport
uniform vec4 uResolution; // total screen size

//!< out-variables
out vec3 passPosition;
out vec2 passUV;
out vec2 passUVCoord;

void main() {
	passPosition = pos.xyz;
	passUV = pos.xy;
	passUVCoord = pos.xy;

	vec2 screenpos = pos.xy * uResolution.xy;
	screenpos = screenpos.xy - uViewport.xy;
	screenpos = screenpos.xy / (uViewport.zw);

	gl_Position = vec4(screenpos * 2 - 1, 0, 1);
}
