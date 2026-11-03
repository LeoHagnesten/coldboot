#include "vm/unattend.hh"
#include "base/files.hh"
#include "pc/iso.hh"

namespace vm {

	static const char component_tail [ ] = "\" processorArchitecture=\"amd64\" publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" versionScope=\"nonSxS\" xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\r\n";

	static void put ( base::list < char >& out, const char* text ) {

		for ( int32_t i = 0; text [ i ]; i++ )
			out.push ( text [ i ] );

	}

	static void put_escaped ( base::list < char >& out, const wchar_t* text ) {

		char bytes [ 8 ];

		for ( int32_t i = 0; text [ i ]; i++ ) {

			wchar_t c = text [ i ];

			if ( c == L'&' )
				put ( out, "&amp;" );
			else if ( c == L'<' )
				put ( out, "&lt;" );
			else if ( c == L'>' )
				put ( out, "&gt;" );
			else if ( c == L'"' )
				put ( out, "&quot;" );
			else if ( c == L'\'' )
				put ( out, "&apos;" );
			else if ( c >= 32 ) {

				int32_t pair = c >= 0xD800 && c <= 0xDBFF && text [ i + 1 ] ? 2 : 1;
				int32_t size = WideCharToMultiByte ( CP_UTF8, 0, text + i, pair, bytes, sizeof ( bytes ), nullptr, nullptr );

				for ( int32_t b = 0; b < size; b++ )
					out.push ( bytes [ b ] );

				i += pair - 1;

			}

		}

	}

	static void open_component ( base::list < char >& out, const char* name ) {

		put ( out, "    <component name=\"" );
		put ( out, name );
		put ( out, component_tail );

	}

	static void element ( base::list < char >& out, const char* indent, const char* name, const char* value ) {

		put ( out, indent );
		put ( out, "<" );
		put ( out, name );
		put ( out, ">" );
		put ( out, value );
		put ( out, "</" );
		put ( out, name );
		put ( out, ">\r\n" );

	}

	static void locales ( base::list < char >& out, const char* language ) {

		element ( out, "      ", "InputLocale", language );
		element ( out, "      ", "SystemLocale", language );
		element ( out, "      ", "UILanguage", language );
		element ( out, "      ", "UserLocale", language );

	}

	static void password ( base::list < char >& out, const char* indent, const wchar_t* value ) {

		put ( out, indent );
		put ( out, "<Password><Value>" );
		put_escaped ( out, value );
		put ( out, "</Value><PlainText>true</PlainText></Password>\r\n" );

	}

