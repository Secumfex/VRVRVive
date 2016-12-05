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
	std::vector< std::vector<GLuint> > m_queryBuffer;
	int m_timingsBufferSize;
	int m_currentChunkIdx;	
	int m_currentFrameIdx;

	int m_currentFrontQuery;
	int m_currentBackQuery;

public:
	ChunkedAdaptiveRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize, int timingsBufferSize = 32);
	virtual ~ChunkedAdaptiveRenderPass() {}
	virtual void render() override;
	void resetTimingsBuffers();
	void profileTimings();
	void swapQueryBuffers();
	inline std::vector< std::vector<float> >& getTimingsBuffer(){return m_timingsBuffer;}
	inline int getCurrentFrameIdx() {return m_currentFrameIdx;}
	inline int getTimingsBufferSize() {return m_timingsBufferSize;}
	void imguiInterface(bool* open = NULL);
};


#endif
