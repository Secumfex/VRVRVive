#version 430

layout (points) in;
//layout (triangles, max_vertices = 36) out;
layout (points, max_vertices = 36) out; //debug

//in vec2 passUVCoord; // must be an array

out vec3 passUVWCoord;
out vec3 passWorldPosition; // world space
out vec3 passPosition; // view space

uniform mat4 viewToTexture;

uniform sampler2D firstHitMap;
uniform int uOcclusionBlockSize;

#define MAX_DISTANCE 30.0

void main()
{
	// point position
	vec4 pos = vec4(gl_in[0].gl_Position.xyz, 1.0);

	// sample texture
	ivec2 texCoord = ivec2( vec2(textureSize( firstHitMap, 0 )) * (pos.xy / vec2(2.0) + vec2(0.5)) ); 
	vec4 texel = texelFetch(firstHitMap, texCoord, 0);
	// TODO traverse neighbourhood, find texel with min depth value
	
	for (int i = 0; i < uOcclusionBlockSize; i++)
	{
		for (int j = 0; j < uOcclusionBlockSize; j++)
		{
			texel = texelFetch(firstHitMap, texCoord + ivec2(j,i), 0);
			passUVWCoord = texel.xyz;
			gl_Position = pos + vec4( float(j) / 800.0, float(i) / 800.0 ,0,0); //debug
			EmitVertex();
		}		
	}


	// TODO define 8 corner vertices
	// TODO compute uvw coords

	// TODO emit 12 triangles defining the frustum
	// TODO 
	
	//passUVWCoord = texel.xyz;
	//gl_Position = pos;
	//EmitVertex();
}