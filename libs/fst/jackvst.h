#ifndef __jack_vst_h__
#define __jack_vst_h__

#include <sys/types.h>
#include <sys/time.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <fst.h>
#include <alsa/asoundlib.h>

typedef struct _JackVST JackVST;

struct _JackVST {
    jack_client_t *client;
    FSTHandle*     handle;
    FST*           fst;
    float        **ins;
    float        **outs;
    jack_port_t  *midi_port;
    jack_port_t  **inports;
    jack_port_t  **outports;
    void*          userdata;
    int            bypassed;
    int            muted;
    int		   current_program;

    int		   midi_map[128];
    volatile int   midi_learn;
    volatile int   midi_learn_CC;
    volatile int   midi_learn_PARAM;

    int		   resume_called;

    /* For VST/i support */

    int want_midi;
    pthread_t          midi_thread;
    snd_seq_t*         seq;
    int                midiquit;
    jack_ringbuffer_t* event_queue;
    struct VstEvents*  events;
};

#define MIDI_EVENT_MAX 1024

#endif /* __jack_vst_h__ */
