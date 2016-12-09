#version 430

layout (points) in;
layout (triangle_strip, max_vertices = 36) out;
//layout (points, max_vertices = 36) out; //debug
//layout (line_strip, max_vertices = 36) out; //debug

out vec4 passUVWCoord;
out vec3 passWorldPosition; // world space
out vec3 passPosition; // view space

//!< samplers
uniform sampler2D first_hit_map;

//!< uniforms
uniform int uOcclusionBlockSize;
uniform vec4 uGridSize; // vec4(width, height, 1/width, 1/height)
uniform mat4 uScreenToView;
uniform mat4 uViewToNewViewProjection; // from old view to new projection space
uniform mat4 uViewToTexture;		   // from old view to texture space

#define MAX_DISTANCE 10.0
#define DEPTH_SCALE 10.0
#define DEPTH_BIAS 0.05

struct VertexData
{
	vec4 pos;
	vec4 uvw;
};

/** 
*   @param screenPos screen position in [0..1]
*   @param depth in view space [near..far] 
**/
vec4 getViewCoord( vec2 screenPos, float depth )
{
	return vec4(depth * normalize( ( uScreenToView * vec4(screenPos, 0.0, 1.0) ).xyz ), 1.0);
}

/** 
*   @param screenPos screen position in [0..1]
*   @param depth in view space [near..far] 
**/
VertexData getVertexData(vec2 screenPos, float depth)
{
	VertexData result;
	vec4 posView = getViewCoord(screenPos, depth);
	result.pos = uViewToNewViewProjection * posView;
	result.uvw = vec4((uViewToTexture * posView).xyz, length(posView.xyz) / DEPTH_SCALE);
	return result;
}

void emitVertex(VertexData vert)
{
	gl_Position = vert.pos;
	passUVWCoord= vert.uvw;
	EmitVertex();
}

void main()
{
	// point position
	vec4 pos = vec4(gl_in[0].gl_Position.xyz, 1.0);

	vec2 texSize = vec2(textureSize(first_hit_map, 0));
	vec2 screenPos = (uGridSize.zw * pos.xy); // gridSize -> 0..1
	ivec2 texCoord = ivec2( texSize * screenPos ); // 0..1 -> texSize

	//traverse neighbourhood, find texel with min depth value
	vec4 minSample = vec4(1.0, 0.0, 0.0, MAX_DISTANCE); //arbitrary color
	for (int i = 0; i < uOcclusionBlockSize; i++)
	{
		for (int j = 0; j < uOcclusionBlockSize; j++)
		{
			vec4 texel = texelFetch(first_hit_map, texCoord + ivec2(j,i), 0);
			if (texel.a != 0.0 && DEPTH_SCALE * texel.a < minSample.a)
			{
				minSample = texel;
			}
		}		
	}

	// define 8 corner vertices: projected position and uvw coordinates
	VertexData b00 = getVertexData( screenPos + vec2(0.0,				  0.0)				   / texSize, DEPTH_SCALE * minSample.a - DEPTH_BIAS);
	VertexData b10 = getVertexData( screenPos + vec2(uOcclusionBlockSize, 0.0)				   / texSize, DEPTH_SCALE * minSample.a - DEPTH_BIAS );
	VertexData b11 = getVertexData( screenPos + vec2(uOcclusionBlockSize, uOcclusionBlockSize) / texSize, DEPTH_SCALE * minSample.a - DEPTH_BIAS );
	VertexData b01 = getVertexData( screenPos + vec2(0.0,                 uOcclusionBlockSize) / texSize, DEPTH_SCALE * minSample.a - DEPTH_BIAS );

	VertexData t00 = getVertexData( screenPos + vec2(0.0,				  0.0)				   / texSize, MAX_DISTANCE );
	VertexData t10 = getVertexData( screenPos + vec2(uOcclusionBlockSize, 0.0)				   / texSize, MAX_DISTANCE );
	VertexData t11 = getVertexData( screenPos + vec2(uOcclusionBlockSize, uOcclusionBlockSize) / texSize, MAX_DISTANCE );
	VertexData t01 = getVertexData( screenPos + vec2(0.0,				  uOcclusionBlockSize) / texSize, MAX_DISTANCE );

	// TODO better utilize triangle_strip
	// emit 12 triangles defining the frustum
	// front
	emitVertex(b00);
	emitVertex(b10);
	emitVertex(b11);
	EndPrimitive();
	
	emitVertex(b11);
	emitVertex(b01);
	emitVertex(b00);
	EndPrimitive();

	// back
	emitVertex(t00);
	emitVertex(t01);
	emitVertex(t11);
	EndPrimitive();
	
	emitVertex(t11);
	emitVertex(t10);
	emitVertex(t00);
	EndPrimitive();

	// right
	emitVertex(b10);
	emitVertex(t10);
	emitVertex(t11);
	EndPrimitive();
	
	emitVertex(t11);
	emitVertex(b11);
	emitVertex(b10);
	EndPrimitive();

	// left
	emitVertex(t00);
	emitVertex(b00);
	emitVertex(b01);
	EndPrimitive();
	
	emitVertex(b01);
	emitVertex(t01);
	emitVertex(t00);
	EndPrimitive();
	

	// up
	emitVertex(b01);
	emitVertex(b11);
	emitVertex(t11);
	EndPrimitive();
	
	emitVertex(t11);
	emitVertex(t01);
	emitVertex(b01);
	EndPrimitive();
	
	// down
	emitVertex(b10);
	emitVertex(b00);
	emitVertex(t00);
	EndPrimitive();
	
	emitVertex(t00);
	emitVertex(t10);
	emitVertex(b10);
	EndPrimitive();
}