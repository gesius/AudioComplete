/*
    Copyright (C) 2000 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifdef WAF_BUILD
#include "libardour-config.h"
#endif

#include <string>

#include "pbd/failed_constructor.h"
#include "pbd/xml++.h"
#include "pbd/convert.h"

#include "ardour/audio_buffer.h"
#include "ardour/automation_list.h"
#include "ardour/buffer_set.h"
#include "ardour/event_type_map.h"
#include "ardour/ladspa_plugin.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/port.h"
#include "ardour/route.h"

#ifdef HAVE_SLV2
#include "ardour/lv2_plugin.h"
#endif

#ifdef VST_SUPPORT
#include "ardour/vst_plugin.h"
#endif

#ifdef HAVE_AUDIOUNITS
#include "ardour/audio_unit.h"
#endif

#include "ardour/audioengine.h"
#include "ardour/session.h"
#include "ardour/types.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

const string PluginInsert::port_automation_node_name = "PortAutomation";

PluginInsert::PluginInsert (Session& s, boost::shared_ptr<Plugin> plug)
	: Processor (s, (plug ? plug->name() : string ("toBeRenamed")))
	, _signal_analysis_collected_nframes(0)
	, _signal_analysis_collect_nframes_max(0)
	, _splitting (false)
{
	/* the first is the master */

	if (plug) {
		plug->set_insert_info (this);
		_plugins.push_back (plug);
		set_automatable ();

		Glib::Mutex::Lock em (_session.engine().process_lock());
		IO::PortCountChanged (max(input_streams(), output_streams()));
	}
}

bool
PluginInsert::set_count (uint32_t num)
{
	bool require_state = !_plugins.empty();

	/* this is a bad idea.... we shouldn't do this while active.
	   only a route holding their redirect_lock should be calling this
	*/

	if (num == 0) {
		return false;
	} else if (num > _plugins.size()) {
		uint32_t diff = num - _plugins.size();

		for (uint32_t n = 0; n < diff; ++n) {
			add_plugin_with_activation (plugin_factory (_plugins[0]));

			if (require_state) {
				/* XXX do something */
			}
		}

	} else if (num < _plugins.size()) {
		uint32_t diff = _plugins.size() - num;
		for (uint32_t n= 0; n < diff; ++n) {
			_plugins.pop_back();
		}
	}

	return true;
}

PluginInsert::~PluginInsert ()
{
}

void
PluginInsert::control_list_automation_state_changed (Evoral::Parameter which, AutoState s)
{
	if (which.type() != PluginAutomation)
		return;

	boost::shared_ptr<AutomationControl> c
			= boost::dynamic_pointer_cast<AutomationControl>(control (which));

	if (c && s != Off) {
		_plugins[0]->set_parameter (which.id(), c->list()->eval (_session.transport_frame()));
	}
}

ChanCount
PluginInsert::output_streams() const
{
	ChanCount out = _plugins.front()->get_info()->n_outputs;

	if (out == ChanCount::INFINITE) {
		return _plugins.front()->output_streams ();
	} else {
		out.set_audio (out.n_audio() * _plugins.size());
		out.set_midi (out.n_midi() * _plugins.size());
		return out;
	}
}

ChanCount
PluginInsert::input_streams() const
{
	ChanCount in = _plugins[0]->get_info()->n_inputs;

	if (_splitting) {

		/* we are splitting 1 processor input to multiple plugin inputs,
		   so we have a maximum of 1 stream of each type.
		*/
		for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
			if (in.get (*t) > 1) {
				in.set (*t, 1);
			}
		}
		return in;

	} else if (in == ChanCount::INFINITE) {
		return _plugins[0]->input_streams ();
	} else {
		in.set_audio (in.n_audio() * _plugins.size());
		in.set_midi (in.n_midi() * _plugins.size());
		return in;
	}
}

ChanCount
PluginInsert::natural_output_streams() const
{
	return _plugins[0]->get_info()->n_outputs;
}

ChanCount
PluginInsert::natural_input_streams() const
{
	return _plugins[0]->get_info()->n_inputs;
}

