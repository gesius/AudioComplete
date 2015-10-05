/*
    Copyright (C) 2013 Waves Audio Ltd.

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

#include <iostream>

#include "pbd/error.h"
#include "pbd/debug.h"
#include "pbd/compose.h"
#include "pbd/stacktrace.h"

#include "waves_midi_device.h"
#include "waves_midi_event.h"

// use non-zero latency because we want output to be timestapmed
#define LATENCY 0

#define QUEUE_LENGTH 1024

using namespace ARDOUR;
using namespace PBD;

WavesMidiDevice::WavesMidiDevice (const std::string& device_name)
    : _pm_input_id (pmNoDevice)
    , _pm_output_id (pmNoDevice)
    , _name (device_name)
    , _input_queue (NULL)
    , _output_queue (NULL)
    , _input_pm_stream (NULL)
    , _output_pm_stream (NULL)
    , _incomplete_waves_midi_event (NULL)
{
	_pm_input_id = _pm_output_id = pmNoDevice;
	int count = Pm_CountDevices ();

	for (int i = 0; i < count; i++) {

		const PmDeviceInfo* pm_device_info = Pm_GetDeviceInfo (i);

		if (pm_device_info == NULL) {
			continue;
		}
		if (name () == pm_device_info->name) {
			if (pm_device_info->input){
				_pm_input_id = i;
			}
			if (pm_device_info->output){
				_pm_output_id = i;
			}
		}
	}
}

WavesMidiDevice::~WavesMidiDevice ()
{
        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::~WavesMidiDevice (): %1\n", name()));
        close ();
}

int
WavesMidiDevice::open (PmTimeProcPtr time_proc, void* time_info)
{
    DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::open (): %1", name ()));

    if (is_input () ) {
		// COMMENTED DBG LOGS */ std::cout << "WavesMidiDevice::open (): INPUT" << _pm_input_id << "-[" << name () <<  "]" << std::endl;

		if (!_input_pm_stream) {
			// create queue
			if (!_input_queue) {
				// COMMENTED DBG LOGS */ std::cout << "    going to Pm_QueueCreate for INPUT: " << std::endl;
				_input_queue = Pm_QueueCreate (QUEUE_LENGTH, sizeof (const WavesMidiEvent*));
				// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
				if (NULL == _input_queue) {
                    std::cerr << "WavesMidiDevice::open (): _input_queue = Pm_QueueCreate () failed for " << _pm_input_id << "-[" << name () <<  "]!" << std::endl;
                    return -1;
				}
			}
			// create stream
			// COMMENTED DBG LOGS */ std::cout << "    going to Pm_OpenInput : " << std::endl;
            if (pmNoError != Pm_OpenInput (&_input_pm_stream,
                                            _pm_input_id,
                                            NULL,
                                            1024,
                                            time_proc,
                                            time_info)) {
					// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
                    char* err_msg = new char[256];
					Pm_GetHostErrorText(err_msg, 256);
					std::cerr << "WavesMidiDevice::open (): Pm_OpenInput () failed for " << _pm_input_id << "-[" << name () <<  "]!" << std::endl;
					std::cerr << "    Port Midi Host Error: " << err_msg << std::endl;
					close ();
                    return -1;
            }
			// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
		}
	}

	if (is_output () ) {
		// COMMENTED DBG LOGS */ std::cout << "WavesMidiDevice::open (): OUTPUT" << _pm_output_id << "-[" << name () <<  "]" << std::endl;

		if (!_output_pm_stream) {
			// create queue
			if (!_output_queue) {
				// COMMENTED DBG LOGS */ std::cout << "    going to Pm_QueueCreate for OUTPUT : " << std::endl;
				_output_queue = Pm_QueueCreate (QUEUE_LENGTH, sizeof (const WavesMidiEvent*));
				// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
				if (NULL == _output_queue) {
					std::cerr << "WavesMidiDevice::open (): _output_queue = Pm_QueueCreate () failed for " << _pm_output_id << "-[" << name () <<  "]!" << std::endl;
					return -1;
				}
			}
			// create stream
			// COMMENTED DBG LOGS */ std::cout << "    going to Pm_OpenOutput : " << std::endl;
			if (pmNoError != Pm_OpenOutput (&_output_pm_stream,
			                                _pm_output_id,
			                                NULL,
			                                1024,
			                                time_proc,
			                                time_info,
			                                LATENCY)) {
				// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
				char* err_msg = new char[256];
				Pm_GetHostErrorText(err_msg, 256);
				std::cerr << "WavesMidiDevice::open (): Pm_OpenOutput () failed for " << _pm_output_id << "-[" << name () <<  "]!" << std::endl;
				std::cerr << "    Port Midi Host Error: " << err_msg << std::endl;
				close ();
				return -1;
			}
			// COMMENTED DBG LOGS */ std::cout << "    DONE : " << std::endl;
		}
	}
	return 0;
}

