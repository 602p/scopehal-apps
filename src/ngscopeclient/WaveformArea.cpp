/***********************************************************************************************************************
*                                                                                                                      *
* glscopeclient                                                                                                        *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of WaveformArea
 */
#include "ngscopeclient.h"
#include "WaveformArea.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WaveformArea::WaveformArea()
{
	m_displayedChannels.push_back(make_shared<DisplayedChannel>("ohai"));
	m_displayedChannels.push_back(make_shared<DisplayedChannel>("asdf"));
}

WaveformArea::~WaveformArea()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void WaveformArea::Render(int iArea, int numAreas, ImVec2 clientArea)
{
	auto height = (clientArea.y - ImGui::GetFrameHeightWithSpacing()) / numAreas;
	if(ImGui::BeginChild(ImGui::GetID(this), ImVec2(clientArea.x, height)))
	{
		auto csize = ImGui::GetContentRegionAvail();
		auto start = ImGui::GetWindowContentRegionMin();

		//Draw texture for the actual waveform
		//(todo: repeat for each channel)
		ImTextureID my_tex_id = ImGui::GetIO().Fonts->TexID;
		ImGui::Image(my_tex_id, ImVec2(csize.x, csize.y),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
		ImGui::SetItemAllowOverlap();

		//Drag/drop areas for splitting
		float widthOfVerticalEdge = csize.x*0.25;
		float leftOfMiddle = start.x + widthOfVerticalEdge;
		float rightOfMiddle = start.x + csize.x*0.75;
		float topOfMiddle = start.y;
		float bottomOfMiddle = start.y + csize.y;
		float widthOfMiddle = rightOfMiddle - leftOfMiddle;
		if(iArea == 0)
		{
			DropArea("top", ImVec2(leftOfMiddle, start.y), ImVec2(widthOfMiddle, csize.y*0.125));
			topOfMiddle += csize.y * 0.125;
		}
		if(iArea == (numAreas-1))
		{
			bottomOfMiddle -= csize.y * 0.125;
			DropArea("bottom", ImVec2(leftOfMiddle, bottomOfMiddle), ImVec2(widthOfMiddle, csize.y*0.125));
		}
		float heightOfMiddle = bottomOfMiddle - topOfMiddle;
		DropArea("middle", ImVec2(leftOfMiddle, topOfMiddle), ImVec2(widthOfMiddle, heightOfMiddle));
		DropArea("left", ImVec2(start.x, topOfMiddle), ImVec2(widthOfVerticalEdge, heightOfMiddle));
		DropArea("right", ImVec2(rightOfMiddle, topOfMiddle), ImVec2(widthOfVerticalEdge, heightOfMiddle));

		//Draw control widgets
		ImGui::SetCursorPos(ImGui::GetWindowContentRegionMin());
		ImGui::BeginGroup();

			for(auto c : m_displayedChannels)
				DraggableButton(c);

		ImGui::EndGroup();
		ImGui::SetItemAllowOverlap();
	}
	ImGui::EndChild();
}

void WaveformArea::DropArea(const string& name, ImVec2 start, ImVec2 size)
{
	ImGui::SetCursorPos(start);
	ImGui::InvisibleButton(name.c_str(), size);
	ImGui::SetItemAllowOverlap();

	//Add drop target
	if(ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Waveform");
		if( (payload != nullptr) && (payload->DataSize == sizeof(DisplayedChannel*)) )
		{
			auto value = reinterpret_cast<DisplayedChannel*>(payload->Data);

			//TODO: process payload
			LogDebug("Waveform %s dropped in %s\n", value->GetName().c_str(), name.c_str());
		}

		ImGui::EndDragDropTarget();
	}
}

void WaveformArea::DraggableButton(shared_ptr<DisplayedChannel> chan)
{
	ImGui::Button(chan->GetName().c_str());

	if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		ImGui::SetDragDropPayload("Waveform", chan.get(), sizeof(DisplayedChannel*));

		//Preview of what we're dragging
		ImGui::Text("Drag %s", chan->GetName().c_str());

		ImGui::EndDragDropSource();
	}
}
