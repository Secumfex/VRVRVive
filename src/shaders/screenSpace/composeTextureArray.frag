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

//!< out-variables
layout(location = 0) out vec4 fragColor;

void main() 
{
	float textureWidth = float( textureSize( tex, 0 ).x );
	int initialLayerIdx = (uBlockWidth - ( int(passPosition.xy * textureWidth) % uBlockWidth ) - 1 ); // this defines the "entry" layer for this pixel

	fragColor = vec4(0,0,0,0);
	for (int i = textureSize(tex, lod).z - 1; i >= 0; i--)
	{
		float sampleLayerIdx = float( (initialLayerIdx + i) % uBlockWidth);
		vec4 texColor = texture(tex, vec3(passPosition.xy, sampleLayerIdx) ); // blend
		fragColor.rgb = texColor.rgb + (1.0 - texColor.a) * fragColor.rgb;
		fragColor.a   = texColor.a   + (1.0 - texColor.a) * fragColor.a;
	}
}