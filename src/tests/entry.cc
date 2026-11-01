#ifdef COLDBOOT_DEBUG

#include "tests/harness.hh"
#include <windows.h>
#include <shellapi.h>

bool test_main ( int32_t& result ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	int32_t mode = 0;
	const wchar_t* folder = nullptr;
	const wchar_t* only = nullptr;
	bool exact = false;
	bool timing = false;

	for ( int32_t i = 1; arguments && i < count; i++ ) {

		if ( base::same_text ( arguments [ i ], L"--test" ) )
			mode = 1;
		else if ( base::same_text ( arguments [ i ], L"--cpu" ) && i + 1 < count ) {

			mode = 2;
			folder = arguments [ ++i ];

		} else if ( base::same_text ( arguments [ i ], L"--only" ) && i + 1 < count )
			only = arguments [ ++i ];
		else if ( base::same_text ( arguments [ i ], L"--exact" ) )
			exact = true;
		else if ( base::same_text ( arguments [ i ], L"--timing" ) )
			timing = true;
		else if ( ( base::same_text ( arguments [ i ], L"--bios" ) || base::same_text ( arguments [ i ], L"--own" ) || base::same_text ( arguments [ i ], L"--asm" ) ) && !mode )
			mode = 3;

	}

	if ( !mode ) {

		if ( arguments )
			LocalFree ( arguments );

		return false;

	}

	tests::open_output ( );

	if ( mode == 3 )
		result = tests::capture_main ( arguments, count );
	else
		result = mode == 1 ? tests::foundation_tests ( ) : tests::single_step_suite ( folder, only, exact, timing );

	if ( arguments )
		LocalFree ( arguments );

	return true;

}

#endif