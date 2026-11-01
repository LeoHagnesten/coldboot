#pragma once

#include "base/base.hh"

namespace tests {

	void open_output ( );

	void print ( const char* text );

	void expect ( bool passed, const char* name );

	int32_t failure_count ( );

	void append ( char* out, int32_t room, const char* text );

	void append_hex ( char* out, int32_t room, uint32_t value, int32_t digits );

	void append_number ( char* out, int32_t room, int64_t value );

	void device_tests ( );

	void storage_tests ( );

	void assembler_tests ( );

	void bios_tests ( );

	void emulator_tests ( );

	void machine_file_tests ( );

	void hypervisor_tests ( );

	int32_t foundation_tests ( );

	int32_t single_step_suite ( const wchar_t* folder, const wchar_t* only, bool exact, bool timing );

	int32_t capture_main ( wchar_t** arguments, int32_t count );

}