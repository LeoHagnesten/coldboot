#include "render/gpu.hh"
#include "base/math.hh"
#include <windows.h>
#include <d2d1_1.h>

namespace render {

	void gpu::arc ( float x, float y, float radius, float from, float to, float line_width, color tint ) {

		float start = math::radians ( from );
		float stop = math::radians ( to );
		ID2D1GeometrySink* sink = nullptr;
		ID2D1PathGeometry* path = begin_path ( sink, x + math::cos ( start ) * radius, y + math::sin ( start ) * radius, false );

		if ( !path )
			return;

		D2D1_ARC_SEGMENT segment = { };

		segment.point = D2D1_POINT_2F { x + math::cos ( stop ) * radius, y + math::sin ( stop ) * radius };
		segment.size = D2D1_SIZE_F { radius, radius };
		segment.rotationAngle = 0.0f;
		segment.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
		segment.arcSize = to - from > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
		sink->AddArc ( segment );
		finish_path ( path, sink, line_width, tint, false );

	}

	void gpu::corners ( rect area, float radius, color tint ) {

		const float points [ 4 ] [ 6 ] = {

			{ area.x, area.y, area.x + radius, area.y, area.x, area.y + radius },
			{ area.right ( ), area.y, area.right ( ), area.y + radius, area.right ( ) - radius, area.y },
			{ area.right ( ), area.bottom ( ), area.right ( ) - radius, area.bottom ( ), area.right ( ), area.bottom ( ) - radius },
			{ area.x, area.bottom ( ), area.x, area.bottom ( ) - radius, area.x + radius, area.bottom ( ) },

		};

		for ( const auto& corner : points ) {

			ID2D1GeometrySink* sink = nullptr;
			ID2D1PathGeometry* path = begin_path ( sink, corner [ 0 ], corner [ 1 ], true );

			if ( !path )
				continue;

			D2D1_ARC_SEGMENT segment = { };

			sink->AddLine ( D2D1_POINT_2F { corner [ 2 ], corner [ 3 ] } );
			segment.point = D2D1_POINT_2F { corner [ 4 ], corner [ 5 ] };
			segment.size = D2D1_SIZE_F { radius, radius };
			segment.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
			segment.arcSize = D2D1_ARC_SIZE_SMALL;
			sink->AddArc ( segment );
			finish_path ( path, sink, 0.0f, tint, true );

		}

	}

