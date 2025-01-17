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
	@brief Implementation of MainWindow
 */
#include "ngscopeclient.h"
#include "MainWindow.h"

//Dock builder API is not yet public, so might change...
#include "imgui_internal.h"

//Dialogs
#include "AddGeneratorDialog.h"
#include "AddMultimeterDialog.h"
#include "AddPowerSupplyDialog.h"
#include "AddScopeDialog.h"
#include "FunctionGeneratorDialog.h"
#include "MultimeterDialog.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MainWindow::MainWindow(vk::raii::Queue& queue)
	: VulkanWindow("ngscopeclient", queue)
	, m_showDemo(true)
	, m_showPlot(false)
	, m_session(this)
{
	m_waveformGroups.push_back(make_shared<WaveformGroup>("Waveform Group 1", 2));
	m_waveformGroups.push_back(make_shared<WaveformGroup>("Waveform Group 2", 3));

	LoadRecentInstrumentList();

	//Set up a better font.
	//Add default Latin-1 glyph ranges plus some Greek letters and symbols we use
	ImGuiIO& io = ImGui::GetIO();
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
	builder.AddChar(L'°');
	for(wchar_t i=0x370; i<=0x3ff; i++)	//Greek and Coptic
		builder.AddChar(i);

	ImVector<ImWchar> ranges;
	builder.BuildRanges(&ranges);

	auto font = io.Fonts->AddFontFromFileTTF(
		FindDataFile("fonts/DejaVuSans.ttf").c_str(),
		13,
		nullptr,
		ranges.Data);
	io.Fonts->Build();
	io.FontDefault = font;
}

