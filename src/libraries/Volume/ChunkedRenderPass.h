#ifndef CHUNKEDRENDERPASS_H_
#define CHUNKEDRENDERPASS_H_

//#include <Rendering/GLTools.h>
#include <Rendering/RenderPass.h>

class ChunkedRenderPass {
private:
	std::vector<GLenum> m_clearBitsTmp;
protected:

	glm::ivec2 m_viewportSize; // total size of viewport to fill
	glm::ivec2 m_chunkSize; // how big each chunk (viewport) should be
	glm::ivec2 m_currentPos; // bottom left position of viewport that is rendered next

	RenderPass* m_pRenderPass; // the renderpass that is called repeatedly

public:
	
	bool m_isFinished; // true after the last call to render(), when finished

	ChunkedRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize);
	virtual ~ChunkedRenderPass();

	inline RenderPass* getRenderPass(){return m_pRenderPass;}
	inline void setRenderPass(RenderPass* renderPass){m_pRenderPass = renderPass;}
	inline glm::ivec2 getViewportSize(){return m_viewportSize;}
	inline void setViewportSize(glm::ivec2 viewportSize){m_viewportSize = viewportSize;}
	inline glm::ivec2 getChunkSize(){return m_chunkSize;}
	inline void setChunkSize(glm::ivec2 chunkSize){m_chunkSize = chunkSize;}
	inline bool isFinished(){return m_isFinished;}

	virtual void render(); 
	virtual void updateViewport(); //!< update viewport in OpenGL and ShaderProgram
	virtual void updatePosition(); //!< move viewport chunk forward
	virtual void activateClearbits(); //!< activate the renderpass's clearbits
	virtual void deactivateClearbits(); //!< deactivate the renderpass's clearbits
};


class ChunkedAdaptiveRenderPass : public ChunkedRenderPass
{
protected:
	std::vector< std::vector<float> >m_timingsBuffer;
	int m_timingsBufferSize;
	int m_currentChunkIdx;	
	int m_currentFrameIdx;

	// query handling
	std::vector< std::vector<GLuint> > m_queryBuffer;
	int m_currentFrontQuery;
	int m_currentBackQuery;

	//++ Render-Iteration Time ++//
	float m_targetRenderTime;
	float m_renderTimeBias;
	bool m_autoAdjustRenderTime;

	//++ for profiling ++//
	std::vector< float >m_numChunksBuffer; 
	int m_currentIterationIdx;
	std::vector< float >m_totalRenderTimesBuffer; 
	float m_lastTotalRenderTime; // time for one complete render-iteration

public:
	/** @brief Constructor, a suitable RenderPass must have a vertex Shade wich uses the vec4 uniforms 'uViewport' and 'uResolution'
	* @param pRenderPass pointer to RenderPass (Screenfilling Quad with 'splittable' Vertex Shader) that will be split into chunks
	* @param viewportSize the total viewportSize in pixels
	* @param chunkSize the size of a chunk in pixels
	* @param timingsBufferSize the number of last render times that will be cached for each chunk
	* @param targetRenderTime the targeted render time in (ms) for one render iteration
	* @param bias a time bias scale by which each chunk's predicted render time will be multiplied
	*/
	ChunkedAdaptiveRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize, int timingsBufferSize = 32, float targetRenderTime = 14.0f, float bias = 1.25f);
	virtual ~ChunkedAdaptiveRenderPass();
	virtual void render() override;
	void resetTimingsBuffers();
	void profileTimings();
	void swapQueryBuffers();

	float predictChunkRenderTime(int idx); // predicts the render time for the provided chunk

	//++ Getters ++//
	inline std::vector< std::vector<float> >& getTimingsBuffer(){return m_timingsBuffer;}
	inline int getCurrentFrameIdx() {return m_currentFrameIdx;}
	inline int getTimingsBufferSize() {return m_timingsBufferSize;}
	inline float& getRenderTimeBias() {return m_renderTimeBias;}
	inline float& getTargetRenderTime() {return m_targetRenderTime;}
	inline void setAutoAdjustRenderTime(bool enabled) {m_autoAdjustRenderTime = enabled;}
	inline bool& getAutoAdjustRenderTime() {return m_autoAdjustRenderTime;}
	inline float& getLastTotalRenderTime() {return m_lastTotalRenderTime;}

	//++ ImGui++//
	void imguiInterface(bool* open = NULL);
};


#endif