bool
PluginInsert::is_generator() const
{
	/* XXX more finesse is possible here. VST plugins have a
	   a specific "instrument" flag, for example.
	 */

	return _plugins[0]->get_info()->n_inputs.n_audio() == 0;
}

void
PluginInsert::set_automatable ()
{
	set<Evoral::Parameter> a = _plugins.front()->automatable ();

	Plugin::ParameterDescriptor desc;

	for (set<Evoral::Parameter>::iterator i = a.begin(); i != a.end(); ++i) {
		if (i->type() == PluginAutomation) {

			Evoral::Parameter param(*i);

			_plugins.front()->get_parameter_descriptor(i->id(), desc);

			/* the Parameter belonging to the actual plugin doesn't have its range set
			   but we want the Controllable related to this Parameter to have those limits.
			*/

			param.set_range (desc.lower, desc.upper, _plugins.front()->default_value(i->id()), desc.toggled);
			can_automate (param);
			boost::shared_ptr<AutomationList> list(new AutomationList(param));
			add_control (boost::shared_ptr<AutomationControl> (new PluginControl(this, param, list)));
		}
	}
}

void
PluginInsert::parameter_changed (Evoral::Parameter which, float val)
{
	if (which.type() != PluginAutomation)
		return;

	Plugins::iterator i = _plugins.begin();

	/* don't set the first plugin, just all the slaves */

	if (i != _plugins.end()) {
		++i;
		for (; i != _plugins.end(); ++i) {
			(*i)->set_parameter (which, val);
		}
	}
}

int
PluginInsert::set_block_size (pframes_t nframes)
{
	int ret = 0;
	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		if ((*i)->set_block_size (nframes) != 0) {
			ret = -1;
		}
	}
	return ret;
}

void
PluginInsert::activate ()
{
	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->activate ();
	}

	Processor::activate ();
}

void
PluginInsert::deactivate ()
{
	Processor::deactivate ();

	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->deactivate ();
	}
}

void
PluginInsert::flush ()
{
	for (vector<boost::shared_ptr<Plugin> >::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->flush ();
	}
}

void
PluginInsert::connect_and_run (BufferSet& bufs, pframes_t nframes, framecnt_t offset, bool with_auto, framepos_t now)
{
	// Calculate if, and how many frames we need to collect for analysis
	framecnt_t collect_signal_nframes = (_signal_analysis_collect_nframes_max -
					     _signal_analysis_collected_nframes);
	if (nframes < collect_signal_nframes) { // we might not get all frames now
		collect_signal_nframes = nframes;
	}

	ChanMapping in_map(input_streams());
	ChanMapping out_map(output_streams());

	if (_splitting) {
		/* fix the input mapping so that we have maps for each of the plugin's inputs */
		in_map = ChanMapping (natural_input_streams ());

		/* copy the first stream's buffer contents to the others */
		/* XXX: audio only */
		Sample const * mono = bufs.get_audio (in_map.get (DataType::AUDIO, 0)).data (offset);
		for (uint32_t i = input_streams().n_audio(); i < natural_input_streams().n_audio(); ++i) {
			memcpy (bufs.get_audio (in_map.get (DataType::AUDIO, i)).data() + offset, mono + offset, sizeof (Sample) * (nframes - offset));
		}
	}

	/* Note that we've already required that plugins
	   be able to handle in-place processing.
	*/

	if (with_auto) {

		uint32_t n = 0;

		for (Controls::iterator li = controls().begin(); li != controls().end(); ++li, ++n) {

			boost::shared_ptr<AutomationControl> c
				= boost::dynamic_pointer_cast<AutomationControl>(li->second);

			if (c->parameter().type() == PluginAutomation && c->automation_playback()) {
				bool valid;

				const float val = c->list()->rt_safe_eval (now, valid);

				if (valid) {
					c->set_value(val);
				}

			}
		}
	}

	if (collect_signal_nframes > 0) {
		// collect input
		//std::cerr << "collect input, bufs " << bufs.count().n_audio() << " count,  " << bufs.available().n_audio() << " available" << std::endl;
		//std::cerr << "               streams " << input_streams().n_audio() << std::endl;
		//std::cerr << "filling buffer with " << collect_signal_nframes << " frames at " << _signal_analysis_collected_nframes << std::endl;

		_signal_analysis_inputs.set_count(input_streams());

		for (uint32_t i = 0; i < input_streams().n_audio(); ++i) {
			_signal_analysis_inputs.get_audio(i).read_from(
				bufs.get_audio(i),
				collect_signal_nframes,
				_signal_analysis_collected_nframes); // offset is for target buffer
		}

	}

	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->connect_and_run(bufs, in_map, out_map, nframes, offset);
		for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
			in_map.offset_to(*t, natural_input_streams().get(*t));
			out_map.offset_to(*t, natural_output_streams().get(*t));
		}
	}

	if (collect_signal_nframes > 0) {
		// collect output
		//std::cerr << "       output, bufs " << bufs.count().n_audio() << " count,  " << bufs.available().n_audio() << " available" << std::endl;
		//std::cerr << "               streams " << output_streams().n_audio() << std::endl;

		_signal_analysis_outputs.set_count(output_streams());

		for (uint32_t i = 0; i < output_streams().n_audio(); ++i) {
			_signal_analysis_outputs.get_audio(i).read_from(
				bufs.get_audio(i),
				collect_signal_nframes,
				_signal_analysis_collected_nframes); // offset is for target buffer
		}

		_signal_analysis_collected_nframes += collect_signal_nframes;
		assert(_signal_analysis_collected_nframes <= _signal_analysis_collect_nframes_max);

		if (_signal_analysis_collected_nframes == _signal_analysis_collect_nframes_max) {
			_signal_analysis_collect_nframes_max = 0;
			_signal_analysis_collected_nframes   = 0;

			AnalysisDataGathered(&_signal_analysis_inputs,
					     &_signal_analysis_outputs);
		}
	}
	/* leave remaining channel buffers alone */
}

