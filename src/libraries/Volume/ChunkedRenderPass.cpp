#include "ChunkedRenderPass.h"

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++ ChunkedRenderPass ++++++++++++++++++++++++++//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

ChunkedRenderPass::ChunkedRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize)
: m_chunkSize(chunkSize),
m_viewportSize(viewportSize),
m_currentPos(0,0),
m_isFinished(true)
{
	m_pRenderPass = pRenderPass;

	m_clearBitsTmp = m_pRenderPass->getClearBits();
	for (auto e : m_clearBitsTmp)
	{
		m_pRenderPass->removeClearBit(e);
	}
}

ChunkedRenderPass::~ChunkedRenderPass() {
}


void ChunkedRenderPass::updateViewport()
{
	// setViewport
	m_pRenderPass->setViewport(
		m_currentPos.x, 
		m_currentPos.y, 
		m_chunkSize.x, 
		m_chunkSize.y
		);
	m_pRenderPass->getShaderProgram()->update("uViewport",glm::vec4( 
		(float) m_currentPos.x, 
		(float) m_currentPos.y, 
		(float) m_chunkSize.x, 
		(float) m_chunkSize.y
		));
	m_pRenderPass->getShaderProgram()->update("uResolution", glm::vec4(
		(float) m_viewportSize.x,
		(float) m_viewportSize.y,
		0,
		0
		));
}
void ChunkedRenderPass::updatePosition()
{
	// move viewport for next iteration
	m_currentPos.x = m_currentPos.x + m_chunkSize.x;
	if (m_currentPos.x >= m_viewportSize.x)
	{
		m_currentPos.x = 0;
		m_currentPos.y = m_currentPos.y + m_chunkSize.y;
		if (m_currentPos.y >= m_viewportSize.y)
		{
			m_isFinished = true;
			m_currentPos.y = 0;
		}
	}
}

void ChunkedRenderPass::activateClearbits()
{
	for (auto e : m_clearBitsTmp)
	{
		m_pRenderPass->addClearBit(e);
	}
}

void ChunkedRenderPass::deactivateClearbits()
{
		m_clearBitsTmp = m_pRenderPass->getClearBits();
		for (auto e : m_clearBitsTmp)
		{
			m_pRenderPass->removeClearBit(e);
		}
}

void ChunkedRenderPass::render()
{
	if (m_isFinished)
	{
		activateClearbits();
	}

	updateViewport();

	// call renderpass
	m_pRenderPass->render();

	if (m_isFinished)
	{
		deactivateClearbits();
	}

	m_isFinished = false;

	updatePosition();
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++ ChunkedAdaptiveRenderPass ++++++++++++++++++//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

ChunkedAdaptiveRenderPass::ChunkedAdaptiveRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize, int timingsBufferSize)
	: ChunkedRenderPass(pRenderPass, viewportSize, chunkSize),
	m_timingsBufferSize(timingsBufferSize),
	m_currentChunkIdx(0),
	m_currentFrameIdx(0),
	m_currentBackQuery(0),
	m_currentFrontQuery(1)
{
	//initialize timings buffers
	resetTimingsBuffers();
}

void ChunkedAdaptiveRenderPass::render()
{
	//++++++ DEBUG +++++++++++++++
	int numIterationsPerFrame = 10;
	//++++++++++++++++++++++++++++

	for (int i = m_currentChunkIdx; i < std::min<int>(m_currentChunkIdx + numIterationsPerFrame, m_queryBuffer.size()); i++)
	{
		if (m_isFinished)
		{
			activateClearbits();
		}
		updateViewport();

		//Setup time query
		glBeginQuery(GL_TIME_ELAPSED, m_queryBuffer[i][m_currentBackQuery]);

		m_pRenderPass->render();

		glEndQuery(GL_TIME_ELAPSED);

		if (m_isFinished)
		{
			deactivateClearbits();
			m_isFinished = false;
		}

		updatePosition(); // move forward for next chunk, may set isFinished to true again
		if (m_isFinished)
		{
			break;
		}
	}

	if (m_isFinished)
	{
		m_currentChunkIdx = 0;
		profileTimings();
	}
	else
	{
		m_currentChunkIdx = m_currentChunkIdx + numIterationsPerFrame;
	}
}