	void gpu::icon ( icon_kind kind, float x, float y, float size, color tint ) {

		float u = size / 24.0f;
		float w = base::max ( 1.5f, size / 12.0f );

		auto at_x = [ & ] ( float grid ) {

			return x + grid * u;

		};

		auto at_y = [ & ] ( float grid ) {

			return y + grid * u;

		};

		auto poly = [ & ] ( const float* points, int32_t count, bool filled ) {

			ID2D1GeometrySink* sink = nullptr;
			ID2D1PathGeometry* path = begin_path ( sink, at_x ( points [ 0 ] ), at_y ( points [ 1 ] ), filled );

			if ( !path )
				return;

			for ( int32_t i = 1; i < count; i++ )
				sink->AddLine ( D2D1_POINT_2F { at_x ( points [ i * 2 ] ), at_y ( points [ i * 2 + 1 ] ) } );

			finish_path ( path, sink, w, tint, filled );

		};

		auto segment = [ & ] ( float x0, float y0, float x1, float y1 ) {

			line ( at_x ( x0 ), at_y ( y0 ), at_x ( x1 ), at_y ( y1 ), w, tint );

		};

		auto box_line = [ & ] ( float left, float top, float box_width, float box_height, float radius ) {

			stroke ( rect { at_x ( left ) - w * 0.5f, at_y ( top ) - w * 0.5f, box_width * u + w, box_height * u + w }, radius * u, w, tint );

		};

		switch ( kind ) {

			case ICON_POWER:
				arc ( x, y + u, 7.5f * u, -55.0f, 235.0f, w, tint );
				segment ( 0.0f, -9.0f, 0.0f, -1.0f );
				return;

			case ICON_RESET: {

				static const float head [ ] = { -2.5f, -10.5f, 1.0f, -7.0f, -2.5f, -3.5f };

				arc ( x, y, 7.0f * u, -20.0f, 270.0f, w, tint );
				poly ( head, 3, false );

				return;

			}

			case ICON_PAUSE:
				fill ( rect { at_x ( -6.0f ), at_y ( -7.5f ), 4.0f * u, 15.0f * u }, 1.2f * u, tint );
				fill ( rect { at_x ( 2.0f ), at_y ( -7.5f ), 4.0f * u, 15.0f * u }, 1.2f * u, tint );
				return;

			case ICON_PLAY: {

				static const float shape [ ] = { -5.0f, -8.0f, 8.0f, 0.0f, -5.0f, 8.0f };

				poly ( shape, 3, true );

				return;

			}

			case ICON_SAVE:
			case ICON_LOAD: {

				static const float tray [ ] = { -8.5f, 2.5f, -8.5f, 8.5f, 8.5f, 8.5f, 8.5f, 2.5f };
				static const float down [ ] = { -4.0f, -1.5f, 0.0f, 2.5f, 4.0f, -1.5f };
				static const float up [ ] = { -4.0f, -4.5f, 0.0f, -8.5f, 4.0f, -4.5f };

				poly ( tray, 4, false );

				if ( kind == ICON_SAVE ) {

					segment ( 0.0f, -8.5f, 0.0f, 2.5f );
					poly ( down, 3, false );

				} else {

					segment ( 0.0f, 3.5f, 0.0f, -8.5f );
					poly ( up, 3, false );

				}

				return;

			}

			case ICON_CAMERA: {

				static const float bump [ ] = { -4.5f, -5.0f, -3.0f, -8.0f, 3.0f, -8.0f, 4.5f, -5.0f };

				box_line ( -10.0f, -5.0f, 20.0f, 14.0f, 3.0f );
				poly ( bump, 4, false );
				ring ( x, at_y ( 2.0f ), 3.8f * u, w, tint );

				return;

			}

			case ICON_DISK:
				box_line ( -8.5f, -8.5f, 17.0f, 17.0f, 2.5f );
				box_line ( -4.5f, -8.5f, 8.0f, 5.5f, 0.5f );
				box_line ( -5.0f, 1.5f, 10.0f, 7.0f, 1.0f );
				return;

			case ICON_SLIDERS:
				segment ( -9.0f, -6.0f, 9.0f, -6.0f );
				segment ( -9.0f, 0.0f, 9.0f, 0.0f );
				segment ( -9.0f, 6.0f, 9.0f, 6.0f );
				circle ( at_x ( 4.0f ), at_y ( -6.0f ), 2.6f * u, tint );
				circle ( at_x ( -3.5f ), at_y ( 0.0f ), 2.6f * u, tint );
				circle ( at_x ( 1.5f ), at_y ( 6.0f ), 2.6f * u, tint );
				return;

			case ICON_EXPAND: {

				static const float a [ ] = { -9.0f, -3.5f, -9.0f, -9.0f, -3.5f, -9.0f };
				static const float b [ ] = { 3.5f, -9.0f, 9.0f, -9.0f, 9.0f, -3.5f };
				static const float c [ ] = { 9.0f, 3.5f, 9.0f, 9.0f, 3.5f, 9.0f };
				static const float d [ ] = { -3.5f, 9.0f, -9.0f, 9.0f, -9.0f, 3.5f };

				poly ( a, 3, false );
				poly ( b, 3, false );
				poly ( c, 3, false );
				poly ( d, 3, false );

				return;

			}

			case ICON_SHRINK: {

				static const float a [ ] = { -9.0f, -3.5f, -3.5f, -3.5f, -3.5f, -9.0f };
				static const float b [ ] = { 3.5f, -9.0f, 3.5f, -3.5f, 9.0f, -3.5f };
				static const float c [ ] = { 9.0f, 3.5f, 3.5f, 3.5f, 3.5f, 9.0f };
				static const float d [ ] = { -3.5f, 9.0f, -3.5f, 3.5f, -9.0f, 3.5f };

				poly ( a, 3, false );
				poly ( b, 3, false );
				poly ( c, 3, false );
				poly ( d, 3, false );

				return;

			}

			case ICON_BOLT: {

				static const float shape [ ] = { 2.0f, -10.0f, -7.0f, 1.5f, -0.5f, 1.5f, -2.0f, 10.0f, 7.0f, -1.5f, 0.5f, -1.5f };

				poly ( shape, 6, true );

				return;

			}

			case ICON_EJECT: {

				static const float shape [ ] = { -8.0f, 2.0f, 0.0f, -7.0f, 8.0f, 2.0f };

				poly ( shape, 3, true );
				fill ( rect { at_x ( -8.0f ), at_y ( 5.0f ), 16.0f * u, 3.0f * u }, 1.0f * u, tint );

				return;

			}

			case ICON_LOCK:
			case ICON_UNLOCK:
				box_line ( -7.0f, -1.5f, 14.0f, 10.0f, 2.5f );
				arc ( x, at_y ( -3.5f ), 4.5f * u, 180.0f, kind == ICON_LOCK ? 360.0f : 320.0f, w, tint );
				segment ( -4.5f, -3.5f, -4.5f, -1.5f );

				if ( kind == ICON_LOCK )
					segment ( 4.5f, -3.5f, 4.5f, -1.5f );

				circle ( x, at_y ( 3.5f ), 1.4f * u, tint );
				return;

			case ICON_PLUS:
				segment ( 0.0f, -7.5f, 0.0f, 7.5f );
				segment ( -7.5f, 0.0f, 7.5f, 0.0f );
				return;

			case ICON_FOLDER: {

				static const float shape [ ] = { -10.0f, -7.0f, -3.5f, -7.0f, -1.5f, -4.5f, 10.0f, -4.5f, 10.0f, 7.5f, -10.0f, 7.5f, -10.0f, -7.0f };

				poly ( shape, 7, false );

				return;

			}

			case ICON_KEYBOARD:
				box_line ( -11.0f, -6.5f, 22.0f, 13.0f, 2.5f );
				circle ( at_x ( -6.0f ), at_y ( -2.0f ), 1.1f * u, tint );
				circle ( at_x ( -2.0f ), at_y ( -2.0f ), 1.1f * u, tint );
				circle ( at_x ( 2.0f ), at_y ( -2.0f ), 1.1f * u, tint );
				circle ( at_x ( 6.0f ), at_y ( -2.0f ), 1.1f * u, tint );
				segment ( -5.0f, 2.5f, 5.0f, 2.5f );
				return;

			case ICON_CHECK: {

				static const float shape [ ] = { -6.5f, 0.0f, -2.0f, 4.5f, 7.0f, -5.5f };

				poly ( shape, 3, false );

				return;

			}

			default:
				return;

		}

	}

}