void
PluginInsert::silence (framecnt_t nframes)
{
	if (!active ()) {
		return;
	}

	ChanMapping in_map(input_streams());
	ChanMapping out_map(output_streams());

	if (_splitting) {
		/* fix the input mapping so that we have maps for each of the plugin's inputs */
		in_map = ChanMapping (natural_input_streams ());
	}

	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->connect_and_run (_session.get_silent_buffers ((*i)->get_info()->n_inputs), in_map, out_map, nframes, 0);
	}
}

void
PluginInsert::run (BufferSet& bufs, framepos_t /*start_frame*/, framepos_t /*end_frame*/, pframes_t nframes, bool)
{
	if (_pending_active) {
		/* run as normal if we are active or moving from inactive to active */

		if (_session.transport_rolling()) {
			automation_run (bufs, nframes);
		} else {
			connect_and_run (bufs, nframes, 0, false);
		}

	} else {

		/* FIXME: type, audio only */

		uint32_t in = _plugins[0]->get_info()->n_inputs.n_audio();
		uint32_t out = _plugins[0]->get_info()->n_outputs.n_audio();

		if (out > in) {

			/* not active, but something has make up for any channel count increase */

			for (uint32_t n = out - in; n < out; ++n) {
				memcpy (bufs.get_audio(n).data(), bufs.get_audio(in - 1).data(), sizeof (Sample) * nframes);
			}
		}

		bufs.count().set_audio(out);
	}

	_active = _pending_active;
}

void
PluginInsert::set_parameter (Evoral::Parameter param, float val)
{
	if (param.type() != PluginAutomation) {
		return;
	}

	/* the others will be set from the event triggered by this */

	_plugins[0]->set_parameter (param.id(), val);

	boost::shared_ptr<AutomationControl> ac
			= boost::dynamic_pointer_cast<AutomationControl>(control(param));

	if (ac) {
		ac->set_value(val);
	} else {
		warning << "set_parameter called for nonexistant parameter "
			<< EventTypeMap::instance().to_symbol(param) << endmsg;
	}

	_session.set_dirty();
}

float
PluginInsert::get_parameter (Evoral::Parameter param)
{
	if (param.type() != PluginAutomation) {
		return 0.0;
	} else {
		assert (!_plugins.empty ());
		return _plugins[0]->get_parameter (param.id());
	}
}

