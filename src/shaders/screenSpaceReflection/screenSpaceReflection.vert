//!< vertex shader
#version 430 
 
//!< in-vars
layout(location = 0) in vec3 vertex; 
layout(location = 1) in vec2 uv; 
 
//!< in-vars
 
//!< in-vars
out vec2 vert_UV; 
 
//!< main
void main(void) {	 
 vert_UV = uv; 
 gl_Position = vec4(vertex, 1.0); 
} 
