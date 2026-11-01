#include "render/gpu.hh"
#include "base/png.hh"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include "screen_vs.h"
#include "screen_ps.h"

namespace render {

	template < typename type >
	static void drop ( type*& object ) {

		if ( object )
			object->Release ( );

		object = nullptr;

	}

	static D2D1_RECT_F box ( rect area ) {

		return D2D1_RECT_F { area.x, area.y, area.x + area.w, area.y + area.h };

	}

	gpu::~gpu ( ) {

		destroy ( );

	}

	bool gpu::create ( HWND__* target_window, int32_t pixel_width, int32_t pixel_height ) {

		destroy ( );
		window = target_window;
		width = base::max ( pixel_width, 1 );
		height = base::max ( pixel_height, 1 );

		static const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
		HRESULT made = D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION, &device, &level, &context );

		if ( FAILED ( made ) )
			made = D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION, &device, &level, &context );

		if ( FAILED ( made ) )
			return false;

		IDXGIDevice* dxgi = nullptr;
		IDXGIAdapter* adapter = nullptr;
		IDXGIFactory2* dxgi_factory = nullptr;
		bool ok = SUCCEEDED ( device->QueryInterface ( __uuidof ( IDXGIDevice ), reinterpret_cast < void** > ( &dxgi ) ) );

		if ( ok && window ) {

			ok = SUCCEEDED ( dxgi->GetAdapter ( &adapter ) ) && SUCCEEDED ( adapter->GetParent ( __uuidof ( IDXGIFactory2 ), reinterpret_cast < void** > ( &dxgi_factory ) ) );

			DXGI_SWAP_CHAIN_DESC1 description = { };

			description.Width = static_cast < UINT > ( width );
			description.Height = static_cast < UINT > ( height );
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = 2;
			description.Scaling = DXGI_SCALING_STRETCH;
			description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			ok = ok && SUCCEEDED ( dxgi_factory->CreateSwapChainForHwnd ( device, window, &description, nullptr, nullptr, &chain ) );

			if ( ok )
				dxgi_factory->MakeWindowAssociation ( window, DXGI_MWA_NO_ALT_ENTER );

		}

		D2D1_FACTORY_OPTIONS options = { };

		ok = ok && SUCCEEDED ( D2D1CreateFactory ( D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof ( ID2D1Factory1 ), &options, reinterpret_cast < void** > ( &factory ) ) );
		ok = ok && SUCCEEDED ( factory->CreateDevice ( dxgi, &device2d ) );
		ok = ok && SUCCEEDED ( device2d->CreateDeviceContext ( D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context2d ) );
		ok = ok && SUCCEEDED ( context2d->CreateSolidColorBrush ( D2D1_COLOR_F { 1.0f, 1.0f, 1.0f, 1.0f }, &brush ) );

		D2D1_STROKE_STYLE_PROPERTIES1 style = { };

		style.startCap = D2D1_CAP_STYLE_ROUND;
		style.endCap = D2D1_CAP_STYLE_ROUND;
		style.dashCap = D2D1_CAP_STYLE_ROUND;
		style.lineJoin = D2D1_LINE_JOIN_ROUND;
		style.miterLimit = 10.0f;
		ok = ok && SUCCEEDED ( factory->CreateStrokeStyle ( style, nullptr, 0, reinterpret_cast < ID2D1StrokeStyle1** > ( &rounded ) ) );
		ok = ok && SUCCEEDED ( DWriteCreateFactory ( DWRITE_FACTORY_TYPE_SHARED, __uuidof ( IDWriteFactory ), reinterpret_cast < IUnknown** > ( &writer ) ) );

		drop ( dxgi_factory );
		drop ( adapter );
		drop ( dxgi );

		if ( !ok )
			return false;

		D3D11_TEXTURE2D_DESC description = { };

		description.Width = texture_width;
		description.Height = texture_height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ok = SUCCEEDED ( device->CreateTexture2D ( &description, nullptr, &picture ) );
		ok = ok && SUCCEEDED ( device->CreateShaderResourceView ( picture, nullptr, &picture_view ) );

		D3D11_BUFFER_DESC buffer = { };

		buffer.ByteWidth = 32;
		buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &buffer, nullptr, &constants ) );
		ok = ok && SUCCEEDED ( device->CreateVertexShader ( screen_vs_code, sizeof ( screen_vs_code ), nullptr, &vertex_shader ) );
		ok = ok && SUCCEEDED ( device->CreatePixelShader ( screen_ps_code, sizeof ( screen_ps_code ), nullptr, &pixel_shader ) );

		D3D11_SAMPLER_DESC filter = { };

		filter.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		filter.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		filter.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		filter.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		filter.MaxLOD = D3D11_FLOAT32_MAX;
		ok = ok && SUCCEEDED ( device->CreateSamplerState ( &filter, &sampler ) );

		return ok && make_fonts ( ) && make_targets ( );

	}

	void gpu::destroy ( ) {

		if ( drawing && context2d )
			context2d->EndDraw ( );

		drawing = false;
		free_targets ( );

		for ( IDWriteTextFormat*& format : formats )
			drop ( format );

		drop ( writer );
		drop ( rounded );
		drop ( brush );
		drop ( context2d );
		drop ( device2d );
		drop ( factory );
		drop ( sampler );
		drop ( pixel_shader );
		drop ( vertex_shader );
		drop ( constants );
		drop ( picture_view );
		drop ( picture );
		drop ( chain );
		drop ( context );
		drop ( device );

	}

	bool gpu::make_targets ( ) {

		bool ok = true;

		if ( chain )
			ok = SUCCEEDED ( chain->GetBuffer ( 0, __uuidof ( ID3D11Texture2D ), reinterpret_cast < void** > ( &target_texture ) ) );
		else {

			D3D11_TEXTURE2D_DESC description = { };

			description.Width = static_cast < UINT > ( width );
			description.Height = static_cast < UINT > ( height );
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_RENDER_TARGET;
			ok = SUCCEEDED ( device->CreateTexture2D ( &description, nullptr, &target_texture ) );
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ok = ok && SUCCEEDED ( device->CreateTexture2D ( &description, nullptr, &readback ) );

		}

		ok = ok && SUCCEEDED ( device->CreateRenderTargetView ( target_texture, nullptr, &target_view ) );

		IDXGISurface* surface = nullptr;

		ok = ok && SUCCEEDED ( target_texture->QueryInterface ( __uuidof ( IDXGISurface ), reinterpret_cast < void** > ( &surface ) ) );

		D2D1_BITMAP_PROPERTIES1 properties = { };

		properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
		properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		properties.dpiX = dpi;
		properties.dpiY = dpi;
		properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
		ok = ok && SUCCEEDED ( context2d->CreateBitmapFromDxgiSurface ( surface, &properties, &target_bitmap ) );
		drop ( surface );

		if ( ok ) {

			context2d->SetTarget ( target_bitmap );
			context2d->SetDpi ( dpi, dpi );
			context2d->SetTextAntialiasMode ( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );

		}

		return ok;

	}

	void gpu::free_targets ( ) {

		if ( context2d )
			context2d->SetTarget ( nullptr );

		if ( context )
			context->OMSetRenderTargets ( 0, nullptr, nullptr );

		drop ( target_bitmap );
		drop ( target_view );
		drop ( readback );
		drop ( target_texture );

	}

	bool gpu::resize ( int32_t pixel_width, int32_t pixel_height ) {

		pixel_width = base::max ( pixel_width, 1 );
		pixel_height = base::max ( pixel_height, 1 );

		if ( !device || ( pixel_width == width && pixel_height == height ) )
			return device != nullptr;

		free_targets ( );
		width = pixel_width;
		height = pixel_height;

		if ( chain && FAILED ( chain->ResizeBuffers ( 0, static_cast < UINT > ( width ), static_cast < UINT > ( height ), DXGI_FORMAT_UNKNOWN, 0 ) ) )
			return false;

		return make_targets ( );

	}

	void gpu::set_dpi ( float value ) {

		dpi = value;

		if ( context2d )
			context2d->SetDpi ( dpi, dpi );

	}

	bool gpu::make_fonts ( ) {

		IDWriteFontCollection* fonts = nullptr;
		UINT32 index = 0;
		BOOL found = FALSE;
		const wchar_t* face = L"Segoe UI";
		const wchar_t* mono = L"Consolas";

		if ( SUCCEEDED ( writer->GetSystemFontCollection ( &fonts, FALSE ) ) ) {

			if ( SUCCEEDED ( fonts->FindFamilyName ( L"Segoe UI Variable Text", &index, &found ) ) && found )
				face = L"Segoe UI Variable Text";

			found = FALSE;

			if ( SUCCEEDED ( fonts->FindFamilyName ( L"Cascadia Mono", &index, &found ) ) && found )
				mono = L"Cascadia Mono";

			fonts->Release ( );

		}

		struct role {

			const wchar_t* family;
			DWRITE_FONT_WEIGHT weight;
			float size;

		};

		const role roles [ FONT_COUNT ] = {

			{ face, DWRITE_FONT_WEIGHT_NORMAL, 13.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f },
			{ face, DWRITE_FONT_WEIGHT_NORMAL, 12.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 15.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 22.0f },
			{ mono, DWRITE_FONT_WEIGHT_NORMAL, 12.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.0f },
			{ face, DWRITE_FONT_WEIGHT_SEMI_BOLD, 18.0f },

		};

		for ( int32_t i = 0; i < FONT_COUNT; i++ ) {

			if ( FAILED ( writer->CreateTextFormat ( roles [ i ].family, nullptr, roles [ i ].weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, roles [ i ].size, L"en-us", &formats [ i ] ) ) )
				return false;

			IDWriteInlineObject* sign = nullptr;
			DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };

			formats [ i ]->SetWordWrapping ( DWRITE_WORD_WRAPPING_NO_WRAP );
			formats [ i ]->SetParagraphAlignment ( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );

			if ( SUCCEEDED ( writer->CreateEllipsisTrimmingSign ( formats [ i ], &sign ) ) ) {

				formats [ i ]->SetTrimming ( &trimming, sign );
				sign->Release ( );

			}

		}

		return true;

	}

	bool gpu::begin ( color background ) {

		if ( !device || !target_view )
			return false;

		float clear [ 4 ] = { background.r, background.g, background.b, 1.0f };

		context->OMSetRenderTargets ( 1, &target_view, nullptr );
		context->ClearRenderTargetView ( target_view, clear );
		opacity = 1.0f;

		return true;

	}

	void gpu::screen ( const uint32_t* pixels, int32_t frame_width, int32_t frame_height, rect area, bool crt ) {

		if ( !pixels || frame_width <= 0 || frame_height <= 0 || area.w < 1.0f || area.h < 1.0f )
			return;

		if ( drawing ) {

			context2d->EndDraw ( );
			drawing = false;

		}

		frame_width = base::min ( frame_width, texture_width );
		frame_height = base::min ( frame_height, texture_height );

		D3D11_MAPPED_SUBRESOURCE mapped;

		if ( SUCCEEDED ( context->Map ( picture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

			for ( int32_t y = 0; y < frame_height; y++ ) {

				uint32_t* row = reinterpret_cast < uint32_t* > ( static_cast < uint8_t* > ( mapped.pData ) + static_cast < size_t > ( y ) * mapped.RowPitch );
				const uint32_t* source = pixels + static_cast < size_t > ( y ) * frame_width;

				for ( int32_t x = 0; x < frame_width; x++ )
					row [ x ] = source [ x ] | 0xFF000000u;

			}

			context->Unmap ( picture, 0 );

		}

		float scale = dpi / 96.0f;
		D3D11_VIEWPORT view = { area.x * scale, area.y * scale, area.w * scale, area.h * scale, 0.0f, 1.0f };
		float values [ 8 ] = { static_cast < float > ( frame_width ), static_cast < float > ( frame_height ), 1.0f / texture_width, 1.0f / texture_height, view.Width, view.Height, crt ? 1.0f : 0.0f, 0.0f };

		if ( SUCCEEDED ( context->Map ( constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

			float* out = static_cast < float* > ( mapped.pData );

			for ( int32_t i = 0; i < 8; i++ )
				out [ i ] = values [ i ];

			context->Unmap ( constants, 0 );

		}

		context->OMSetRenderTargets ( 1, &target_view, nullptr );
		context->RSSetViewports ( 1, &view );
		context->IASetPrimitiveTopology ( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		context->IASetInputLayout ( nullptr );
		context->VSSetShader ( vertex_shader, nullptr, 0 );
		context->PSSetShader ( pixel_shader, nullptr, 0 );
		context->PSSetConstantBuffers ( 0, 1, &constants );
		context->PSSetShaderResources ( 0, 1, &picture_view );
		context->PSSetSamplers ( 0, 1, &sampler );
		context->OMSetBlendState ( nullptr, nullptr, 0xFFFFFFFF );
		context->Draw ( 3, 0 );

		ID3D11ShaderResourceView* none = nullptr;

		context->PSSetShaderResources ( 0, 1, &none );

	}

	void gpu::finish ( ) {

		if ( drawing ) {

			context2d->EndDraw ( );
			drawing = false;

		}

	}

	void gpu::end ( ) {

		finish ( );

		if ( chain )
			chain->Present ( 1, 0 );

	}

	bool gpu::save ( const wchar_t* path ) {

		finish ( );

		if ( !readback && target_texture ) {

			D3D11_TEXTURE2D_DESC description = { };

			target_texture->GetDesc ( &description );
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			description.MiscFlags = 0;
			device->CreateTexture2D ( &description, nullptr, &readback );

		}

		if ( !readback )
			return false;

		context->CopyResource ( readback, target_texture );

		D3D11_MAPPED_SUBRESOURCE mapped;

		if ( FAILED ( context->Map ( readback, 0, D3D11_MAP_READ, 0, &mapped ) ) )
			return false;

		uint32_t* pixels = static_cast < uint32_t* > ( runtime_allocate ( sizeof ( uint32_t ) * static_cast < size_t > ( width ) * height ) );

		for ( int32_t y = 0; y < height; y++ ) {

			const uint32_t* row = reinterpret_cast < const uint32_t* > ( static_cast < const uint8_t* > ( mapped.pData ) + static_cast < size_t > ( y ) * mapped.RowPitch );

			for ( int32_t x = 0; x < width; x++ )
				pixels [ static_cast < size_t > ( y ) * width + x ] = row [ x ] & 0xFFFFFF;

		}

		context->Unmap ( readback, 0 );

		bool saved = png::save ( path, pixels, width, height );

		runtime_release ( pixels );

		return saved;

	}

	ID2D1SolidColorBrush* gpu::paint ( color tint ) {

		if ( !drawing ) {

			context2d->BeginDraw ( );
			context2d->SetTransform ( D2D1_MATRIX_3X2_F { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } );
			drawing = true;

		}

		brush->SetColor ( D2D1_COLOR_F { tint.r, tint.g, tint.b, tint.a * opacity } );

		return brush;

	}

	void gpu::fill ( rect area, float radius, color tint ) {

		ID2D1SolidColorBrush* ink = paint ( tint );

		if ( radius <= 0.0f )
			context2d->FillRectangle ( box ( area ), ink );
		else
			context2d->FillRoundedRectangle ( D2D1_ROUNDED_RECT { box ( area ), radius, radius }, ink );

	}

	void gpu::stroke ( rect area, float radius, float line_width, color tint ) {

		ID2D1SolidColorBrush* ink = paint ( tint );
		rect inner = area.inset ( line_width * 0.5f );

		if ( radius <= 0.0f )
			context2d->DrawRectangle ( box ( inner ), ink, line_width );
		else
			context2d->DrawRoundedRectangle ( D2D1_ROUNDED_RECT { box ( inner ), radius, radius }, ink, line_width );

	}

	void gpu::line ( float x0, float y0, float x1, float y1, float line_width, color tint ) {

		ID2D1SolidColorBrush* ink = paint ( tint );

		context2d->DrawLine ( D2D1_POINT_2F { x0, y0 }, D2D1_POINT_2F { x1, y1 }, ink, line_width, rounded );

	}

	void gpu::circle ( float x, float y, float radius, color tint ) {

		ID2D1SolidColorBrush* ink = paint ( tint );

		context2d->FillEllipse ( D2D1_ELLIPSE { D2D1_POINT_2F { x, y }, radius, radius }, ink );

	}

	void gpu::ring ( float x, float y, float radius, float line_width, color tint ) {

		ID2D1SolidColorBrush* ink = paint ( tint );

		context2d->DrawEllipse ( D2D1_ELLIPSE { D2D1_POINT_2F { x, y }, radius, radius }, ink, line_width );

	}

	void gpu::shadow ( rect area, float radius, float spread, color tint ) {

		for ( int32_t i = 4; i >= 1; i-- ) {

			float grow = spread * static_cast < float > ( i ) / 4.0f;
			rect layer = { area.x - grow, area.y - grow + spread * 0.35f, area.w + grow * 2.0f, area.h + grow * 2.0f };

			fill ( layer, radius + grow, fade ( tint, 0.32f / static_cast < float > ( i + 1 ) ) );

		}

	}

	void gpu::text ( const wchar_t* value, font_role role, rect area, color tint, text_align align ) {

		if ( !value || !value [ 0 ] )
			return;

		ID2D1SolidColorBrush* ink = paint ( tint );
		IDWriteTextFormat* format = formats [ role ];

		format->SetTextAlignment ( align == ALIGN_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : ( align == ALIGN_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING ) );
		context2d->DrawText ( value, static_cast < UINT32 > ( base::text_length ( value ) ), format, box ( area ), ink, D2D1_DRAW_TEXT_OPTIONS_NONE );

	}

	float gpu::measure ( const wchar_t* value, font_role role ) {

		if ( !value || !value [ 0 ] )
			return 0.0f;

		IDWriteTextLayout* layout = nullptr;
		float result = 0.0f;

		if ( SUCCEEDED ( writer->CreateTextLayout ( value, static_cast < UINT32 > ( base::text_length ( value ) ), formats [ role ], 4096.0f, 64.0f, &layout ) ) ) {

			DWRITE_TEXT_METRICS metrics = { };

			if ( SUCCEEDED ( layout->GetMetrics ( &metrics ) ) )
				result = metrics.widthIncludingTrailingWhitespace;

			layout->Release ( );

		}

		return result;

	}

	void gpu::push_clip ( rect area ) {

		paint ( color { } );
		context2d->PushAxisAlignedClip ( box ( area ), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE );

	}

	void gpu::pop_clip ( ) {

		if ( drawing )
			context2d->PopAxisAlignedClip ( );

	}

	ID2D1PathGeometry* gpu::begin_path ( ID2D1GeometrySink*& sink, float x, float y, bool filled ) {

		ID2D1PathGeometry* path = nullptr;

		sink = nullptr;

		if ( FAILED ( factory->CreatePathGeometry ( &path ) ) )
			return nullptr;

		if ( FAILED ( path->Open ( &sink ) ) ) {

			path->Release ( );

			return nullptr;

		}

		sink->BeginFigure ( D2D1_POINT_2F { x, y }, filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW );

		return path;

	}

	void gpu::finish_path ( ID2D1PathGeometry* path, ID2D1GeometrySink* sink, float line_width, color tint, bool filled ) {

		if ( !path )
			return;

		sink->EndFigure ( filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN );
		sink->Close ( );
		sink->Release ( );

		ID2D1SolidColorBrush* ink = paint ( tint );

		if ( filled )
			context2d->FillGeometry ( path, ink, nullptr );
		else
			context2d->DrawGeometry ( path, ink, line_width, rounded );

		path->Release ( );

	}

}