void
PluginInsert::automation_run (BufferSet& bufs, pframes_t nframes)
{
	Evoral::ControlEvent next_event (0, 0.0f);
	framepos_t now = _session.transport_frame ();
	framepos_t end = now + nframes;
	framecnt_t offset = 0;

	Glib::Mutex::Lock lm (control_lock(), Glib::TRY_LOCK);

	if (!lm.locked()) {
		connect_and_run (bufs, nframes, offset, false);
		return;
	}

	if (!find_next_event (now, end, next_event) || requires_fixed_sized_buffers()) {

		/* no events have a time within the relevant range */

		connect_and_run (bufs, nframes, offset, true, now);
		return;
	}

	while (nframes) {

		framecnt_t cnt = min (((framecnt_t) ceil (next_event.when) - now), (framecnt_t) nframes);

		connect_and_run (bufs, cnt, offset, true, now);

		nframes -= cnt;
		offset += cnt;
		now += cnt;

		if (!find_next_event (now, end, next_event)) {
			break;
		}
	}

	/* cleanup anything that is left to do */

	if (nframes) {
		connect_and_run (bufs, nframes, offset, true, now);
	}
}

float
PluginInsert::default_parameter_value (const Evoral::Parameter& param)
{
	if (param.type() != PluginAutomation)
		return 1.0;

	if (_plugins.empty()) {
		fatal << _("programming error: ") << X_("PluginInsert::default_parameter_value() called with no plugin")
		      << endmsg;
		/*NOTREACHED*/
	}

	return _plugins[0]->default_value (param.id());
}

boost::shared_ptr<Plugin>
PluginInsert::plugin_factory (boost::shared_ptr<Plugin> other)
{
	boost::shared_ptr<LadspaPlugin> lp;
#ifdef HAVE_SLV2
	boost::shared_ptr<LV2Plugin> lv2p;
#endif
#ifdef VST_SUPPORT
	boost::shared_ptr<VSTPlugin> vp;
#endif
#ifdef HAVE_AUDIOUNITS
	boost::shared_ptr<AUPlugin> ap;
#endif

	if ((lp = boost::dynamic_pointer_cast<LadspaPlugin> (other)) != 0) {
		return boost::shared_ptr<Plugin> (new LadspaPlugin (*lp));
#ifdef HAVE_SLV2
	} else if ((lv2p = boost::dynamic_pointer_cast<LV2Plugin> (other)) != 0) {
		return boost::shared_ptr<Plugin> (new LV2Plugin (*lv2p));
#endif
#ifdef VST_SUPPORT
	} else if ((vp = boost::dynamic_pointer_cast<VSTPlugin> (other)) != 0) {
		return boost::shared_ptr<Plugin> (new VSTPlugin (*vp));
#endif
#ifdef HAVE_AUDIOUNITS
	} else if ((ap = boost::dynamic_pointer_cast<AUPlugin> (other)) != 0) {
		return boost::shared_ptr<Plugin> (new AUPlugin (*ap));
#endif
	}

	fatal << string_compose (_("programming error: %1"),
			  X_("unknown plugin type in PluginInsert::plugin_factory"))
	      << endmsg;
	/*NOTREACHED*/
	return boost::shared_ptr<Plugin> ((Plugin*) 0);
}

bool
PluginInsert::configure_io (ChanCount in, ChanCount out)
{
	if (set_count (count_for_configuration (in, out)) == false) {
		set_splitting (false);
		return false;
	}

	if (_plugins.front()->get_info()->n_inputs <= in) {
		set_splitting (false);
		if (_plugins.front()->configure_io (in, out) == false) {
			return false;
		}
	} else {
		/* we must be splitting a single processor input to
		   multiple plugin inputs
		*/
		set_splitting (true);
		_plugins.front()->configure_io (_plugins.front()->get_info()->n_inputs, out);
	}

	// we don't know the analysis window size, so we must work with the
	// current buffer size here. each request for data fills in these
	// buffers and the analyser makes sure it gets enough data for the
	// analysis window
	session().ensure_buffer_set (_signal_analysis_inputs, in);
	//_signal_analysis_inputs.set_count (in);

	session().ensure_buffer_set (_signal_analysis_outputs, out);
	//_signal_analysis_outputs.set_count (out);

	// std::cerr << "set counts to i" << in.n_audio() << "/o" << out.n_audio() << std::endl;

	return Processor::configure_io (in, out);
}

