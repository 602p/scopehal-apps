/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg	                                                                       *
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

#include "scopehal.h"
#include "TektronixOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OSCILLOSCOPE_INITPROC_CPP(TektronixOscilloscope)

TektronixOscilloscope::TektronixOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_bandwidth(1000)
{
	//Figure out what device family we are
	if(m_model.find("MSO5") == 0)
		m_family = FAMILY_MSO5;
	else if(m_model.find("MSO6") == 0)
		m_family = FAMILY_MSO6;
	else
		m_family = FAMILY_UNKNOWN;

	//Last digit of the model number is the number of channels
	std::string model_number = m_model;
	model_number.erase(
		std::remove_if(
			model_number.begin(),
			model_number.end(),
			[]( char const& c ) -> bool { return !std::isdigit(c); }
		),
		model_number.end()
	);
	int nchans = stoi(model_number) % 10;

	// No header in the reply of queries
	m_transport->SendCommand("HEAD 0");

	//Device specific initialization
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand("ACQ:MOD SAM");		//actual sampled data, no averaging etc
			m_transport->SendCommand("VERB OFF");			//Disable verbose mode (send shorter commands)
			m_transport->SendCommand("DAT:ENC SRI");		//signed, little endian binary
			m_transport->SendCommand("DAT:WID 2");			//16-bit data
			m_transport->SendCommand("ACQ:STOPA SEQ");		//Stop after acquiring a single waveform
			m_transport->SendCommand("CONFIG:ANALO:BANDW?");
			m_bandwidth = stof(m_transport->ReadReply()) * 1e-6;
			LogDebug("Instrument has %d MHz bandwidth\n", m_bandwidth);
			break;

		default:
			// 8-bit signed data
			m_transport->SendCommand("DATA:ENC RIB;WID 1");
			m_transport->SendCommand("DATA:SOURCE CH1, CH2, CH3, CH4;START 0; STOP 100000");

			// FIXME: where to put this?
			m_transport->SendCommand("ACQ:STOPA SEQ;REPE 1");
			break;
	}

	//TODO: get colors for channels 5-8 on wide instruments
	const char* colors_default[4] = { "#ffff00", "#32ff00", "#5578ff", "#ff0084" };	//yellow-green-violet-pink
	const char* colors_mso56[4] = { "#ffff00", "#20d3d8", "#f23f59", "#f16727" };	//yellow-cyan-pink-orange

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CH") + to_string(i+1);

		//Color the channels based on Tektronix's standard color sequence
		string color = "#ffffff";
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				color = colors_mso56[i % 4];
				break;

			default:
				color = colors_default[i % 4];
				break;
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			color,
			1,
			i,
			true));
	}
	m_analogChannelCount = nchans;

	//Add the external trigger input
	switch(m_family)
	{
		//MSO5 does not appear to have an external trigger input
		//except in low-profile rackmount models (not yet supported)
		case FAMILY_MSO5:
			m_extTrigChannel = NULL;
			break;

		//MSO6 calls it AUX, not EXT
		case FAMILY_MSO6:
			m_extTrigChannel = new OscilloscopeChannel(
				this,
				"AUX",
				OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
				"",
				1,
				m_channels.size(),
				true);
			m_channels.push_back(m_extTrigChannel);
			break;

		default:
			m_extTrigChannel = new OscilloscopeChannel(
				this,
				"EX",
				OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
				"",
				1,
				m_channels.size(),
				true);
			m_channels.push_back(m_extTrigChannel);
			break;
	}

	//See what options we have
	vector<string> options;
	/*
	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			break;

		default:
			{
				for (std::string::size_type prev_pos=0, pos=0;
					 (pos = reply.find(',', pos)) != std::string::npos;
					 prev_pos=++pos)
				{
					std::string opt( reply.substr(prev_pos, pos-prev_pos) );
					if (opt == "0")
						continue;
					if (opt.substr(opt.length()-3, 3) == "(d)")
						opt.erase(opt.length()-3);

					options.push_back(opt);
				}
			}
			break;
	}
	*/

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto opt : options)
	{
		LogDebug("* %s (unknown)\n", opt.c_str());
	}
}

