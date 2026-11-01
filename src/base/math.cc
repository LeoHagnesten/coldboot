#include "base/math.hh"

namespace math {

	static uint32_t float_bits ( float value ) {

		uint32_t bits;

		__builtin_memcpy ( &bits, &value, 4 );

		return bits;

	}

	static float bits_float ( uint32_t bits ) {

		float value;

		__builtin_memcpy ( &value, &bits, 4 );

		return value;

	}

	void sin_cos ( float angle, float& sine, float& cosine ) {

		float quadrant = round ( angle * 0.63661977236758134f );
		int32_t index = static_cast < int32_t > ( quadrant );
		float reduced = ( ( angle - quadrant * 1.5703125f ) - quadrant * 4.837512969970703125e-4f ) - quadrant * 7.54978995489188216e-8f;
		float z = reduced * reduced;
		float s = ( ( ( -1.9515295891e-4f * z + 8.3321608736e-3f ) * z - 1.6666654611e-1f ) * z * reduced ) + reduced;
		float c = ( ( ( 2.443315711809948e-5f * z - 1.388731625493765e-3f ) * z + 4.166664568298827e-2f ) * z * z ) - 0.5f * z + 1.0f;

		switch ( index & 3 ) {

			case 0:
				sine = s;
				cosine = c;
				break;

			case 1:
				sine = c;
				cosine = -s;
				break;

			case 2:
				sine = -s;
				cosine = -c;
				break;

			default:
				sine = -c;
				cosine = s;
				break;

		}

	}

	float sin ( float angle ) {

		float sine;
		float cosine;

		sin_cos ( angle, sine, cosine );

		return sine;

	}

	float cos ( float angle ) {

		float sine;
		float cosine;

		sin_cos ( angle, sine, cosine );

		return cosine;

	}

	float atan ( float value ) {

		float x = abs ( value );
		float offset = 0.0f;

		if ( x > 2.414213562373095f ) {

			offset = half_pi;
			x = -1.0f / x;

		} else if ( x > 0.4142135623730950f ) {

			offset = pi * 0.25f;
			x = ( x - 1.0f ) / ( x + 1.0f );

		}

		float z = x * x;
		float result = offset + ( ( ( ( 8.05374449538e-2f * z - 1.38776856032e-1f ) * z + 1.99777106478e-1f ) * z - 3.33329491539e-1f ) * z * x + x );

		return value < 0.0f ? -result : result;

	}

	float atan2 ( float y, float x ) {

		if ( x == 0.0f ) {

			if ( y > 0.0f )
				return half_pi;

			if ( y < 0.0f )
				return -half_pi;

			return 0.0f;

		}

		float result = atan ( y / x );

		if ( x < 0.0f )
			result += y >= 0.0f ? pi : -pi;

		return result;

	}

	float asin ( float value ) {

		value = value < -1.0f ? -1.0f : ( value > 1.0f ? 1.0f : value );

		return atan2 ( value, sqrt ( 1.0f - value * value ) );

	}

	float acos ( float value ) {

		value = value < -1.0f ? -1.0f : ( value > 1.0f ? 1.0f : value );

		return atan2 ( sqrt ( 1.0f - value * value ), value );

	}

	float exp ( float value ) {

		if ( value > 88.0f )
			return infinity;

		if ( value < -87.0f )
			return 0.0f;

		float n = round ( value * 1.44269504088896341f );
		float x = value - n * 0.693359375f + n * 2.12194440e-4f;
		float z = x * x;
		float p = ( ( ( ( ( 1.9875691500e-4f * x + 1.3981999507e-3f ) * x + 8.3334519073e-3f ) * x + 4.1665795894e-2f ) * x + 1.6666665459e-1f ) * x + 5.0000001201e-1f ) * z + x + 1.0f;
		int32_t exponent = static_cast < int32_t > ( n ) + 127;

		return p * bits_float ( static_cast < uint32_t > ( exponent ) << 23 );

	}

	float log ( float value ) {

		if ( value <= 0.0f )
			return value == 0.0f ? -infinity : bits_float ( 0x7FC00000u );

		uint32_t bits = float_bits ( value );
		int32_t exponent = static_cast < int32_t > ( ( bits >> 23 ) & 0xFF ) - 126;
		float x = bits_float ( ( bits & 0x807FFFFFu ) | 0x3F000000u );

		if ( x < 0.707106781186547524f ) {

			exponent -= 1;
			x = x + x - 1.0f;

		} else
			x = x - 1.0f;

		float z = x * x;
		float y = ( ( ( ( ( ( ( ( 7.0376836292e-2f * x - 1.1514610310e-1f ) * x + 1.1676998740e-1f ) * x - 1.2420140846e-1f ) * x + 1.4249322787e-1f ) * x - 1.6668057665e-1f ) * x + 2.0000714765e-1f ) * x - 2.4999993993e-1f ) * x + 3.3333331174e-1f ) * x * z;
		float e = static_cast < float > ( exponent );

		y += -2.12194440e-4f * e;
		y += -0.5f * z;

		return x + y + 0.693359375f * e;

	}