void
WavesMidiDevice::close ()
{
	DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::close (): %1\n", name ()));
	WavesMidiEvent *waves_midi_event;

	// save _input_pm_stream and _output_pm_stream to local buf
	PmStream* input_pm_stream = _input_pm_stream;
	PmStream* output_pm_stream = _output_pm_stream;
	_input_pm_stream = _output_pm_stream = NULL;

        // input
	if (input_pm_stream) {
		// close stream
		PmError err = Pm_Close (input_pm_stream);
		if (err != pmNoError) {
			char* err_msg = new char[256];
			Pm_GetHostErrorText(err_msg, 256);
			std::cerr << "WavesMidiDevice::close (): Pm_Close (input_pm_stream) failed (" << err << ") for " << input_pm_stream << "-[" << name () <<  "]!" << std::endl;
			std::cerr << "    Port Midi Host Error: " << err_msg << std::endl;
		}
		_pm_input_id = pmNoDevice;
	}

	// close queue
	if (_input_queue) {
		while (1 == Pm_Dequeue (_input_queue, &waves_midi_event)) {
			delete waves_midi_event; // XXX possible dup free in ~WavesMidiBuffer() (?)
		}
		Pm_QueueDestroy (_input_queue);
		_input_queue = NULL;
	}

        // output
	if ( output_pm_stream ) {
		// close stream
		PmError err = Pm_Close (output_pm_stream);
		if (err != pmNoError) {
			char* err_msg = new char[256];
			Pm_GetHostErrorText(err_msg, 256);
			std::cerr << "WavesMidiDevice::close (): Pm_Close (output_pm_stream) failed (" << err << ") for " << output_pm_stream << "-[" << name () <<  "]!" << std::endl;
			std::cerr << "    Port Midi Host Error: " << err_msg << std::endl;
		}
		_pm_output_id = pmNoDevice;
	}

	// close queue
	if (_output_queue) {
		while (1 == Pm_Dequeue (_output_queue, &waves_midi_event)) {
			delete waves_midi_event; // XXX possible dup free in ~WavesMidiBuffer() (?)
		}
		Pm_QueueDestroy (_output_queue);
		_output_queue = NULL;
	}
}

void
WavesMidiDevice::do_io ()
{
        read_midi ();
        write_midi ();
}

