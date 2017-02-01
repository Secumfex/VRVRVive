#version 330

/*
* This shader renders a stencil mesh
*/

//!< in-variables
in vec2 pos;

//!< out-variables
out vec3 passPosition;
out vec2 passUV;
out vec2 passUVCoord;

void main() {
	passPosition = vec3(pos.xy,0);
	passUV = pos.xy * 0.5 + 0.5;
	passUVCoord = passUV;
	gl_Position = vec4(pos.xy, 0, 1);
}
