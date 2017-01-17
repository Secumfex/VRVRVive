#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec3 passUV;
in vec3 passWorldPosition;
in vec3 passPosition;

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

//!< compute color and transmission values from emission and absorption coefficients and distance traveled through medium
vec4 beerLambertEmissionAbsorptionToColorTransmission(vec3 E, float A, float z)
{
	float T = exp( -A * z);
	vec3 C = E * T;
	return vec4(C,T);
}

//!< compute distance value from screen coordinates (uv + depth)
float depthToDistance(vec2 uv, float depth)
{
	vec4 unProject = inverse(uProjection) * vec4( (vec3(uv, depth) * 2.0) - 1.0 , 1.0);
	unProject /= unProject.w;
	
	return length(unProject.xyz);
}

//!< simple Front-To-Back blending
vec4 accumulateFrontToBack(vec4 color, vec4 backColor)
{
	return vec4( (1.0 - color.a) * (backColor.rgb) + color.rgb,
	             (1.0 - color.a) * backColor.a     + color.a);
}

//!< DEBUG simple, but expensive condition method to check whether samplepoint is valid
bool isInVolume(vec3 samplePoint)
{
	// check position compared to last layer
	bool onScreen = all( greaterThanEqual( samplePoint.xy, vec2(0.0) ) && lessThanEqual( samplePoint.xy, vec2(1.0) ) );
	bool inRange  = ( samplePoint.z <= texture(depth, samplePoint.xy).w ); // infront of "last depth"
	return (onScreen && inRange);
}

//!< DEBUG simple, but expensive update method to retrieve ea values
vec4[5] readTexturesEA( vec2 uv ) // TODO THIS IS ONLY DEBUG!!
{
	vec4[5] layerEA = vec4[5](0.0);
	layerEA[1] = texture(layer1, uv);
	layerEA[2] = texture(layer2, uv);
	layerEA[3] = texture(layer3, uv);
	layerEA[4] = texture(layer4, uv);
	return layerEA;
}

//!< DEBUG simple, but expensive update method to retrieve depth values
float[5] readTexturesDepth( vec2 uv )
{
	float[5] depths = float[5](0.0);

	vec4 depths_ = texture(depth, uv);
	depths[0] = depthToDistance(uv, texture(depth0, uv).x);
	depths[1] = depths_.x;
	depths[2] = depths_.y;
	depths[3] = depths_.z;
	depths[4] = depths_.w;
	return depths;
}