void ChunkedAdaptiveRenderPass::resetTimingsBuffers()
{
	int numEntries = std::ceil( (float) m_viewportSize.x / (float) m_chunkSize.x) * std::ceil( (float) m_viewportSize.y / (float) m_chunkSize.y);
	m_timingsBuffer.resize( numEntries, std::vector<float>( m_timingsBufferSize )); 
	m_queryBuffer.resize(   numEntries, std::vector<GLuint>(2,0) );

	for (int i = 0; i < m_queryBuffer.size(); i++)
	{
		glDeleteQueries(1, &m_queryBuffer[i][m_currentBackQuery]); //delete old query object
		glDeleteQueries(1, &m_queryBuffer[i][m_currentFrontQuery]); //delete old query object
		glGenQueries(1, &m_queryBuffer[i][m_currentBackQuery]);
		glGenQueries(1, &m_queryBuffer[i][m_currentFrontQuery]);
		//glQueryCounter(m_queryBuffer[i][m_currentFrontQuery], GL_TIMESTAMP); // dummy query
	}

}

void ChunkedAdaptiveRenderPass::swapQueryBuffers(){
	if ( m_currentBackQuery ) {
		m_currentBackQuery= 0;
		m_currentFrontQuery = 1;
	}
	else
	{
		m_currentBackQuery = 1;
		m_currentFrontQuery = 0;
	}
}


namespace { const double NANOSECONDS_TO_MILLISECONDS = 1.0 / 1000000.0; }
void ChunkedAdaptiveRenderPass::profileTimings(){
	for ( int i = 0; i < m_timingsBuffer.size(); i++)
	{
		GLint available = 0;
		GLuint timeElapsed = 0;
		
		static int numFailed = 0;
		glGetQueryObjectiv(m_queryBuffer[i][m_currentFrontQuery], 
			GL_QUERY_RESULT_AVAILABLE, 
			&available);
		if (!available) {
			DEBUGLOG->log("Query result not available, #", numFailed++);
		}
		
		// Read query from front buffer (the one which finished last frame)
		glGetQueryObjectuiv(m_queryBuffer[i][m_currentFrontQuery], GL_QUERY_RESULT, &timeElapsed);
		
		// convert to ms
		float renderTime = (float) (NANOSECONDS_TO_MILLISECONDS * (double) timeElapsed);

		// save for profiling
		m_timingsBuffer[i][m_currentFrameIdx] = renderTime;
	}

	m_currentFrameIdx = (m_currentFrameIdx + 1) % m_timingsBufferSize;
	swapQueryBuffers();
}



namespace { static std::vector< std::pair<ChunkedAdaptiveRenderPass*, int>* > s_refs; 
static void clearRefs() {for (auto r : s_refs) { delete r;}}
}
#include <UI/imgui/imgui.h>
void ChunkedAdaptiveRenderPass::imguiInterface(bool* open)
{
	if (!ImGui::Begin("Chunk Profiler", open, ImVec2(300, 300), -1.0f, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) return;

	int numCols = std::ceil( (float) m_viewportSize.x / (float) m_chunkSize.x);
	int numRows = std::ceil( (float) m_viewportSize.y / (float) m_chunkSize.y);
	
	// clean up from last iteration 
	clearRefs(); s_refs.resize(numCols * numRows);

	ImGui::Columns( numCols, "", true );
	for ( int i = numRows-1; i > 0; i--)
	{
		ImGui::Separator();
		for ( int j = 0; j < numCols; j++)
		{
			int idx = i * numCols + j;
			if (idx < m_timingsBuffer.size())
			{
				//std::string label = std::to_string(i) + "_" + std::to_string(j);
				std::string label = "";
				static auto value_getter = [](void* data, int idx){
					std::pair<ChunkedAdaptiveRenderPass*, int>* ref = static_cast< std::pair <ChunkedAdaptiveRenderPass*, int>* >(data);	
					return ref->first->getTimingsBuffer()[ref->second][ (ref->first->getCurrentFrameIdx() + idx) % ref->first->getTimingsBufferSize()];
				};

				//ImGui::PlotLines( label.c_str(), &m_timingsBuffer[idx][0], m_timingsBuffer[idx].size(), 0, NULL, 0.0, 1.0, ImVec2(0,ImGui::GetColumnWidth() - 20)); 
				s_refs[idx] = new std::pair<ChunkedAdaptiveRenderPass*, int> (this, idx);
				ImGui::PlotHistogram(
					label.c_str(), 
					value_getter,
					(void*) s_refs[idx],
					(int) m_timingsBuffer[idx].size(),
					0, NULL,
					0.0, 1.0,
					ImVec2(ImGui::GetColumnWidth()-10,ImGui::GetColumnWidth()-10)); 
				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns( 1 );
	ImGui::Separator();

	ImGui::End();
}
