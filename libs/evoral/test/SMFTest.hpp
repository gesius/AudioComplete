#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <evoral/SMF.hpp>

#include "SequenceTest.hpp"

#include <sigc++/sigc++.h>

#include <cassert>

using namespace Evoral;

template<typename Time>
class TestSMF : public SMF<Time> {
public:
	int  open(const std::string& path) {
		return SMF<Time>::open(path);
	}
	
	int read_event(uint32_t* delta_t, uint32_t* size, uint8_t** buf) const {
		return SMF<Time>::read_event(delta_t, size, buf);
	}
};

class SMFTest : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE (SMFTest);
    CPPUNIT_TEST (takeFiveTest);
    CPPUNIT_TEST_SUITE_END ();

    public:
    
    	typedef double Time;
       	
        void setUp (void) { 
           type_map = new DummyTypeMap();
           assert(type_map);
           seq = new MySequence<Time>(*type_map, 0);
           assert(seq);
        }
        
        void tearDown (void) {
        	delete seq;
        	delete type_map;
        }

        void takeFiveTest (void);

    private:
       	DummyTypeMap*       type_map;
       	MySequence<Time>*   seq;
};