	float pow ( float base, float exponent ) {

		if ( base <= 0.0f )
			return 0.0f;

		return exp ( exponent * log ( base ) );

	}

	float wrap_angle ( float radians ) {

		while ( radians > pi )
			radians -= tau;

		while ( radians < -pi )
			radians += tau;

		return radians;

	}

	float ease_out ( float t ) {

		float u = 1.0f - saturate ( t );

		return 1.0f - u * u * u;

	}

	float ease_in_out ( float t ) {

		t = saturate ( t );

		if ( t < 0.5f )
			return 4.0f * t * t * t;

		float u = -2.0f * t + 2.0f;

		return 1.0f - u * u * u * 0.5f;

	}

	float ease_out_back ( float t ) {

		t = saturate ( t );

		float c = 1.70158f;
		float u = t - 1.0f;

		return 1.0f + ( c + 1.0f ) * u * u * u + c * u * u;

	}

	float ease_out_quint ( float t ) {

		float u = 1.0f - saturate ( t );

		return 1.0f - u * u * u * u * u;

	}

	float spring ( float t ) {

		t = saturate ( t );

		return 1.0f - exp ( -6.0f * t ) * cos ( 7.0f * t );

	}

	float angle_at ( vec2 a, vec2 vertex, vec2 b ) {

		vec2 u = normalize ( a - vertex );
		vec2 v = normalize ( b - vertex );

		return degrees ( acos ( dot ( u, v ) ) );

	}

	float angle_between ( vec2 a0, vec2 a1, vec2 b0, vec2 b1 ) {

		vec2 u = normalize ( a1 - a0 );
		vec2 v = normalize ( b1 - b0 );

		return degrees ( acos ( dot ( u, v ) ) );

	}

	float signed_distance ( vec2 p, vec2 a, vec2 b ) {

		vec2 d = b - a;
		float size = length ( d );

		return size > 1e-9f ? cross ( d, p - a ) / size : 0.0f;

	}

	vec2 project ( vec2 p, vec2 a, vec2 b ) {

		vec2 d = b - a;
		float size = dot ( d, d );
		float t = size > 1e-9f ? dot ( p - a, d ) / size : 0.0f;

		return a + d * t;

	}

	bool intersect ( vec2 a0, vec2 a1, vec2 b0, vec2 b1, vec2& result ) {

		vec2 r = a1 - a0;
		vec2 s = b1 - b0;
		float denominator = cross ( r, s );

		if ( abs ( denominator ) < 1e-9f )
			return false;

		float t = cross ( b0 - a0, s ) / denominator;

		result = a0 + r * t;

		return true;

	}

	bool fit_line ( const vec2* points, int32_t count, vec2& center, vec2& direction ) {

		if ( count < 2 )
			return false;

		float mx = 0.0f;
		float my = 0.0f;

		for ( int32_t i = 0; i < count; i++ ) {

			mx += points [ i ].x;
			my += points [ i ].y;

		}

		mx /= static_cast < float > ( count );
		my /= static_cast < float > ( count );

		float xx = 0.0f;
		float xy = 0.0f;
		float yy = 0.0f;

		for ( int32_t i = 0; i < count; i++ ) {

			float dx = points [ i ].x - mx;
			float dy = points [ i ].y - my;

			xx += dx * dx;
			xy += dx * dy;
			yy += dy * dy;

		}

		float angle = 0.5f * atan2 ( 2.0f * xy, xx - yy );

		center = vec2 ( mx, my );
		direction = vec2 ( cos ( angle ), sin ( angle ) );

		return true;

	}

	rect bounds ( const vec2* points, int32_t count ) {

		if ( count <= 0 )
			return rect ( );

		float x0 = points [ 0 ].x;
		float y0 = points [ 0 ].y;
		float x1 = x0;
		float y1 = y0;

		for ( int32_t i = 1; i < count; i++ ) {

			x0 = points [ i ].x < x0 ? points [ i ].x : x0;
			y0 = points [ i ].y < y0 ? points [ i ].y : y0;
			x1 = points [ i ].x > x1 ? points [ i ].x : x1;
			y1 = points [ i ].y > y1 ? points [ i ].y : y1;

		}

		return rect ( x0, y0, x1 - x0, y1 - y0 );

	}

	float median ( float* values, int32_t count ) {

		if ( count <= 0 )
			return 0.0f;

		base::sort ( values, count, [ ] ( float a, float b ) { return a < b; } );

		return count % 2 ? values [ count / 2 ] : 0.5f * ( values [ count / 2 - 1 ] + values [ count / 2 ] );

	}

}