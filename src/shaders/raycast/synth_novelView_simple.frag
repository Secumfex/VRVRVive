#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
/*********** LIST OF POSSIBLE DEFINES ***********
	RANDOM_OFFSET
***********/
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
// uniform sampler2D back_uvw_map_old;   // uvw coordinates map of back faces FROM OLD VIEW
uniform sampler2D back_uvw_map;   // uvw coordinates map of back  faces in novel view
uniform sampler2D front_uvw_map;  // uvw coordinates map of front faces in novel view

#ifdef SCENE_DEPTH
	uniform sampler2D scene_depth_map;   // depth map of scene
#endif

// uniform sampler2D depth0; // first hit, aka depth layer 0
uniform sampler2D depth;   // depth layers 1 to 4
uniform sampler2D layer0;  // Emission Absorption layer 1 // always black/transparent: vec4(0.0)
uniform sampler2D layer1;  // Emission Absorption layer 2
uniform sampler2D layer2;  // Emission Absorption layer 3
uniform sampler2D layer3;  // Emission Absorption layer 4

uniform mat4 uProjection; //
uniform mat4 uViewNovel; // 
uniform mat4 uViewOld; // 

//uniform mat4 uScreenToTexture;

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

#ifdef RANDOM_OFFSET 
float rand(vec2 co) { //!< http://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
	return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}
#endif

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
	if		( layer == 0 )  { return texture(layer0, uv);}
	else if ( layer == 1 )  { return texture(layer1, uv); }
	else if ( layer == 2 )  { return texture(layer2, uv); }
	else if ( layer == 3 )  { return texture(layer3, uv); }
}

//!< DEBUG simple, but expensive update method to retrieve depth values
float getLayerDistance(vec2 uv, int layer)
{
	if ( layer == 0 )       { return texture(depth, uv).x; }
	else if ( layer == 1 )  { return texture(depth, uv).y; }
	else if ( layer == 2 )  { return texture(depth, uv).z; }
	else if( layer == 3 )   { return texture(depth, uv).w; }
}

vec2 getScreenCoord(vec4 viewCoord)
{
	vec4 project = uProjection * viewCoord;
	project /= project.w;
	project.xyz = project.xyz / 2.0 + 0.5;
	return project.xy;
}

void main()
{	
	// reproject entry point (image coordinates in old view)
	vec4 uvwStart = texture(front_uvw_map, passUV);
	vec4 uvwEnd   = texture(back_uvw_map, passUV);

	if (uvwEnd.a == 0.0) {
		discard;
	} //invalid pixel

	#ifdef SCENE_DEPTH
		float scene_depth = texture(scene_depth_map, passUV).x;
		uvwStart.a = min(uvwStart.a, scene_depth);
		uvwEnd.a   = min(uvwEnd.a,   scene_depth);
	#endif

	vec4 oldViewStart = uViewOld * inverse(uViewNovel) * screenToView( vec3(passUV, uvwStart.a) );
	vec4 oldViewEnd   = uViewOld * inverse(uViewNovel) * screenToView( vec3(passUV, uvwEnd.a)   );

	//<<<< initialize running variables
	vec4 color = vec4(0.0); // result color

	int debugCtr = 0;
	float numSamples = float(uThreshold);
	// float numSamples = 12.0;
	float stepSize = 1.0 / numSamples;
	float t = stepSize;
	#ifdef RANDOM_OFFSET 
		t += (0.5 * stepSize) * rand(passUV) - (0.25 * stepSize);
	#endif
	float distanceStepSize = stepSize * length( (oldViewEnd - oldViewStart).xyz );

	vec4 lastPos = oldViewStart;
	
	while(t <= 1.0 + (stepSize * 0.5) && ++debugCtr <= uThreshold)
	{
		vec4 curPos   = mix(oldViewStart, oldViewEnd, t);
		vec2 screenPos   = getScreenCoord(curPos);
		// skip invalid positions
		if ( any( lessThan(screenPos, vec2(0.0)) || greaterThan(screenPos, vec2(1.0)) ) )
		{
			t += stepSize;		
			lastPos = curPos;
			continue;
		} 

		vec4 layerDistances = texture(depth, screenPos);

		float curDist = length(curPos.xyz); // view space distance
		
		// find out in which layer the sample lies
		int curLayer = 0;
		float curLayerDistance = layerDistances[curLayer];
		while ( curDist >= curLayerDistance && curLayer < 3)
		{
			curLayer++;
			curLayerDistance = layerDistances[curLayer];
		}

		// recognize layer-border passage, move sample back to layer distance if so
		float segmentLength = distanceStepSize;

		int lastLayer = 0;
		float lastDist = length(lastPos.xyz);
		float lastLayerDistance = layerDistances[lastLayer];
		while ( lastDist >= lastLayerDistance && lastLayer < curLayer ) // layer corresponding to last point (but on current screenpos)
		{
			lastLayer++;
			lastLayerDistance = layerDistances[lastLayer];
		}
		
		while ( lastLayer < curLayer && lastDist < curLayerDistance ) // there is still one in the middle!
		{
			// get corresponding ea values
			vec4 lastSegmentEA = getLayerEA( screenPos, lastLayer );
			float layerDistance = layerDistances[lastLayer];
			
			vec4 lastSegmentColor = beerLambertEmissionAbsorptionToColorTransmission(
				lastSegmentEA.rgb, 
				lastSegmentEA.a, 
				abs(layerDistance - lastDist)
				);
			lastSegmentColor.a = 1.0 - lastSegmentColor.a; // transmission to alpha

			// update color
			color = accumulateFrontToBack(color, lastSegmentColor);

			// update
			lastLayer++;
			lastDist = layerDistance;
			segmentLength = abs(curDist - lastDist); // remaining segment in current layer
		}

		// get corresponding ea values
		vec4 segmentEA = vec4(0);
		if ( curDist <= curLayerDistance )
		{
			segmentEA = getLayerEA( screenPos, curLayer );
		}
		vec4 segmentColor = beerLambertEmissionAbsorptionToColorTransmission(segmentEA.rgb, segmentEA.a, segmentLength);
		segmentColor.a = 1.0 - segmentColor.a;

		// update color
		color = accumulateFrontToBack(color, segmentColor);

		lastPos = curPos;
		t += stepSize;
	}

	fragColor = color;
}
