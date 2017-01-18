#version 430
 
// in-variables
layout(location = 0) in vec4 positionAttribute;
layout(location = 1) in vec3 uvwCoordAttribute;
layout(location = 2) in vec4 normalAttribute;

// uniforms
uniform mat4 uModel;
uniform mat4 uViewNovel;
uniform mat4 uProjection;

// out-variables
out vec3 passWorldPosition;
out vec3 passPosition;
out vec3 passUVWCoord; // 3-scalar uvs
out vec3 passWorldNormal;
out vec3 passNormal;

void main(){
    passUVWCoord = uvwCoordAttribute;
    vec4 worldPos = (uModel * positionAttribute);

    passWorldPosition = worldPos.xyz;
    passPosition = (uViewNovel * worldPos).xyz;
 
    gl_Position =  uProjection * uViewNovel * uModel * positionAttribute;

    passWorldNormal = normalize( ( transpose( inverse( uModel ) ) * normalAttribute).xyz );
	passNormal = normalize( ( transpose( inverse( uViewNovel * uModel ) ) * normalAttribute ).xyz );
}
