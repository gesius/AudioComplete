/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Vamp

    An API for audio analysis and feature extraction plugins.

    Centre for Digital Music, Queen Mary, University of London.
    Copyright 2006 Chris Cannam.

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "EBUr128.h"

using std::string;
using std::vector;
using std::cerr;
using std::endl;


VampEBUr128::VampEBUr128(float inputSampleRate)
    : Plugin(inputSampleRate)
    , m_stepSize(0)
{
}

VampEBUr128::~VampEBUr128()
{
}

string
VampEBUr128::getIdentifier() const
{
    return "ebur128";
}

string
VampEBUr128::getName() const
{
    return "EBU R128 Loudness";
}

string
VampEBUr128::getDescription() const
{
    return "Loudness measurements according to the EBU Recommendation 128";
}

string
VampEBUr128::getMaker() const
{
    return "Harrison Consoles";
}

int
VampEBUr128::getPluginVersion() const
{
    return 2;
}

string
VampEBUr128::getCopyright() const
{
    return "GPL version 2 or later";
}

bool
VampEBUr128::initialise(size_t channels, size_t stepSize, size_t blockSize)
{
    if (channels < getMinChannelCount() ||
	channels > getMaxChannelCount()) return false;

    m_stepSize = std::min(stepSize, blockSize);
    m_channels = channels;

    ebu.init (m_channels, m_inputSampleRate);

    return true;
}

void
VampEBUr128::reset()
{
    ebu.reset ();
}

VampEBUr128::OutputList
VampEBUr128::getOutputDescriptors() const
{
    OutputList list;

    OutputDescriptor zc;
    zc.identifier = "loundless";
    zc.name = "Integrated loudness";
    zc.description = "Integrated loudness";
    zc.unit = "LUFS";
    zc.hasFixedBinCount = true;
    zc.binCount = 0;
    zc.hasKnownExtents = false;
    zc.isQuantized = false;
    zc.sampleType = OutputDescriptor::OneSamplePerStep;
    list.push_back(zc);

    zc.identifier = "range";
    zc.name = "Integrated loudness Range";
    zc.description = "Dynamic Range of the audio";
    zc.unit = "LU";
    zc.hasFixedBinCount = true;
    zc.binCount = 0;
    zc.hasKnownExtents = false;
    zc.isQuantized = false;
    zc.sampleType = OutputDescriptor::OneSamplePerStep;
    list.push_back(zc);

    return list;
}

VampEBUr128::FeatureSet
VampEBUr128::process(const float *const *inputBuffers,
                      Vamp::RealTime timestamp)
{
    if (m_stepSize == 0) {
	cerr << "ERROR: VampEBUr128::process: "
	     << "VampEBUr128 has not been initialised"
	     << endl;
	return FeatureSet();
    }

    ebu.integr_start (); // noop if already started
    ebu.process (m_stepSize, inputBuffers);

    return FeatureSet();
}

VampEBUr128::FeatureSet
VampEBUr128::getRemainingFeatures()
{
    FeatureSet returnFeatures;

    Feature loudness;
    loudness.hasTimestamp = false;
    loudness.values.push_back(ebu.integrated());
    returnFeatures[0].push_back(loudness);

    Feature range;
    range.hasTimestamp = false;
    range.values.push_back(ebu.range_max () - ebu.range_min ());
    returnFeatures[1].push_back(range);

    return returnFeatures;
}
