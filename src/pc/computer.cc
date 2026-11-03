#include "pc/computer.hh"
#include "base/files.hh"
#include "pc/iso.hh"

namespace pc {

	static const char setup_answers [ ] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
		"<unattend xmlns=\"urn:schemas-microsoft-com:unattend\">\r\n"
		"  <settings pass=\"windowsPE\">\r\n"
		"    <component name=\"Microsoft-Windows-Setup\" processorArchitecture=\"amd64\" language=\"neutral\" xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\r\n"
		"      <RunSynchronous>\r\n"
		"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
		"          <Order>1</Order>\r\n"
		"          <Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f</Path>\r\n"
		"        </RunSynchronousCommand>\r\n"
		"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
		"          <Order>2</Order>\r\n"
		"          <Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1 /f</Path>\r\n"
		"        </RunSynchronousCommand>\r\n"
		"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
		"          <Order>3</Order>\r\n"
		"          <Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v BypassRAMCheck /t REG_DWORD /d 1 /f</Path>\r\n"
		"        </RunSynchronousCommand>\r\n"
		"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
		"          <Order>4</Order>\r\n"
		"          <Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v BypassCPUCheck /t REG_DWORD /d 1 /f</Path>\r\n"
		"        </RunSynchronousCommand>\r\n"
		"      </RunSynchronous>\r\n"
		"    </component>\r\n"
		"  </settings>\r\n"
		"  <settings pass=\"specialize\">\r\n"
		"    <component name=\"Microsoft-Windows-Deployment\" processorArchitecture=\"amd64\" language=\"neutral\" xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" publicKeyToken=\"31bf3856ad364e35\" versionScope=\"nonSxS\">\r\n"
		"      <RunSynchronous>\r\n"
		"        <RunSynchronousCommand wcm:action=\"add\">\r\n"
		"          <Order>1</Order>\r\n"
		"          <Path>reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE /v BypassNRO /t REG_DWORD /d 1 /f</Path>\r\n"
		"        </RunSynchronousCommand>\r\n"
		"      </RunSynchronous>\r\n"
		"    </component>\r\n"
		"  </settings>\r\n"
		"</unattend>\r\n";

	bool make_setup_helper ( const wchar_t* folder, wchar_t* out, char* error, int32_t room ) {

		base::writer target;
		base::writer answers;

		target.add ( folder ).add ( L"\\setup-helper-2.iso" );
		answers.add ( folder ).add ( L"\\autounattend.xml" );

		int32_t length = 0;

		for ( ; target.text ( ) [ length ] && length < MAX_PATH - 1; length++ )
			out [ length ] = target.text ( ) [ length ];

		out [ length ] = 0;

		if ( GetFileAttributesW ( out ) != INVALID_FILE_ATTRIBUTES )
			return true;

		if ( !files::write ( answers.text ( ), setup_answers, static_cast < int32_t > ( sizeof ( setup_answers ) - 1 ) ) ) {

			int32_t used = 0;
			const char* text = "Couldn't write the Windows setup answers";

			for ( ; text [ used ] && used < room - 1; used++ )
				error [ used ] = text [ used ];

			error [ used ] = 0;

			return false;

		}

		iso_builder* builder = new iso_builder ( );

		builder->set_label ( "COLDBOOT_SETUP" );

		bool built = builder->add_file ( "autounattend.xml", answers.text ( ) ) && builder->write ( out, error, room );

		delete builder;
		DeleteFileW ( answers.text ( ) );

		return built;

	}

	static void copy_wide ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t used = 0;

		for ( ; text && text [ used ] && used < room - 1; used++ )
			out [ used ] = text [ used ];

