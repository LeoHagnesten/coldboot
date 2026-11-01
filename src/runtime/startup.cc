#include "runtime/runtime.hh"

extern "C" void start ( ) {

	runtime_initialize ( );
	runtime_exit ( program_main ( ) );

}