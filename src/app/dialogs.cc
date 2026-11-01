#include "app/dialogs.hh"
#include "base/png.hh"
#include <shobjidl.h>
#include <shlobj.h>

namespace app {

	static void copy_text ( wchar_t* out, const wchar_t* text, int32_t room ) {

		int32_t length = 0;

		for ( ; text [ length ] && length < room - 1; length++ )
			out [ length ] = text [ length ];

		out [ length ] = 0;

	}

	static void apply_filter ( IFileDialog* dialog, file_kind kind, bool opening ) {

		static const COMDLG_FILTERSPEC disks [ ] = { { L"Floppy images", L"*.img;*.ima;*.vfd;*.dsk;*.flp" }, { L"All files", L"*.*" } };
		static const COMDLG_FILTERSPEC sources [ ] = { { L"Floppy images and programs", L"*.img;*.ima;*.vfd;*.dsk;*.flp;*.asm" }, { L"All files", L"*.*" } };
		static const COMDLG_FILTERSPEC roms [ ] = { { L"BIOS images", L"*.rom;*.bin" }, { L"All files", L"*.*" } };
		static const COMDLG_FILTERSPEC states [ ] = { { L"Coldboot states", L"*.state" }, { L"All files", L"*.*" } };
		static const COMDLG_FILTERSPEC machines [ ] = { { L"Coldboot machines", L"*.coldboot" }, { L"All files", L"*.*" } };

		if ( kind == FILE_MACHINE ) {

			dialog->SetFileTypes ( 2, machines );
			dialog->SetDefaultExtension ( L"coldboot" );

		} else if ( kind == FILE_DISK ) {

			dialog->SetFileTypes ( 2, opening ? sources : disks );
			dialog->SetDefaultExtension ( L"img" );

		} else if ( kind == FILE_ROM ) {

			dialog->SetFileTypes ( 2, roms );
			dialog->SetDefaultExtension ( L"rom" );

		} else {

			dialog->SetFileTypes ( 2, states );
			dialog->SetDefaultExtension ( L"state" );

		}

	}

	static bool take_result ( IFileDialog* dialog, wchar_t* out ) {

		IShellItem* item = nullptr;
		wchar_t* path = nullptr;
		bool ok = SUCCEEDED ( dialog->GetResult ( &item ) ) && SUCCEEDED ( item->GetDisplayName ( SIGDN_FILESYSPATH, &path ) );

		if ( ok )
			copy_text ( out, path, MAX_PATH );

		if ( path )
			CoTaskMemFree ( path );

		if ( item )
			item->Release ( );

		return ok;

	}

	bool open_file ( HWND owner, file_kind kind, wchar_t* out ) {

		IFileOpenDialog* dialog = nullptr;

		if ( FAILED ( CoCreateInstance ( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &dialog ) ) ) )
			return false;

		apply_filter ( dialog, kind, true );
		dialog->SetTitle ( kind == FILE_DISK ? L"Insert a floppy image" : ( kind == FILE_ROM ? L"Choose a BIOS ROM" : ( kind == FILE_MACHINE ? L"Add a Coldboot machine" : L"Load a state" ) ) );

		bool ok = SUCCEEDED ( dialog->Show ( owner ) ) && take_result ( dialog, out );

		dialog->Release ( );

		return ok;

	}

	bool save_file ( HWND owner, file_kind kind, const wchar_t* suggested, wchar_t* out ) {

		IFileSaveDialog* dialog = nullptr;

		if ( FAILED ( CoCreateInstance ( CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &dialog ) ) ) )
			return false;

		apply_filter ( dialog, kind, false );
		dialog->SetTitle ( kind == FILE_DISK ? L"Create a blank floppy image" : L"Save" );

		if ( suggested )
			dialog->SetFileName ( suggested );

		bool ok = SUCCEEDED ( dialog->Show ( owner ) ) && take_result ( dialog, out );

		dialog->Release ( );

		return ok;

	}

	bool save_screenshot ( const uint32_t* pixels, int32_t width, int32_t height, const wchar_t* path ) {

		if ( !pixels || width <= 0 || height <= 0 )
			return false;

		int32_t out_width = width * 2;
		int32_t out_height = out_width * 3 / 4;
		uint32_t* out = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * static_cast < size_t > ( out_width ) * out_height ) );

		for ( int32_t y = 0; y < out_height; y++ ) {

			double top = static_cast < double > ( y ) * height / out_height;
			double bottom = static_cast < double > ( y + 1 ) * height / out_height;

			for ( int32_t x = 0; x < out_width; x++ ) {

				double sum [ 3 ] = { };

				for ( int32_t source = static_cast < int32_t > ( top ); source < height && source < bottom; source++ ) {

					double weight = base::min ( bottom, source + 1.0 ) - base::max ( top, static_cast < double > ( source ) );
					uint32_t value = pixels [ source * width + x / 2 ];

					sum [ 0 ] += weight * ( value >> 16 & 0xFF );
					sum [ 1 ] += weight * ( value >> 8 & 0xFF );
					sum [ 2 ] += weight * ( value & 0xFF );

				}

				double span = bottom - top;
				uint32_t red = base::min ( static_cast < uint32_t > ( sum [ 0 ] / span + 0.5 ), 255u );
				uint32_t green = base::min ( static_cast < uint32_t > ( sum [ 1 ] / span + 0.5 ), 255u );
				uint32_t blue = base::min ( static_cast < uint32_t > ( sum [ 2 ] / span + 0.5 ), 255u );

				out [ y * out_width + x ] = red << 16 | green << 8 | blue;

			}

		}

		bool saved = png::save ( path, out, out_width, out_height );

		runtime_release ( out );

		return saved;

	}

	bool screenshot_path ( wchar_t* out ) {

		wchar_t* pictures = nullptr;

		if ( FAILED ( SHGetKnownFolderPath ( FOLDERID_Pictures, 0, nullptr, &pictures ) ) )
			return false;

		base::writer path;
		SYSTEMTIME now;

		GetLocalTime ( &now );
		path.add ( pictures ).add ( L"\\Coldboot" );
		CoTaskMemFree ( pictures );
		CreateDirectoryW ( path.text ( ), nullptr );

		auto two = [ & ] ( int32_t value ) {

			path.add ( static_cast < wchar_t > ( L'0' + value / 10 ) ).add ( static_cast < wchar_t > ( L'0' + value % 10 ) );

		};

		path.add ( L"\\Coldboot " ).number ( now.wYear ).add ( L"-" );
		two ( now.wMonth );
		path.add ( L"-" );
		two ( now.wDay );
		path.add ( L" " );
		two ( now.wHour );
		path.add ( L"." );
		two ( now.wMinute );
		path.add ( L"." );
		two ( now.wSecond );
		path.add ( L".png" );
		copy_text ( out, path.text ( ), MAX_PATH );

		return true;

	}

}