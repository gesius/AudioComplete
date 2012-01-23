#!/bin/bash
#
# Run libardour profiling tests.
#

if [ ! -f './tempo.cc' ]; then
    echo "This script must be run from within the libs/ardour directory";
    exit 1;
fi

srcdir=`pwd`
cd ../../build

libs='libs'

export LD_LIBRARY_PATH=$libs/audiographer:$libs/vamp-sdk:$libs/surfaces:$libs/surfaces/control_protocol:$libs/ardour:$libs/midi++2:$libs/pbd:$libs/rubberband:$libs/soundtouch:$libs/gtkmm2ext:$libs/appleutility:$libs/taglib:$libs/evoral:$libs/evoral/src/libsmf:$libs/timecode:/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

export ARDOUR_PANNER_PATH=$libs/panners/2in2out:$libs/panners/1in2out:$libs/panners/vbap

export LD_PRELOAD=/home/carl/src/libfakejack/libjack.so

if [ "$1" == "--debug" ]; then
        gdb ./libs/ardour/run-profiling
elif [ "$1" == "--valgrind" ]; then
        valgrind ./libs/ardour/run-profiling
elif [ "$1" == "--callgrind" ]; then
        valgrind --tool=callgrind ./libs/ardour/run-profiling
else
        ./libs/ardour/run-profiling $*
fi
