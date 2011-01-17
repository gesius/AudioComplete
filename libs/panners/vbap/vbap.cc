#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <iostream>
#include <string>

#include "pbd/cartesian.h"

#include "ardour/pannable.h"
#include "ardour/speakers.h"
#include "ardour/vbap.h"
#include "ardour/vbap_speakers.h"
#include "ardour/audio_buffer.h"
#include "ardour/buffer_set.h"
#include "ardour/pan_controllable.h"

using namespace PBD;
using namespace ARDOUR;
using namespace std;

static PanPluginDescriptor _descriptor = {
        "VBAP 2D panner",
        1, -1, 2, -1,
        VBAPanner::factory
};

extern "C" { PanPluginDescriptor* panner_descriptor () { return &_descriptor; } }

VBAPanner::Signal::Signal (Session& session, VBAPanner& p, uint32_t n)
        : azimuth_control (new PanControllable (session, string_compose (_("azimuth %1"), n+1), &p, Evoral::Parameter (PanAzimuthAutomation, 0, n)))
        , elevation_control (new PanControllable (session, string_compose (_("elevation %1"), n+1), &p, Evoral::Parameter (PanElevationAutomation, 0, n)))
{
        gains[0] = gains[1] = gains[2] = 0;
        desired_gains[0] = desired_gains[1] = desired_gains[2] = 0;
        outputs[0] = outputs[1] = outputs[2] = -1;
        desired_outputs[0] = desired_outputs[1] = desired_outputs[2] = -1;
};

VBAPanner::VBAPanner (boost::shared_ptr<Pannable> p, Speakers& s)
	: Panner (p)
	, _dirty (true)
	, _speakers (VBAPSpeakers::instance (s))
{
}

VBAPanner::~VBAPanner ()
{
        for (vector<Signal*>::iterator i = _signals.begin(); i != _signals.end(); ++i) {
                delete *i;
        }
}

void
VBAPanner::configure_io (const ChanCount& in, const ChanCount& /* ignored - we use Speakers */)
{
        uint32_t n = in.n_audio();

        /* 2d panning: spread signals equally around a circle */
        
        double degree_step = 360.0 / _speakers.n_speakers();
        double deg;
        
        /* even number of signals? make sure the top two are either side of "top".
           otherwise, just start at the "top" (90.0 degrees) and rotate around
        */
        
        if (n % 2) {
                deg = 90.0 - degree_step;
        } else {
                deg = 90.0;
        }

        _signals.clear ();
        
        for (uint32_t i = 0; i < n; ++i) {
                _signals.push_back (new Signal (_pannable->session(), *this, i));
                _signals[i]->direction = AngularVector (deg, 0.0);
                deg += degree_step;
        }
}

void 
VBAPanner::compute_gains (double gains[3], int speaker_ids[3], int azi, int ele) 
{
	/* calculates gain factors using loudspeaker setup and given direction */
	double cartdir[3];
	double power;
	int i,j,k;
	double small_g;
	double big_sm_g, gtmp[3];

	azi_ele_to_cart (azi,ele, cartdir[0], cartdir[1], cartdir[2]);  
	big_sm_g = -100000.0;

	gains[0] = gains[1] = gains[2] = 0;
	speaker_ids[0] = speaker_ids[1] = speaker_ids[2] = 0;

	for (i = 0; i < _speakers.n_tuples(); i++) {

		small_g = 10000000.0;

		for (j = 0; j < _speakers.dimension(); j++) {

			gtmp[j] = 0.0;

			for (k = 0; k < _speakers.dimension(); k++) {
				gtmp[j] += cartdir[k] * _speakers.matrix(i)[j*_speakers.dimension()+k]; 
			}

			if (gtmp[j] < small_g) {
				small_g = gtmp[j];
			}
		}

		if (small_g > big_sm_g) {

			big_sm_g = small_g;

			gains[0] = gtmp[0]; 
			gains[1] = gtmp[1]; 

			speaker_ids[0] = _speakers.speaker_for_tuple (i, 0);
			speaker_ids[1] = _speakers.speaker_for_tuple (i, 1);
                        
			if (_speakers.dimension() == 3) {
				gains[2] = gtmp[2];
				speaker_ids[2] = _speakers.speaker_for_tuple (i, 2);
			} else {
				gains[2] = 0.0;
				speaker_ids[2] = -1;
			}
		}
	}
        
	power = sqrt (gains[0]*gains[0] + gains[1]*gains[1] + gains[2]*gains[2]);

	if (power > 0) {
		gains[0] /= power; 
		gains[1] /= power;
		gains[2] /= power;
	}

	_dirty = false;
}