	bool image_language ( const wchar_t* iso, char* out, int32_t room ) {

		out [ 0 ] = 0;

		HANDLE file = CreateFileW ( iso, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		uint8_t sector [ 2048 ];
		LARGE_INTEGER at;
		DWORD read = 0;
		char label [ 33 ] = { };

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		at.QuadPart = 0x8000;

		bool ok = SetFilePointerEx ( file, at, nullptr, FILE_BEGIN ) && ReadFile ( file, sector, sizeof ( sector ), &read, nullptr ) && read == sizeof ( sector ) && sector [ 1 ] == 'C' && sector [ 2 ] == 'D';

		CloseHandle ( file );

		if ( !ok || room < 6 )
			return false;

		for ( int32_t i = 0; i < 32; i++ )
			label [ i ] = static_cast < char > ( sector [ 40 + i ] );

		auto letter = [ ] ( char c ) {

			return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' );

		};

		for ( int32_t i = 0; i + 5 <= 32; i++ ) {

			bool before = i == 0 || label [ i - 1 ] == '_';
			bool after = i + 5 == 32 || label [ i + 5 ] == '_' || label [ i + 5 ] == ' ';

			if ( !before || !after || !letter ( label [ i ] ) || !letter ( label [ i + 1 ] ) || label [ i + 2 ] != '-' || !letter ( label [ i + 3 ] ) || !letter ( label [ i + 4 ] ) )
				continue;

			out [ 0 ] = static_cast < char > ( label [ i ] | 0x20 );
			out [ 1 ] = static_cast < char > ( label [ i + 1 ] | 0x20 );
			out [ 2 ] = '-';
			out [ 3 ] = static_cast < char > ( label [ i + 3 ] & ~0x20 );
			out [ 4 ] = static_cast < char > ( label [ i + 4 ] & ~0x20 );
			out [ 5 ] = 0;

			return true;

		}

		base::list < uint8_t > ini;

		if ( !pc::iso_read_file ( iso, "sources/lang.ini", ini, 0x10000 ) )
			return false;

		if ( ini.size ( ) >= 2 && ini [ 0 ] == 0xFF && ini [ 1 ] == 0xFE ) {

			uint32_t kept = 0;

			for ( uint32_t i = 2; i + 1 < ini.size ( ); i += 2 )
				ini [ kept++ ] = ini [ i + 1 ] ? '?' : ini [ i ];

			ini.resize ( kept );

		}

		static const char heading [ ] = "[Available UI Languages]";
		uint32_t start = 0;

		for ( uint32_t i = 0; i + sizeof ( heading ) - 1 <= ini.size ( ) && !start; i++ ) {

			bool same = true;

			for ( uint32_t j = 0; same && heading [ j ]; j++ )
				same = ( ini [ i + j ] | 0x20 ) == ( static_cast < uint8_t > ( heading [ j ] ) | 0x20 );

			if ( same )
				start = i + sizeof ( heading ) - 1;

		}

		for ( uint32_t i = start; start && i + 5 <= ini.size ( ); i++ ) {

			if ( ini [ i ] == '[' )
				break;

			char a = static_cast < char > ( ini [ i ] );
			char b = static_cast < char > ( ini [ i + 1 ] );
			char c = static_cast < char > ( ini [ i + 3 ] );
			char d = static_cast < char > ( ini [ i + 4 ] );

			if ( !letter ( a ) || !letter ( b ) || ini [ i + 2 ] != '-' || !letter ( c ) || !letter ( d ) || ( i && letter ( static_cast < char > ( ini [ i - 1 ] ) ) ) )
				continue;

			out [ 0 ] = static_cast < char > ( a | 0x20 );
			out [ 1 ] = static_cast < char > ( b | 0x20 );
			out [ 2 ] = '-';
			out [ 3 ] = static_cast < char > ( c & ~0x20 );
			out [ 4 ] = static_cast < char > ( d & ~0x20 );
			out [ 5 ] = 0;

			return true;

		}

		return false;

	}

	bool user_name_valid ( const wchar_t* name ) {

		int32_t length = 0;
		bool visible = false;

		for ( ; name [ length ]; length++ ) {

			wchar_t c = name [ length ];

			if ( c < 32 || c == L'"' || c == L'/' || c == L'\\' || c == L'[' || c == L']' || c == L':' || c == L';' || c == L'|' || c == L'=' || c == L',' || c == L'+' || c == L'*' || c == L'?' || c == L'<' || c == L'>' || c == L'@' || c == L'%' )
				return false;

			visible = visible || ( c != L' ' && c != L'.' );

		}

		return visible && length <= 20 && name [ length - 1 ] != L'.';

	}

	void default_user_name ( wchar_t* out, int32_t room ) {

		wchar_t name [ 257 ] = { };
		DWORD size = 256;
		int32_t used = 0;

		if ( GetUserNameW ( name, &size ) ) {

			for ( int32_t i = 0; name [ i ] && used < 20 && used < room - 1; i++ ) {

				wchar_t single [ 2 ] = { name [ i ], 0 };

				if ( user_name_valid ( single ) || name [ i ] == L' ' )
					out [ used++ ] = name [ i ];

			}

		}

		out [ used ] = 0;

		if ( !user_name_valid ( out ) ) {

			static const wchar_t fallback [ ] = L"User";

			for ( used = 0; fallback [ used ] && used < room - 1; used++ )
				out [ used ] = fallback [ used ];

			out [ used ] = 0;

		}

	}

	void build_answers ( const setup_answers& answers, base::list < char >& out ) {

		bool language = answers.language [ 0 ] != 0;

		out.clear ( );
		put ( out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<unattend xmlns=\"urn:schemas-microsoft-com:unattend\">\r\n  <settings pass=\"windowsPE\">\r\n" );

		if ( language ) {

			open_component ( out, "Microsoft-Windows-International-Core-WinPE" );
			put ( out, "      <SetupUILanguage><UILanguage>" );
			put ( out, answers.language );
			put ( out, "</UILanguage></SetupUILanguage>\r\n" );
			locales ( out, answers.language );
			put ( out, "    </component>\r\n" );

		}

		open_component ( out, "Microsoft-Windows-Setup" );
		put ( out, "      <DiskConfiguration>\r\n        <Disk wcm:action=\"add\">\r\n          <DiskID>0</DiskID>\r\n          <WillWipeDisk>true</WillWipeDisk>\r\n          <CreatePartitions>\r\n" );

		if ( answers.uefi ) {

			put ( out, "            <CreatePartition wcm:action=\"add\"><Order>1</Order><Type>EFI</Type><Size>300</Size></CreatePartition>\r\n" );
			put ( out, "            <CreatePartition wcm:action=\"add\"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>\r\n" );
			put ( out, "            <CreatePartition wcm:action=\"add\"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>\r\n" );
			put ( out, "          </CreatePartitions>\r\n          <ModifyPartitions>\r\n" );
			put ( out, "            <ModifyPartition wcm:action=\"add\"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>\r\n" );
			put ( out, "            <ModifyPartition wcm:action=\"add\"><Order>2</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>\r\n" );

		} else {

			put ( out, "            <CreatePartition wcm:action=\"add\"><Order>1</Order><Type>Primary</Type><Size>500</Size></CreatePartition>\r\n" );
			put ( out, "            <CreatePartition wcm:action=\"add\"><Order>2</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>\r\n" );
			put ( out, "          </CreatePartitions>\r\n          <ModifyPartitions>\r\n" );
			put ( out, "            <ModifyPartition wcm:action=\"add\"><Order>1</Order><PartitionID>1</PartitionID><Active>true</Active><Format>NTFS</Format><Label>System Reserved</Label></ModifyPartition>\r\n" );
			put ( out, "            <ModifyPartition wcm:action=\"add\"><Order>2</Order><PartitionID>2</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>\r\n" );

		}

		put ( out, "          </ModifyPartitions>\r\n        </Disk>\r\n      </DiskConfiguration>\r\n" );
		put ( out, "      <ImageInstall>\r\n        <OSImage>\r\n          <InstallFrom>\r\n            <MetaData wcm:action=\"add\"><Key>/IMAGE/NAME</Key><Value>" );
		put ( out, answers.windows11 ? "Windows 11 Pro" : "Windows 10 Pro" );
		put ( out, "</Value></MetaData>\r\n          </InstallFrom>\r\n          <InstallTo><DiskID>0</DiskID><PartitionID>" );
		put ( out, answers.uefi ? "3" : "2" );
		put ( out, "</PartitionID></InstallTo>\r\n        </OSImage>\r\n      </ImageInstall>\r\n" );

		if ( answers.windows11 ) {

			static const char* const bypasses [ 4 ] = { "BypassTPMCheck", "BypassSecureBootCheck", "BypassRAMCheck", "BypassCPUCheck" };

			put ( out, "      <RunSynchronous>\r\n" );

			for ( int32_t i = 0; i < 4; i++ ) {

				char order [ 2 ] = { static_cast < char > ( '1' + i ), 0 };

				put ( out, "        <RunSynchronousCommand wcm:action=\"add\"><Order>" );
				put ( out, order );
				put ( out, "</Order><Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v " );
				put ( out, bypasses [ i ] );
				put ( out, " /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>\r\n" );

			}

			put ( out, "      </RunSynchronous>\r\n" );

		}

		put ( out, "      <UserData>\r\n        <ProductKey><Key></Key><WillShowUI>OnError</WillShowUI></ProductKey>\r\n        <AcceptEula>true</AcceptEula>\r\n      </UserData>\r\n    </component>\r\n  </settings>\r\n" );

		put ( out, "  <settings pass=\"specialize\">\r\n" );
		open_component ( out, "Microsoft-Windows-Deployment" );
		put ( out, "      <RunSynchronous>\r\n        <RunSynchronousCommand wcm:action=\"add\"><Order>1</Order><Path>reg add HKLM\\SYSTEM\\CurrentControlSet\\Control\\Network\\NewNetworkWindowOff /f</Path></RunSynchronousCommand>\r\n" );

		if ( answers.windows11 )
			put ( out, "        <RunSynchronousCommand wcm:action=\"add\"><Order>2</Order><Path>reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE /v BypassNRO /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>\r\n" );

		put ( out, "      </RunSynchronous>\r\n    </component>\r\n  </settings>\r\n" );

		put ( out, "  <settings pass=\"oobeSystem\">\r\n" );

		if ( language ) {

			open_component ( out, "Microsoft-Windows-International-Core" );
			locales ( out, answers.language );
			put ( out, "    </component>\r\n" );

		}

		open_component ( out, "Microsoft-Windows-Shell-Setup" );
		put ( out, "      <AutoLogon>\r\n        <Username>" );
		put_escaped ( out, answers.user );
		put ( out, "</Username>\r\n        <Enabled>true</Enabled>\r\n        <LogonCount>1</LogonCount>\r\n" );
		password ( out, "        ", answers.password );
		put ( out, "      </AutoLogon>\r\n" );
		put ( out, "      <FirstLogonCommands>\r\n        <SynchronousCommand wcm:action=\"add\">\r\n          <Order>1</Order>\r\n" );
		put ( out, "          <CommandLine>cmd /c for %d in (D E F G H I J K L) do if exist \"%d:\\Coldboot Tools.exe\" start \"\" \"%d:\\Coldboot Tools.exe\" --install</CommandLine>\r\n" );
		put ( out, "          <Description>Install Coldboot Tools</Description>\r\n        </SynchronousCommand>\r\n      </FirstLogonCommands>\r\n" );
		put ( out, "      <OOBE>\r\n        <HideEULAPage>true</HideEULAPage>\r\n        <HideLocalAccountScreen>true</HideLocalAccountScreen>\r\n        <HideOEMRegistrationScreen>true</HideOEMRegistrationScreen>\r\n        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>\r\n        <HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>\r\n        <ProtectYourPC>3</ProtectYourPC>\r\n      </OOBE>\r\n" );
		put ( out, "      <UserAccounts>\r\n        <LocalAccounts>\r\n          <LocalAccount wcm:action=\"add\">\r\n            <Name>" );
		put_escaped ( out, answers.user );
		put ( out, "</Name>\r\n            <DisplayName>" );
		put_escaped ( out, answers.user );
		put ( out, "</DisplayName>\r\n            <Group>Administrators</Group>\r\n" );
		password ( out, "            ", answers.password );
		put ( out, "          </LocalAccount>\r\n        </LocalAccounts>\r\n      </UserAccounts>\r\n" );

		DYNAMIC_TIME_ZONE_INFORMATION zone = { };

		if ( GetDynamicTimeZoneInformation ( &zone ) != TIME_ZONE_ID_INVALID && zone.TimeZoneKeyName [ 0 ] ) {

			put ( out, "      <TimeZone>" );
			put_escaped ( out, zone.TimeZoneKeyName );
			put ( out, "</TimeZone>\r\n" );

		}

		put ( out, "    </component>\r\n  </settings>\r\n</unattend>\r\n" );

	}

	bool write_setup_disc ( const wchar_t* folder, const setup_answers& answers, wchar_t* out, char* error, int32_t room ) {

		base::list < char > text;
		base::writer target;
		base::writer answer_file;
		wchar_t self [ MAX_PATH ] = { };

		build_answers ( answers, text );
		target.add ( folder ).add ( L"\\setup-answers.iso" );
		answer_file.add ( folder ).add ( L"\\autounattend.xml" );
		GetModuleFileNameW ( nullptr, self, MAX_PATH );

		int32_t used = 0;

		for ( ; target.text ( ) [ used ] && used < MAX_PATH - 1; used++ )
			out [ used ] = target.text ( ) [ used ];

		out [ used ] = 0;

		if ( !files::write ( answer_file.text ( ), text.data ( ), text.count ( ) ) ) {

			const char* message = "Couldn't write the Windows setup answers";
			int32_t length = 0;

			for ( ; message [ length ] && length < room - 1; length++ )
				error [ length ] = message [ length ];

			error [ length ] = 0;

			return false;

		}

		pc::iso_builder* builder = new pc::iso_builder ( );

		builder->set_label ( "COLDBOOT_SETUP" );

		bool built = builder->add_file ( "autounattend.xml", answer_file.text ( ) ) && builder->add_file ( "Coldboot Tools.exe", self ) && builder->write ( out, error, room );

		delete builder;
		DeleteFileW ( answer_file.text ( ) );

		return built;

	}

}