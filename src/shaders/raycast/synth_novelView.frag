#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces FROM OLD VIEW
uniform sampler2D front_uvw_map;  // uvw coordinates map of front faces FROM OLD VIEW

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

vec4 screenToView(vec3 screen)
{
	vec4 unProject = inverse(uProjection) * vec4((screen * 2.0) - 1.0, 1.0);
	unProject /= unProject.w;

	return unProject;
}

//!< simple Front-To-Back blending
vec4 accumulateFrontToBack(vec4 color, vec4 backColor)
{
	return vec4( (1.0 - color.a) * (backColor.rgb) + color.rgb,
	             (1.0 - color.a) * backColor.a     + color.a);
}

//!< DEBUG simple, but expensive condition method to check whether samplepoint is valid
//bool isInVolume(vec3 samplePoint)
//{
//	// check position compared to last layer
//	bool onScreen = all( greaterThanEqual( samplePoint.xy, vec2(0.0) ) && lessThanEqual( samplePoint.xy, vec2(1.0) ) );
//
//	bool inRange  = ( samplePoint.z <= backDistance ) && (samplePoint.z >= frontDistance); // inbetween "front" and "back"
//	return (onScreen && inRange);
//}

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
float[6] readTexturesDepth( vec2 uv )
{
	float[6] depths = float[6](0.0);

	vec4 depths_ = texture(depth, uv);
	depths[0] = depthToDistance(uv, texture(depth0, uv).x);
	depths[1] = depths_.x;
	depths[2] = depths_.y;
	depths[3] = depths_.z;
	depths[4] = depths_.w;
	depths[5] = depthToDistance(uv, texture(back_uvw_map, uv).w);
	return depths;
}


void main()
{	
	// reproject entry point (image coordinates in old view)
	vec4 uvwStart = texture(front_uvw_map, passUV);
	vec4 uvwEnd   = texture(back_uvw_map, passUV);

	if (uvwStart.a == 0.0) { discard; } //invalid pixel

	vec4 oldViewStart = uViewOld * inverse(uViewNovel) * screenToView( vec3(passUV, uvwStart.a) );
	vec4 reprojectedStart = uProjection * oldViewStart;
	reprojectedStart /= reprojectedStart.w;
	reprojectedStart.xyz = reprojectedStart.xyz / 2.0 + 0.5;

	vec4 oldViewEnd   = uViewOld * inverse(uViewNovel) * screenToView( vec3(passUV, uvwEnd.a)   );
	vec4 reprojectedEnd = uProjection * oldViewEnd;
	reprojectedEnd /= reprojectedEnd.w;
	reprojectedEnd.xyz = reprojectedEnd.xyz / 2.0 + 0.5;
	
	// image plane di rection
	vec3 startPoint = vec3(reprojectedStart.xy, length( oldViewStart.xyz) );
	vec3 backPoint  = vec3(reprojectedEnd.xy,   length( oldViewEnd.xyz ) );
	vec3 rayDir = backPoint - startPoint; // no need to normalize

	//rayDir = vec3(greaterThan(rayDir, vec3(0.001))) * rayDir;

	// variables indicating the direction of the ray (which scalar coordinates will increment, which will decrement)
	vec3 stepDir = 
		  vec3( -1.0 ) * vec3( lessThan(	rayDir, vec3(-0.001) ) ) // scalars that will decrement // some non-zero threshold (re-normalization...)
		+ vec3(  1.0 ) * vec3( greaterThan( rayDir, vec3(0.001)  ) );// scalars that will increment // else: 0.0

    //if (true) { fragColor = vec4(vec3(abs(stepDir.y)), 1.0); return; }

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
	float layerDepth[6] = float[6](0.0);
	layerEA    = readTexturesEA(	startPoint.xy );
	layerDepth = readTexturesDepth( startPoint.xy );

	// delta: size of current voxel (all positive), tDelta : parametric size of current voxel (all positive)
	vec3 delta  = vec3( 1.0 / textureSize(depth0, 0), (d0 - startPoint.z) ); // delta.z changes according to current Layer
	vec3 tDelta = (vec3(1.0) - abs(stepDir)) * 99999.9 + abs(stepDir * delta / rayDir); // all positive, infty for step == 0

	// tMax: initial values for maximum parametric traversal before entering the next voxel
	vec2 tMaxXY = (vec2(1.0) - abs(stepDir.xy)) * 99999.9
		         + abs(stepDir.xy) * abs( ( max(vec2(0.0), stepDir.xy) * delta.xy - mod( startPoint.xy, delta.xy ) ) / rayDir.xy ); // complicated
	float tMaxZ = delta.z / rayDir.z; // easy
	vec3 tMax   = vec3(tMaxXY, tMaxZ);

	//<<<< 3DDA Traversal
	int debugCtr = 0;	
	while( tLast >= 0.0 && tLast < 1.0 && (min(tMax.x, min(tMax.y, tMax.z)) <= 1.0) && ++debugCtr <= 100)
	{
		if (debugCtr == 100) { fragColor = vec4(0.0, 1.0, 0.0, 1.0); return; }

		// perform a step: first find min value
		float tNext = min( tMax.x, min(tMax.y, tMax.z) ); // find nearest "border"
		bvec3 tNextFlag = lessThanEqual( tMax.xyz, tMax.yzx ) && lessThanEqual(tMax.xyz, tMax.zxy); // (hopefully) only one component is true
		
		if (!any(tNextFlag)) { tNextFlag.z = true; tNext = tMax.z; }

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
			while ( layerDepth[currentLayer] <= samplePoint.z && currentLayer < 5){
				currentLayer++;
			}
		}
		else // if ( tNextFlag.z ) // dda went in z direction => entering deeper layer
		{
			currentLayer = min(currentLayer + 1, 5);
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
