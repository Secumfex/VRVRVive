#version 430 core

////////////////////////////////     DEFINES      ////////////////////////////////
/*********** LIST OF POSSIBLE DEFINES ***********
	LOCAL_SIZE_X <int>
	LOCAL_SIZE_Y <int>
	IMAGE_TYPE <internalType>

	AVERAGE_MIPMAP
	MAXIMUM_MIPMAP
	MINIMUM_MIPMAP
	SUM_MIPMAP
***********/

#ifndef LOCAL_SIZE_X
#define LOCAL_SIZE_X 32
#endif
#ifndef LOCAL_SIZE_Y
#define LOCAL_SIZE_Y 32
#endif

#ifndef IMAGE_TYPE
#define IMAGE_TYPE rgba16f
#endif

// standard behaviour: average
#ifndef MAXIMUM_MIPMAP
#ifndef MINIMUM_MIPMAP
#ifndef ADD_MIPMAP
#ifndef AVERAGE_MIPMAP
#define AVERAGE_MIPMAP
#endif
#endif
#endif
#endif

///////////////////////////////////////////////////////////////////////////////////

// specify local work group size
layout (local_size_x = LOCAL_SIZE_X, local_size_y = LOCAL_SIZE_Y) in;

// specify image to mipmap
layout(binding = 0, IMAGE_TYPE) readonly uniform image2D mipmap_base;

// specify image to write to
layout(binding = 1, IMAGE_TYPE) writeonly uniform image2D mipmap_target;

void main()
{
	// read x & y index of global invocation as index to write to and read from
	ivec2 index = ivec2( gl_GlobalInvocationID.xy );
	
	// load values
	vec4 valL  = imageLoad( mipmap_base, index * 2 );					
	vec4 valR  = imageLoad( mipmap_base, index * 2 + ivec2( 1, 0 ) );
	vec4 valTL = imageLoad( mipmap_base, index * 2 + ivec2( 0, 1 ) );
	vec4 valTR = imageLoad( mipmap_base, index * 2 + ivec2( 1, 1 ) );
				
	// mipmap values
	vec4 value = vec4(1);

	#ifdef AVERAGE_MIPMAP
	value = (valL + valR + valTL + valTR) / 4.0; 
	#endif
		
	#ifdef MAXIMUM_MIPMAP
	value = max(max(max(valL, valR), valTL), valTR); 
	#endif

	#ifdef MINIMUM_MIPMAP
	value = min(min(min(valL, valR), valTL), valTR); 
	#endif

	#ifdef SUM_MIPMAP
	value = (valL + valR + valTL + valTR);
	#endif

	// write value
	imageStore( mipmap_target, index, value );
}