#version 330

/*
* Simple fragmentshader that composes a texture array back to front. 
* https://de.wikipedia.org/wiki/Porter-Duff_Composition
*/

//!< in-variable
in vec3 passPosition;

//!< uniforms
uniform sampler2DArray tex;
uniform float uPixelOffsetFar;
uniform float uPixelOffsetNear;
uniform float uImageOffset;

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	ivec3 texSize = textureSize(tex, 0); 

	// assertion: all rays will ONLY operate between Near and Far
	//float xLeft  = floor(passPosition.x * float(texSize.x) ) + uPixelOffsetFar;  // identify leftmost  ray-coordinate that could write to this pixel
	//float xRight = ceil( passPosition.x * float(texSize.x) ) + uPixelOffsetNear; // identify rightmost ray-coordinate that could write to this pixel

	//int numLayersToBlend = int(xRight) - int(xLeft);
	//int initialLayerIdx = texSize.z - (int(xLeft)  % texSize.z ) - 1; // this defines the "entry" layer for this pixel
	//int initialLayerIdx = texSize.z - ( (int(passPosition.x * texSize.x) - int(ceil(uPixelOffsetNear))) % texSize.z ) - 1; // this defines the "entry" layer for this pixel
	//int endLayerIdx     = texSize.z - (int(xRight) % texSize.z ) - 1; // this defines the "exit"  layer for this pixel
	
	int d_p = texSize.z;
	float x_r = passPosition.x * texSize.x;
	float x_0 = x_r - uPixelOffsetNear;
	int x_l = d_p + int(x_0);
	int initialLayerIdx = int(uImageOffset) - ( x_l % d_p ) - 1; // this defines the "entry" layer for this pixel

	fragColor = vec4(0.0);

	// for visual debugging: expect things to go wrong!
	//if (numLayersToBlend >= texSize.z)
	//{
		//fragColor = vec4(0.1,0.0,0.0,0.1);
	//}

	// blend layers (front to back)
	//for (int i = initialLayerIdx; i != endLayerIdx; i = (i-1) % texSize.z)
	for (int i = 0; i < texSize.z; i ++)
	{
		// BACK TO FRONT
		//vec4 texColor = texture(tex, vec3(passPosition.xy, (initialLayerIdx - i) % texSize.z) ); // blend
		//fragColor.rgb = texColor.rgb + (1.0 - texColor.a) * fragColor.rgb;
		//fragColor.a   = texColor.a   + (1.0 - texColor.a) * fragColor.a;

		// FRONT TO BACK
		vec4 texColor = texture(tex, vec3(passPosition.xy, (initialLayerIdx + i) % texSize.z) ); // blend
		fragColor.rgb = (1.0 - fragColor.a) * (texColor.rgb) + fragColor.rgb;
		fragColor.a = (1.0 - fragColor.a) * texColor.a + fragColor.a;
	}
}