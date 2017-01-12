#version 430

////////////////////////////////     DEFINES      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// in-variables
in vec2 passUV;

// textures
uniform sampler2D depth0; // first hit, aka depth layer 0
uniform sampler2D depth;   // depth layers 1 to 4
uniform sampler2D layer1;  // Emission Absorption layer 1
uniform sampler2D layer2;  // Emission Absorption layer 2
uniform sampler2D layer3;  // Emission Absorption layer 3
uniform sampler2D layer4;  // Emission Absorption layer 4

uniform mat4 uProjection; // cuz im lazy

////////////////////////////////     UNIFORMS      ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

// out-variables
layout(location = 0) out vec4 fragColor; 

vec4 beerLambert(vec3 E, float A, float z)
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

void main()
{	
	float d0 = texture(depth0, passUV).x;
	if (d0 == 1.0) { discard; } //invalid pixel
	d0 = depthToDistance(passUV, d0); // d0 is from depth-attachment, so convert to distance first
	
	vec4 d  = texture(depth, passUV);


	//<<<< retrieve values
	vec4 EA1 = texture(layer1, passUV);
	vec4 EA2 = texture(layer2, passUV);
	vec4 EA3 = texture(layer3, passUV);
	vec4 EA4 = texture(layer4, passUV);

	//<<<< compute colors
	vec4 layerColor1 = beerLambert(EA1.rgb, EA1.a, d.x - d0);
	vec4 layerColor2 = beerLambert(EA1.rgb, EA2.a, d.y - d.x);
	vec4 layerColor3 = beerLambert(EA1.rgb, EA3.a, d.z - d.y);
	vec4 layerColor4 = beerLambert(EA1.rgb, EA4.a, d.w - d.z);

	//<<<< compute pixel color, using front-to-back compositing
	vec4 color = vec4(0.0);

	// Layer 1
	color.rgb = (1.0 - color.a) * (layerColor1.rgb) + color.rgb;
	color.a   = (1.0 - color.a) * layerColor1.a     + color.a;

	// Layer 2
	color.rgb = (1.0 - color.a) * (layerColor2.rgb) + color.rgb;
	color.a   = (1.0 - color.a) * layerColor2.a     + color.a;

	// Layer 3
	color.rgb = (1.0 - color.a) * (layerColor3.rgb) + color.rgb;
	color.a   = (1.0 - color.a) * layerColor3.a     + color.a;

	// Layer 4
	color.rgb = (1.0 - color.a) * (layerColor4.rgb) + color.rgb;
	color.a   = (1.0 - color.a) * layerColor4.a     + color.a;

	fragColor = color;
	fragColor = vec4(d.x - d0);
}
