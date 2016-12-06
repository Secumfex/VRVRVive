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

ChunkedAdaptiveRenderPass::ChunkedAdaptiveRenderPass(RenderPass* pRenderPass, glm::ivec2 viewportSize, glm::ivec2 chunkSize, int timingsBufferSize, float targetRenderTime, float bias )
	: ChunkedRenderPass(pRenderPass, viewportSize, chunkSize),
	m_timingsBufferSize(timingsBufferSize),
	m_totalRenderTimesBuffer(timingsBufferSize),
	m_currentChunkIdx(0),
	m_currentFrameIdx(0),
	m_currentBackQuery(0),
	m_currentFrontQuery(1),
	m_renderTimeBias(bias),
	m_targetRenderTime(targetRenderTime),
	m_autoAdjustRenderTime(false),
	m_numChunksBuffer(16),
	m_currentIterationIdx(0)
{
	//initialize timings buffers
	resetTimingsBuffers();
}

void ChunkedAdaptiveRenderPass::render()
{
	//++++++ Predict number of iterations +++++++++++++++
	int numChunksToRender = 0;
	float predictedIterationRenderTime = 0.0f;
	for (int i = m_currentChunkIdx; i < m_queryBuffer.size(); i++)
	{
		predictedIterationRenderTime += predictChunkRenderTime(i);
		if ( predictedIterationRenderTime<= m_targetRenderTime )
		{
			numChunksToRender++; // good to go
		}
	}
	numChunksToRender = std::max( numChunksToRender, 1); // minimum 1 chunk

	m_numChunksBuffer[ m_currentIterationIdx ]=(float) numChunksToRender;
	m_currentIterationIdx = (m_currentIterationIdx+1)%m_numChunksBuffer.size();
	//++++++++++++++++++++++++++++

	for (int i = m_currentChunkIdx; i < std::min<int>(m_currentChunkIdx + numChunksToRender, m_queryBuffer.size()); i++)
	{
		if (m_isFinished)
		{
			profileTimings();
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
	}
	else
	{
		m_currentChunkIdx = m_currentChunkIdx + numChunksToRender;
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
namespace {int mod(int a, int b)
{ return (a%b+b)%b; }}
float ChunkedAdaptiveRenderPass::predictChunkRenderTime(int idx)
{
	float predicted = 1.0f;
	int numCols = std::ceil( (float) m_viewportSize.x / (float) m_chunkSize.x);
	int numRows = std::ceil( (float) m_viewportSize.y / (float) m_chunkSize.y);
	int idx_ = idx % m_timingsBuffer.size();
	int x = idx_ % numCols;
	int y = idx_ / numCols;
	int lastProfiledFrameIdx = mod((m_currentFrameIdx - 1), m_timingsBufferSize);

	// retrieve this chunk's last render time
	predicted = m_timingsBuffer[idx_].back();

	// look at render times neighbouring chunks (4-neighborhood)
	// always interpolate towards the maximum in the neighbourhood
	if ( x < (numCols - 1) ) // there is a right-hand chunk
	{
		float r = m_timingsBuffer[idx_+1][lastProfiledFrameIdx]; 
		predicted = std::max( predicted, predicted + 0.5f * (r - predicted) );
	}

	if ( x > 0 ) // there is a left-hand chunk
	{
		float l = m_timingsBuffer[idx_-1][lastProfiledFrameIdx]; 
		predicted = std::max( predicted, predicted + 0.5f * (l - predicted) );
	}
	if ( y < (numRows - 1) ) // there is a top chunk
	{
		float t = m_timingsBuffer[idx_+ numCols][lastProfiledFrameIdx]; 
		predicted = std::max( predicted, predicted + 0.5f * (t - predicted) );
	}
	if ( y > 0 ) // there is a bottom chunk
	{
		float b = m_timingsBuffer[idx_ - numCols][lastProfiledFrameIdx]; 
		predicted = std::max( predicted, predicted + 0.5f * (b - predicted) );
	}
	
	// apply conservative bias and return predicted value
	return predicted * m_renderTimeBias;
}


namespace { const double NANOSECONDS_TO_MILLISECONDS = 1.0 / 1000000.0; }
void ChunkedAdaptiveRenderPass::profileTimings(){
	float totalRenderTime = 0.0f;
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

		totalRenderTime += renderTime;
	}

	m_totalRenderTimesBuffer[m_currentFrameIdx] = totalRenderTime;
	m_currentFrameIdx = (m_currentFrameIdx + 1) % m_timingsBufferSize;
	swapQueryBuffers();
}

namespace { static std::vector< std::pair<ChunkedAdaptiveRenderPass*, int>* > s_refs; 
static void clearRefs() {for (auto r : s_refs) { delete r;}}
}
#include <UI/imgui/imgui.h>
void ChunkedAdaptiveRenderPass::imguiInterface(bool* open)
{
	if (!ImGui::Begin("Chunk Profiler", open, ImVec2(300, 370), -1.0f, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) return;

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

				ImGui::SameLine(0,0);
				s_refs[idx] = new std::pair<ChunkedAdaptiveRenderPass*, int> (this, idx);
				//ImGui::PlotLines(
				ImGui::PlotHistogram(
					label.c_str(), 
					value_getter,
					(void*) s_refs[idx],
					(int) m_timingsBuffer[idx].size(),
					0,
					std::to_string((m_timingsBuffer[idx][mod(m_currentFrameIdx - 1, m_timingsBufferSize)])).c_str(),
					0.0, 1.0,
					ImVec2(ImGui::GetColumnWidth(),ImGui::GetColumnWidth())); 
				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns( 1 );
	ImGui::Separator();

	//++++ Adaptive behaviour ++++//
	ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
	ImGui::SliderFloat("Render Time Bias", &getRenderTimeBias(), 0.5f, 2.0f);
	ImGui::SliderFloat("Target Render Time", &getTargetRenderTime(), 0.0f, 20.0f);  
	//ImGui::Checkbox("Auto Adjust Render Time", &getAutoAdjustRenderTime());  
	ImGui::PopItemWidth();
	
	//++++ View number of number of rendered chunks ++++//
	if (ImGui::CollapsingHeader("Total Render Time"))
	{
		ImGui::PlotHistogram(
			"total Render Time",
			&m_totalRenderTimesBuffer[0],
			m_totalRenderTimesBuffer.size(),
			0,
			std::to_string( m_totalRenderTimesBuffer[mod(m_currentFrameIdx-1, m_totalRenderTimesBuffer.size())]).c_str(),
			0.0f,
			m_queryBuffer.size(),
			ImVec2(0,ImGui::GetTextLineHeight()*3));
	}

	//++++ View number of number of rendered chunks ++++//
	if (ImGui::CollapsingHeader("Number of Rendered Chunks Profiler"))
	{
		ImGui::PlotHistogram(
			"numChunksToRender",
			&m_numChunksBuffer[0],
			m_numChunksBuffer.size(),
			0,
			std::to_string((int) m_numChunksBuffer[mod(m_currentIterationIdx-1, m_numChunksBuffer.size())]).c_str(),
			0.0f,
			m_queryBuffer.size(),
			ImVec2(0,ImGui::GetTextLineHeight()*3));
	}

	//++++ Debug output of Predicted RenderTimes ++++//
 	if( ImGui::Button("Print Predicted Render Times") )
	{
		for (int i = 0; i < m_timingsBuffer.size(); i++)
		{
			DEBUGLOG->log( std::to_string( i%numCols ) + "," + std::to_string( i/numRows )  + ": " + std::to_string(predictChunkRenderTime(i)) );
		}
	}

	ImGui::End();
}

ChunkedAdaptiveRenderPass::~ChunkedAdaptiveRenderPass()
{
	for (auto e : m_queryBuffer)
	{
		glDeleteQueries(e.size(), &e[0]);
	}

	// just in case they still existed
	clearRefs();
}
