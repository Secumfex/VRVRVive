#version 330

/*
* Simple fragmentshader that composes a texture array back to front. 
* https://de.wikipedia.org/wiki/Porter-Duff_Composition
*/

//!< in-variable
in vec3 passPosition;

//!< uniforms
uniform sampler2DArray tex;
uniform int lod;
uniform int uBlockWidth;
uniform float uPixelOffsetFar;
uniform float uPixelOffsetNear;

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	// assertion: all rays will ONLY operate between Near and Far
	float xLeft  = floor(gl_FragCoord.x) + uPixelOffsetFar;  // identify leftmost  ray-coordinate that could write to this pixel
	float xRight = ceil( gl_FragCoord.x) + uPixelOffsetNear; // identify rightmost ray-coordinate that could write to this pixel

	int numLayersToBlend = int(xRight) - int(xLeft);
	int initialLayerIdx = uBlockWidth - (int(xLeft)  % uBlockWidth ) - 1; // this defines the "entry" layer for this pixel
	int endLayerIdx     = uBlockWidth - (int(xRight) % uBlockWidth ) - 1; // this defines the "exit"  layer for this pixel

	fragColor = vec4(0.0);

	// for visual debugging: expect things to go wrong!
	if (numLayersToBlend >= uBlockWidth)
	{
		fragColor = vec4(0.1,0.0,0.0,0.1);
	}

	// blend layers
	//for (int i = initialLayerIdx; i != endLayerIdx; i = (i-1) % uBlockWidth)
	for (int i = 0; i < uBlockWidth; i ++)
	{
		vec4 texColor = texture(tex, vec3(passPosition.xy, (initialLayerIdx - i) % uBlockWidth) ); // blend
		fragColor.rgb = texColor.rgb + (1.0 - texColor.a) * fragColor.rgb;
		fragColor.a   = texColor.a   + (1.0 - texColor.a) * fragColor.a;
	}
}