/*
 * Copyright (C) 2016 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef AUDIOGRAPHER_ANALYSER_H
#define AUDIOGRAPHER_ANALYSER_H

#include <fftw3.h>

#include <vamp-hostsdk/PluginLoader.h>
#include <vamp-sdk/Plugin.h>

#include "audiographer/visibility.h"
#include "audiographer/sink.h"
#include "audiographer/utils/listed_source.h"

#include "ardour/export_analysis.h"

namespace AudioGrapher
{

class /*LIBAUDIOGRAPHER_API*/ Analyser : public ListedSource<float>, public Sink<float>
{
  public:
	Analyser (float sample_rate, unsigned int channels, framecnt_t bufsize, framecnt_t n_samples);
	~Analyser ();
	void process (ProcessContext<float> const & c);
	ARDOUR::ExportAnalysisPtr result ();

	using Sink<float>::process;

	private:
	float fft_power_at_bin (const uint32_t b, const float norm) const;

	ARDOUR::ExportAnalysis _result;
	Vamp::Plugin* _ebur128_plugin;

	float        _sample_rate;
	unsigned int _channels;
	framecnt_t   _bufsize;
	framecnt_t   _n_samples;
	framecnt_t   _pos;
	framecnt_t   _spp;

	float* _bufs[2];

	float*     _hann_window;
	uint32_t   _fft_data_size;
	double     _fft_freq_per_bin;
	float*     _fft_data_in;
	float*     _fft_data_out;
	float*     _fft_power;
	fftwf_plan _fft_plan;

};

} // namespace

#endif