void
VBAPanner::do_distribute (BufferSet& inbufs, BufferSet& obufs, gain_t gain_coefficient, pframes_t nframes)
{
	bool was_dirty = _dirty;
        uint32_t n;
        vector<Signal*>::iterator s;

        assert (inbufs.count().n_audio() == _signals.size());

        /* XXX need to handle mono case */

        for (s = _signals.begin(), n = 0; s != _signals.end(); ++s, ++n) {

                Signal* signal (*s);

                if (was_dirty) {
                        compute_gains (signal->desired_gains, signal->desired_outputs, signal->direction.azi, signal->direction.ele);
                        cerr << " @ " << signal->direction.azi << " /= " << signal->direction.ele
                             << " Outputs: "
                             << signal->desired_outputs[0] + 1 << ' '
                             << signal->desired_outputs[1] + 1 << ' '
                             << " Gains "
                             << signal->desired_gains[0] << ' '
                             << signal->desired_gains[1] << ' '
                             << endl;
                }
                
                do_distribute_one (inbufs.get_audio (n), obufs, gain_coefficient, nframes, n);

                if (was_dirty) {
                        memcpy (signal->gains, signal->desired_gains, sizeof (signal->gains));
                        memcpy (signal->outputs, signal->desired_outputs, sizeof (signal->outputs));
                }
        }
}


void
VBAPanner::do_distribute_one (AudioBuffer& srcbuf, BufferSet& obufs, gain_t gain_coefficient, pframes_t nframes, uint32_t which)
{
	Sample* const src = srcbuf.data();
	Sample* dst;
	pan_t pan;
	uint32_t n_audio = obufs.count().n_audio();
	bool todo[n_audio];
        Signal* signal (_signals[which]);

	for (uint32_t o = 0; o < n_audio; ++o) {
		todo[o] = true;
	}
        
	/* VBAP may distribute the signal across up to 3 speakers depending on
	   the configuration of the speakers.
	*/

	for (int o = 0; o < 3; ++o) {
		if (signal->desired_outputs[o] != -1) {
                        
			pframes_t n = 0;

			/* XXX TODO: interpolate across changes in gain and/or outputs
			 */

			dst = obufs.get_audio (signal->desired_outputs[o]).data();

			pan = gain_coefficient * signal->desired_gains[o];
			mix_buffers_with_gain (dst+n,src+n,nframes-n,pan);

			todo[o] = false;
		}
	}
        
	for (uint32_t o = 0; o < n_audio; ++o) {
		if (todo[o]) {
			/* VBAP decided not to deliver any audio to this output, so we write silence */
			dst = obufs.get_audio(o).data();
			memset (dst, 0, sizeof (Sample) * nframes);
		}
	}
        
}

void 
VBAPanner::do_distribute_one_automated (AudioBuffer& src, BufferSet& obufs,
                                        framepos_t start, framepos_t end, pframes_t nframes, pan_t** buffers, uint32_t which)
{
}

XMLNode&
VBAPanner::get_state ()
{
	return state (true);
}

XMLNode&
VBAPanner::state (bool full_state)
{
        XMLNode& node (Panner::get_state());
	node.add_property (X_("type"), _descriptor.name);
	return node;
}

int
VBAPanner::set_state (const XMLNode& node, int /*version*/)
{
	return 0;
}

boost::shared_ptr<AutomationControl>
VBAPanner::azimuth_control (uint32_t n)
{
        if (n >= _signals.size()) {
                return boost::shared_ptr<AutomationControl>();
        }
        return _signals[n]->azimuth_control;
}

boost::shared_ptr<AutomationControl>
VBAPanner::evelation_control (uint32_t n)
{
        if (n >= _signals.size()) {
                return boost::shared_ptr<AutomationControl>();
        }
        return _signals[n]->elevation_control;
}

Panner*
VBAPanner::factory (boost::shared_ptr<Pannable> p, Speakers& s)
{
	return new VBAPanner (p, s);
}

string
VBAPanner::describe_parameter (Evoral::Parameter param)
{
        stringstream ss;
        switch (param.type()) {
        case PanElevationAutomation:
                return string_compose ( _("Pan:elevation %1"), param.id() + 1);
        case PanWidthAutomation:
                return string_compose ( _("Pan:diffusion %1"), param.id() + 1);
        case PanAzimuthAutomation:
                return string_compose ( _("Pan:azimuth %1"), param.id() + 1);
        }

        return Automatable::describe_parameter (param);
}

ChanCount
VBAPanner::in() const
{
        return ChanCount (DataType::AUDIO, _signals.size());
}

ChanCount
VBAPanner::out() const
{
        return ChanCount (DataType::AUDIO, _speakers.n_speakers());
}
