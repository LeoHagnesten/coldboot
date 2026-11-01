#include "machine/archive.hh"

namespace machine {

	void archive::block ( void* data, uint32_t size ) {

		uint8_t* at = static_cast < uint8_t* > ( data );

		if ( output ) {

			output->reserve ( output->size ( ) + size );

			for ( uint32_t i = 0; i < size; i++ )
				output->push ( at [ i ] );

			return;

		}

		if ( failed || remaining < size ) {

			failed = true;

			for ( uint32_t i = 0; i < size; i++ )
				at [ i ] = 0;

			return;

		}

		for ( uint32_t i = 0; i < size; i++ )
			at [ i ] = input [ i ];

		input += size;
		remaining -= size;

	}

	void archive::bytes ( base::list < uint8_t >& items, uint32_t limit ) {

		uint32_t count = static_cast < uint32_t > ( items.count ( ) );

		field ( count );

		if ( !output ) {

			if ( failed || count > limit || count > remaining ) {

				failed = true;
				items.clear ( );

				return;

			}

			items.resize ( count );

		}

		if ( count )
			block ( items.data ( ), count );

	}

}