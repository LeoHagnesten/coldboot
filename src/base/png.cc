#include "base/png.hh"
#include <windows.h>
#include <wincodec.h>

namespace png {

	template < typename type >
	static void drop ( type*& object ) {

		if ( object )
			object->Release ( );

		object = nullptr;

	}

	bool save ( const wchar_t* path, const uint32_t* pixels, int32_t width, int32_t height ) {

		if ( !pixels || width <= 0 || height <= 0 )
			return false;

		HRESULT started = CoInitializeEx ( nullptr, COINIT_MULTITHREADED );
		IWICImagingFactory* factory = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		uint32_t stride = static_cast < uint32_t > ( width ) * 3;
		uint8_t* row = static_cast < uint8_t* > ( runtime_allocate ( stride ) );
		bool ok = row != nullptr;

		ok = ok && SUCCEEDED ( CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &factory ) ) );
		ok = ok && SUCCEEDED ( factory->CreateStream ( &stream ) );
		ok = ok && SUCCEEDED ( stream->InitializeFromFilename ( path, GENERIC_WRITE ) );
		ok = ok && SUCCEEDED ( factory->CreateEncoder ( GUID_ContainerFormatPng, nullptr, &encoder ) );
		ok = ok && SUCCEEDED ( encoder->Initialize ( stream, WICBitmapEncoderNoCache ) );
		ok = ok && SUCCEEDED ( encoder->CreateNewFrame ( &frame, nullptr ) );
		ok = ok && SUCCEEDED ( frame->Initialize ( nullptr ) );
		ok = ok && SUCCEEDED ( frame->SetSize ( static_cast < UINT > ( width ), static_cast < UINT > ( height ) ) );

		WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;

		ok = ok && SUCCEEDED ( frame->SetPixelFormat ( &format ) ) && IsEqualGUID ( format, GUID_WICPixelFormat24bppBGR );

		for ( int32_t y = 0; ok && y < height; y++ ) {

			const uint32_t* source = pixels + static_cast < size_t > ( y ) * width;

			for ( int32_t x = 0; x < width; x++ ) {

				row [ x * 3 ] = static_cast < uint8_t > ( source [ x ] );
				row [ x * 3 + 1 ] = static_cast < uint8_t > ( source [ x ] >> 8 );
				row [ x * 3 + 2 ] = static_cast < uint8_t > ( source [ x ] >> 16 );

			}

			ok = SUCCEEDED ( frame->WritePixels ( 1, stride, stride, row ) );

		}

		ok = ok && SUCCEEDED ( frame->Commit ( ) ) && SUCCEEDED ( encoder->Commit ( ) );

		drop ( frame );
		drop ( encoder );
		drop ( stream );
		drop ( factory );
		runtime_release ( row );

		if ( SUCCEEDED ( started ) )
			CoUninitialize ( );

		return ok;

	}

}