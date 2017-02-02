#ifndef MISC_PARAMETERS_H
#define MISC_PARAMETERS_H

#include <algorithm>

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
* NOTE: These variables are just for convenient, default accessibility. Updates MUST be performed manually.
*/

namespace RaycastingParameters
{
	static float s_minValue = (float) INT_MIN; // minimal value in data set; to be overwitten after import
	static float s_maxValue = (float) INT_MAX;  // maximal value in data set; to be overwitten after import

	static bool  s_isRotating = false; 	// initial state for rotating animation
	static float s_rayStepSize = 0.1f;  // ray sampling step size; to be overwritten after volume data import

	static float s_windowingMinValue = -FLT_MAX / 2.0f;
	static float s_windowingMaxValue = FLT_MAX / 2.0f;
	static float s_windowingRange = FLT_MAX;

	static float s_lodMaxLevel = 2.5f;
	static float s_lodBegin  = 0.25f;
	static float s_lodRange  = 3.0f;
};

namespace ViewParameters
{
	static float s_eyeDistance = 0.065f; // distance between eyes

	static float s_near = 0.1f;  // near plane
	static float s_far = 10.0f;  // far plane
	static float s_fovY = 45.0f; // full vertical camera angle (degrees)
	static float s_aspect = 1.0f; // width to height aspect ratio
	static float s_nearH = s_near * std::tanf( glm::radians(s_fovY/2.0f) ); // HALF-Height of Near plane
	static float s_nearW = s_nearH * s_aspect; // HALF-Width of Near plane

	//!< call when s_near, s_aspect or s_fovY changed
	inline void updateNearHeightWidth() {
		s_nearH = s_near * std::tanf( glm::radians(s_fovY/2.0f) ); 
		s_nearW = s_nearH * s_aspect;
	}

	static glm::mat4 s_screenToView = glm::scale(glm::vec3(s_nearW, s_nearH, s_near)) * 
		glm::inverse( 
			glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * 
			glm::scale(glm::vec3(0.5f,0.5f,0.5f)) 
			);   // const from [0..1] to view coordinates on near plane
	
	//!< call when s_near, s_far or s_fovY changed
	inline void updateScreenToViewMatrix() {
		s_screenToView = glm::scale(glm::vec3(s_nearW, s_nearH, s_near)) * 
		glm::inverse( 
			glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * 
			glm::scale(glm::vec3(0.5f,0.5f,0.5f)) 
			);
	}

	static glm::mat4 s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -1.5f)); // translational part of model matrix
	static glm::mat4 s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // rotational part of model matrix
	static glm::mat4 s_scale = glm::scale(glm::vec3(0.5f)); // scalation part of model matrix
	
	static glm::mat4 s_model = s_translation * s_rotation * s_scale; // model matrix (discouraged to use if something else interferes (i.e. additional scalation/rotation etc)
	//!< call when s_translation, s_rotation, or s_scale changed
	inline void updateModel()
	{
		s_model = s_translation * s_rotation * s_scale;
	}

	static glm::vec4 s_eye = glm::vec4(0.0f, 0.0f, 1.5f, 1.0f); // (left) eye position
	static glm::vec4 s_center = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // center point
	static glm::vec4 s_up = glm::vec4(0.0f,1.0f,0.0f,0.0f); // up vector
	static glm::mat4 s_view = glm::lookAt(glm::vec3(s_eye), glm::vec3(s_center), glm::vec3(s_up)); // view of left eye
	static glm::mat4 s_view_r = glm::lookAt(glm::vec3(s_eye) +  glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(s_center) + glm::vec3(s_eyeDistance, 0.0f, 0.0f), glm::vec3(s_up)); // view of right eye (parallel)

	//!< call when s_eye, s_center, s_eyeDistance or s_up changed
	inline void updateView()
	{
		s_view = glm::lookAt(glm::vec3(s_eye), glm::vec3(s_center), glm::vec3(s_up));
		s_view_r = glm::lookAt(glm::vec3(s_eye) +  glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(s_center) + glm::vec3(s_eyeDistance, 0.0f, 0.0f), glm::vec3(s_up));
	}

	static glm::mat4 s_perspective   = glm::perspective(glm::radians( s_fovY ), s_aspect, s_near, s_far); // perspective of left view
	static glm::mat4 s_perspective_r = glm::perspective(glm::radians( s_fovY ), s_aspect, s_near, s_far); // perspective of right eye (probably identical to left)

	//!< call when s_fovY, s_aspect, s_near or s_far changed
	inline void updatePerspective()
	{
		s_perspective   = glm::perspective(glm::radians( s_fovY ), s_aspect, s_near, s_far);
		s_perspective_r = glm::perspective(glm::radians( s_fovY ), s_aspect, s_near, s_far);
	}

};

namespace VolumeParameters
{
	static glm::vec3 s_volumeSize(1.0f, 0.886f, 1.0);
	static glm::mat4 s_modelToTexture =	
		glm::mat4( 
			glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),  // column 1
			glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),  // column 2
			glm::vec4(0.0f, -1.0f, 0.0f, 0.0f), //column 3
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)	//column 4 
		) // swap components	
		* glm:: inverse(glm::scale( 2.0f * s_volumeSize) ) // moves origin to front left
		* glm::translate( glm::vec3(s_volumeSize.x, s_volumeSize.y, -s_volumeSize.z) );; // const from model coordinates to texture coordinates
};

#endif