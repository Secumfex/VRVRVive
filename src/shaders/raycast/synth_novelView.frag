#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passWorldPosition;

// textures
//uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces
//uniform sampler2D front_uvw_map;  // uvw coordinates map of front faces

uniform sampler2D depth0; // first hit, aka depth layer 0
uniform sampler2D depth;   // depth layers 1 to 4
uniform sampler2D layer1;  // Emission Absorption layer 1
uniform sampler2D layer2;  // Emission Absorption layer 2
uniform sampler2D layer3;  // Emission Absorption layer 3
uniform sampler2D layer4;  // Emission Absorption layer 4

uniform mat4 uProjection; //
uniform mat4 uViewNovel; // 
uniform mat4 uViewOld; // 

////////////////////////////////     UNIFORMS      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor; 

vec4 beerLambertEmissionAbsorptionToColorTransmission(vec3 E, float A, float z)
{
	float T = exp( -A * z);
	vec3 C = E * T;
	return vec4(C,T);
}

float depthToDistance(vec2 uv, float depth)
{
	vec4 unProject = inverse(uProjection) * vec4( (vec3(passUV, depth) * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;
	
	return length(unProject.xyz);
}

vec4 accumulateFrontToBack(vec4 color, vec4 sampleColor)
{
	return vec4( (1.0 - color.a) * (sampleColor.rgb) + color.rgb,
	             (1.0 - color.a) * sampleColor.a     + color.a);
}

void main()
{	
	// reproject entry point (image coordinates in old view)
	vec4 reprojected = uProjection * uViewOld * passWorldPosition;
	reprojected /= reprojected.w;
	reprojected.xyz = reprojected.xyz * 0.5 + 0.5; 
	
	// reproject (imaginary) exit point
	vec4 reprojectedBack = uProjection * uViewOld * inverse(view) * ( passPosition * 3.0 ); // arbitrary point that lies further back on the ray
	reprojectedBack /= reprojectedBack.w;
	reprojectedBack.xyz = reprojectedBack.xyz * 0.5 + 0.5; 

	// image plane direction
	vec3 startPoint = vec3( reprojected.xy, depthToDistance( reprojected.z ) );
	vec3 backPoint = vec3( reprojectedBack.xy, depthToDistance( reprojectedBack.z ) );
	vec3 rayDir = backPoint - startPoint; // no need to normalize
	
	// variables indicating the direction of the ray (which scalar coordinates will increment, which will decrement)
	vec3 stepDir = 
		  vec3( -1.0 ) * vec3( lessThan(		 rayDir, vec3(0.0) ) ) // scalars that will decrement
		+ vec3(  1.0 ) * vec3( greaterThanEqual( rayDir, vec3(0.0) ) );// scalars that will increment

	// sample first layer depth
	float d0 = texture(depth0, startPoint.xy).x;
	if (d0 == 1.0) { discard; } //invalid pixel
	d0 = depthToDistance(passUV, d0); // d0 is from depth-attachment, so convert to distance first

	//<<<< initialize result
	vec4 color = vec4(0.0); 

	
	//<<<< 3DDA Traversal
	
	// TODO loop dda until breaking condition
	//while(...)
	{
		// delta: size of current voxel (all positive), tDelta : parametric size of current voxel (all positive)
		vec3  delta  = vec3( 1.0 / textureSize(depth0, 0), (d0 - startPoint.z) ); // delta.z changes according to current Layer
		vec3  tDelta = (delta / rayDir) * stepDir; // all positive

		// tMax: maximum 
		vec3 tMax =
				  vec3( mod(startPoint, delta) ) * tDelta  // "lower border"
				+ vec3( equal(stepDir, 1.0)    ) * tDelta; // "upper border", if incrementing scalar
	

		// perform a step: first find min value, then increment only the tMax scalar to which the value correlates
		float tNext = min( tMax.x, min(tMax.y, tMax.z) ); // find nearest "border"
		tMax += vec3( equal( tMax, tNext ) ) * tDelta;    // vector component is 1.0 if correlates with tNext, 0.0 else --> only increments one scalar

		//<<<< Sampling
		
		// TODO compute next sampling position, new delta.z, tDelta.z, tMax.z, layer index

		// TODO first, simplified sampling: just sample ALL layers >.<, put into vector array

		//<<<< retrieve values // TODO dynamic sampling of sources
		vec4  sampleEA         = texture(layer1, samplePoint.xy);
		float sampleDistBack   = texture(depth,  samplePoint.xy).x;
		float sampleDistFront  = texture(depth0, samplePoint.xy).x;

		//<<<< compute colors
		vec4 sampleColor = beerLambertEmissionAbsorptionToColorTransmission( sampleEA.rgb, sampleEA.a, sampleDistBack - sampleDistFront);
		sampleColor.a = 1.0 - sampleColor.a; // turn T to alpha

		//<<<< compute pixel color, using front-to-back compositing
		accumulateFrontToBack(color, sampleColor);
	}

	fragColor = color;
}
