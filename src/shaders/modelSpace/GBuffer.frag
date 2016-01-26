#version 430

//incoming data for the single textures
in vec3 passWorldPosition;
in vec3 passPosition;
in vec2 passUVCoord;
in vec3 passNormal;
in vec3 passTangent;

uniform vec4  color;
uniform float mixTexture;
uniform sampler2D tex;

uniform bool hasNormalTex;
uniform sampler2D normalTex;

//writable textures for deferred screen space calculations
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragNormal;
layout(location = 2) out vec4 fragPosition;
layout(location = 3) out vec4 fragUVCoord;
layout(location = 4) out vec4 fragWorldPos;
 
void main(){
	fragColor = color;
	vec3 normalView = passNormal;

	if ( mixTexture != 0.0)
	{
		fragColor = mix(color, texture(tex, passUVCoord), mixTexture );
	}

	if (hasNormalTex)
	{
		vec3 nrm = normalize(passNormal);
		vec3 tan = normalize(passTangent);
		vec3 binormalView = normalize(cross(nrm, tan));
		mat3 tangentSpaceView = mat3(
			tan.x,  tan.y,   tan.z,  // first column
			binormalView.x, binormalView.y,  binormalView.z, // second column
			nrm.x,   nrm.y,     nrm.z   // third column
		);
		vec3 normalTangentSpace = texture(normalTex, passUVCoord).xyz;

		normalView = tangentSpaceView * normalTangentSpace;
	}

	fragPosition = vec4(passPosition,1);
	fragUVCoord = vec4(passUVCoord,0,0);
	fragNormal = vec4(normalView,0);
}