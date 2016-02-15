//!< gbuffer vertex shader
#version 430

//!< in-vars
//nach guido
//layout (location=0) in vec3 vertex;
//layout (location=1) in vec3 normal;
//layout (location=2) in vec2 uv;
//nach arend
layout(location = 0) in vec3 vertex;	//pos	//vec4
layout(location = 1) in vec2 uv;		//coord
layout(location = 2) in vec3 normal;			//vec4
layout(location = 3) in vec4 tangent;

//!< out-vars
out vec3 vert_wsPosition;
out vec3 vert_wsNormal;
out vec3 vert_vsPosition;
out vec3 vert_vsNormal;
out vec2 vert_UV;
out vec3 vert_vsEyeVector;
out vec3 vert_wsEyePosition;
out vec3 vert_wsEyeVector;

//!< uniforms
uniform vec3 camPosition;
//uniform vec3 camLookAt;
//uniform float camNearPlane;
//uniform float camFarPlane;

uniform mat4 wsNormalMatrix;
uniform mat4 vsNormalMatrix;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
//uniform mat4 MVPMatrix;	//not used!

//!< main
void main(void){	
	//pos, normals
	//ws
	vec3 wsPosition = ( model * vec4(vertex, 1.0f) ).xyz;
	vert_wsPosition = wsPosition; 
	vert_wsNormal = ( wsNormalMatrix * vec4(normal, 0.0f) ).xyz;
	//vs
	vec3 vsPosition = ( view * (model * vec4(vertex, 1.0f)) ).xyz;
	vert_vsPosition = vsPosition;
	vert_vsNormal = ( vsNormalMatrix * vec4(normal, 0.0f) ).xyz;
	//tex coords
	vert_UV = uv;	
	//eye vec
	vert_vsEyeVector = vert_vsPosition;
	vert_wsEyePosition = camPosition;
	vert_wsEyeVector = vert_wsPosition - camPosition;

	//vertex transform with MVP-mat
	gl_Position = projection * view * model * vec4(vertex, 1.0f);
}