bool
PluginInsert::can_support_io_configuration (const ChanCount& in, ChanCount& out) const
{
	// Plugin has flexible I/O, so delegate to it
	if (_plugins.front()->reconfigurable_io()) {
		return _plugins.front()->can_support_io_configuration (in, out);
	}

	ChanCount inputs  = _plugins[0]->get_info()->n_inputs;
	ChanCount outputs = _plugins[0]->get_info()->n_outputs;

	bool no_inputs = true;
	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
		if (inputs.get (*t) != 0) {
			no_inputs = false;
			break;
		}
	}

	if (no_inputs) {
		/* no inputs so we can take any input configuration since we throw it away */
		out = outputs;
		return true;
	}

	// Plugin inputs match requested inputs exactly
	if (inputs == in) {
		out = outputs;
		return true;
	}

	// See if replication is possible
	// We allow replication only for plugins with either zero or 1 inputs and outputs
	// for every valid data type.
	uint32_t f             = 0;
	bool     can_replicate = true;
	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {

		uint32_t nin = inputs.get (*t);

		// No inputs of this type
		if (nin == 0 && in.get(*t) == 0) {
			continue;
		}

		if (nin != 1 || outputs.get (*t) != 1) {
			can_replicate = false;
			break;
		}

		// Potential factor not set yet

		if (f == 0) {
			f = in.get(*t) / nin;
		}

		// Factor for this type does not match another type, can not replicate
		if (f != (in.get(*t) / nin)) {
			can_replicate = false;
			break;
		}
	}

	if (can_replicate) {
		for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {
			out.set (*t, outputs.get(*t) * f);
		}
		return true;
	}

	/* If the processor has exactly one input of a given type, and
	   the plugin has more, we can feed the single processor input
	   to some or all of the plugin inputs.  This is rather
	   special-case-y, but the 1-to-many case is by far the
	   simplest.  How do I split thy 2 processor inputs to 3
	   plugin inputs?  Let me count the ways ...
	*/

	bool can_split = true;
	for (DataType::iterator t = DataType::begin(); t != DataType::end(); ++t) {

		bool const can_split_type = (in.get (*t) == 1 && inputs.get (*t) > 1);
		bool const nothing_to_do_for_type = (in.get (*t) == 0 && inputs.get (*t) == 0);

		if (!can_split_type && !nothing_to_do_for_type) {
			can_split = false;
		}
	}

	if (can_split) {
		out = outputs;
		return true;
	}

	return false;
}

/* Number of plugin instances required to support a given channel configuration.
 * (private helper)
 */
int32_t
PluginInsert::count_for_configuration (ChanCount in, ChanCount /*out*/) const
{
	if (_plugins.front()->reconfigurable_io()) {
		/* plugin has flexible I/O, so the answer is always 1 */
		/* this could change if we ever decide to replicate AU's */
		return 1;
	}

	// FIXME: take 'out' into consideration

	ChanCount outputs = _plugins[0]->get_info()->n_outputs;
	ChanCount inputs = _plugins[0]->get_info()->n_inputs;

	if (inputs.n_total() == 0) {
		/* instrument plugin, always legal, but throws away any existing streams */
		return 1;
	}

	if (inputs.n_total() == 1 && outputs == inputs
			&& ((inputs.n_audio() == 0 && in.n_audio() == 0)
				|| (inputs.n_midi() == 0 && in.n_midi() == 0))) {
		/* mono plugin, replicate as needed to match in */
		return in.n_total();
	}

	if (inputs == in) {
		/* exact match */
		return 1;
	}

	if (inputs > in) {
		/* more plugin inputs than processor inputs, so we are splitting */
		return 1;
	}

	// assumes in is valid, so we must be replicating
	if (inputs.n_total() < in.n_total()
			&& (in.n_total() % inputs.n_total() == 0)) {

		return in.n_total() / inputs.n_total();
	}

	/* err... */
	return 0;
}

