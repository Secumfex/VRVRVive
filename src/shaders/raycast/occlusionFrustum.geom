#version 430

layout (points) in;
//layout (triangles, max_vertices = 36) out;
layout (points, max_vertices = 36) out; //debug

//in vec2 passUVCoord; // must be an array

out vec3 passUVWCoord;
out vec3 passWorldPosition; // world space
out vec3 passPosition; // view space

uniform sampler2D firstHitMap;
uniform int uOcclusionBlockSize;
uniform vec4 uResolution;
uniform mat4 uScreenToView;
uniform mat4 uViewToNewViewProjection;

#define MAX_DISTANCE 30.0

vec4 getViewCoord( vec2 screenPos, float depth )
{
	return vec4(depth * normalize( ( uScreenToView * vec4(screenPos, 0.0, 1.0) ).xyz ), 1.0);
}

void main()
{
	// point position
	vec4 pos = vec4(gl_in[0].gl_Position.xyz, 1.0);

	vec2 texSize = vec2(textureSize(firstHitMap, 0));
	vec2 screenPos = (uResolution.zw * pos.xy); // 0..1
	vec2 ndcPos = screenPos * 2.0 - 1.0;

	// sample texture
	ivec2 texCoord = ivec2( texSize * screenPos );
	//vec4 texel = texelFetch(firstHitMap, texCoord, 0);

	//traverse neighbourhood, find texel with min depth value
	vec4 minSample = vec4(1.0, 1.0, 1.0, MAX_DISTANCE);
	for (int i = 0; i < uOcclusionBlockSize; i++)
	{
		for (int j = 0; j < uOcclusionBlockSize; j++)
		{
			vec4 texel = texelFetch(firstHitMap, texCoord + ivec2(j,i), 0);
			if (texel.a != 0.0 && texel.a < minSample.a)
			{
				minSample = texel;
			}
		}		
	}

	// DEBUG
	for (int i = 0; i < uOcclusionBlockSize; i++)
	{
		for (int j = 0; j < uOcclusionBlockSize; j++)
		{
			vec4 viewCoord = getViewCoord(screenPos + (vec2(j, i) / texSize), minSample.a);

			passUVWCoord = viewCoord;
			
			gl_Position = vec4(ndcPos, 0.0, 1.0) + vec4(2.0 * vec2(j, i) / texSize, 0, 0); //debug
			EmitVertex();
		}
	}

	// define 8 corner vertices //TODO project and stuff
	vec4 b00 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(0.0,				 0.0) / texSize,				 minSample.a);
	vec4 b10 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(uOcclusionBlockSize, 0.0) / texSize,				 minSample.a);
	vec4 b11 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(uOcclusionBlockSize, uOcclusionBlockSize) / texSize, minSample.a);
	vec4 b01 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(0.0,                 uOcclusionBlockSize) / texSize, minSample.a);

	vec4 t00 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(0.0,				 0.0) / texSize,				 MAX_DISTANCE);
	vec4 t10 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(uOcclusionBlockSize, 0.0) / texSize,				 MAX_DISTANCE);
	vec4 t11 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(uOcclusionBlockSize, uOcclusionBlockSize) / texSize, MAX_DISTANCE);
	vec4 t01 = uViewToNewViewProjection * getViewCoord(screenPos + vec2(0.0,				 uOcclusionBlockSize) / texSize, MAX_DISTANCE);

	// TODO compute uvw coords

	// TODO emit 12 triangles defining the frustum
	// TODO 
	
	//passUVWCoord = texel.xyz;
	//gl_Position = pos;
	//EmitVertex();
}