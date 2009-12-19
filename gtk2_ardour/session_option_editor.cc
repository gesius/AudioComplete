#include "ardour/session.h"
#include "ardour/io.h"
#include "ardour/auditioner.h"
#include "ardour/audioengine.h"
#include "ardour/port.h"
#include "session_option_editor.h"
#include "port_matrix.h"
#include "i18n.h"

using namespace std;
using namespace ARDOUR;

class OptionsPortMatrix : public PortMatrix
{
public:
	OptionsPortMatrix (Gtk::Window* parent, ARDOUR::Session* session)
		: PortMatrix (parent, session, DataType::AUDIO)
	{
		_port_group.reset (new PortGroup (""));
		_ports[OURS].add_group (_port_group);

		setup_all_ports ();
		init ();
	}

	void setup_ports (int dim)
	{
		if (dim == OURS) {
			_port_group->clear ();
			_port_group->add_bundle (_session->click_io()->bundle());
			_port_group->add_bundle (_session->the_auditioner()->output()->bundle());
		} else {
			_ports[OTHER].gather (_session, true, false);
		}
	}

	void set_state (ARDOUR::BundleChannel c[2], bool s)
	{
		Bundle::PortList const & our_ports = c[OURS].bundle->channel_ports (c[OURS].channel);
		Bundle::PortList const & other_ports = c[OTHER].bundle->channel_ports (c[OTHER].channel);

		if (c[OURS].bundle == _session->click_io()->bundle()) {

			for (ARDOUR::Bundle::PortList::const_iterator i = our_ports.begin(); i != our_ports.end(); ++i) {
				for (ARDOUR::Bundle::PortList::const_iterator j = other_ports.begin(); j != other_ports.end(); ++j) {

					Port* f = _session->engine().get_port_by_name (*i);
					assert (f);

					if (s) {
						_session->click_io()->connect (f, *j, 0);
					} else {
						_session->click_io()->disconnect (f, *j, 0);
					}
				}
			}
		}
	}

	PortMatrixNode::State get_state (ARDOUR::BundleChannel c[2]) const
	{
		Bundle::PortList const & our_ports = c[OURS].bundle->channel_ports (c[OURS].channel);
		Bundle::PortList const & other_ports = c[OTHER].bundle->channel_ports (c[OTHER].channel);

		if (c[OURS].bundle == _session->click_io()->bundle()) {

			for (ARDOUR::Bundle::PortList::const_iterator i = our_ports.begin(); i != our_ports.end(); ++i) {
				for (ARDOUR::Bundle::PortList::const_iterator j = other_ports.begin(); j != other_ports.end(); ++j) {
					Port* f = _session->engine().get_port_by_name (*i);
					assert (f);

					if (f->connected_to (*j)) {
						return PortMatrixNode::ASSOCIATED;
					} else {
						return PortMatrixNode::NOT_ASSOCIATED;
					}
				}
			}

		} else {

			/* XXX */

		}

		return PortMatrixNode::NOT_ASSOCIATED;
	}

	bool list_is_global (int dim) const {
		return (dim == OTHER);
	}

	bool can_remove_channels (boost::shared_ptr<Bundle>) const {
		return false;
	}

	void remove_channel (ARDOUR::BundleChannel) {}

	std::string disassociation_verb () const {
		return _("Disassociate");
	}

private:
	/* see PortMatrix: signal flow from 0 to 1 (out to in) */
	enum {
		OURS = 0,
		OTHER = 1,
	};

	boost::shared_ptr<PortGroup> _port_group;

};


class ConnectionOptions : public OptionEditorBox
{
public:
	ConnectionOptions (Gtk::Window* parent, ARDOUR::Session* s)
		: _port_matrix (parent, s)
	{
		_box->pack_start (_port_matrix);
	}

	void parameter_changed (string const &)
	{

	}

	void set_state_from_config ()
	{

	}

private:
	OptionsPortMatrix _port_matrix;
};

