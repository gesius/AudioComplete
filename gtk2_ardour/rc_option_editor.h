#include "option_editor.h"

/** @file rc_option_editor.h
 *  @brief Editing of options which are obtained from and written back to one of the .rc files.
 *
 *  This is subclassed from OptionEditor.  Simple options (e.g. boolean and simple choices)
 *  are expressed using subclasses of Option.  More complex UI elements are represented
 *  using individual classes subclassed from OptionEditorBox.
 */

/** Editor for options which are obtained from and written back to one of the .rc files. */
class RCOptionEditor : public OptionEditor
{
public:
	RCOptionEditor ();

private:
	void parameter_changed (std::string const &);
	
	ARDOUR::RCConfiguration* _rc_config;
	BoolOption* _solo_control_is_listen_control;
	ComboOption<ARDOUR::ListenPosition>* _listen_position;
};
