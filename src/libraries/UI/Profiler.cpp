#include "Profiler.h"

#include <UI/imgui/imgui.h>
void Profiler::imguiInterface(float startTime, float endTime)
{
	ImGui::Begin("Profiler", NULL, ImVec2(600,100));
	ImGui::Separator();
	// range
	float wWidth = (float) ImGui::GetWindowContentRegionWidth();
	float r = endTime - startTime;
	
	// MARKERS
	for (int i = 0; i < m_markerTimes.size(); i++)
	{
		int x = ((m_markerTimes[i] - startTime) / r) * wWidth;
		if ( x < 0 || x > wWidth ) { continue; }
		ImGui::SameLine(x);
		ImGui::BeginGroup();
			ImGui::Button("", ImVec2(0,0));
			ImGui::Text(m_markerTags[i].c_str());
		ImGui::EndGroup();
	}

	ImGui::Separator();

	// RANGES
	for (int i = 0; i < m_startTimes.size(); i++)
	{
		int xStart = ((m_startTimes[i] - startTime) / r) * wWidth;
		int xEnd =   ((m_endTimes[i] - startTime) / r) * wWidth;

		if ( xEnd < 0 || xStart > wWidth )
		{
			continue;
		}
		if ( xStart < 0 )
		{
			xStart = 0;
		}
		if (xEnd > wWidth) 
		{
			xEnd = wWidth;
		}

		ImGui::SameLine(xStart);
		ImGui::BeginGroup();
		ImGui::Button(m_rangeTags[i].c_str(), ImVec2(xEnd-xStart,0));
		ImGui::EndGroup();
	}

	ImGui::End();
}
