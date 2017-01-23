#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
uniform sampler2D back_uvw_map_old;   // uvw coordinates map of back faces FROM OLD VIEW
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces in novel view
uniform sampler2D front_uvw_map;  // uvw coordinates map of front faces in novel view

uniform sampler2D depth0; // first hit, aka depth layer 0
uniform sampler2D depth;   // depth layers 1 to 4
uniform sampler2D layer1;  // Emission Absorption layer 1
uniform sampler2D layer2;  // Emission Absorption layer 2
uniform sampler2D layer3;  // Emission Absorption layer 3
uniform sampler2D layer4;  // Emission Absorption layer 4

uniform mat4 uProjection; //
uniform mat4 uViewNovel; // 
uniform mat4 uViewOld; // 

uniform int uThreshold;

////////////////////////////////     UNIFORMS      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor; 

//!< compute color and transmission values from emission and absorption coefficients and distance traveled through medium
vec4 beerLambertEmissionAbsorptionToColorTransmission(vec3 E, float A, float z)
{
	float T = exp( -A * z);
	vec3 C = E * (1.0 - T);
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

//!< DEBUG simple, but expensive update method to retrieve ea values
vec4 getLayerEA(vec2 uv, int layer)
{
	if		( layer == 0 )  { return vec4(0.0,0.0,0.0,1.0); }
	else if ( layer == 1 )  { return texture(layer1, uv); }
	else if ( layer == 2 )  { return texture(layer2, uv); }
	else if ( layer == 3 )  { return texture(layer3, uv); }
	else if ( layer == 4 )  { return texture(layer4, uv); }
	else/*if( layer == 5 )*/{ return vec4(0.0,0.0,0.0,1.0); }
}

//!< DEBUG simple, but expensive update method to retrieve ea values
float getLayerDistance(vec2 uv, int layer)
{
	if		( layer == 0 )  { return depthToDistance(uv, texture(depth0, uv).x); }
	else if ( layer == 1 )  { return texture(depth, uv).x; }
	else if ( layer == 2 )  { return texture(depth, uv).y; }
	else if ( layer == 3 )  { return texture(depth, uv).z; }
	else if ( layer == 4 )  { return texture(depth, uv).w; }
	else/*if( layer == 5 )*/{ return depthToDistance(uv, texture(back_uvw_map_old, uv).w); }
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
	
	// early out	
	if ( any( lessThan(reprojectedStart.xy, vec2(0.0)) || greaterThan(reprojectedStart.xy, vec2(1.0)) ) )
	{
		discard;
	} 

	// image plane direction
	vec3 startPoint = vec3(reprojectedStart.xy, length( oldViewStart.xyz) );
	vec3 backPoint  = vec3(reprojectedEnd.xy,   length( oldViewEnd.xyz ) );
	vec3 rayDir = backPoint - startPoint; // no need to normalize

	// variables indicating the direction of the ray (which scalar coordinates will increment, which will decrement)
	vec3 stepDir = 
		  vec3( -1.0 ) * vec3( lessThan(	rayDir, vec3(-0.001) ) ) // scalars that will decrement // some non-zero threshold (re-normalization...)
		+ vec3(  1.0 ) * vec3( greaterThan( rayDir, vec3(0.001)  ) );// scalars that will increment // else: 0.0

	//<<<< initialize running variables
	vec4 color = vec4(0.0); // result color

	float tLast = 0.0;
	vec3 samplePoint = startPoint;
	vec3 lastSampleView = oldViewStart.xyz;
	int  currentLayer = 0; // start at 'empty-layer'
	float  currentLayerDistance = getLayerDistance(startPoint.xy, currentLayer);
	while ( currentLayerDistance <= samplePoint.z && currentLayer < 5){
		currentLayer++;
		currentLayerDistance = getLayerDistance(samplePoint.xy, currentLayer);
	}
	vec4 currentSegmentEA = getLayerEA( samplePoint.xy, currentLayer );

	// delta: size of current voxel (all positive), tDelta : parametric size of current voxel (all positive)
	vec3 delta  = vec3( 1.0 / textureSize(depth0, 0), (currentLayerDistance - startPoint.z) ); // delta.z changes according to current Layer
	vec3 tDelta = (vec3(1.0) - abs(stepDir)) * 99999.9 + abs(stepDir * delta / rayDir); // all positive, infty for step == 0

	// tMax: initial values for maximum parametric traversal before entering the next voxel
	vec2 tMaxXY = (vec2(1.0) - abs(stepDir.xy)) * 99999.9
		         + abs(stepDir.xy) * abs( ( max(vec2(0.0), stepDir.xy) * delta.xy - mod( startPoint.xy, delta.xy ) ) / rayDir.xy ); // complicated
	float tMaxZ = delta.z / rayDir.z; // easy
	vec3 tMax   = vec3(tMaxXY, tMaxZ);

	//<<<< 3DDA Traversal
	int debugCtr = 0;	
	while( tLast >= 0.0 && tLast < 1.0 && ++debugCtr <= uThreshold)
	{
		// Determine how far the ray can be traversed beofre entering next voxel
		float tNext = min( tMax.x, min(tMax.y, tMax.z) ); // find nearest "border"

		//<<<< compute next sample point
		samplePoint = startPoint + tNext * rayDir; // find next samplePoint
		vec3 sampleView = samplePoint.z * normalize( screenToView( vec3(samplePoint.xy, 0.0) ).xyz );

		float distanceBetweenPoints = abs( sampleView.z - lastSampleView.z ); // traveled distance // TODO THIS IS ONLY DEBUG!!

		//<<<< accumulate currentSegmentEA for traversed segment length
		vec4 segmentColor = beerLambertEmissionAbsorptionToColorTransmission( currentSegmentEA.rgb, currentSegmentEA.a, distanceBetweenPoints );
		segmentColor.a = 1.0 - segmentColor.a; // turn T to alpha

		//if ( debugCtr == uThreshold ){ fragColor = vec4( segmentColor ); return; }

		color = accumulateFrontToBack(color, segmentColor);

		//<<<< read new values from pixel we are entering OR layer we are entering
		if ( tNext == tMax.x || tNext == tMax.y)
		{

			// update by finding corresponding layer
			currentLayer = 0;
			currentLayerDistance = getLayerDistance(samplePoint.xy, currentLayer);
			while ( currentLayerDistance < samplePoint.z && currentLayer < 5){
				currentLayer++;
				currentLayerDistance = getLayerDistance(samplePoint.xy, currentLayer);
			}
			
			// recompute tMax.z
			tMax.z = abs( (currentLayerDistance - startPoint.z) / rayDir.z);
		}
		else // if ( tNext == tMax.z ) // dda went in z direction => entering deeper layer
		{
			currentLayer = min(currentLayer + 1, 5);
			currentLayerDistance = getLayerDistance(samplePoint.xy, currentLayer);
		}


		//<<<< retrieve colors and depth borders for next segment
		if (tNext == tMax.x) { tMax.x += tDelta.x; }
		else if (tNext == tMax.y) { tMax.y += tDelta.y; }
		else if (tNext == tMax.z) {
			// add Distance delta from freshly entered layer
			tMax.z = abs((currentLayerDistance - startPoint.z) / rayDir.z);
		} 

		//<<<< Update running variables
		currentSegmentEA = getLayerEA( samplePoint.xy, currentLayer );
		
		lastSampleView = sampleView;
		tLast = tNext; // save last position
	}

	fragColor = color;
}
