#pragma once

#include "base/base.hh"

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct IDXGISwapChain1;
struct ID2D1Factory1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1PathGeometry;
struct ID2D1GeometrySink;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace render {

	struct color {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

	};

	constexpr color rgb ( uint32_t hex, float alpha = 1.0f ) {

		return color { static_cast < float > ( hex >> 16 & 0xFF ) / 255.0f, static_cast < float > ( hex >> 8 & 0xFF ) / 255.0f, static_cast < float > ( hex & 0xFF ) / 255.0f, alpha };

	}

	inline color fade ( color tint, float amount ) {

		return color { tint.r, tint.g, tint.b, tint.a * amount };

	}

	inline color mix ( color x, color y, float t ) {

		return color { x.r + ( y.r - x.r ) * t, x.g + ( y.g - x.g ) * t, x.b + ( y.b - x.b ) * t, x.a + ( y.a - x.a ) * t };

	}

	struct rect {

		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;

		bool contains ( float px, float py ) const {

			return px >= x && py >= y && px < x + w && py < y + h;

		}

		float right ( ) const {

			return x + w;

		}

		float bottom ( ) const {

			return y + h;

		}

		rect inset ( float amount ) const {

			return rect { x + amount, y + amount, w - amount * 2.0f, h - amount * 2.0f };

		}

	};

	enum font_role : uint8_t {

		FONT_BODY,
		FONT_STRONG,
		FONT_SMALL,
		FONT_SMALL_STRONG,
		FONT_TITLE,
		FONT_HERO,
		FONT_MONO,
		FONT_COUNT,

	};

	enum text_align : uint8_t {

		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,

	};

	enum icon_kind : uint8_t {

		ICON_POWER,
		ICON_RESET,
		ICON_PAUSE,
		ICON_PLAY,
		ICON_SAVE,
		ICON_LOAD,
		ICON_CAMERA,
		ICON_DISK,
		ICON_SLIDERS,
		ICON_EXPAND,
		ICON_SHRINK,
		ICON_BOLT,
		ICON_EJECT,
		ICON_LOCK,
		ICON_UNLOCK,
		ICON_PLUS,
		ICON_FOLDER,
		ICON_KEYBOARD,
		ICON_CHECK,

	};

	class gpu {

	public:

		static constexpr int32_t texture_width = 1024;
		static constexpr int32_t texture_height = 512;

		~gpu ( );

		bool create ( HWND__* window, int32_t pixel_width, int32_t pixel_height );

		void destroy ( );

		bool resize ( int32_t pixel_width, int32_t pixel_height );

		void set_dpi ( float value );

		bool ready ( ) const {

			return device != nullptr && target_view != nullptr && context2d != nullptr;

		}

		bool begin ( color background );

		void screen ( const uint32_t* pixels, int32_t frame_width, int32_t frame_height, rect area, bool crt );

		void finish ( );

		void end ( );

		bool save ( const wchar_t* path );

		void fill ( rect area, float radius, color tint );

		void stroke ( rect area, float radius, float width, color tint );

		void line ( float x0, float y0, float x1, float y1, float width, color tint );

		void circle ( float x, float y, float radius, color tint );

		void ring ( float x, float y, float radius, float width, color tint );

		void shadow ( rect area, float radius, float spread, color tint );

		void corners ( rect area, float radius, color tint );

		void text ( const wchar_t* value, font_role role, rect area, color tint, text_align align );

		float measure ( const wchar_t* value, font_role role );

		void icon ( icon_kind kind, float x, float y, float size, color tint );

		void push_clip ( rect area );

		void pop_clip ( );

		int32_t width = 0;
		int32_t height = 0;
		float dpi = 96.0f;
		float opacity = 1.0f;

	private:

		bool make_targets ( );

		void free_targets ( );

		bool make_fonts ( );

		ID2D1SolidColorBrush* paint ( color tint );

		ID2D1PathGeometry* begin_path ( ID2D1GeometrySink*& sink, float x, float y, bool filled );

		void finish_path ( ID2D1PathGeometry* path, ID2D1GeometrySink* sink, float width, color tint, bool filled );

		void arc ( float x, float y, float radius, float from, float to, float width, color tint );

		HWND__* window = nullptr;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain1* chain = nullptr;
		ID3D11Texture2D* target_texture = nullptr;
		ID3D11Texture2D* readback = nullptr;
		ID3D11RenderTargetView* target_view = nullptr;
		ID3D11Texture2D* picture = nullptr;
		ID3D11ShaderResourceView* picture_view = nullptr;
		ID3D11Buffer* constants = nullptr;
		ID3D11VertexShader* vertex_shader = nullptr;
		ID3D11PixelShader* pixel_shader = nullptr;
		ID3D11SamplerState* sampler = nullptr;
		ID2D1Factory1* factory = nullptr;
		ID2D1Device* device2d = nullptr;
		ID2D1DeviceContext* context2d = nullptr;
		ID2D1Bitmap1* target_bitmap = nullptr;
		ID2D1SolidColorBrush* brush = nullptr;
		ID2D1StrokeStyle* rounded = nullptr;
		IDWriteFactory* writer = nullptr;
		IDWriteTextFormat* formats [ FONT_COUNT ] = { };
		bool drawing = false;

	};

}