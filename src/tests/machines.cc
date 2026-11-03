#ifdef COLDBOOT_DEBUG

#include "base/files.hh"
#include "base/gif.hh"
#include "pc/iso.hh"
#include "pc/storage.hh"
#include "pc/vga.hh"
#include "vm/library.hh"
#include "vm/unattend.hh"
#include "tests/harness.hh"
#include <shellapi.h>
#include <wincodec.h>

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
			&& rejected ( "coldboot=1\nname=Box\nmachine=mainframe\n", "machine type" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\nprocessors=65\n", "1 to 64" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\nboot=floppy\n", "cd or disk" )
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

		vm::config* modern = new vm::config ( );
		vm::config* modern_copy = new vm::config ( );
		base::list < char > modern_text;

		copy_wide ( modern->name, L"Windows box", vm::name_room );
		copy_wide ( modern->iso, L"D:\\Images\\Win11_24H2_English_x64.iso", MAX_PATH );
		copy_wide ( modern->disk, L"C:\\VMs\\Windows box\\Windows box.img", MAX_PATH );

		for ( int32_t i = 0; "windows11" [ i ]; i++ ) {

			modern->os [ i ] = "windows11" [ i ];
			modern->os [ i + 1 ] = 0;

		}

		modern->machine = vm::MACHINE_PC;
		modern->memory_mb = 8192;
		modern->processors = 4;
		modern->cap = 80;
		modern->disk_first = true;
		modern->uefi = true;
		modern->network = false;
		vm::serialize ( *modern, modern_text );

		bool modern_parsed = vm::parse ( modern_text.data ( ), modern_text.count ( ), *modern_copy, error, sizeof ( error ) );

		expect ( modern_parsed && modern_copy->machine == vm::MACHINE_PC && modern_copy->memory_mb == 8192 && modern_copy->processors == 4 && modern_copy->cap == 80 && modern_copy->disk_first && modern_copy->uefi && !modern_copy->network && base::same_text ( modern_copy->iso, modern->iso ) && base::same_text ( modern_copy->disk, modern->disk ), "machines: a modern PC keeps its memory, processors, CPU cap, boot order, firmware, network, ISO and disk" );

		const vm::preset* windows = vm::detect_image ( L"D:\\x\\Win11_24H2_English_x64.iso" );
		const vm::preset* older = vm::detect_image ( L"D:\\x\\Win10_22H2_English_x64.iso" );
		const vm::preset* ubuntu = vm::detect_image ( L"D:\\x\\ubuntu-24.04.1-desktop-amd64.iso" );
		const vm::preset* other = vm::detect_image ( L"D:\\x\\tools.iso" );

		expect ( windows && base::same_text ( windows->id, "windows11" ) && older && base::same_text ( older->id, "windows10" ) && ubuntu && base::same_text ( ubuntu->id, "ubuntu" ) && other && base::same_text ( other->id, "pc" ), "machines: dropped ISOs are recognised as Windows 11, Windows 10, Ubuntu or a plain PC" );

		base::writer disk_path;

		disk_path.add ( scratch.text ( ) ).add ( L"\\sparse.img" );

		bool disk_made = vm::create_disk ( disk_path.text ( ), 64ull * 1024 * 1024 * 1024 );
		WIN32_FILE_ATTRIBUTE_DATA disk_data = { };
		DWORD allocated_high = 0;
		DWORD allocated_low = GetCompressedFileSizeW ( disk_path.text ( ), &allocated_high );
		bool disk_sized = GetFileAttributesExW ( disk_path.text ( ), GetFileExInfoStandard, &disk_data ) && disk_data.nFileSizeHigh == 16 && disk_data.nFileSizeLow == 0;

		expect ( disk_made && disk_sized && allocated_high == 0 && allocated_low < 1024 * 1024 && !vm::create_disk ( disk_path.text ( ), 1024 ), "machines: a 64 GB disk is created instantly as a sparse file and never overwrites an existing one" );

		vm::config* doomed = new vm::config ( );
		wchar_t owned [ MAX_PATH ] = { };
		wchar_t shared [ MAX_PATH ] = { };
		wchar_t owned_folder [ MAX_PATH ] = { };
		wchar_t shared_folder [ MAX_PATH ] = { };
		base::writer outside;
		base::writer kept;

		outside.add ( scratch.text ( ) ).add ( L"\\outside.img" );
		files::write ( outside.text ( ), "disk", 4 );
		copy_wide ( doomed->name, L"Doomed", vm::name_room );
		copy_wide ( doomed->iso, outside.text ( ), MAX_PATH );
		doomed->machine = vm::MACHINE_PC;

		bool built = shelf->create ( *doomed, machines.text ( ), owned, error, sizeof ( error ), 1024 * 1024 );

		copy_wide ( doomed->disk, outside.text ( ), MAX_PATH );
		built = built && shelf->create ( *doomed, machines.text ( ), shared, error, sizeof ( error ) );
		vm::folder_of ( owned, owned_folder );
		vm::folder_of ( shared, shared_folder );
		vm::write_preview ( owned_folder, pixels, 3, 2 );
		kept.add ( shared_folder ).add ( L"\\notes.txt" );
		files::write ( kept.text ( ), "mine", 4 );

		vm::config* owned_settings = new vm::config ( );
		bool owned_loaded = vm::load ( owned, *owned_settings, error, sizeof ( error ) );
		wchar_t owned_disk [ MAX_PATH ];

		copy_wide ( owned_disk, owned_settings->disk, MAX_PATH );

		bool erased = owned_loaded && owned_disk [ 0 ] && vm::erase ( owned, owned_settings ) && vm::erase ( shared, doomed );
		bool gone = GetFileAttributesW ( owned ) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( owned_disk ) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( owned_folder ) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( shared ) == INVALID_FILE_ATTRIBUTES;
		bool spared = GetFileAttributesW ( outside.text ( ) ) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( kept.text ( ) ) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW ( machines.text ( ) ) != INVALID_FILE_ATTRIBUTES;

		expect ( built && erased && gone && spared, "machines: deleting erases the machine file, preview, own disk and empty folder but never ISOs, outside disks or other files" );

		delete doomed;
		delete owned_settings;

		vm::host_info host = vm::read_host ( );

		expect ( host.memory_mb >= 1024 && host.logical >= host.physical && host.physical >= 1, "machines: the host's memory and core counts are read for the sliders" );

		delete modern;
		delete modern_copy;

		bool names = vm::user_name_valid ( L"Sam" ) && vm::user_name_valid ( L"Ana Lu" ) && !vm::user_name_valid ( L"" ) && !vm::user_name_valid ( L" . " ) && !vm::user_name_valid ( L"a/b" ) && !vm::user_name_valid ( L"Sam." ) && !vm::user_name_valid ( L"abcdefghijklmnopqrstu" );

		expect ( names, "machines: Windows user names refuse blanks, trailing dots, reserved characters and more than 20 letters" );

		vm::setup_answers* answers = new vm::setup_answers ( );
		base::list < char > answer_text;

		copy_wide ( answers->user, L"Zoë", 64 );
		copy_wide ( answers->password, L"a&b<c", 64 );
		answers->uefi = true;
		vm::build_answers ( *answers, answer_text );
		answer_text.push ( 0 );

		bool unattended = contains ( answer_text.data ( ), "<Username>Zo\xC3\xAB</Username>" ) && contains ( answer_text.data ( ), "<Value>a&amp;b&lt;c</Value>" ) && contains ( answer_text.data ( ), "<Type>EFI</Type>" ) && contains ( answer_text.data ( ), "Windows 10 Pro" ) && contains ( answer_text.data ( ), "Coldboot Tools.exe\" --install" ) && contains ( answer_text.data ( ), "NewNetworkWindowOff" ) && !contains ( answer_text.data ( ), "BypassNRO" ) && !contains ( answer_text.data ( ), "International-Core-WinPE" ) && !contains ( answer_text.data ( ), "LabConfig" );

		copy_wide ( answers->user, L"Sam", 64 );
		answers->password [ 0 ] = 0;
		answers->uefi = false;
		answers->windows11 = true;
		answers->language [ 0 ] = 'd';
		answers->language [ 1 ] = 'e';
		answers->language [ 2 ] = '-';
		answers->language [ 3 ] = 'D';
		answers->language [ 4 ] = 'E';
		vm::build_answers ( *answers, answer_text );
		answer_text.push ( 0 );
		unattended = unattended && contains ( answer_text.data ( ), "<UILanguage>de-DE</UILanguage>" ) && contains ( answer_text.data ( ), "System Reserved" ) && contains ( answer_text.data ( ), "Windows 11 Pro" ) && contains ( answer_text.data ( ), "BypassTPMCheck" ) && contains ( answer_text.data ( ), "BypassNRO" ) && contains ( answer_text.data ( ), "<Value></Value>" );

		expect ( unattended, "machines: setup answers carry the escaped user and password, the disk layout, edition and Windows 11 bypasses" );

		static const char ini_text [ ] = "[Available UI Languages]\r\nde-DE = 3\r\n\r\n[Fallback Languages]\r\nde-DE = en-us\r\n";
		base::list < char > wide_ini;
		base::writer ini_path;
		base::writer plain_iso;
		base::writer named_iso;
		char built_error [ 160 ] = { };
		char found [ 16 ] = { };
		char named [ 16 ] = { };

		wide_ini.push ( static_cast < char > ( 0xFF ) );
		wide_ini.push ( static_cast < char > ( 0xFE ) );

		for ( int32_t i = 0; ini_text [ i ]; i++ ) {

			wide_ini.push ( ini_text [ i ] );
			wide_ini.push ( 0 );

		}

		ini_path.add ( scratch.text ( ) ).add ( L"\\lang.ini" );
		plain_iso.add ( scratch.text ( ) ).add ( L"\\plain.iso" );
		named_iso.add ( scratch.text ( ) ).add ( L"\\named.iso" );
		files::write ( ini_path.text ( ), wide_ini.data ( ), wide_ini.count ( ) );

		pc::iso_builder* plain = new pc::iso_builder ( );
		pc::iso_builder* labelled = new pc::iso_builder ( );

		plain->set_label ( "ESD-ISO" );
		labelled->set_label ( "CCSA_X64FRE_FR-FR_DV9" );

		bool images = plain->add_file ( "sources/lang.ini", ini_path.text ( ) ) && plain->write ( plain_iso.text ( ), built_error, sizeof ( built_error ) ) && labelled->add_file ( "setup.exe", ini_path.text ( ) ) && labelled->write ( named_iso.text ( ), built_error, sizeof ( built_error ) );
		bool languages = images && vm::image_language ( plain_iso.text ( ), found, sizeof ( found ) ) && base::same_text ( found, "de-DE" ) && vm::image_language ( named_iso.text ( ), named, sizeof ( named ) ) && base::same_text ( named, "fr-FR" );

		expect ( languages, "machines: the setup language comes from the disc label or a UTF-16 sources\\lang.ini" );

		delete plain;
		delete labelled;
		delete answers;

		vm::config* tuned = new vm::config ( );
		vm::config* tuned_copy = new vm::config ( );
		base::list < char > tuned_text;

		copy_wide ( tuned->name, L"Private box", vm::name_room );
		tuned->machine = vm::MACHINE_PC;
		tuned->screen_width = 1920;
		tuned->screen_height = 1080;
		tuned->frame_rate = 144;
		tuned->scaling = 1;
		tuned->smooth = true;
		tuned->frame_counter = true;
		tuned->local_network = false;
		tuned->use_proxy = true;
		tuned->ask_internet = true;
		tuned->fresh_identity = true;
		tuned->disposable = true;
		tuned->keep_preview = false;

		for ( int32_t i = 0; "127.0.0.1:9050" [ i ]; i++ )
			tuned->proxy [ i ] = "127.0.0.1:9050" [ i ];

		vm::serialize ( *tuned, tuned_text );

		bool tuned_parsed = vm::parse ( tuned_text.data ( ), tuned_text.count ( ), *tuned_copy, error, sizeof ( error ) );
		bool tuned_kept = tuned_parsed && tuned_copy->screen_width == 1920 && tuned_copy->screen_height == 1080 && tuned_copy->frame_rate == 144 && tuned_copy->scaling == 1 && tuned_copy->smooth && tuned_copy->frame_counter && !tuned_copy->local_network && tuned_copy->use_proxy && base::same_text ( tuned_copy->proxy, "127.0.0.1:9050" ) && tuned_copy->fresh_identity && tuned_copy->disposable && !tuned_copy->keep_preview && tuned_copy->ask_internet;
		vm::config* plain_box = new vm::config ( );
		bool quiet = !plain_box->disposable && !plain_box->fresh_identity && plain_box->local_network && !plain_box->use_proxy && plain_box->keep_preview && !plain_box->ask_internet && !plain_box->screen_width && !vm::private_run ( *plain_box );
		bool picky = rejected ( "coldboot=1\nname=Box\nmachine=pc\nscreen=9000x10\n", "640x400" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\nproxy=no port\n", "host:port" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\nframe_rate=fast\n", "display" )
			&& rejected ( "coldboot=1\nname=Box\nmachine=pc\nidentity=secret\n", "stable or fresh" );

		expect ( tuned_kept && quiet && picky, "machines: display and privacy settings round-trip, start out off, and bad values are refused" );

		static const wchar_t* const launch_line [ ] = { L"Coldboot.exe", L"--vm", L"x.coldboot", L"--disposable", L"1", L"--identity", L"fresh", L"--local-network", L"0", L"--route", L"proxy" };
		vm::launch_choice choice;
		vm::config* once = new vm::config ( *plain_box );

		bool read_choice = vm::read_launch ( launch_line, 11, choice );

		vm::apply_launch ( choice, *once );

		bool no_proxy_yet = once->disposable && once->fresh_identity && !once->local_network && !once->use_proxy;

		for ( int32_t i = 0; "10.1.2.3:1080" [ i ]; i++ )
			once->proxy [ i ] = "10.1.2.3:1080" [ i ];

		vm::apply_launch ( choice, *once );

		expect ( read_choice && no_proxy_yet && once->use_proxy && vm::private_run ( *once ) && !plain_box->disposable, "machines: start options apply to one run only, and the proxy route needs a saved proxy" );

		delete once;
		delete plain_box;
		delete tuned;
		delete tuned_copy;

		uint8_t monitor [ 128 ];
		uint8_t standard [ 128 ];
		uint8_t sum = 0;

		pc::vga::build_edid ( 1280, 800, monitor );
		pc::vga::build_edid ( 1024, 768, standard );

		for ( uint8_t value : monitor )
			sum = static_cast < uint8_t > ( sum + value );

		bool timed = sum == 0 && ( monitor [ 56 ] | ( monitor [ 58 ] >> 4 ) << 8 ) == 1280 && ( monitor [ 59 ] | ( monitor [ 61 ] >> 4 ) << 8 ) == 800 && standard [ 12 ] == 1 && monitor [ 12 ] != standard [ 12 ] && ( standard [ 56 ] | ( standard [ 58 ] >> 4 ) << 8 ) == 1024;

		expect ( timed, "machines: the monitor's EDID names the chosen resolution as its native mode and stays the same for 1024 x 768" );

		base::writer base_path;
		base::list < char > pattern;

		base_path.add ( scratch.text ( ) ).add ( L"\\base.img" );

		for ( int32_t i = 0; i < 65536; i++ )
			pattern.push ( static_cast < char > ( i * 7 ) );

		files::write ( base_path.text ( ), pattern.data ( ), 65536 );

		pc::disk_file* scratch_disk = new pc::disk_file ( );
		uint8_t sector [ 512 ];
		uint8_t disk_back [ 9000 ];
		uint8_t wide_write [ 8192 ];
		bool opened = scratch_disk->open_disposable ( base_path.text ( ) ) && scratch_disk->disposable ( ) && scratch_disk->writable && scratch_disk->size == 65536;

		for ( int32_t i = 0; i < 512; i++ )
			sector [ i ] = 0xA5;

		for ( int32_t i = 0; i < 8192; i++ )
			wide_write [ i ] = static_cast < uint8_t > ( 0x40 + i % 13 );

		bool writes = opened && scratch_disk->write ( 4096 + 1024, sector, 512 ) && scratch_disk->write ( 16384, wide_write, 8192 );
		bool reads = writes && scratch_disk->read ( 4096, disk_back, 9000 );
		bool mixed = reads && disk_back [ 0 ] == static_cast < uint8_t > ( 4096 * 7 ) && disk_back [ 1024 ] == 0xA5 && disk_back [ 1535 ] == 0xA5 && disk_back [ 1536 ] == static_cast < uint8_t > ( ( 4096 + 1536 ) * 7 );
		bool wide_back = scratch_disk->read ( 16384 - 100, disk_back, 8292 ) && disk_back [ 99 ] == static_cast < uint8_t > ( ( 16384 - 1 ) * 7 ) && disk_back [ 100 ] == 0x40 && disk_back [ 100 + 8191 ] == static_cast < uint8_t > ( 0x40 + 8191 % 13 );

		scratch_disk->close ( );

		base::list < char > after;
		bool untouched = files::read ( base_path.text ( ), after ) && after.count ( ) >= 65536;

		for ( int32_t i = 0; untouched && i < 65536; i++ )
			untouched = after [ i ] == pattern [ i ];

		expect ( mixed && wide_back && untouched, "machines: a disposable disk reads its own writes, mixes them with the image, and leaves the image untouched" );

		delete scratch_disk;

		base::writer gif_path;
		gif::writer* recorder = new gif::writer ( );
		uint32_t* picture = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * 128 * 96 ) );

		gif_path.add ( scratch.text ( ) ).add ( L"\\demo.gif" );

		for ( int32_t i = 0; i < 128 * 96; i++ )
			picture [ i ] = static_cast < uint32_t > ( ( i % 128 ) * 2 ) << 16 | static_cast < uint32_t > ( ( i / 128 ) * 2 ) << 8 | 0x40;

		bool recorded = recorder->open ( gif_path.text ( ), 64, 48 );

		recorder->frame ( picture, 128, 96, 10, false );
		recorder->frame ( picture, 128, 96, 10, false );

		for ( int32_t y = 40; y < 60; y++ ) {

			for ( int32_t x = 20; x < 40; x++ )
				picture [ y * 128 + x ] = 0xFF2020;

		}

		recorder->frame ( picture, 128, 96, 10, false );
		recorded = recorded && recorder->frames == 2 && recorder->close ( );
		runtime_release ( picture );
		delete recorder;

		HRESULT com = CoInitializeEx ( nullptr, COINIT_MULTITHREADED );
		IWICImagingFactory* factory = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* first_frame = nullptr;
		IWICBitmapFrameDecode* second_frame = nullptr;
		IWICFormatConverter* converter = nullptr;
		UINT frame_count = 0;
		UINT second_width = 0;
		UINT second_height = 0;
		uint32_t decoded [ 64 * 48 ] = { };
		bool decodes = recorded && SUCCEEDED ( CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &factory ) ) );

		decodes = decodes && SUCCEEDED ( factory->CreateDecoderFromFilename ( gif_path.text ( ), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder ) );
		decodes = decodes && SUCCEEDED ( decoder->GetFrameCount ( &frame_count ) ) && frame_count == 2;
		decodes = decodes && SUCCEEDED ( decoder->GetFrame ( 0, &first_frame ) ) && SUCCEEDED ( decoder->GetFrame ( 1, &second_frame ) );
		decodes = decodes && SUCCEEDED ( second_frame->GetSize ( &second_width, &second_height ) );
		decodes = decodes && SUCCEEDED ( factory->CreateFormatConverter ( &converter ) ) && SUCCEEDED ( converter->Initialize ( first_frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) );
		decodes = decodes && SUCCEEDED ( converter->CopyPixels ( nullptr, 64 * 4, sizeof ( decoded ), reinterpret_cast < BYTE* > ( decoded ) ) );

		uint32_t sample = decoded [ 24 * 64 + 32 ];
		int32_t red = static_cast < int32_t > ( sample >> 16 & 255 );
		int32_t green = static_cast < int32_t > ( sample >> 8 & 255 );
		bool close_enough = red > 128 - 16 && red < 128 + 16 && green > 96 - 16 && green < 96 + 16;

		if ( converter )
			converter->Release ( );

		if ( second_frame )
			second_frame->Release ( );

		if ( first_frame )
			first_frame->Release ( );

		if ( decoder )
			decoder->Release ( );

		if ( factory )
			factory->Release ( );

		if ( SUCCEEDED ( com ) )
			CoUninitialize ( );

		expect ( decodes && close_enough && second_width == 10 && second_height == 10, "machines: recordings are GIFs Windows decodes, with only the changed part stored after the first frame" );

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