void main()
{	
	// reproject entry point (image coordinates in old view)
	vec4 reprojected = uProjection * uViewOld * vec4(passWorldPosition, 1.0);
	reprojected /= reprojected.w;
	reprojected.xyz = reprojected.xyz * 0.5 + 0.5; 
	
	// reproject (imaginary) exit point
	vec4 reprojectedBack = uProjection * uViewOld * inverse(uViewNovel) * ( vec4( passPosition * 3.0, 1.0) ); // arbitrary point that lies further back on the ray
	reprojectedBack /= reprojectedBack.w;
	reprojectedBack.xyz = reprojectedBack.xyz * 0.5 + 0.5; 

	// image plane direction
	vec3 startPoint = vec3( reprojected.xy, length( (uViewOld * vec4(passWorldPosition, 1.0) ).xyz ) );
	vec3 backPoint = vec3( reprojectedBack.xy, length( (uViewOld * inverse(uViewNovel) * vec4( passPosition * 3.0, 1.0 ) ).xyz ) );
	vec3 rayDir = backPoint - startPoint; // no need to normalize
	
	// variables indicating the direction of the ray (which scalar coordinates will increment, which will decrement)
	vec3 stepDir = 
		  vec3( -1.0 ) * vec3( lessThan(		 rayDir, vec3(0.0) ) ) // scalars that will decrement
		+ vec3(  1.0 ) * vec3( greaterThanEqual( rayDir, vec3(0.0) ) );// scalars that will increment

	// sample first layer depth
	float d0 = texture(depth0, startPoint.xy).x;
	if (d0 == 1.0) { discard; } //invalid pixel
	d0 = depthToDistance(startPoint.xy, d0); // d0 is from depth-attachment, so convert to distance first

	//<<<< initialize running variables
	vec4 color = vec4(0.0); // result color

	float tLast = 0.0;
	vec3 samplePoint = startPoint;
	vec4 currentSegmentEA = vec4(0.0); // fully transparent
	int  currentLayer = 0; // start at 'empty-layer'

	vec4  layerEA[5]    =  vec4[5](0.0);
	float layerDepth[5] = float[5](0.0);
	layerEA    = readTexturesEA(    samplePoint.xy ); 
	layerDepth = readTexturesDepth( samplePoint.xy );

	// delta: size of current voxel (all positive), tDelta : parametric size of current voxel (all positive)
	vec3 delta  = vec3( 1.0 / textureSize(depth0, 0), (d0 - startPoint.z) ); // delta.z changes according to current Layer
	vec3 tDelta = abs(delta / rayDir); // all positive

	// tMax: initial values for maximum parametric traversal before entering the next voxel
	vec2 tMaxXY = abs( ( max(vec2(0.0), stepDir.xy) * delta.xy - mod( startPoint.xy, delta.xy ) ) / rayDir.xy ); // complicated
	float tMaxZ = delta.z / rayDir.z; // easy
	vec3 tMax   = vec3(tMaxXY, tMaxZ);

	//<<<< 3DDA Traversal
	int debugCtr = 0;	
	while( isInVolume(samplePoint) && (++debugCtr < 10))
	{
		// perform a step: first find min value
		float tNext = min( tMax.x, min(tMax.y, tMax.z) ); // find nearest "border"
		bvec3 tNextFlag = lessThanEqual( tMax.xyz, tMax.yzx ) && lessThanEqual(tMax.xyz, tMax.zxy); // (hopefully) only one component is true
		
		//<<<< compute next sample point
		samplePoint = startPoint + tNext * rayDir; // find next samplePoint
		float distanceBetweenPoints = length( (tNext - tLast) * rayDir ); // traveled distance // TODO THIS IS ONLY DEBUG!!

		//<<<< accumulate currentSegmentEA for traversed segment length
		vec4 segmentColor = beerLambertEmissionAbsorptionToColorTransmission( currentSegmentEA.rgb, currentSegmentEA.a, distanceBetweenPoints );
		segmentColor.a = 1.0 - segmentColor.a; // turn T to alpha

		color = accumulateFrontToBack(color, segmentColor);

		//<<<< read new values from pixel we are entering OR layer we are entering
		if ( any(tNextFlag.xy) )
		{
			//<<<< retrieve values 
			// TODO THIS IS ONLY DEBUG!!
			layerEA    = readTexturesEA(    vec2(samplePoint.xy + 0.5 * delta.xy * vec2(tNextFlag) * stepDir.xy) ); 
			layerDepth = readTexturesDepth( vec2(samplePoint.xy + 0.5 * delta.xy * vec2(tNextFlag) * stepDir.xy) );

			// update by finding corresponding layer
			currentLayer = 0;
			while ( layerDepth[currentLayer] <= samplePoint.z && currentLayer <= 4){
				currentLayer++;
			}
		}
		else // if ( tNextFlag.z ) // dda went in z direction => entering deeper layer
		{
			currentLayer = min(currentLayer + 1, 4);
		}

		//<<<< retrieve colors and depth borders for next segment
		delta.z = layerDepth[currentLayer] - samplePoint.z;
		tDelta.z = abs(delta.z / rayDir.z);
		currentSegmentEA = layerEA[ currentLayer ];

		//<<<< Update running variables
		tMax  = tMax + vec3( tNextFlag ) * tDelta;    // vector component is 1.0 if correlates with tNext, 0.0 else --> only increments one scalar
		tLast = tNext; // save last position
	}

	fragColor = color;
}