TektronixOscilloscope::~TektronixOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixOscilloscope::GetDriverNameInternal()
{
	return "tektronix";
}

unsigned int TektronixOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void TektronixOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelCouplings.clear();
	m_channelsEnabled.clear();

	delete m_trigger;
	m_trigger = NULL;
}

bool TektronixOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
		return m_channelsEnabled[i];

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(string("DISP:GLOB:") + m_channels[i]->GetHwname() + ":STATE?");
	string reply = m_transport->ReadReply();

	if(reply == "0")
	{
		m_channelsEnabled[i] = false;
		return false;
	}
	else
	{
		m_channelsEnabled[i] = true;
		return true;
	}
}

void TektronixOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DISP:GLOB:") + m_channels[i]->GetHwname() + ":STATE ON");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void TektronixOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DISP:GLOB:") + m_channels[i]->GetHwname() + ":STATE OFF");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType TektronixOscilloscope::GetChannelCoupling(size_t i)
{
	//Check cache first
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	OscilloscopeChannel::CouplingType coupling = OscilloscopeChannel::COUPLE_DC_1M;
	{
		lock_guard<recursive_mutex> lock2(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				{
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
					string coup = m_transport->ReadReply();
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TER?");
					float nterm = stof(m_transport->ReadReply());

					//TODO: Tek's 1 GHz passive probes are 250K ohm impedance at the scope side.
					//We report anything other than 50 ohm as 1M because scopehal doesn't have API support for that.
					if(coup == "AC")
						coupling = OscilloscopeChannel::COUPLE_AC_1M;
					else if(nterm == 50)
						coupling = OscilloscopeChannel::COUPLE_DC_50;
					else
						coupling = OscilloscopeChannel::COUPLE_DC_1M;
				}
				break;

			default:

				// FIXME
				coupling = OscilloscopeChannel::COUPLE_DC_1M;
			/*
			#if 0

				m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
				string coup_reply = m_transport->ReadReply();
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP?");
				string imp_reply = m_transport->ReadReply();

				OscilloscopeChannel::CouplingType coupling;
				if(coup_reply == "AC")
					coupling = OscilloscopeChannel::COUPLE_AC_1M;
				else if(coup_reply == "DC")
				{
					if(imp_reply == "ONEM")
						coupling = OscilloscopeChannel::COUPLE_DC_1M;
					else if(imp_reply == "FIFT")
						coupling = OscilloscopeChannel::COUPLE_DC_50;
				}
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_channelCouplings[i] = coupling;
				return coupling;
			#endif
			*/
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
}

void TektronixOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 50");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;

		default:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP FIFT");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelCouplings[i] = type;
}

double TektronixOscilloscope::GetChannelAttenuation(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PRO:GAIN?");
				float probegain = stof(m_transport->ReadReply());
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROBEF:EXTA?");
				float extatten = stof(m_transport->ReadReply());

				//Calculate the overall system attenuation.
				//Note that probes report *gain* while the external is *attenuation*.
				double atten = extatten / probegain;
				m_channelAttenuations[i] = atten;
				return atten;
			}
			break;

		default:
			// FIXME

			/*
			lock_guard<recursive_mutex> lock(m_mutex);

			m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

			string reply = m_transport->ReadReply();
			double atten;
			sscanf(reply.c_str(), "%lf", &atten);

			lock_guard<recursive_mutex> lock2(m_cacheMutex);
			m_channelAttenuations[i] = atten;
			return atten;
			*/

			return 1.0;
	}
}

void TektronixOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//This function takes the overall system attenuation as an argument.
				//We need to scale this by the probe gain to figure out the necessary external attenuation.
				//At the moment, this isn't cached, but we probably should do this in the future.
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PRO:GAIN?");
				float probegain = stof(m_transport->ReadReply());

				float extatten = atten * probegain;
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROBEF:EXTA " + to_string(extatten));
			}
			break;

		default:
			//FIXME
			break;
	}
}

int TektronixOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	int bwl = 0;
	{
		lock_guard<recursive_mutex> lock(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				{
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN?");
					string reply = m_transport->ReadReply();
					if(reply == "FUL")		//no limit
						bwl = 0;
					else
						bwl = stof(reply) * 1e-6;

					//If the returned bandwidth is the same as the instrument's upper bound, report "no limit"
					if(bwl == m_bandwidth)
						bwl = 0;
				}
				break;

			default:
				/*
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
				string reply = m_transport->ReadReply();
				int bwl;
				if(reply == "1")
					bwl = 25;
				else
					bwl = 0;
				*/
				break;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bwl;
	return bwl;
}

void TektronixOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelBandwidthLimits[i] = limit_mhz;
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Instrument wants Hz, not MHz, or "FUL" for no limit
				size_t limit_hz = limit_mhz;
				limit_hz *= 1000 * 1000;

				if(limit_mhz == 0)
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN FUL");
				else
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN " + to_string(limit_hz));
			}
			break;

		default:
			break;
	}
}

double TektronixOscilloscope::GetChannelVoltageRange(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":SCA?");
	string reply = m_transport->ReadReply();
	double vdiv;
	sscanf(reply.c_str(), "%lf", &vdiv);

	double range = vdiv * 10;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void TektronixOscilloscope::SetChannelVoltageRange(size_t /*i*/, double /*range*/)
{
	//FIXME
}

OscilloscopeChannel* TektronixOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double TektronixOscilloscope::GetChannelOffset(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");
	string reply = m_transport->ReadReply();

	double offset;
	sscanf(reply.c_str(), "%lf", &offset);
	offset = -offset;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void TektronixOscilloscope::SetChannelOffset(size_t /*i*/, double /*offset*/)
{
	//FIXME
}

Oscilloscope::TriggerMode TektronixOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
	m_transport->SendCommand("TRIG:STATE?");
	string ter = m_transport->ReadReply();

	if(ter == "SAV")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	if(ter == "REA")
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//TODO: AUTO, TRIGGER. For now consider that same as RUN
	return TRIGGER_MODE_RUN;
}

