#include "app/settings.hh"
#include "base/files.hh"

namespace app {

	static const wchar_t* bios_setting ( ) {

		static base::writer path;

		if ( !path.size ( ) )
			path.add ( files::folder ( ) ).add ( L"\\bios.path" );

		return path.text ( );

	}

	bool remembered_bios ( wchar_t* out, int32_t room ) {

		base::list < char > data;

		if ( !files::read ( bios_setting ( ), data ) )
			return false;

		int32_t length = ( data.count ( ) - 1 ) / 2;

		if ( length <= 0 || length >= room )
			return false;

		const wchar_t* text = reinterpret_cast < const wchar_t* > ( data.data ( ) );

		for ( int32_t i = 0; i < length; i++ )
			out [ i ] = text [ i ];

		out [ length ] = 0;

		return true;

	}

	void remember_bios ( const wchar_t* path ) {

		files::write ( bios_setting ( ), reinterpret_cast < const char* > ( path ), base::text_length ( path ) * 2 );

	}

}