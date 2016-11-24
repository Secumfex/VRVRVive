#version 330

 //!< in-variables
layout(location = 0) in vec4 positionAttribute;

//!< out-variables
out vec3 passVertexColor;

void main(){
	passVertexColor = vec3( float(gl_VertexID) / (512.0 * 512.0) );

	vec4 pos = vec4(1.0);
	pos.x = ((positionAttribute.x + 0.5) / 256.0 ) - 1.0 ;
	pos.y = ((positionAttribute.y + 0.5) / 256.0 ) - 1.0 ;
	pos.z = 0.0;
	gl_Position = pos;
}
