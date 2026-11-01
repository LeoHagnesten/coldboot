#ifdef COLDBOOT_DEBUG

#include "base/files.hh"
#include "vm/library.hh"
#include "tests/harness.hh"
#include <shellapi.h>

namespace tests {

	static void copy_wide ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < room - 1; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	static bool contains ( const char* text, const char* part ) {

		for ( int32_t i = 0; text [ i ]; i++ ) {

			int32_t j = 0;

			while ( part [ j ] && text [ i + j ] == part [ j ] )
				j++;

			if ( !part [ j ] )
				return true;

		}

		return false;

	}

	static bool rejected ( const char* text, const char* expected ) {

		vm::config* settings = new vm::config ( );
		char error [ 160 ] = { };
		bool parsed = vm::parse ( text, base::text_length ( text ), *settings, error, sizeof ( error ) );

		delete settings;

		return !parsed && contains ( error, expected );

	}

	static void remove_tree ( const wchar_t* folder ) {

		wchar_t from [ MAX_PATH + 2 ] = { };
		SHFILEOPSTRUCTW operation = { };

		copy_wide ( from, folder, MAX_PATH );
		operation.wFunc = FO_DELETE;
		operation.pFrom = from;
		operation.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		SHFileOperationW ( &operation );

	}

	void machine_file_tests ( ) {

		print ( "\r\n" );

		vm::config* original = new vm::config ( );
		vm::config* copy = new vm::config ( );
		base::list < char > text;
		char error [ 160 ] = { };

		copy_wide ( original->name, L"Test PC", vm::name_room );
		copy_wide ( original->floppies [ 0 ], L"C:\\Disks\\Fr\u00FCh \u65E5\u672C.img", MAX_PATH );
		copy_wide ( original->notes, L"Line one\nback\\slash", vm::notes_room );
		original->os [ 0 ] = 'f';
		original->os [ 1 ] = 'd';
		original->os [ 2 ] = 0;
		original->memory = 512;
		original->turbo = true;
		original->crt = true;
		original->window_x = -40;
		vm::serialize ( *original, text );

		bool parsed = vm::parse ( text.data ( ), text.count ( ), *copy, error, sizeof ( error ) );

		expect ( parsed && base::same_text ( copy->name, original->name ) && base::same_text ( copy->os, "fd" ) && copy->memory == 512 && copy->turbo && copy->crt && !copy->fast_floppy && copy->window_x == -40 && base::same_text ( copy->floppies [ 0 ], original->floppies [ 0 ] ) && base::same_text ( copy->notes, original->notes ), "machines: a machine file round-trips names, Unicode paths, flags and escaped notes" );

		bool strict = rejected ( "name=Box\n", "isn't a Coldboot machine file" )
			&& rejected ( "coldboot=2\nname=Box\n", "newer Coldboot" )
			&& rejected ( "coldboot=1\nname=Box\nmemory=abc\n", "line 3" )
			&& rejected ( "coldboot=1\nname=Box\nmemory=650\n", "steps of 16" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\n", "machine type" )
			&& rejected ( "coldboot=1\nname=CON\n", "folder name" )
			&& rejected ( "coldboot=1\nname=a/b\n", "folder name" )
			&& rejected ( "coldboot=1\nmemory=512\n", "no name" )
			&& rejected ( "coldboot=1\nname=Box\nturbo=yes\n", "0 or 1" );

		expect ( strict, "machines: broken, newer or unsafe machine files are refused with the reason and line" );

		wchar_t root [ MAX_PATH ];
		base::writer scratch;
		base::writer machines;
		base::writer list;

		GetTempPathW ( MAX_PATH, root );
		scratch.add ( root ).add ( L"coldboot-tests-" ).number ( GetCurrentProcessId ( ) );
		machines.add ( scratch.text ( ) ).add ( L"\\machines" );
		list.add ( scratch.text ( ) ).add ( L"\\library.list" );
		CreateDirectoryW ( scratch.text ( ), nullptr );

		vm::library* shelf = new vm::library ( );
		wchar_t first [ MAX_PATH ] = { };
		wchar_t second [ MAX_PATH ] = { };
		bool empty = !shelf->load ( list.text ( ) ) && shelf->count ( ) == 0;
		vm::config* settings = new vm::config ( );

		copy_wide ( settings->name, L"Alpha", vm::name_room );

		bool made = shelf->create ( *settings, machines.text ( ), first, error, sizeof ( error ) ) && shelf->create ( *settings, machines.text ( ), second, error, sizeof ( error ) );
		bool separate = !base::same_text ( first, second ) && GetFileAttributesW ( first ) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( second ) != INVALID_FILE_ATTRIBUTES;
		vm::library* again = new vm::library ( );
		bool reloaded = again->load ( list.text ( ) ) && again->count ( ) == 2 && again->at ( 0 ).readable && again->at ( 1 ).readable && base::same_text ( again->at ( 1 ).settings.name, L"Alpha" );
		wchar_t shouting [ MAX_PATH ];

		copy_wide ( shouting, first, MAX_PATH );
		CharUpperW ( shouting );

		bool unique = !again->add ( shouting );

		again->remove ( 0 );
		again->save ( );

		vm::library* trimmed = new vm::library ( );
		bool shrunk = trimmed->load ( list.text ( ) ) && trimmed->count ( ) == 1 && base::same_text ( trimmed->at ( 0 ).path, second );

		expect ( empty && made && separate && reloaded && unique && shrunk, "machines: the library creates separate folders for equal names, reloads, refuses duplicates and forgets removed machines" );

		wchar_t name [ 64 ];

		vm::running_name ( second, name, 64 );

		HANDLE mutex = CreateMutexW ( nullptr, TRUE, name );
		bool seen = vm::is_running ( shouting ) == false && vm::is_running ( second );

		CharUpperW ( second );
		seen = seen && vm::is_running ( second );
		CloseHandle ( mutex );
		seen = seen && !vm::is_running ( second );

		expect ( mutex && seen, "machines: a running machine is found by its path whatever the letter case" );

		static const uint32_t pixels [ 6 ] = { 0x112233, 0x445566, 0x778899, 0xAABBCC, 0xDDEEFF, 0x000001 };
		base::list < uint32_t > back;
		int32_t across = 0;
		int32_t down = 0;
		bool round = vm::write_preview ( scratch.text ( ), pixels, 3, 2 ) && vm::read_preview ( scratch.text ( ), back, across, down ) && across == 3 && down == 2 && back [ 4 ] == 0xDDEEFF && back [ 5 ] == 1;
		base::writer bogus;

		bogus.add ( scratch.text ( ) ).add ( L"\\preview.bin" );
		files::write ( bogus.text ( ), "CBPV\x10\x00\x00\x00\x10\x00\x00\x00", 12 );

		bool refused = !vm::read_preview ( scratch.text ( ), back, across, down ) && across == 0;

		expect ( round && refused, "machines: screen previews round-trip and truncated ones are refused" );

		remove_tree ( scratch.text ( ) );

		delete original;
		delete copy;
		delete settings;
		delete shelf;
		delete again;
		delete trimmed;

	}

}

#endif