XMLNode&
PluginInsert::get_state(void)
{
	return state (true);
}

XMLNode&
PluginInsert::state (bool full)
{
	XMLNode& node = Processor::state (full);

	node.add_property("type", _plugins[0]->state_node_name());
	node.add_property("unique-id", _plugins[0]->unique_id());
	node.add_property("count", string_compose("%1", _plugins.size()));
	node.add_child_nocopy (_plugins[0]->get_state());

	for (Controls::iterator c = controls().begin(); c != controls().end(); ++c) {
		boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl> ((*c).second);
		if (ac) {
			node.add_child_nocopy (ac->get_state());
		}
	}

	return node;
}

void
PluginInsert::set_control_ids (const XMLNode& node, int version)
{
	const XMLNodeList& nlist = node.children();
	XMLNodeConstIterator iter;
	set<Evoral::Parameter>::const_iterator p;

	for (iter = nlist.begin(); iter != nlist.end(); ++iter) {
		if ((*iter)->name() == Controllable::xml_node_name) {
			const XMLProperty* prop;

			if ((prop = (*iter)->property (X_("parameter"))) != 0) {
				uint32_t p = atoi (prop->value());
				boost::shared_ptr<Evoral::Control> c = control (Evoral::Parameter (PluginAutomation, 0, p));
				if (!c) {
					continue;
				}
				boost::shared_ptr<AutomationControl> ac = boost::dynamic_pointer_cast<AutomationControl> (c);
				if (ac) {
					ac->set_state (**iter, version);
				}
			}
		}
	}
}

int
PluginInsert::set_state(const XMLNode& node, int version)
{
	XMLNodeList nlist = node.children();
	XMLNodeIterator niter;
	XMLPropertyList plist;
	const XMLProperty *prop;
	ARDOUR::PluginType type;

	if ((prop = node.property ("type")) == 0) {
		error << _("XML node describing plugin is missing the `type' field") << endmsg;
		return -1;
	}

	if (prop->value() == X_("ladspa") || prop->value() == X_("Ladspa")) { /* handle old school sessions */
		type = ARDOUR::LADSPA;
	} else if (prop->value() == X_("lv2")) {
		type = ARDOUR::LV2;
	} else if (prop->value() == X_("vst")) {
		type = ARDOUR::VST;
	} else if (prop->value() == X_("audiounit")) {
		type = ARDOUR::AudioUnit;
	} else {
		error << string_compose (_("unknown plugin type %1 in plugin insert state"),
				  prop->value())
		      << endmsg;
		return -1;
	}

	prop = node.property ("unique-id");

	if (prop == 0) {
#ifdef VST_SUPPORT
		/* older sessions contain VST plugins with only an "id" field.
		 */

		if (type == ARDOUR::VST) {
			prop = node.property ("id");
		}
#endif
		/* recheck  */

		if (prop == 0) {
			error << _("Plugin has no unique ID field") << endmsg;
			return -1;
		}
	}

	boost::shared_ptr<Plugin> plugin = find_plugin (_session, prop->value(), type);

	if (plugin == 0) {
		error << string_compose(
			_("Found a reference to a plugin (\"%1\") that is unknown.\n"
			  "Perhaps it was removed or moved since it was last used."),
			prop->value())
		      << endmsg;
		return -1;
	}

	uint32_t count = 1;
	bool need_automatables = true;

	if (_plugins.empty()) {
		/* if we are adding the first plugin, we will need to set
		   up automatable controls.
		*/
		need_automatables = true;
	}

	Processor::set_state (node, version);
	plugin->set_insert_info (this);

	if ((prop = node.property ("count")) != 0) {
		sscanf (prop->value().c_str(), "%u", &count);
	}

	/* Handle the node list for this Processor (or Insert if an A2 session).
	 * This needs to happen before the add_plugin_with_activation below, as
	 * the plugin set_state calls may themselves call latency_compute_run,
	 * which will leave the plugin deactivated whether it is currently
	 * activated or not.
	 */
	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		if ((*niter)->name() == plugin->state_node_name()) {

			plugin->set_state (**niter, version);
			for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
				(*i)->set_state (**niter, version);
			}
			break;
		}
	}

	if (_plugins.size() != count) {

		add_plugin_with_activation (plugin);

		for (uint32_t n = 1; n < count; ++n) {
			add_plugin_with_activation (plugin_factory (plugin));
		}
	}

	if (need_automatables) {
		set_automatable ();
		set_control_ids (node, version);
	}

	if (version < 3000) {

		/* Only 2.X sessions need a call to set_parameter_state() - in 3.X and above
		   this is all handled by Automatable
		*/

		for (niter = nlist.begin(); niter != nlist.end(); ++niter) {
			if ((*niter)->name() == "Redirect") {
				/* XXX do we need to tackle placement? i think not (pd; oct 16 2009) */
				Processor::set_state (**niter, version);
				break;
			}
		}

		set_parameter_state_2X (node, version);
	}

	// The name of the PluginInsert comes from the plugin, nothing else
	_name = plugin->get_info()->name;

	/* catch up on I/O */

	{
		Glib::Mutex::Lock em (_session.engine().process_lock());
		IO::PortCountChanged (max(input_streams(), output_streams()));
	}

	return 0;
}