bool TektronixOscilloscope::AcquireData()
{
	//LogDebug("Acquiring data\n");

	map<int, vector<AnalogWaveform*> > pending_waveforms;

	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(!AcquireDataMSO56(pending_waveforms))
				return false;
			break;

		default:
			//m_transport->SendCommand("WFMPRE:" + m_channels[i]->GetHwname() + "?");
				/*
			//		string reply = m_transport->ReadReply();
			//		sscanf(reply.c_str(), "%u,%u,%lu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
			//				&format, &type, &length, &average_count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);

			for(int j=0;j<10;++j)
			m_transport->ReadReply();

			//format = 0;
			//type = 0;
			//average_count = 0;
			xincrement = 1000;
			//xorigin = 0;
			//xreference = 0;
			yincrement = 0.01;
			yorigin = 0;
			yreference = 0;
			length = 500;

			//Figure out the sample rate
			int64_t ps_per_sample = round(xincrement * 1e12f);
			//LogDebug("%ld ps/sample\n", ps_per_sample);

			//LogDebug("length = %d\n", length);

			//Set up the capture we're going to store our data into
			//(no TDC data available on Tektronix scopes?)
			AnalogWaveform* cap = new AnalogWaveform;
			cap->m_timescale = ps_per_sample;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

			//Ask for the data
			m_transport->SendCommand("CURV?");

			char tmp[16] = {0};

			//Read the length header
			m_transport->ReadRawData(2, (unsigned char*)tmp);
			tmp[2] = '\0';
			int numDigits = atoi(tmp+1);
			LogDebug("numDigits = %d\n", numDigits);

			// Read the number of points
			m_transport->ReadRawData(numDigits, (unsigned char*)tmp);
			tmp[numDigits] = '\0';
			int numPoints = atoi(tmp);
			LogDebug("numPoints = %d\n", numPoints);

			uint8_t* temp_buf = new uint8_t[numPoints / sizeof(uint8_t)];

			//Read the actual data
			m_transport->ReadRawData(numPoints, (unsigned char*)temp_buf);
			//Discard trailing newline
			m_transport->ReadRawData(1, (unsigned char*)tmp);

			//Format the capture
			cap->Resize(length);
			for(size_t j=0; j<length; j++)
			{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = yincrement * (temp_buf[j] - yreference) + yorigin;
			}

			//Done, update the data
			pending_waveforms[i].push_back(cap);

			//Clean up
			delete[] temp_buf;
			*/
			break;
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture support
	for(size_t i=0; i<num_pending; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_analogChannelCount; j++)
		{
			if(IsChannelEnabled(j))
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		m_transport->SendCommand("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

bool TektronixOscilloscope::AcquireDataMSO56(map<int, vector<AnalogWaveform*> >& pending_waveforms)
{
	//Get record length
	m_transport->SendCommand("HOR:RECO?");
	size_t length = stos(m_transport->ReadReply());
	m_transport->SendCommand("DAT:START 0");
	m_transport->SendCommand(string("DAT:STOP ") + to_string(length));

	map<size_t, double> xincrements;
	map<size_t, double> ymults;
	map<size_t, double> yoffs;

	//Ask for the preambles
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		// Set source & get preamble+data
		m_transport->SendCommand(string("DAT:SOU ") + m_channels[i]->GetHwname());

		//Ask for the waveform preamble
		m_transport->SendCommand("WFMO?");

		//Process it
		for(int j=0; j<22; j++)
		{
			string reply = m_transport->ReadReply();

			//LogDebug("preamble block %d = %s\n", j, reply.c_str());

			if(j == 11)
			{
				xincrements[i] = stof(reply) * 1e12;	//scope gives sec, not ps
				//LogDebug("xincrement = %s\n", Unit(Unit::UNIT_PS).PrettyPrint(xincrement).c_str());
			}
			else if(j == 15)
			{
				ymults[i] = stof(reply);
				//LogDebug("ymult = %s\n", Unit(Unit::UNIT_VOLTS).PrettyPrint(ymult).c_str());
			}
			else if(j == 17)
			{
				yoffs[i] = stof(reply);
				m_channelOffsets[i] = -yoffs[i];
				//LogDebug("yoff = %s\n", Unit(Unit::UNIT_VOLTS).PrettyPrint(yoff).c_str());
			}
		}
	}

	//Ask for and get the data
	//(seems like batching here doesn't work)
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		//LogDebug("Channel %zu (%s)\n", i, m_channels[i]->GetHwname().c_str());
		LogIndenter li2;

		//Read the data blocks
		m_transport->SendCommand(string("DAT:SOU ") + m_channels[i]->GetHwname());
		m_transport->SendCommand("CURV?");

		//Read length of the actual data
		char tmplen[3] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmplen);	//expect #n
		int ndigits = atoi(tmplen+1);

		char digits[10] = {0};
		m_transport->ReadRawData(ndigits, (unsigned char*)digits);
		int msglen = atoi(digits);

		//Read the actual data
		char* rxbuf = new char[msglen];
		m_transport->ReadRawData(msglen, (unsigned char*)rxbuf);

		//convert bytes to samples
		size_t nsamples = msglen/2;
		int16_t* samples = (int16_t*)rxbuf;

		//Set up the capture we're going to store our data into
		//(no TDC data or fine timestamping available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = xincrements[i];
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;
		cap->Resize(nsamples);

		//Convert to volts
		float ymult = ymults[i];
		float yoff = yoffs[i];
		for(size_t j=0; j<nsamples; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = ymult*samples[j] + yoff;
		}

		//Done, update the data
		pending_waveforms[i].push_back(cap);

		//Done
		delete[] rxbuf;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	return true;
}

void TektronixOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool TektronixOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> TektronixOscilloscope::GetInterleaveConflicts()
{
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

uint64_t TektronixOscilloscope::GetSampleRate()
{
	//FIXME
	return 1;
}

uint64_t TektronixOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void TektronixOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//FIXME
}

void TektronixOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//FIXME
}

void TektronixOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t TektronixOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool TektronixOscilloscope::IsInterleaving()
{
	return false;
}

bool TektronixOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void TektronixOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand("TRIG:A:TYP?");
				string reply = m_transport->ReadReply();

				if(reply == "EDG")
					PullEdgeTrigger();
				else
				{
					LogWarning("Unknown trigger type %s\n", reply.c_str());
					delete m_trigger;
					m_trigger = NULL;
				}
			}
			break;

		default:
			LogWarning("PullTrigger() not implemented for this scope family\n");
			break;
	}
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void TektronixOscilloscope::PullEdgeTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				m_transport->SendCommand("TRIG:A:EDGE:SOU?");
				auto reply = m_transport->ReadReply();
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				m_transport->SendCommand("TRIG:A:LEV?");
				et->SetLevel(stof(m_transport->ReadReply()));

				//For some reason we get 3 more values after this. Discard them.
				for(int i=0; i<3; i++)
					m_transport->ReadReply();

				//Edge slope
				m_transport->SendCommand("TRIG:A:EDGE:SLO?");
				reply = m_transport->ReadReply();
				if(reply == "RIS")
					et->SetType(EdgeTrigger::EDGE_RISING);
				else if(reply == "FALL")
					et->SetType(EdgeTrigger::EDGE_FALLING);
				else if(reply == "EIT")
					et->SetType(EdgeTrigger::EDGE_ANY);
			}
			break;

		default:
			/*
			//Check cache
			//No locking, worst case we return a result a few seconds old
			if(m_triggerChannelValid)
				return m_triggerChannel;

			lock_guard<recursive_mutex> lock(m_mutex);

			//Look it up
			m_transport->SendCommand("TRIG:SOUR?");
			string ret = m_transport->ReadReply();

			if(ret.find("CHAN") == 0)
			{
				m_triggerChannelValid = true;
				m_triggerChannel = atoi(ret.c_str()+4) - 1;
				return m_triggerChannel;
			}
			else if(ret == "EXT")
			{
				m_triggerChannelValid = true;
				m_triggerChannel = m_extTrigChannel->GetIndex();
				return m_triggerChannel;
			}
			else
			{
				m_triggerChannelValid = false;
				LogWarning("Unknown trigger source %s\n", ret.c_str());
				return 0;
			}

			//Check cache.
			//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
			if(m_triggerLevelValid)
				return m_triggerLevel;

			lock_guard<recursive_mutex> lock(m_mutex);

			m_transport->SendCommand("TRIG:LEV?");
			string ret = m_transport->ReadReply();

			double level;
			sscanf(ret.c_str(), "%lf", &level);
			m_triggerLevel = level;
			m_triggerLevelValid = true;
			return level;
			*/
			break;
	}
}

void TektronixOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void TektronixOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand(string("TRIG:A:EDGE:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				m_transport->SendCommand(
					string("TRIG:A:LEV:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLevel()));

				switch(trig->GetType())
				{
					case EdgeTrigger::EDGE_RISING:
						m_transport->SendCommand("TRIG:A:EDGE:SLO RIS");
						break;

					case EdgeTrigger::EDGE_FALLING:
						m_transport->SendCommand("TRIG:A:EDGE:SLO FALL");
						break;

					case EdgeTrigger::EDGE_ANY:
						m_transport->SendCommand("TRIG:A:EDGE:SLO ANY");
						break;

					default:
						break;
				}
			}
			break;

		default:
			{
				char tmp[32];
				snprintf(tmp, sizeof(tmp), "TRIG:LEV %.3f", trig->GetLevel());
				m_transport->SendCommand(tmp);
			}
			break;
	}
}