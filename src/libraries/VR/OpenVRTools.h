#ifndef OPENVRTOOLS_H_
#define OPENVRTOOLS_H_

#include <openvr.h>
#include <glm/glm.hpp>
#include <string>
#include <Rendering/GLTools.h>

class OpenVRSystem
{
public: 
	//-----------------------------------------------------------------------------
	// Direct Acess to the most recent matrix values
	//-----------------------------------------------------------------------------
	glm::mat4 m_mat4ProjectionLeft;
	glm::mat4  m_mat4ProjectionRight;

	glm::mat4 m_mat4HMDPose;
	glm::mat4 m_mat4eyePosLeft;
	glm::mat4 m_mat4eyePosRight;

	float m_near;
	float m_far;

	//-----------------------------------------------------------------------------
	// OpenVR related values
	//-----------------------------------------------------------------------------
	vr::IVRSystem *m_pHMD;	// reference to the HMD, if found during initialization
	vr::IVRRenderModels *m_pRenderModels; //reference to the device render models
	std::string m_strDriver; // Driver Tracking System Name
	std::string m_strDisplay;// Display Serial Number
	vr::TrackedDevicePose_t m_rTrackedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
	glm::mat4 m_rmat4DevicePose[ vr::k_unMaxTrackedDeviceCount ];
	bool m_rbShowTrackedDevice[ vr::k_unMaxTrackedDeviceCount ];

	std::string m_strPoseClasses; // what classes we saw poses for this frame
	char m_rDevClassChar[ vr::k_unMaxTrackedDeviceCount ];   // for each device, a character representing its class

	int m_iValidPoseCount; // to keep track of currently visible poses (HMD, controller, controller,...)
	int m_iValidPoseCount_Last; // as seen last

	//-----------------------------------------------------------------------------
	// Purpose: Constructor/Destructor
	//-----------------------------------------------------------------------------
	OpenVRSystem(float near = 0.1f, float far = 15.0f);
	~OpenVRSystem();

	//-----------------------------------------------------------------------------
	// Purpose: Setup OpenVR the Driver
	//-----------------------------------------------------------------------------
	bool initialize();

	//-----------------------------------------------------------------------------
	// Purpose: Shutdown OpenVR the Driver
	//-----------------------------------------------------------------------------
	void shutdown();

	//-----------------------------------------------------------------------------
	// Purpose: Setup the left/right projection and eyePos matrices
	//-----------------------------------------------------------------------------
	void initializeHMDMatrices();

	//-----------------------------------------------------------------------------
	// Purpose: retrieve all tracked poses (including HMD) and save them in the m_rTrackedDevicePose array
	//-----------------------------------------------------------------------------
	void updateTrackedDevicePoses();

	//-----------------------------------------------------------------------------
	// Purpose: submits the content of texture source for the specified eye to the compositor
	//-----------------------------------------------------------------------------
	void submitImage(GLuint source, vr::EVREye eye);

	//-----------------------------------------------------------------------------
	// Purpose: Helper to get a string from a tracked device property and turn it
	//			into a std::string
	//-----------------------------------------------------------------------------
	std::string GetTrackedDeviceString( vr::IVRSystem *pHmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError *peError = NULL);
	
	//-----------------------------------------------------------------------------
	// Purpose: Returns the Projection Matrix for an eye
	//-----------------------------------------------------------------------------
	glm::mat4 GetHMDMatrixProjectionEye( vr::Hmd_Eye nEye );

	//-----------------------------------------------------------------------------
	// Purpose: Returns the Pose Matrix for an eye (head->eye), essentially a translation
	//-----------------------------------------------------------------------------
	glm::mat4 GetHMDMatrixPoseEye( vr::Hmd_Eye nEye );

	//-----------------------------------------------------------------------------
	// Purpose: Returns the ViewProjection Matrix or an eye
	//-----------------------------------------------------------------------------
	glm::mat4 GetCurrentViewProjectionMatrix( vr::Hmd_Eye nEye );

	//-----------------------------------------------------------------------------
	// Purpose: Converts a SteamVR matrix to our local matrix class (adds a column)
	//-----------------------------------------------------------------------------
	glm::mat4 ConvertSteamVRMatrixToGLMMat4( const vr::HmdMatrix34_t &matPose );

	//-----------------------------------------------------------------------------
	// Purpose: Set near/far -> recomputes projection matrices
	//-----------------------------------------------------------------------------
	inline void setNear(float near) {m_near = near; initializeHMDMatrices(); };
	inline void setFar(float far) {m_far = far; initializeHMDMatrices(); };
	
	// computes the vertical field of view from the current raw projection matrix
	float getFovY(vr::Hmd_Eye nEye = vr::Eye_Left);
};



#endif

