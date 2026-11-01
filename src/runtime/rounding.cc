#include "base/base.hh"

extern "C" float floorf ( float value ) {

	uint32_t bits = __builtin_bit_cast ( uint32_t, value );
	int32_t exponent = static_cast < int32_t > ( ( bits >> 23 ) & 0xFF ) - 127;

	if ( exponent >= 23 )
		return value;

	if ( exponent < 0 ) {

		if ( ( bits & 0x7FFFFFFFu ) == 0 )
			return value;

		return ( bits >> 31 ) ? -1.0f : 0.0f;

	}

	uint32_t mask = 0x007FFFFFu >> exponent;

	if ( ( bits & mask ) == 0 )
		return value;

	if ( bits >> 31 )
		bits += mask;

	return __builtin_bit_cast ( float, bits & ~mask );

}

extern "C" float ceilf ( float value ) {

	uint32_t bits = __builtin_bit_cast ( uint32_t, value );
	int32_t exponent = static_cast < int32_t > ( ( bits >> 23 ) & 0xFF ) - 127;

	if ( exponent >= 23 )
		return value;

	if ( exponent < 0 ) {

		if ( ( bits & 0x7FFFFFFFu ) == 0 )
			return value;

		return ( bits >> 31 ) ? -0.0f : 1.0f;

	}

	uint32_t mask = 0x007FFFFFu >> exponent;

	if ( ( bits & mask ) == 0 )
		return value;

	if ( !( bits >> 31 ) )
		bits += mask;

	return __builtin_bit_cast ( float, bits & ~mask );

}

extern "C" float roundf ( float value ) {

	uint32_t bits = __builtin_bit_cast ( uint32_t, value );
	int32_t exponent = static_cast < int32_t > ( ( bits >> 23 ) & 0xFF ) - 127;

	if ( exponent >= 23 )
		return value;

	if ( exponent < -1 )
		return ( bits >> 31 ) ? -0.0f : 0.0f;

	if ( exponent == -1 )
		return ( bits >> 31 ) ? -1.0f : 1.0f;

	uint32_t mask = 0x007FFFFFu >> exponent;

	if ( ( bits & mask ) == 0 )
		return value;

	bits += 0x00400000u >> exponent;

	return __builtin_bit_cast ( float, bits & ~mask );

}

extern "C" double floor ( double value ) {

	uint64_t bits = __builtin_bit_cast ( uint64_t, value );
	int32_t exponent = static_cast < int32_t > ( ( bits >> 52 ) & 0x7FF ) - 1023;

	if ( exponent >= 52 )
		return value;

	if ( exponent < 0 ) {

		if ( ( bits & 0x7FFFFFFFFFFFFFFFull ) == 0 )
			return value;

		return ( bits >> 63 ) ? -1.0 : 0.0;

	}

	uint64_t mask = 0x000FFFFFFFFFFFFFull >> exponent;

	if ( ( bits & mask ) == 0 )
		return value;

	if ( bits >> 63 )
		bits += mask;

	return __builtin_bit_cast ( double, bits & ~mask );

}