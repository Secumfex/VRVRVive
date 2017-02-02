#version 330

/*
* This shader renders a stencil mesh
*/

//!< in-variables
in vec2 pos;

//!< out-variables

void main() {
	gl_Position = vec4(pos.xy * 2.0 - 1.0, 0, 1);
}
