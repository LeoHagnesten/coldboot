#pragma once

#include "base/base.hh"

namespace math {

	constexpr float pi = 3.14159265358979323846f;
	constexpr float tau = 6.28318530717958647692f;
	constexpr float half_pi = 1.57079632679489661923f;
	constexpr float degrees_to_radians = pi / 180.0f;
	constexpr float radians_to_degrees = 180.0f / pi;
	constexpr float infinity = __builtin_huge_valf ( );

	inline float sqrt ( float value ) {

		return __builtin_sqrtf ( value );

	}

	inline double sqrt ( double value ) {

		return __builtin_sqrt ( value );

	}

	inline float abs ( float value ) {

		return __builtin_fabsf ( value );

	}

	inline int32_t abs ( int32_t value ) {

		return value < 0 ? -value : value;

	}

	inline float floor ( float value ) {

		return __builtin_floorf ( value );

	}

	inline float ceil ( float value ) {

		return __builtin_ceilf ( value );

	}

	inline float round ( float value ) {

		return __builtin_roundf ( value );

	}

	inline int32_t floor_int ( float value ) {

		return static_cast < int32_t > ( __builtin_floorf ( value ) );

	}

	inline int32_t round_int ( float value ) {

		return static_cast < int32_t > ( __builtin_roundf ( value ) );

	}

	inline float saturate ( float value ) {

		return value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );

	}

	inline float lerp ( float a, float b, float t ) {

		return a + ( b - a ) * t;

	}

	inline float smoothstep ( float low, float high, float value ) {

		float t = saturate ( ( value - low ) / ( high - low ) );

		return t * t * ( 3.0f - 2.0f * t );

	}

	inline float radians ( float degrees ) {

		return degrees * degrees_to_radians;

	}

	inline float degrees ( float radians ) {

		return radians * radians_to_degrees;

	}

	inline bool finite ( float value ) {

		return value == value && abs ( value ) <= 3.4e38f;

	}

	void sin_cos ( float angle, float& sine, float& cosine );

	float sin ( float angle );

	float cos ( float angle );

	float atan ( float value );

	float atan2 ( float y, float x );

	float asin ( float value );

	float acos ( float value );

	float exp ( float value );

	float log ( float value );

	float pow ( float base, float exponent );

	float wrap_angle ( float radians );

	float ease_out ( float t );

	float ease_in_out ( float t );

	float ease_out_back ( float t );

	float ease_out_quint ( float t );

	float spring ( float t );

	struct vec2 {

		float x = 0.0f;
		float y = 0.0f;

		vec2 ( ) = default;

		constexpr vec2 ( float a, float b ) : x ( a ), y ( b ) { }

		vec2 operator+ ( vec2 other ) const {

			return vec2 ( x + other.x, y + other.y );

		}

		vec2 operator- ( vec2 other ) const {

			return vec2 ( x - other.x, y - other.y );

		}

		vec2 operator* ( float s ) const {

			return vec2 ( x * s, y * s );

		}

		vec2 operator/ ( float s ) const {

			return vec2 ( x / s, y / s );

		}

		vec2 operator- ( ) const {

			return vec2 ( -x, -y );

		}

		vec2& operator+= ( vec2 other ) {

			x += other.x;
			y += other.y;

			return *this;

		}

		vec2& operator-= ( vec2 other ) {

			x -= other.x;
			y -= other.y;

			return *this;

		}

		vec2& operator*= ( float s ) {

			x *= s;
			y *= s;

			return *this;

		}

	};

	struct vec3 {

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		vec3 ( ) = default;

		constexpr vec3 ( float a, float b, float c ) : x ( a ), y ( b ), z ( c ) { }

		vec3 operator+ ( vec3 other ) const {

			return vec3 ( x + other.x, y + other.y, z + other.z );

		}

		vec3 operator- ( vec3 other ) const {

			return vec3 ( x - other.x, y - other.y, z - other.z );

		}

		vec3 operator* ( float s ) const {

			return vec3 ( x * s, y * s, z * s );

		}

	};

	struct rect {

		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;

		rect ( ) = default;

		constexpr rect ( float a, float b, float c, float d ) : x ( a ), y ( b ), w ( c ), h ( d ) { }

		float right ( ) const {

			return x + w;

		}

		float bottom ( ) const {

			return y + h;

		}

		vec2 center ( ) const {

			return vec2 ( x + w * 0.5f, y + h * 0.5f );

		}

		bool contains ( vec2 p ) const {

			return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;

		}

		rect inset ( float amount ) const {

			return rect ( x + amount, y + amount, w - amount * 2.0f, h - amount * 2.0f );

		}

	};

	inline vec2 operator* ( float s, vec2 v ) {

		return vec2 ( v.x * s, v.y * s );

	}

	inline float dot ( vec2 a, vec2 b ) {

		return a.x * b.x + a.y * b.y;

	}

	inline float cross ( vec2 a, vec2 b ) {

		return a.x * b.y - a.y * b.x;

	}

	inline float length ( vec2 a ) {

		return sqrt ( a.x * a.x + a.y * a.y );

	}

	inline float distance ( vec2 a, vec2 b ) {

		return length ( a - b );

	}

	inline vec2 normalize ( vec2 a ) {

		float size = length ( a );

		return size > 1e-9f ? a / size : vec2 ( 0.0f, 0.0f );

	}

	inline vec2 perpendicular ( vec2 a ) {

		return vec2 ( -a.y, a.x );

	}

	inline vec2 mid ( vec2 a, vec2 b ) {

		return vec2 ( ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f );

	}

	inline vec2 lerp ( vec2 a, vec2 b, float t ) {

		return vec2 ( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t );

	}

	inline vec2 rotate ( vec2 p, float radians ) {

		float s;
		float c;

		sin_cos ( radians, s, c );

		return vec2 ( p.x * c - p.y * s, p.x * s + p.y * c );

	}

	inline float dot ( vec3 a, vec3 b ) {

		return a.x * b.x + a.y * b.y + a.z * b.z;

	}

	inline vec3 cross ( vec3 a, vec3 b ) {

		return vec3 ( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );

	}

	inline vec3 normalize ( vec3 a ) {

		float size = sqrt ( dot ( a, a ) );

		return size > 1e-9f ? a * ( 1.0f / size ) : vec3 ( 0.0f, 0.0f, 0.0f );

	}

	float angle_at ( vec2 a, vec2 vertex, vec2 b );

	float angle_between ( vec2 a0, vec2 a1, vec2 b0, vec2 b1 );

	float signed_distance ( vec2 p, vec2 a, vec2 b );

	vec2 project ( vec2 p, vec2 a, vec2 b );

	bool intersect ( vec2 a0, vec2 a1, vec2 b0, vec2 b1, vec2& result );

	bool fit_line ( const vec2* points, int32_t count, vec2& center, vec2& direction );

	rect bounds ( const vec2* points, int32_t count );

	float median ( float* values, int32_t count );

}