MainWindow::~MainWindow()
{
	CloseSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Session termination

void MainWindow::CloseSession()
{
	//TODO: clear the actual session object

	SaveRecentInstrumentList();

	//Clear any open dialogs before destroying the session.
	//This ensures that we have a nice well defined shutdown order.
	m_meterDialogs.clear();
	m_generatorDialogs.clear();
	m_dialogs.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void MainWindow::DoRender(vk::raii::CommandBuffer& /*cmdBuf*/)
{

}

void MainWindow::RenderUI()
{
	//Menu for main window
	MainMenu();

	//Docking area to put all of the groups in
	DockingArea();

	//Waveform groups
	for(auto g : m_waveformGroups)
		g->Render();

	//Dialog boxes
	set< shared_ptr<Dialog> > dlgsToClose;
	for(auto& dlg : m_dialogs)
	{
		if(!dlg->Render())
			dlgsToClose.emplace(dlg);
	}
	for(auto& dlg : dlgsToClose)
	{
		//Multimeter dialogs are stored in a separate list as well
		auto meterDlg = dynamic_pointer_cast<MultimeterDialog>(dlg);
		if(meterDlg)
			m_meterDialogs.erase(meterDlg->GetMeter());

		//Function generator dialogs are stored in a separate list as well
		auto genDlg = dynamic_pointer_cast<FunctionGeneratorDialog>(dlg);
		if(genDlg)
			m_generatorDialogs.erase(genDlg->GetGenerator());

		m_dialogs.erase(dlg);
	}

	//DEBUG: draw the demo windows
	ImGui::ShowDemoWindow(&m_showDemo);
	//ImPlot::ShowDemoWindow(&m_showPlot);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Top level menu

void MainWindow::AddDialog(shared_ptr<Dialog> dlg)
{
	m_dialogs.emplace(dlg);

	auto mdlg = dynamic_cast<MultimeterDialog*>(dlg.get());
	if(mdlg != nullptr)
		m_meterDialogs[mdlg->GetMeter()] = dlg;

	auto fdlg = dynamic_cast<FunctionGeneratorDialog*>(dlg.get());
	if(fdlg != nullptr)
		m_generatorDialogs[fdlg->GetGenerator()] = dlg;
}

/**
	@brief Run the top level menu bar
 */
void MainWindow::MainMenu()
{
	if(ImGui::BeginMainMenuBar())
	{
		FileMenu();
		ViewMenu();
		AddMenu();
		WindowMenu();
		HelpMenu();
		ImGui::EndMainMenuBar();
	}
}

/**
	@brief Run the File menu
 */
void MainWindow::FileMenu()
{
	if(ImGui::BeginMenu("File"))
	{
		if(ImGui::MenuItem("Exit"))
			glfwSetWindowShouldClose(m_window, 1);

		ImGui::EndMenu();
	}
}

/**
	@brief Run the View menu
 */
void MainWindow::ViewMenu()
{
	if(ImGui::BeginMenu("View"))
	{
		if(ImGui::MenuItem("Fullscreen"))
			SetFullscreen(!m_fullscreen);

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Add menu
 */
void MainWindow::AddMenu()
{
	if(ImGui::BeginMenu("Add"))
	{
		//Make a reverse mapping: timestamp -> instruments last used at that time
		map<time_t, vector<string> > reverseMap;
		for(auto it : m_recentInstruments)
			reverseMap[it.second].push_back(it.first);

		//Get a sorted list of timestamps, most recent first, with no duplicates
		set<time_t> timestampsDeduplicated;
		for(auto it : m_recentInstruments)
			timestampsDeduplicated.emplace(it.second);
		vector<time_t> timestamps;
		for(auto t : timestampsDeduplicated)
			timestamps.push_back(t);
		std::sort(timestamps.begin(), timestamps.end());

		AddGeneratorMenu(timestamps, reverseMap);
		AddMultimeterMenu(timestamps, reverseMap);
		AddOscilloscopeMenu(timestamps, reverseMap);
		AddPowerSupplyMenu(timestamps, reverseMap);

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Add | Generator menu
 */
void MainWindow::AddGeneratorMenu(vector<time_t>& timestamps, map<time_t, vector<string> >& reverseMap)
{
	if(ImGui::BeginMenu("Generator"))
	{
		if(ImGui::MenuItem("Connect..."))
			m_dialogs.emplace(make_shared<AddGeneratorDialog>(m_session));
		ImGui::Separator();

		//Find all known function generator drivers.
		//Any recent instrument using one of these drivers is assumed to be a generator.
		vector<string> drivers;
		SCPIFunctionGenerator::EnumDrivers(drivers);
		set<string> driverset;
		for(auto s : drivers)
			driverset.emplace(s);

		//Recent instruments
		for(int i=timestamps.size()-1; i>=0; i--)
		{
			auto t = timestamps[i];
			auto cstrings = reverseMap[t];
			for(auto cstring : cstrings)
			{
				auto fields = explode(cstring, ':');
				auto nick = fields[0];
				auto drivername = fields[1];
				auto transname = fields[2];

				if(driverset.find(drivername) != driverset.end())
				{
					if(ImGui::MenuItem(nick.c_str()))
					{
						auto path = fields[3];
						for(size_t j=4; j<fields.size(); j++)
							path = path + ":" + fields[j];

						auto transport = MakeTransport(transname, path);
						if(transport != nullptr)
						{
							//Create the scope
							auto gen = SCPIFunctionGenerator::CreateFunctionGenerator(drivername, transport);
							if(gen == nullptr)
							{
								ShowErrorPopup(
									"Driver error",
									"Failed to create function generator driver of type \"" + drivername + "\"");
								delete transport;
							}

							else
							{
								//TODO: apply preferences
								LogDebug("FIXME: apply PreferenceManager settings to newly created generator\n");

								gen->m_nickname = nick;
								m_session.AddFunctionGenerator(gen);
							}
						}
					}
				}
			}
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Add | Multimeter menu
 */
void MainWindow::AddMultimeterMenu(vector<time_t>& timestamps, map<time_t, vector<string> >& reverseMap)
{
	if(ImGui::BeginMenu("Multimeter"))
	{
		if(ImGui::MenuItem("Connect..."))
			m_dialogs.emplace(make_shared<AddMultimeterDialog>(m_session));
		ImGui::Separator();

		//Find all known multimeter drivers.
		//Any recent instrument using one of these drivers is assumed to be a multimeter.
		vector<string> drivers;
		SCPIMultimeter::EnumDrivers(drivers);
		set<string> driverset;
		for(auto s : drivers)
			driverset.emplace(s);

		//Recent instruments
		for(int i=timestamps.size()-1; i>=0; i--)
		{
			auto t = timestamps[i];
			auto cstrings = reverseMap[t];
			for(auto cstring : cstrings)
			{
				auto fields = explode(cstring, ':');
				auto nick = fields[0];
				auto drivername = fields[1];
				auto transname = fields[2];

				if(driverset.find(drivername) != driverset.end())
				{
					if(ImGui::MenuItem(nick.c_str()))
					{
						auto path = fields[3];
						for(size_t j=4; j<fields.size(); j++)
							path = path + ":" + fields[j];

						auto transport = MakeTransport(transname, path);
						if(transport != nullptr)
						{
							//Create the scope
							auto meter = SCPIMultimeter::CreateMultimeter(drivername, transport);
							if(meter == nullptr)
							{
								ShowErrorPopup(
									"Driver error",
									"Failed to create multimeter driver of type \"" + drivername + "\"");
								delete transport;
							}

							else
							{
								//TODO: apply preferences
								LogDebug("FIXME: apply PreferenceManager settings to newly created meter\n");

								meter->m_nickname = nick;
								m_session.AddMultimeter(meter);
							}
						}
					}
				}
			}
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Add | Oscilloscope menu
 */
void MainWindow::AddOscilloscopeMenu(vector<time_t>& timestamps, map<time_t, vector<string> >& reverseMap)
{
	if(ImGui::BeginMenu("Oscilloscope"))
	{
		if(ImGui::MenuItem("Connect..."))
			m_dialogs.emplace(make_shared<AddScopeDialog>(m_session));
		ImGui::Separator();

		//Find all known scope drivers.
		//Any recent instrument using one of these drivers is assumed to be a scope.
		vector<string> drivers;
		Oscilloscope::EnumDrivers(drivers);
		set<string> driverset;
		for(auto s : drivers)
			driverset.emplace(s);

		//Recent instruments
		for(int i=timestamps.size()-1; i>=0; i--)
		{
			auto t = timestamps[i];
			auto cstrings = reverseMap[t];
			for(auto cstring : cstrings)
			{
				auto fields = explode(cstring, ':');
				auto nick = fields[0];
				auto drivername = fields[1];
				auto transname = fields[2];

				if(driverset.find(drivername) != driverset.end())
				{
					if(ImGui::MenuItem(nick.c_str()))
					{
						auto path = fields[3];
						for(size_t j=4; j<fields.size(); j++)
							path = path + ":" + fields[j];

						auto transport = MakeTransport(transname, path);
						if(transport != nullptr)
						{
							//Create the scope
							auto scope = Oscilloscope::CreateOscilloscope(drivername, transport);
							if(scope == nullptr)
							{
								ShowErrorPopup(
									"Driver error",
									"Failed to create oscilloscope driver of type \"" + drivername + "\"");
								delete transport;
							}

							else
							{
								//TODO: apply preferences
								LogDebug("FIXME: apply PreferenceManager settings to newly created scope\n");

								scope->m_nickname = nick;
								m_session.AddOscilloscope(scope);
							}
						}
					}
				}
			}
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Add | Power Supply menu
 */
void MainWindow::AddPowerSupplyMenu(vector<time_t>& timestamps, map<time_t, vector<string> >& reverseMap)
{
	if(ImGui::BeginMenu("Power Supply"))
	{
		if(ImGui::MenuItem("Connect..."))
			m_dialogs.emplace(make_shared<AddPowerSupplyDialog>(m_session));

		ImGui::Separator();

		//Find all known PSU drivers.
		//Any recent instrument using one of these drivers is assumed to be a PSU.
		vector<string> drivers;
		SCPIPowerSupply::EnumDrivers(drivers);
		set<string> driverset;
		for(auto s : drivers)
			driverset.emplace(s);

		//Recent instruments
		for(int i=timestamps.size()-1; i>=0; i--)
		{
			auto t = timestamps[i];
			auto cstrings = reverseMap[t];
			for(auto cstring : cstrings)
			{
				auto fields = explode(cstring, ':');
				auto nick = fields[0];
				auto drivername = fields[1];
				auto transname = fields[2];

				if(driverset.find(drivername) != driverset.end())
				{
					if(ImGui::MenuItem(nick.c_str()))
					{
						auto path = fields[3];
						for(size_t j=4; j<fields.size(); j++)
							path = path + ":" + fields[j];

						auto transport = MakeTransport(transname, path);
						if(transport != nullptr)
						{
							//Create the PSU
							auto psu = SCPIPowerSupply::CreatePowerSupply(drivername, transport);
							if(psu == nullptr)
							{
								ShowErrorPopup(
									"Driver error",
									"Failed to create PSU driver of type \"" + drivername + "\"");
								delete transport;
							}

							else
							{
								//TODO: apply preferences
								LogDebug("FIXME: apply PreferenceManager settings to newly created PSU\n");

								psu->m_nickname = nick;
								m_session.AddPowerSupply(psu);
							}
						}
					}
				}
			}
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Window menu
 */
void MainWindow::WindowMenu()
{
	if(ImGui::BeginMenu("Window"))
	{
		WindowGeneratorMenu();
		WindowMultimeterMenu();
		ImGui::EndMenu();
	}
}

/**
	@brief Run the Window | Generator menu

	This menu is used for connecting to a function generator that is part of an oscilloscope.
 */
void MainWindow::WindowGeneratorMenu()
{
	if(ImGui::BeginMenu("Generator"))
	{
		auto scopes = m_session.GetScopes();
		for(auto scope : scopes)
		{
			//Is the scope also a function generator? If not, skip it
			if( (scope->GetInstrumentTypes() & Instrument::INST_FUNCTION) == 0)
				continue;

			//Do we already have a dialog open for it? If so, don't make another
			auto generator = dynamic_cast<SCPIFunctionGenerator*>(scope);
			if(m_generatorDialogs.find(generator) != m_generatorDialogs.end())
				continue;

			//Add it to the menu
			if(ImGui::MenuItem(generator->m_nickname.c_str()))
				m_session.AddFunctionGenerator(generator);
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Window | Multimeter menu
 */
void MainWindow::WindowMultimeterMenu()
{
	if(ImGui::BeginMenu("Multimeter"))
	{
		auto scopes = m_session.GetScopes();
		for(auto scope : scopes)
		{
			//Is the scope also a multimeter? If not, skip it
			if( (scope->GetInstrumentTypes() & Instrument::INST_DMM) == 0)
				continue;

			//Do we already have a dialog open for it? If so, don't make another
			auto meter = dynamic_cast<SCPIMultimeter*>(scope);
			if(m_meterDialogs.find(meter) != m_meterDialogs.end())
				continue;

			//Add it to the menu
			if(ImGui::MenuItem(scope->m_nickname.c_str()))
				m_session.AddMultimeter(meter);
		}

		ImGui::EndMenu();
	}
}

/**
	@brief Run the Help menu
 */
void MainWindow::HelpMenu()
{
	if(ImGui::BeginMenu("Help"))
	{
		ImGui::EndMenu();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform views etc

void MainWindow::DockingArea()
{
	//Provide a space we can dock windows into
	auto viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags host_window_flags = 0;
	host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
	host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	char label[32];
	ImFormatString(label, IM_ARRAYSIZE(label), "DockSpaceViewport_%08X", viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(label, NULL, host_window_flags);
	ImGui::PopStyleVar(3);

	auto dockspace_id = ImGui::GetID("DockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), /*dockspace_flags*/0, /*window_class*/nullptr);
	ImGui::End();

	//DEBUG: do initial split of our waveform groups into the dock space
	static bool first = true;
	if(first)
	{
		//Clear out existing docks
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

		ImGuiID idLeft;
		ImGuiID idRight;
		/*auto idParent =*/ ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.5, &idLeft, &idRight);

		ImGui::DockBuilderDockWindow(m_waveformGroups[0]->GetTitle().c_str(), idLeft);
		ImGui::DockBuilderDockWindow(m_waveformGroups[1]->GetTitle().c_str(), idRight);

		ImGui::DockBuilderFinish(dockspace_id);

		first = false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Other GUI handlers

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Recent instruments

void MainWindow::LoadRecentInstrumentList()
{
	try
	{
		auto docs = YAML::LoadAllFromFile(m_preferences.GetConfigDirectory() + "/recent.yml");
		if(docs.empty())
			return;
		auto node = docs[0];

		for(auto it : node)
		{
			auto inst = it.second;
			m_recentInstruments[inst["path"].as<string>()] = inst["timestamp"].as<long long>();
		}
	}
	catch(const YAML::BadFile& ex)
	{
		LogDebug("Unable to open recently used instruments file\n");
		return;
	}

}

void MainWindow::SaveRecentInstrumentList()
{
	auto path = m_preferences.GetConfigDirectory() + "/recent.yml";
	FILE* fp = fopen(path.c_str(), "w");

	for(auto it : m_recentInstruments)
	{
		auto nick = it.first.substr(0, it.first.find(":"));
		fprintf(fp, "%s:\n", nick.c_str());
		fprintf(fp, "    path: \"%s\"\n", it.first.c_str());
		fprintf(fp, "    timestamp: %ld\n", it.second);
	}

	fclose(fp);
}

void MainWindow::AddToRecentInstrumentList(SCPIInstrument* inst)
{
	if(inst == nullptr)
		return;

	auto now = time(NULL);

	auto connectionString =
		inst->m_nickname + ":" +
		inst->GetDriverName() + ":" +
		inst->GetTransportName() + ":" +
		inst->GetTransportConnectionString();
	m_recentInstruments[connectionString] = now;

	//Delete anything old
	//TODO: have a preference for this
	const int maxRecentInstruments = 20;
	while(m_recentInstruments.size() > maxRecentInstruments)
	{
		string oldestPath = "";
		time_t oldestTime = now;

		for(auto it : m_recentInstruments)
		{
			if(it.second < oldestTime)
			{
				oldestTime = it.second;
				oldestPath = it.first;
			}
		}

		m_recentInstruments.erase(oldestPath);
	}
}

/**
	@brief Helper function for creating a transport and printing an error if the connection is unsuccessful
 */
SCPITransport* MainWindow::MakeTransport(const string& trans, const string& args)
{
	//Create the transport
	auto transport = SCPITransport::CreateTransport(trans, args);
	if(transport == nullptr)
	{
		ShowErrorPopup(
			"Transport error",
			"Failed to create transport of type \"" + trans + "\"");
		return nullptr;
	}

	//Make sure we connected OK
	if(!transport->IsConnected())
	{
		delete transport;
		ShowErrorPopup("Connection error", "Failed to connect to \"" + args + "\"");
		return nullptr;
	}

	return transport;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error messages

/**
	@brief Opens the error popup
 */
void MainWindow::ShowErrorPopup(const string& title, const string& msg)
{
	ImGui::OpenPopup(title.c_str());
	m_errorPopupTitle = title;
	m_errorPopupMessage = msg;
}

/**
	@brief Popup message when we fail to connect
 */
void MainWindow::RenderErrorPopup()
{
	if(ImGui::BeginPopupModal(m_errorPopupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text(m_errorPopupMessage.c_str());
		ImGui::Separator();
		if(ImGui::Button("OK"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
