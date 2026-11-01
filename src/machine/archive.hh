#pragma once

#include "base/base.hh"

namespace machine {

	class archive {

	public:

		explicit archive ( base::list < uint8_t >& out ) : output ( &out ) {

		}

		archive ( const uint8_t* data, uint32_t size ) : input ( data ), remaining ( size ) {

		}

		bool writing ( ) const {

			return output != nullptr;

		}

		void block ( void* data, uint32_t size );

		void bytes ( base::list < uint8_t >& items, uint32_t limit );

		template < typename type >
		void field ( type& value ) {

			block ( &value, sizeof ( value ) );

		}

		bool failed = false;

	private:

		base::list < uint8_t >* output = nullptr;
		const uint8_t* input = nullptr;
		uint32_t remaining = 0;

	};

}