/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of QuadratureDecoder
 */
#ifndef QuadratureDecoder_h
#define QuadratureDecoder_h

/**
	@brief Decoder for quadrature encoder signals
 */
class QuadratureDecoder : public Filter
{
public:
	QuadratureDecoder(const std::string& color);

	virtual void Refresh();

	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual float GetVoltageRange(size_t stream);
	virtual float GetOffset(size_t stream);
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual void ClearSweeps();

	enum InterpolationMode
	{
		INTERP_NONE,
		INTERP_LINEAR
	};

	enum RevMode
	{
		MODE_SINGLE_REV,
		MODE_MULTI_REV
	};

	PROTOCOL_DECODER_INITPROC(QuadratureDecoder)

protected:
	std::string m_pulseratename;
	std::string m_interpname;
	std::string m_revname;
	std::string m_debouncename;

	float m_max;
	float m_min;
};

#endif