void
WavesMidiDevice::read_midi ()
{
        if (NULL == _input_pm_stream) {
                return;
        }

        while (Pm_Poll (_input_pm_stream) > 0) {

                PmEvent pm_event; // just one message at a time
                int result = Pm_Read (_input_pm_stream, &pm_event, 1);

                if (result < 0) {
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("Pm_Read failed for (): [%1]\n", name()));
                        break;
                }

                DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_read_midi (): [%1] evt-tm: %2\n", name(), pm_event.timestamp));

                if (_incomplete_waves_midi_event == NULL ) {
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_read_midi (): [%1] new incomplete_waves_midi_event\n", name()));
                        _incomplete_waves_midi_event = new WavesMidiEvent (pm_event.timestamp);
                }

                WavesMidiEvent *nested_pm_event = _incomplete_waves_midi_event->append_data (pm_event);

                if (nested_pm_event) {
                        Pm_Enqueue (_input_queue, &nested_pm_event);
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_read_midi (): [%1] : Pm_Enqueue (_input_queue, nested_pm_event)\n", name()));
                }

                switch ( _incomplete_waves_midi_event->state ()) {
                case WavesMidiEvent::BROKEN:
                        delete _incomplete_waves_midi_event;
                        _incomplete_waves_midi_event = NULL;
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_read_midi (): [%1] : case WavesMidiEvent::BROKEN:\n", name()));
                        break;
                case WavesMidiEvent::COMPLETE:
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_read_midi (): [%1] : Pm_Enqueue (_input_queue, _incomplete_waves_midi_event); %3\n",  name (), _incomplete_waves_midi_event));

						if (pmNoError != Pm_Enqueue (_input_queue, &_incomplete_waves_midi_event) ) {
							char* err_msg = new char[256];
							Pm_GetHostErrorText(err_msg, 256);
							std::cerr << "WavesMidiDevice::read_midi (): Pm_Enqueue () failed for [" << name () <<  "]!" << std::endl;
							std::cerr << "Error: " << err_msg << std::endl;
						}

                        _incomplete_waves_midi_event = NULL;
                        break;
                default:
                        break;
                }
        }
}

void
WavesMidiDevice::write_midi ()
{
        if (NULL == _output_pm_stream) {
                return;
        }

        PmError err;
        WavesMidiEvent *waves_midi_event;

        while (1 == Pm_Dequeue (_output_queue, &waves_midi_event)) {
                if (waves_midi_event->sysex ()) {
                        // LATENCY compensation
                        err = Pm_WriteSysEx (_output_pm_stream, waves_midi_event->timestamp () - LATENCY, waves_midi_event->data ());
                        if (0 > err) {
                                std::cerr << "WavesMidiDevice::write_event_to_device (): [" << name () << "] Pm_WriteSysEx () failed (" << err << ")!" << std::endl;
                        };
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_write_midi (): SYSEX used, ev->tm: %1", waves_midi_event->timestamp () - LATENCY));
                }
                else
                {
                        err = Pm_WriteShort (_output_pm_stream, waves_midi_event->timestamp () - LATENCY, * (PmMessage*)waves_midi_event->data ());
                        if (0 > err) {
                                error << "WavesMidiDevice::write_event_to_device (): [" << name () << "] Pm_WriteShort () failed (" << err << ")!" << endmsg;
                        }
                        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::_write_midi (): SHORTMSG used, ev->tm: %1\n", waves_midi_event->timestamp () - LATENCY));
                }
        }

        return;
}

int
WavesMidiDevice::enqueue_output_waves_midi_event (const WavesMidiEvent* waves_midi_event)
{
        DEBUG_TRACE (DEBUG::WavesMIDI, string_compose ("WavesMidiDevice::enqueue_output_waves_midi_event () [%1]\n", name()));

        if (waves_midi_event == NULL) {
                error << "WavesMidiDevice::put_event_to_callback (): 'waves_midi_event' is NULL!" << endmsg;
                return -1;
        }

        PmError err = Pm_Enqueue (_output_queue, &waves_midi_event);

        if (0 > err) {
                error << "WavesMidiDevice::put_event_to_callback (): Pm_Enqueue () failed (" << err << ")!" << endmsg;
                return -1;
        };

        return 0;
}

WavesMidiEvent*
WavesMidiDevice::dequeue_input_waves_midi_event ()
{
        WavesMidiEvent* waves_midi_event;
        if (Pm_Dequeue (_input_queue, &waves_midi_event) == 1) {
                return waves_midi_event;
        }
        return NULL;
}