void
PluginInsert::set_parameter_state_2X (const XMLNode& node, int version)
{
	XMLNodeList nlist = node.children();
	XMLNodeIterator niter;

	/* look for port automation node */

	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {

		if ((*niter)->name() != port_automation_node_name) {
			continue;
		}

		XMLNodeList cnodes;
		XMLProperty *cprop;
		XMLNodeConstIterator iter;
		XMLNode *child;
		const char *port;
		uint32_t port_id;

		cnodes = (*niter)->children ("port");

		for (iter = cnodes.begin(); iter != cnodes.end(); ++iter){

			child = *iter;

			if ((cprop = child->property("number")) != 0) {
				port = cprop->value().c_str();
			} else {
				warning << _("PluginInsert: Auto: no ladspa port number") << endmsg;
				continue;
			}

			sscanf (port, "%" PRIu32, &port_id);

			if (port_id >= _plugins[0]->parameter_count()) {
				warning << _("PluginInsert: Auto: port id out of range") << endmsg;
				continue;
			}

			boost::shared_ptr<AutomationControl> c = boost::dynamic_pointer_cast<AutomationControl>(
					control(Evoral::Parameter(PluginAutomation, 0, port_id), true));

			if (c) {
				if (!child->children().empty()) {
					c->alist()->set_state (*child->children().front(), version);

					/* In some cases 2.X saves lists with min_yval and max_yval
					   being FLT_MIN and FLT_MAX respectively.  This causes problems
					   in A3 because these min/max values are used to compute
					   where GUI control points should be drawn.  If we see such
					   values, `correct' them to the min/max of the appropriate
					   parameter.
					*/

					float min_y = c->alist()->get_min_y ();
					float max_y = c->alist()->get_max_y ();

					Plugin::ParameterDescriptor desc;
					_plugins.front()->get_parameter_descriptor (port_id, desc);

					if (min_y == FLT_MIN) {
						min_y = desc.lower;
					}

					if (max_y == FLT_MAX) {
						max_y = desc.upper;
					}

					c->alist()->set_yrange (min_y, max_y);
				}
			} else {
				error << string_compose (_("PluginInsert: automatable control %1 not found - ignored"), port_id) << endmsg;
			}
		}

		/* done */

		break;
	}
}


string
PluginInsert::describe_parameter (Evoral::Parameter param)
{
	if (param.type() != PluginAutomation) {
		return Automatable::describe_parameter(param);
	}

	return _plugins[0]->describe_parameter (param);
}

ARDOUR::framecnt_t
PluginInsert::signal_latency() const
{
	if (_user_latency) {
		return _user_latency;
	}

	return _plugins[0]->signal_latency ();
}

ARDOUR::PluginType
PluginInsert::type ()
{
       return plugin()->get_info()->type;
}