SessionOptionEditor::SessionOptionEditor (Session* s)
	: OptionEditor (&(s->config), _("Session Preferences"))
	, _session_config (&(s->config))
{
	/* SYNC */

	ComboOption<uint32_t>* spf = new ComboOption<uint32_t> (
		"subframes-per-frame",
		_("Subframes per frame"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_subframes_per_frame),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_subframes_per_frame)
		);

	spf->add (80, _("80"));
	spf->add (100, _("100"));

	add_option (_("Sync"), spf);

	ComboOption<SyncSource>* ssrc = new ComboOption<SyncSource> (
		"sync-source",
		_("External sync source"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_sync_source),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_sync_source)
		);
	
	s->MTC_PortChanged.connect (_session_connections, boost::bind (&SessionOptionEditor::populate_sync_options, this, s, ssrc));
	s->MIDIClock_PortChanged.connect (_session_connections, boost::bind (&SessionOptionEditor::populate_sync_options, this, s, ssrc));
	s->config.ParameterChanged.connect (_session_connections, boost::bind (&SessionOptionEditor::follow_sync_state, this, _1, s, ssrc));

	populate_sync_options (s, ssrc);
	follow_sync_state (string ("external-sync"), s, ssrc);

	add_option (_("Sync"), ssrc);

	ComboOption<TimecodeFormat>* smf = new ComboOption<TimecodeFormat> (
		"timecode-format",
		_("Timecode frames-per-second"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_timecode_format),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_timecode_format)
		);

	smf->add (timecode_23976, _("23.976"));
	smf->add (timecode_24, _("24"));
	smf->add (timecode_24976, _("24.976"));
	smf->add (timecode_25, _("25"));
	smf->add (timecode_2997, _("29.97"));
	smf->add (timecode_2997drop, _("29.97 drop"));
	smf->add (timecode_30, _("30"));
	smf->add (timecode_30drop, _("30 drop"));
	smf->add (timecode_5994, _("59.94"));
	smf->add (timecode_60, _("60"));

	add_option (_("Sync"), smf);

	add_option (_("Sync"), new BoolOption (
			    "timecode-source-is-synced",
			    _("Timecode source shares sample clock with audio interface"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_timecode_source_is_synced),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_timecode_source_is_synced)
			    ));

	ComboOption<float>* vpu = new ComboOption<float> (
		"video-pullup",
		_("Pull-up / pull-down"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_video_pullup),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_video_pullup)
		);

	vpu->add (4.1667 + 0.1, _("4.1667 + 0.1%"));
	vpu->add (4.1667, _("4.1667"));
	vpu->add (4.1667 - 0.1, _("4.1667 - 0.1%"));
	vpu->add (0.1, _("0.1"));
	vpu->add (0, _("none"));
	vpu->add (-0.1, _("-0.1"));
	vpu->add (-4.1667 + 0.1, _("-4.1667 + 0.1%"));
	vpu->add (-4.1667, _("-4.1667"));
	vpu->add (-4.1667 - 0.1, _("-4.1667 - 0.1%"));

	add_option (_("Sync"), vpu);

	/* FADES */

	ComboOption<CrossfadeModel>* cfm = new ComboOption<CrossfadeModel> (
		"xfade-model",
		_("Crossfades are created"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_xfade_model),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_xfade_model)
		);

	cfm->add (FullCrossfade, _("to span entire overlap"));
	cfm->add (ShortCrossfade, _("short"));

	add_option (_("Fades"), cfm);

	add_option (_("Fades"), new SpinOption<float> (
		_("short-xfade-seconds"),
		_("Short crossfade length"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_short_xfade_seconds),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_short_xfade_seconds),
		0, 1000, 1, 10,
		_("ms"), 0.001
			    ));

	add_option (_("Fades"), new SpinOption<float> (
		_("destructive-xfade-seconds"),
		_("Destructive crossfade length"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_destructive_xfade_msecs),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_destructive_xfade_msecs),
		0, 1000, 1, 10,
		_("ms")
			    ));

	add_option (_("Fades"), new BoolOption (
			    "auto-xfade",
			    _("Create crossfades automatically"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_auto_xfade),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_auto_xfade)
			    ));

        add_option (_("Fades"), new BoolOption (
			    "xfades-active",
			    _("Crossfades active"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_xfades_active),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_xfades_active)
			    ));

	add_option (_("Fades"), new BoolOption (
			    "xfades-visible",
			    _("Crossfades visible"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_xfades_visible),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_xfades_visible)
			    ));

	add_option (_("Fades"), new BoolOption (
			    "use-region-fades",
			    _("Region fades active"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_use_region_fades),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_use_region_fades)
			    ));

	add_option (_("Fades"), new BoolOption (
			    "show-region-fades",
			    _("Region fades visible"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_show_region_fades),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_show_region_fades)
			    ));

	/* MISC */

	add_option (_("Misc"), new OptionEditorHeading (_("Audio file format")));

	ComboOption<SampleFormat>* sf = new ComboOption<SampleFormat> (
		"native-file-data-format",
		_("Sample format"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_native_file_data_format),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_native_file_data_format)
		);

	sf->add (FormatFloat, _("32-bit floating point"));
	sf->add (FormatInt24, _("24-bit integer"));
	sf->add (FormatInt16, _("16-bit integer"));

	add_option (_("Misc"), sf);

	ComboOption<HeaderFormat>* hf = new ComboOption<HeaderFormat> (
		"native-file-header-format",
		_("File type"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_native_file_header_format),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_native_file_header_format)
		);

	hf->add (BWF, _("Broadcast WAVE"));
	hf->add (WAVE, _("WAVE"));
	hf->add (WAVE64, _("WAVE-64"));
	hf->add (CAF, _("CAF"));

	add_option (_("Misc"), hf);

	add_option (_("Misc"), new OptionEditorHeading (_("Layering")));

	ComboOption<LayerModel>* lm = new ComboOption<LayerModel> (
		"layer-model",
		_("Layering model in overlaid mode"),
		sigc::mem_fun (*_session_config, &SessionConfiguration::get_layer_model),
		sigc::mem_fun (*_session_config, &SessionConfiguration::set_layer_model)
		);

	lm->add (LaterHigher, _("later is higher"));
	lm->add (MoveAddHigher, _("most recently moved or added is higher"));
	lm->add (AddHigher, _("most recently added is higher"));

	add_option (_("Misc"), lm);

	add_option (_("Misc"), new OptionEditorHeading (_("Broadcast WAVE metadata")));

	add_option (_("Misc"), new EntryOption (
			    "bwf-country-code",
			    _("Country code"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_bwf_country_code),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_bwf_country_code)
			    ));

	add_option (_("Misc"), new EntryOption (
			    "bwf-organization-code",
			    _("Organization code"),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::get_bwf_organization_code),
			    sigc::mem_fun (*_session_config, &SessionConfiguration::set_bwf_organization_code)
			    ));

	add_option (_("Connections"), new ConnectionOptions (this, s));
}

void
SessionOptionEditor::populate_sync_options (Session* s, Option* opt)
{
	ComboOption<SyncSource>* sync_opt = dynamic_cast<ComboOption<SyncSource>* > (opt);

	vector<SyncSource> sync_opts = s->get_available_sync_options ();

	sync_opt->clear ();

	for (vector<SyncSource>::iterator i = sync_opts.begin(); i != sync_opts.end(); ++i) {
		sync_opt->add (*i, sync_source_to_string (*i));
	}
}

void
SessionOptionEditor::follow_sync_state (std::string p, Session* s, Option* opt)
{
	ComboOption<SyncSource>* sync_opt = dynamic_cast<ComboOption<SyncSource>* > (opt);
	if (p == "external-sync") {
		if (s->config.get_external_sync()) {
			sync_opt->set_sensitive (false);
		} else {
			sync_opt->set_sensitive (true);
		}
	}
}
