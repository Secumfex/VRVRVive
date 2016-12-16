#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 14) out;
//layout (points, max_vertices = 36) out; //debug
//layout (line_strip, max_vertices = 36) out; //debug

in PointData {
	vec3 pos;
	vec2 uv;
} PointGeom[];

out vec3 passUVWCoord;
out vec3 passWorldPosition; // world space
out vec3 passPosition; // view space

//!< samplers
uniform sampler2D first_hit_map; // depth texture

//!< uniforms
uniform int  uOcclusionBlockSize;
uniform vec4 uGridSize; // vec4(width, height, 1/width, 1/height)
uniform mat4 uScreenToView; // const
uniform mat4 uProjection; // projection
uniform mat4 uFirstHitViewToCurrentView; // from old view to new projection space
uniform mat4 uFirstHitViewToTexture; // from old view to texture space

// some defines
#ifndef DEPTH_SCALE 
#define DEPTH_SCALE 5.0 
#endif
#ifndef DEPTH_BIAS 
#define DEPTH_BIAS 0.00 // something
#endif
#ifndef DEPTH_BIAS_VIEW 
#define DEPTH_BIAS_VIEW 0.03 // 3 cm
#endif
#ifndef NEAR_PLANE
#define NEAR_PLANE 0.1 // 10 cm
#endif
#define MAX_DEPTH 1.0

struct VertexData
{
	vec4 pos;
	vec3 uvw;
	vec3 posView;
};

/** 
*   @param screenPos screen space position in [0..1]
**/
vec4 getViewCoord( vec3 screenPos )
{
	vec4 unProject = inverse(uProjection) * vec4( (screenPos * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;

	return unProject;
}

/** 
*   @param screenPos screen space position in [0..1]
*   @return VertexData object with pos in NEW projection space, uvw and posView in NEW view space
**/
VertexData getVertexData(vec3 screenPos)
{
	vec4 posView = getViewCoord(screenPos); // position in first hit view space
	vec4 posNewView = uFirstHitViewToCurrentView * posView; // position in current view
	posNewView.z = min( -NEAR_PLANE, posNewView.z + DEPTH_BIAS_VIEW);
	vec4 posNewProj = uProjection * posNewView; // new view space position projected

	posNewProj.z = max( min( posNewProj.z / posNewProj.w, 1.0), -1.0 ) * posNewProj.w; // clamp to near/far plane to force fragment generation

	VertexData result;
	result.pos = posNewProj; // new view space position projected
	result.posView = posNewView.xyz;
	result.uvw = (uFirstHitViewToTexture * posView).xyz;
	return result;
}

void emitVertex(VertexData vert)
{
	//gl_PointSize = 1.5;
	gl_Position = vert.pos;
	passUVWCoord= vert.uvw;
	passPosition = vert.posView;
	EmitVertex();
}

void main()
{
	// point position
	vec4 pos = vec4( gl_in[0].gl_Position.xyz, 1.0 );
	
	vec2 texSize = vec2(textureSize(first_hit_map, 0));
	vec2 screenPos = ((uGridSize.zw) * (pos.xy + 0.5)); // gridSize -> 0..1
	ivec2 texCoord = ivec2( texSize * PointGeom[0].uv ); // 0..1 -> texSize

	//traverse neighbourhood, find texel with min depth value
	float minDepth = 1.0;
	for (int i = 0; i < uOcclusionBlockSize; i++)
	{
		for (int j = 0; j < uOcclusionBlockSize; j++)
		{
			if ( any( greaterThanEqual(texCoord + ivec2(j,i), texSize) ) ) { continue; } // outside texture
			vec4 texel = texelFetch(first_hit_map, texCoord + ivec2(j,i), 0);
			if ( texel.x < minDepth ) // valid (aka not 1.0) and nearer
			{
				minDepth = texel.x;
			}
		}		
	}

	if ( minDepth == 1.0 )
	{
		return; // stop, no valid samples found
	}

	// define 8 corner vertices: projected position and uvw coordinates // slightly enlarged and moved towards camera
	vec2 offsetMin = vec2(-4.0, -4.0) / texSize;
	vec2 offsetMax = vec2(uOcclusionBlockSize + 4.0,	uOcclusionBlockSize + 4.0) / texSize;
	VertexData b00 = getVertexData( vec3( screenPos + offsetMin,						max(minDepth - DEPTH_BIAS, DEPTH_BIAS)) );
	VertexData b10 = getVertexData( vec3( screenPos + vec2(offsetMax.x, offsetMin.y),	max(minDepth - DEPTH_BIAS, DEPTH_BIAS)) );
	VertexData b11 = getVertexData( vec3( screenPos + offsetMax,						max(minDepth - DEPTH_BIAS, DEPTH_BIAS)) );
	VertexData b01 = getVertexData( vec3( screenPos + vec2(offsetMin.x, offsetMax.y),	max(minDepth - DEPTH_BIAS, DEPTH_BIAS)) );

	VertexData t00 = getVertexData( vec3( screenPos + offsetMin,						MAX_DEPTH) );
	VertexData t10 = getVertexData( vec3( screenPos + vec2(offsetMax.x, offsetMin.y),	MAX_DEPTH) );
	VertexData t11 = getVertexData( vec3( screenPos + offsetMax,						MAX_DEPTH) );
	VertexData t01 = getVertexData( vec3( screenPos + vec2(offsetMin.x, offsetMax.y),	MAX_DEPTH) );

	// single triangle_strip
	emitVertex(b01); // 1
	emitVertex(b11); // 2
	emitVertex(b00); // 3
	emitVertex(b10); // 4
	emitVertex(t10); // 5
	emitVertex(b11); // 6
	emitVertex(t11); // 7
	emitVertex(b01); // 8
	emitVertex(t01); // 9
	emitVertex(b00); // 10
	emitVertex(t00); // 11
	emitVertex(t10); // 12
	emitVertex(t01); // 13
	emitVertex(t11); // 14
	EndPrimitive();
}