PluginInsert::PluginControl::PluginControl (PluginInsert* p, const Evoral::Parameter &param, boost::shared_ptr<AutomationList> list)
	: AutomationControl (p->session(), param, list, p->describe_parameter(param))
	, _plugin (p)
{
	Plugin::ParameterDescriptor desc;
	p->plugin(0)->get_parameter_descriptor (param.id(), desc);
	_logarithmic = desc.logarithmic;
	_sr_dependent = desc.sr_dependent;
	_toggled = desc.toggled;
}

/** @param val `user' value */
void
PluginInsert::PluginControl::set_value (double user_val)
{
	/* FIXME: probably should be taking out some lock here.. */

	double const plugin_val = user_to_plugin (user_val);

	for (Plugins::iterator i = _plugin->_plugins.begin(); i != _plugin->_plugins.end(); ++i) {
		(*i)->set_parameter (_list->parameter().id(), plugin_val);
	}

	boost::shared_ptr<Plugin> iasp = _plugin->_impulseAnalysisPlugin.lock();
	if (iasp) {
		iasp->set_parameter (_list->parameter().id(), plugin_val);
	}

	AutomationControl::set_value (user_val);
}

double
PluginInsert::PluginControl::user_to_plugin (double val) const
{
	/* no known transformations at this time */
	return val;
}

double
PluginInsert::PluginControl::user_to_ui (double val) const
{
	if (_logarithmic) {
		if (val > 0) {
			val = log (val);
		} else {
			val = 0;
		}
	}

	return val;
}

double
PluginInsert::PluginControl::ui_to_user (double val) const
{
	if (_logarithmic) {
		val = exp (val);
	}

	return val;
}

/** Convert plugin values to UI values.  See pbd/controllable.h */
double
PluginInsert::PluginControl::plugin_to_ui (double val) const
{
	return user_to_ui (plugin_to_user (val));
}

double
PluginInsert::PluginControl::plugin_to_user (double val) const
{
	/* no known transformations at this time */
	return val;
}

XMLNode&
PluginInsert::PluginControl::get_state ()
{
	stringstream ss;

	XMLNode& node (AutomationControl::get_state());
	ss << parameter().id();
	node.add_property (X_("parameter"), ss.str());

	return node;
}

/** @return `user' val */
double
PluginInsert::PluginControl::get_value () const
{
	/* FIXME: probably should be taking out some lock here.. */

	return plugin_to_user (_plugin->get_parameter (_list->parameter()));
}

boost::shared_ptr<Plugin>
PluginInsert::get_impulse_analysis_plugin()
{
	boost::shared_ptr<Plugin> ret;
	if (_impulseAnalysisPlugin.expired()) {
		ret = plugin_factory(_plugins[0]);
		_impulseAnalysisPlugin = ret;
	} else {
		ret = _impulseAnalysisPlugin.lock();
	}

	return ret;
}

void
PluginInsert::collect_signal_for_analysis (framecnt_t nframes)
{
	// called from outside the audio thread, so this should be safe
	// only do audio as analysis is (currently) only for audio plugins
	_signal_analysis_inputs.ensure_buffers(  DataType::AUDIO, input_streams().n_audio(),  nframes);
	_signal_analysis_outputs.ensure_buffers( DataType::AUDIO, output_streams().n_audio(), nframes);

	_signal_analysis_collected_nframes   = 0;
	_signal_analysis_collect_nframes_max = nframes;
}

/** Add a plugin to our list and activate it if we have already been activated */
void
PluginInsert::add_plugin_with_activation (boost::shared_ptr<Plugin> plugin)
{
	plugin->set_insert_info (this);
	_plugins.push_back (plugin);
	if (active()) {
		plugin->activate ();
	}
}

void
PluginInsert::realtime_handle_transport_stopped ()
{
	for (Plugins::iterator i = _plugins.begin(); i != _plugins.end(); ++i) {
		(*i)->realtime_handle_transport_stopped ();
	}
}

void
PluginInsert::set_splitting (bool s)
{
	if (_splitting == s) {
		return;
	}

	_splitting = s;
	SplittingChanged (); /* EMIT SIGNAL */
}
