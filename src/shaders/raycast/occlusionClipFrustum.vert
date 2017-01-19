#version 430
 
// in-variables
layout(location = 0) in vec4 positionAttribute;

// uniforms
uniform mat4 uModel; // the "pose" matrix of the frustum (i.e. inverse(view) )
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uWorldToTexture;

// out-variables
out vec3 passWorldPosition;
out vec3 passPosition;
out vec3 passUVWCoord; // 3-scalar uvs

void main(){
	// assume the model is a NDC cube (-1 .. 1), so just unproject the positions, to get the frustum's vertices
	vec4 vertexPosition = inverse(uProjection) * positionAttribute;
	vertexPosition /= vertexPosition.w; // will now go from -near to -far etc. (as if it was a mat4(1.0) view applied)
    
	// move it around
	vec4 worldPos = (uModel * vertexPosition);
    passWorldPosition = worldPos.xyz;

    passPosition = (uView * worldPos).xyz;

	passUVWCoord = (uWorldToTexture * worldPos).xyz;
 
    gl_Position =  uProjection * uView * worldPos;
}