		out [ used ] = 0;

	}

	computer::~computer ( ) {

		power_off ( );

	}

	bool computer::fail ( const char* text ) {

		int32_t used = 0;

		for ( ; text [ used ] && used < static_cast < int32_t > ( sizeof ( problem ) ) - 1; used++ )
			problem [ used ] = text [ used ];

		problem [ used ] = 0;
		release ( );

		return false;

	}

	void computer::release ( ) {

		if ( board )
			board->stop ( );

		delete firmware;
		delete storage;
		delete video;
		firmware = nullptr;
		storage = nullptr;
		video = nullptr;

		if ( board ) {

			board->destroy ( );
			delete board;
			board = nullptr;

		}

	}

	bool computer::power_on ( const computer_options& wanted ) {

		power_off ( );
		problem [ 0 ] = 0;

		if ( &wanted != &options )
			options = wanted;

		board = new platform ( );

		platform_options shape;

		shape.processors = base::clamp ( options.processors, 1u, 64u );
		shape.memory = base::max ( options.memory, 64ull * 1024 * 1024 );

		if ( !board->create ( shape ) )
			return fail ( board->vm.problem [ 0 ] ? board->vm.problem : "Couldn't create the virtual machine" );

		hv::machine& vm = board->vm;

		vm.lenient_msrs = true;

		if ( options.cap && options.cap < 100 )
			vm.core.set_cap ( options.cap );

		video = new vga ( vm );
		storage = new ide_controller ( *board, vm );
		firmware = new bios ( );

		if ( !video->vram )
			return fail ( "Couldn't allocate video memory" );

		vm.attach_ports ( 0x3B0, 0x3DF, video );
		vm.attach_memory ( 0xA0000, 0x10000, video );
		vm.attach_ports ( 0x1F0, 0x1F7, storage );
		vm.attach_ports ( 0x3F6, 0x3F6, storage );
		vm.attach_ports ( 0x170, 0x177, storage );
		vm.attach_ports ( 0x376, 0x376, storage );
		vm.attach_ports ( storage->master_base, static_cast < uint16_t > ( storage->master_base + 15 ), storage );
		vm.attach_ports ( bios::port, bios::port + 7, firmware );
		board->pci.attach ( 1, 1, storage );
		board->pci.attach ( 2, 0, video );
		video->map ( );

		if ( !disc.present ( ) && options.iso [ 0 ] )
			disc.open ( options.iso, false );

		if ( !drive.present ( ) && options.disk [ 0 ] && !drive.open ( options.disk, true ) )
			drive.open ( options.disk, false );

		if ( drive.present ( ) )
			storage->attach ( 0, 0, DRIVE_DISK, &drive );

		if ( !helper.present ( ) && options.helper [ 0 ] )
			helper.open ( options.helper, false );

		storage->attach ( 1, 0, DRIVE_OPTICAL, disc.present ( ) ? &disc : nullptr );

		if ( helper.present ( ) )
			storage->attach ( 1, 1, DRIVE_OPTICAL, &helper );

		storage->reset ( );

		bios_links links;

		links.vm = &vm;
		links.bus = board;
		links.video = video;
		links.keyboard = &board->keyboard;
		links.pics = &board->pics;
		links.timer = &board->timer;
		links.clock = &board->clock;
		links.pci = &board->pci;
		links.serial = &board->serial;
		links.hard_disk = drive.present ( ) ? &drive : nullptr;
		links.optical = &disc;
		links.processors = shape.processors;
		links.optical_first = options.optical_first;
		links.seed = 0xCBF29CE484222325ull;

		for ( const wchar_t* at = options.disk [ 0 ] ? options.disk : options.iso; *at; at++ )
			links.seed = ( links.seed ^ static_cast < uint64_t > ( *at | 0x20 ) ) * 0x100000001B3ull;

		char error [ 160 ] = { };

		if ( !firmware->install ( links, error, sizeof ( error ) ) )
			return fail ( error );

		hv::cpu_state cpu;

		vm.get_state ( 0, cpu );

		for ( uint64_t& value : cpu.gpr )
			value = 0;

		for ( hv::segment& item : cpu.segments )
			item = { 0, 0xFFFF, 0, 0x93 };

		cpu.segments [ hv::SEG_CS ] = { 0xF0000, 0xFFFF, 0xF000, 0x9B };
		cpu.rip = firmware->entry_offset;
		cpu.rflags = 2;
		cpu.cr0 = 0x10;
		cpu.cr3 = 0;
		cpu.cr4 = 0;
		cpu.efer = 0;

		if ( !vm.set_state ( 0, cpu ) || !vm.set_tables ( 0, 0, 0xFFFF, 0, 0x3FF ) )
			return fail ( "Couldn't set up the first processor" );

		if ( !board->start ( ) )
			return fail ( board->vm.problem [ 0 ] ? board->vm.problem : "Couldn't start the processors" );

		return true;

	}

	void computer::power_off ( ) {

		release ( );

	}

	bool computer::restart ( ) {

		release ( );

		return power_on ( options );

	}

	computer_event computer::poll ( ) {

		if ( !board )
			return EVENT_NONE;

		if ( board->power_off_requested )
			return EVENT_OFF;

		if ( board->reset_requested || board->vm.reason == hv::STOP_SHUTDOWN )
			return EVENT_RESET;

		if ( board->vm.reason == hv::STOP_FAILED ) {

			int32_t used = 0;

			for ( ; board->vm.problem [ used ] && used < static_cast < int32_t > ( sizeof ( problem ) ) - 1; used++ )
				problem [ used ] = board->vm.problem [ used ];

			problem [ used ] = 0;

			return EVENT_FAILED;

		}

		return EVENT_NONE;

	}

	void computer::key ( uint8_t scan, bool extended ) {

		if ( !board )
			return;

		board->vm.lock_devices ( );

		if ( extended )
			board->keyboard.key ( 0xE0 );

		board->keyboard.key ( scan );
		board->vm.unlock_devices ( );

	}

	void computer::mouse ( int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons ) {

		if ( !board )
			return;

		board->vm.lock_devices ( );
		board->keyboard.mouse ( dx, dy, wheel, buttons );
		board->vm.unlock_devices ( );

	}

	void computer::press_power ( ) {

		if ( !board )
			return;

		board->vm.lock_devices ( );
		board->power.press_power ( );
		board->vm.unlock_devices ( );

	}

	bool computer::change_disc ( const wchar_t* path ) {

		if ( !board || !storage )
			return false;

		board->vm.lock_devices ( );
		disc.close ( );

		bool opened = path && path [ 0 ] && disc.open ( path, false );

		copy_wide ( options.iso, opened ? path : L"", MAX_PATH );
		storage->change_media ( 1, 0, opened ? &disc : nullptr );
		board->vm.unlock_devices ( );

		return opened || !path || !path [ 0 ];

	}

	bool computer::screen ( uint32_t* out, int32_t& width, int32_t& height, double now ) {

		return video && board && video->render ( out, width, height, now );

	}

	int32_t computer::console_text ( char* out, int32_t room ) {

		if ( !board ) {

			if ( room > 0 )
				out [ 0 ] = 0;

			return 0;

		}

		return board->console_text ( out, room );

	}

}