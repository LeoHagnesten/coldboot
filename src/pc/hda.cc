#include "pc/hda.hh"
#include "base/math.hh"
#include <windows.h>

namespace pc {

	static constexpr uint32_t REG_GCTL = 0x08;
	static constexpr uint32_t REG_WAKEEN = 0x0C;
	static constexpr uint32_t REG_STATESTS = 0x0E;
	static constexpr uint32_t REG_INTCTL = 0x20;
	static constexpr uint32_t REG_INTSTS = 0x24;
	static constexpr uint32_t REG_WALCLK = 0x30;
	static constexpr uint32_t REG_CORBLBASE = 0x40;
	static constexpr uint32_t REG_CORBWP = 0x48;
	static constexpr uint32_t REG_CORBRP = 0x4A;
	static constexpr uint32_t REG_CORBCTL = 0x4C;
	static constexpr uint32_t REG_CORBSTS = 0x4D;
	static constexpr uint32_t REG_CORBSIZE = 0x4E;
	static constexpr uint32_t REG_RIRBLBASE = 0x50;
	static constexpr uint32_t REG_RIRBWP = 0x58;
	static constexpr uint32_t REG_RINTCNT = 0x5A;
	static constexpr uint32_t REG_RIRBCTL = 0x5C;
	static constexpr uint32_t REG_RIRBSTS = 0x5D;
	static constexpr uint32_t REG_RIRBSIZE = 0x5E;
	static constexpr uint32_t REG_ICOI = 0x60;
	static constexpr uint32_t REG_ICII = 0x64;
	static constexpr uint32_t REG_ICIS = 0x68;
	static constexpr uint32_t REG_DPLBASE = 0x70;
	static constexpr uint32_t REG_STREAMS = 0x80;
	static constexpr uint32_t stream_stride = 0x20;
	static constexpr uint32_t mixed_room = 16384;

	static uint32_t ring_entries ( uint8_t code ) {

		return ( code & 3 ) == 0 ? 2u : ( ( code & 3 ) == 1 ? 16u : 256u );

	}

	hda::hda ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		regs = static_cast < uint8_t* > ( runtime_allocate_zero ( memory_size ) );
		mixed = static_cast < float* > ( runtime_allocate_zero ( mixed_room * sizeof ( float ) ) );
		identify ( 0x8086, 0x2668, 0x04, 0x03, 0x00, 0x01, 0x00 );
		set_bar ( 0, memory_size, false, default_memory );
		set_interrupt ( 1, 11 );
		config [ 0x04 ] = 0x06;
		config [ 0x2C ] = 0x86;
		config [ 0x2D ] = 0x80;
		config [ 0x2E ] = 0x68;
		config [ 0x2F ] = 0x26;
		config [ 0x40 ] = 0x01;
		epoch = bus.now ( );
		reset_controller ( );

	}

	hda::~hda ( ) {

		runtime_release ( regs );
		runtime_release ( mixed );

	}

	void hda::reset_controller ( ) {

		for ( uint32_t i = 0; i < 0x200; i++ )
			regs [ i ] = 0;

		put ( 0x00, 2, 0x4401 );
		put ( 0x02, 1, 0x00 );
		put ( 0x03, 1, 0x01 );
		put ( 0x04, 2, 0x003C );
		put ( 0x06, 2, 0x001D );
		put ( REG_CORBSIZE, 1, 0x42 );
		put ( REG_RIRBSIZE, 1, 0x42 );

		for ( uint32_t i = 0; i < stream_count; i++ ) {

			put ( REG_STREAMS + i * stream_stride + 0x10, 2, i < first_output ? 0x0077 : 0x00BF );
			streams [ i ] = stream ( );

		}

		responses = 0;
		state = codec ( );
		update_line ( );

	}

	uint32_t hda::get ( uint32_t offset, uint32_t size ) const {

		uint32_t value = 0;

		for ( uint32_t i = 0; i < size && offset + i < memory_size; i++ )
			value |= static_cast < uint32_t > ( regs [ offset + i ] ) << ( i * 8 );

		return value;

	}

	void hda::put ( uint32_t offset, uint32_t size, uint32_t value ) {

		for ( uint32_t i = 0; i < size && offset + i < memory_size; i++ )
			regs [ offset + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	void hda::written ( uint32_t offset ) {

		if ( offset < 0x10 || offset >= 0x14 )
			return;

		uint32_t wanted = bar_address ( 0 );

		if ( wanted && wanted != memory_base ) {

			vm.relocate_memory ( this, memory_base, wanted );
			memory_base = wanted;

		}

	}

	uint64_t hda::read ( uint64_t address, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( offset >= 0x2000 ) {

			offset -= 0x2000;

			bool alias = offset >= REG_WALCLK && offset < REG_WALCLK + 4;

			for ( uint32_t i = 0; i < stream_count && !alias; i++ )
				alias = offset >= REG_STREAMS + i * stream_stride + 4 && offset < REG_STREAMS + i * stream_stride + 8;

			if ( !alias )
				return 0;

		}

		if ( offset >= REG_WALCLK && offset < REG_WALCLK + 4 ) {

			uint32_t clock = static_cast < uint32_t > ( ( bus.now ( ) - epoch ) * 3 / 125 );

			return ( clock >> ( ( offset - REG_WALCLK ) * 8 ) ) & ( size >= 4 ? 0xFFFFFFFFu : ( 1u << ( size * 8 ) ) - 1 );

		}

		if ( size == 8 )
			return get ( offset, 4 ) | static_cast < uint64_t > ( get ( offset + 4, 4 ) ) << 32;

		return get ( offset, size );

	}

	void hda::write ( uint64_t address, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( offset >= 0x2000 || size > 8 )
			return;

		uint8_t before [ 8 ];

		for ( uint32_t i = 0; i < size; i++ )
			before [ i ] = regs [ offset + i ];

		for ( uint32_t i = 0; i < size; i++ ) {

			uint32_t at = offset + i;
			uint8_t byte = static_cast < uint8_t > ( value >> ( i * 8 ) );
			uint32_t local = at >= REG_STREAMS && at < REG_STREAMS + stream_count * stream_stride ? ( at - REG_STREAMS ) % stream_stride : 0xFFFF;

			if ( at < 0x08 || ( at >= REG_INTSTS && at < REG_INTSTS + 4 ) || ( at >= REG_WALCLK && at < REG_WALCLK + 4 ) || at == REG_CORBWP + 1 || at == REG_CORBRP || at == REG_RIRBWP || at == REG_ICII || ( at > REG_ICII && at < REG_ICII + 4 ) )
				continue;

			if ( local != 0xFFFF && ( ( local >= 0x04 && local < 0x08 ) || local == 0x10 || local == 0x11 ) )
				continue;

			if ( at == REG_STATESTS || at == REG_STATESTS + 1 || at == REG_CORBSTS || at == REG_RIRBSTS || local == 0x03 ) {

				regs [ at ] &= static_cast < uint8_t > ( ~byte );

				continue;

			}

			if ( at == REG_CORBSIZE || at == REG_RIRBSIZE ) {

				regs [ at ] = static_cast < uint8_t > ( ( regs [ at ] & 0xF0 ) | ( byte & 0x03 ) );

				continue;

			}

			if ( at == REG_ICIS ) {

				regs [ at ] = static_cast < uint8_t > ( ( regs [ at ] & ~( byte & 0x02 ) & 0x02 ) | ( byte & 0x01 ) );

				continue;

			}

			regs [ at ] = byte;

		}

		touched ( offset, size, before );

	}

	void hda::touched ( uint32_t offset, uint32_t size, const uint8_t* before ) {

		auto hit = [ & ] ( uint32_t start, uint32_t length ) {

			return offset < start + length && start < offset + size;

		};

		if ( hit ( REG_GCTL, 1 ) ) {

			bool was = offset <= REG_GCTL ? ( before [ REG_GCTL - offset ] & 1 ) != 0 : true;
			bool now = ( regs [ REG_GCTL ] & 1 ) != 0;

			if ( was && !now ) {

				uint8_t kept [ 4 ] = { regs [ REG_GCTL ], regs [ REG_GCTL + 1 ], regs [ REG_GCTL + 2 ], regs [ REG_GCTL + 3 ] };

				reset_controller ( );

				for ( uint32_t i = 0; i < 4; i++ )
					regs [ REG_GCTL + i ] = kept [ i ];

			} else if ( !was && now )
				regs [ REG_STATESTS ] |= 0x01;

		}

		if ( hit ( REG_CORBRP + 1, 1 ) ) {

			regs [ REG_CORBRP + 1 ] &= 0x80;

			if ( regs [ REG_CORBRP + 1 ] & 0x80 )
				regs [ REG_CORBRP ] = 0;

		}

		if ( hit ( REG_RIRBWP + 1, 1 ) ) {

			if ( regs [ REG_RIRBWP + 1 ] & 0x80 )
				regs [ REG_RIRBWP ] = 0;

			regs [ REG_RIRBWP + 1 ] = 0;

		}

		if ( hit ( REG_ICIS, 1 ) && ( regs [ REG_ICIS ] & 0x01 ) ) {

			put ( REG_ICII, 4, verb ( get ( REG_ICOI, 4 ) ) );
			regs [ REG_ICIS ] = static_cast < uint8_t > ( ( regs [ REG_ICIS ] & ~0x01 ) | 0x02 );

		}

		if ( hit ( REG_CORBWP, 2 ) || hit ( REG_CORBCTL, 1 ) )
			run_corb ( );

		for ( uint32_t i = 0; i < stream_count; i++ ) {

			uint32_t base = REG_STREAMS + i * stream_stride;

			if ( !hit ( base, 1 ) )
				continue;

			bool was_running = offset <= base ? ( before [ base - offset ] & 0x02 ) != 0 : false;

			if ( regs [ base ] & 0x01 ) {

				reset_stream ( i );
				regs [ base ] = 0x01;

			} else if ( ( regs [ base ] & 0x02 ) && !was_running )
				streams [ i ].last = bus.now ( );

		}

		update_line ( );

	}

	void hda::reset_stream ( uint32_t index ) {

		uint32_t base = REG_STREAMS + index * stream_stride;

		put ( base, 4, 0 );
		put ( base + 0x04, 4, 0 );
		put ( base + 0x08, 4, 0 );
		put ( base + 0x0C, 2, 0 );
		put ( base + 0x12, 2, 0 );
		put ( base + 0x18, 4, 0 );
		put ( base + 0x1C, 4, 0 );
		streams [ index ] = stream ( );

	}

	void hda::run_corb ( ) {

		if ( !( regs [ REG_CORBCTL ] & 0x02 ) )
			return;

		uint32_t entries = ring_entries ( regs [ REG_CORBSIZE ] );
		uint64_t base = ( get ( REG_CORBLBASE, 4 ) & ~0x7Fu ) | static_cast < uint64_t > ( get ( REG_CORBLBASE + 4, 4 ) ) << 32;
		uint32_t read_at = regs [ REG_CORBRP ] % entries;
		uint32_t write_at = regs [ REG_CORBWP ] % entries;
		bool answered = false;

		for ( uint32_t guard = 0; read_at != write_at && guard < 256; guard++ ) {

			read_at = ( read_at + 1 ) % entries;

			uint8_t bytes [ 4 ];

			vm.read ( base + read_at * 4ull, bytes, 4 );

			uint32_t command = static_cast < uint32_t > ( bytes [ 0 ] ) | static_cast < uint32_t > ( bytes [ 1 ] ) << 8 | static_cast < uint32_t > ( bytes [ 2 ] ) << 16 | static_cast < uint32_t > ( bytes [ 3 ] ) << 24;

			if ( command >> 28 == 0 ) {

				respond ( verb ( command ) );
				answered = true;

			}

		}

		regs [ REG_CORBRP ] = static_cast < uint8_t > ( read_at );

		if ( answered && ( regs [ REG_RIRBCTL ] & 0x01 ) )
			regs [ REG_RIRBSTS ] |= 0x01;

		update_line ( );

	}

	void hda::respond ( uint32_t response ) {

		if ( !( regs [ REG_RIRBCTL ] & 0x02 ) )
			return;

		uint32_t entries = ring_entries ( regs [ REG_RIRBSIZE ] );
		uint64_t base = ( get ( REG_RIRBLBASE, 4 ) & ~0x7Fu ) | static_cast < uint64_t > ( get ( REG_RIRBLBASE + 4, 4 ) ) << 32;
		uint32_t write_at = ( regs [ REG_RIRBWP ] + 1u ) % entries;
		uint8_t bytes [ 8 ] = { static_cast < uint8_t > ( response ), static_cast < uint8_t > ( response >> 8 ), static_cast < uint8_t > ( response >> 16 ), static_cast < uint8_t > ( response >> 24 ), 0, 0, 0, 0 };

		vm.write ( base + write_at * 8ull, bytes, 8 );
		regs [ REG_RIRBWP ] = static_cast < uint8_t > ( write_at );

		uint32_t count = get ( REG_RINTCNT, 2 ) & 0xFF;

		if ( ++responses >= ( count ? count : 256u ) ) {

			responses = 0;

			if ( regs [ REG_RIRBCTL ] & 0x01 )
				regs [ REG_RIRBSTS ] |= 0x01;

		}

	}

	uint32_t hda::parameter ( uint32_t node, uint32_t id ) const {

		switch ( node ) {

			case 0:

				if ( id == 0x00 )
					return 0xCB010001;

				if ( id == 0x02 )
					return 0x00100100;

				if ( id == 0x04 )
					return 0x00010001;

				return 0;

			case 1:

				if ( id == 0x04 )
					return 0x00020002;

				if ( id == 0x05 )
					return 0x00000001;

				if ( id == 0x0A )
					return 0x00020060;

				if ( id == 0x0B )
					return 0x00000001;

				if ( id == 0x0F )
					return 0x00000009;

				return 0;

			case 2:

				if ( id == 0x09 )
					return 0x0000041D;

				if ( id == 0x0A )
					return 0x00020060;

				if ( id == 0x0B )
					return 0x00000001;

				if ( id == 0x0F )
					return 0x00000009;

				if ( id == 0x12 )
					return 0x80034A4A;

				return 0;

			case 3:

				if ( id == 0x09 )
					return 0x0040050D;

				if ( id == 0x0C )
					return 0x00010014;

				if ( id == 0x0E )
					return 0x00000001;

				if ( id == 0x0F )
					return 0x00000009;

				if ( id == 0x12 )
					return 0x80000000;

				return 0;

			default:
				return 0;

		}

	}

	uint32_t hda::verb ( uint32_t command ) {

		uint32_t node = command >> 20 & 0xFF;
		uint32_t data = command & 0xFFFFF;
		uint32_t top = data >> 16;

		verbs_run++;

		if ( top != 0x7 && top != 0xF ) {

			uint32_t payload = data & 0xFFFF;

			switch ( top ) {

				case 0x2:

					if ( node == 2 )
						state.format = static_cast < uint16_t > ( payload );

					return 0;

				case 0xA:
					return node == 2 ? state.format : 0;

				case 0x3:

					if ( node == 2 && ( payload & 0x8000 ) ) {

						for ( int32_t side = 0; side < 2; side++ ) {

							if ( !( payload & ( side == 0 ? 0x2000 : 0x1000 ) ) )
								continue;

							state.gain [ side ] = static_cast < uint8_t > ( base::min ( payload & 0x7F, 0x4Au ) );
							state.mute [ side ] = ( payload & 0x80 ) != 0;

						}

					} else if ( node == 3 && ( payload & 0x8000 ) )
						state.pin_mute = ( payload & 0x80 ) != 0;

					return 0;

				case 0xB:

					if ( node == 2 && ( payload & 0x8000 ) ) {

						int32_t side = payload & 0x2000 ? 0 : 1;

						return ( state.mute [ side ] ? 0x80u : 0u ) | state.gain [ side ];

					}

					if ( node == 3 && ( payload & 0x8000 ) )
						return state.pin_mute ? 0x80u : 0u;

					return 0;

				default:
					return 0;

			}

		}

		uint32_t code = data >> 8;
		uint32_t payload = data & 0xFF;

		switch ( code ) {

			case 0xF00:
				return parameter ( node, payload );

			case 0xF02:
				return node == 3 && payload == 0 ? 0x02u : 0u;

			case 0xF05:
				return node < 4 ? static_cast < uint32_t > ( state.power [ node ] << 4 | state.power [ node ] ) : 0u;

			case 0x705:

				if ( node == 1 ) {

					for ( uint8_t& item : state.power )
						item = static_cast < uint8_t > ( payload & 0x0F );

				} else if ( node < 4 )
					state.power [ node ] = static_cast < uint8_t > ( payload & 0x0F );

				return 0;

			case 0xF06:
				return node == 2 ? state.stream_channel : 0u;

			case 0x706:

				if ( node == 2 )
					state.stream_channel = static_cast < uint8_t > ( payload );

				return 0;

			case 0xF07:
				return node == 3 ? state.pin_control : 0u;

			case 0x707:

				if ( node == 3 )
					state.pin_control = static_cast < uint8_t > ( payload );

				return 0;

			case 0xF08:
				return state.unsolicited;

			case 0x708:
				state.unsolicited = static_cast < uint8_t > ( payload );
				return 0;

			case 0xF09:
				return node == 3 ? 0x80000000u : 0u;

			case 0xF0C:
				return node == 3 ? state.eapd : 0u;

			case 0x70C:

				if ( node == 3 )
					state.eapd = static_cast < uint8_t > ( payload );

				return 0;

			case 0xF1C:
				return node == 3 ? state.config : 0u;

			case 0x71C:
			case 0x71D:
			case 0x71E:
			case 0x71F:

				if ( node == 3 ) {

					uint32_t shift = ( code - 0x71C ) * 8;

					state.config = ( state.config & ~( 0xFFu << shift ) ) | payload << shift;

				}

				return 0;

			case 0xF20:
				return node == 1 ? state.subsystem : 0u;

			case 0x720:
			case 0x721:
			case 0x722:
			case 0x723: {

				uint32_t shift = ( code - 0x720 ) * 8;

				state.subsystem = ( state.subsystem & ~( 0xFFu << shift ) ) | payload << shift;

				return 0;

			}

			case 0x7FF:

				if ( node == 1 ) {

					uint32_t keep_config = state.config;
					uint32_t keep_subsystem = state.subsystem;

					state = codec ( );
					state.config = keep_config;
					state.subsystem = keep_subsystem;

				}

				return 0;

			default:
				return 0;

		}

	}

	void hda::play_stream ( uint32_t index, uint64_t now ) {

		stream& item = streams [ index ];
		uint32_t base = REG_STREAMS + index * stream_stride;
		uint32_t control = get ( base, 3 );

		if ( !( control & 0x02 ) || !item.last || now < item.last ) {

			item.last = now;

			return;

		}

		uint32_t format = get ( base + 0x12, 2 );
		uint32_t rate = ( format & 0x4000 ? 44100u : 48000u ) * ( ( format >> 11 & 7 ) + 1 ) / ( ( format >> 8 & 7 ) + 1 );
		uint32_t channels = ( format & 0x0F ) + 1;
		uint32_t bits = format >> 4 & 7;
		uint32_t sample_bytes = bits == 0 ? 1u : ( bits == 1 ? 2u : 4u );
		uint32_t frame_bytes = channels * sample_bytes;

		item.carry += ( now - item.last ) * rate;
		item.last = now;

		uint64_t frames = item.carry / 1000000000ull;

		item.carry %= 1000000000ull;

		if ( frames > rate / 5 )
			frames = rate / 5;

		uint32_t wanted = static_cast < uint32_t > ( frames ) * frame_bytes;
		uint32_t cyclic = get ( base + 0x08, 4 );
		uint32_t last_index = get ( base + 0x0C, 2 ) & 0xFF;
		uint64_t list = ( get ( base + 0x18, 4 ) & ~0x7Fu ) | static_cast < uint64_t > ( get ( base + 0x1C, 4 ) ) << 32;
		uint32_t position = get ( base + 0x04, 4 );
		uint32_t tag = control >> 20 & 0x0F;
		bool playing = index >= first_output && speaker && tag && tag == static_cast < uint32_t > ( state.stream_channel >> 4 ) && ( state.pin_control & 0x40 ) && !state.pin_mute && !state.power [ 2 ];
		float left = state.mute [ 0 ] ? 0.0f : math::pow ( 10.0f, ( static_cast < float > ( state.gain [ 0 ] ) - 74.0f ) / 20.0f );
		float right = state.mute [ 1 ] ? 0.0f : math::pow ( 10.0f, ( static_cast < float > ( state.gain [ 1 ] ) - 74.0f ) / 20.0f );
		uint32_t made = 0;
		bool completed = false;
		uint8_t data [ 4096 ];

		for ( uint32_t guard = 0; wanted && guard < 64 && list; guard++ ) {

			uint8_t entry [ 16 ];

			vm.read ( list + item.entry * 16ull, entry, 16 );

			uint64_t address = static_cast < uint64_t > ( entry [ 0 ] | entry [ 1 ] << 8 | entry [ 2 ] << 16 ) | static_cast < uint64_t > ( entry [ 3 ] ) << 24 | static_cast < uint64_t > ( entry [ 4 ] | entry [ 5 ] << 8 | entry [ 6 ] << 16 | static_cast < uint32_t > ( entry [ 7 ] ) << 24 ) << 32;
			uint32_t length = static_cast < uint32_t > ( entry [ 8 ] ) | static_cast < uint32_t > ( entry [ 9 ] ) << 8 | static_cast < uint32_t > ( entry [ 10 ] ) << 16 | static_cast < uint32_t > ( entry [ 11 ] ) << 24;
			bool interrupt = ( entry [ 12 ] & 1 ) != 0;

			if ( !length )
				break;

			if ( item.entry_offset >= length )
				item.entry_offset = 0;

			uint32_t chunk = base::min ( base::min ( wanted, length - item.entry_offset ), static_cast < uint32_t > ( sizeof ( data ) ) );

			if ( chunk >= frame_bytes )
				chunk -= chunk % frame_bytes;

			if ( playing && chunk ) {

				vm.read ( address + item.entry_offset, data, chunk );

				for ( uint32_t at = 0; at + frame_bytes <= chunk && made * 2 + 2 <= mixed_room; at += frame_bytes ) {

					float sample [ 2 ] = { };

					for ( uint32_t channel = 0; channel < 2; channel++ ) {

						const uint8_t* where = data + at + ( channel < channels ? channel : 0 ) * sample_bytes;

						if ( sample_bytes == 1 )
							sample [ channel ] = ( static_cast < float > ( where [ 0 ] ) - 128.0f ) / 128.0f;
						else if ( sample_bytes == 2 )
							sample [ channel ] = static_cast < float > ( static_cast < int16_t > ( where [ 0 ] | where [ 1 ] << 8 ) ) / 32768.0f;
						else
							sample [ channel ] = static_cast < float > ( static_cast < int32_t > ( static_cast < uint32_t > ( where [ 0 ] ) | static_cast < uint32_t > ( where [ 1 ] ) << 8 | static_cast < uint32_t > ( where [ 2 ] ) << 16 | static_cast < uint32_t > ( where [ 3 ] ) << 24 ) ) / 2147483648.0f;

					}

					mixed [ made * 2 ] = sample [ 0 ] * left;
					mixed [ made * 2 + 1 ] = sample [ 1 ] * right;
					made++;

				}

			}

			item.entry_offset += chunk;
			position += chunk;
			wanted -= chunk;

			if ( cyclic && position >= cyclic )
				position -= cyclic;

			if ( item.entry_offset >= length ) {

				item.entry_offset = 0;
				item.entry = item.entry >= last_index ? 0 : item.entry + 1;

				if ( interrupt ) {

					regs [ base + 3 ] |= 0x04;
					completed = true;

				}

			}

			if ( !chunk )
				break;

		}

		put ( base + 0x04, 4, position );

		uint32_t buffer = get ( REG_DPLBASE, 4 );

		if ( buffer & 1 ) {

			uint64_t where = ( buffer & ~0x7Fu ) | static_cast < uint64_t > ( get ( REG_DPLBASE + 4, 4 ) ) << 32;
			uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( position ), static_cast < uint8_t > ( position >> 8 ), static_cast < uint8_t > ( position >> 16 ), static_cast < uint8_t > ( position >> 24 ) };

			vm.write ( where + index * 8ull, bytes, 4 );

		}

		if ( made ) {

			speaker->play ( mixed, made, rate );
			InterlockedExchangeAdd ( &frames_played, static_cast < long > ( made ) );

		}

		if ( completed )
			update_line ( );

	}

	void hda::advance ( uint64_t now ) {

		for ( uint32_t i = first_output; i < stream_count; i++ )
			play_stream ( i, now );

	}

	void hda::update_line ( ) {

		uint32_t streams_pending = 0;

		for ( uint32_t i = 0; i < stream_count; i++ ) {

			uint32_t base = REG_STREAMS + i * stream_stride;
			uint8_t status = regs [ base + 3 ];
			uint8_t control = regs [ base ];

			if ( status & control & 0x1C )
				streams_pending |= 1u << i;

		}

		bool controller = ( ( regs [ REG_RIRBSTS ] & 0x01 ) && ( regs [ REG_RIRBCTL ] & 0x01 ) ) || ( ( regs [ REG_RIRBSTS ] & 0x04 ) && ( regs [ REG_RIRBCTL ] & 0x04 ) ) || ( get ( REG_STATESTS, 2 ) & get ( REG_WAKEEN, 2 ) & 0x7FFF ) || ( ( regs [ REG_CORBSTS ] & 0x01 ) && ( regs [ REG_CORBCTL ] & 0x01 ) );
		uint32_t status = streams_pending | ( controller ? 1u << 30 : 0u );

		if ( status )
			status |= 1u << 31;

		put ( REG_INTSTS, 4, status );

		uint32_t enabled = get ( REG_INTCTL, 4 );
		bool wanted = ( enabled & 0x80000000u ) && ( ( streams_pending & enabled ) || ( controller && ( enabled & 0x40000000u ) ) );

		if ( wanted == line )
			return;

		line = wanted;
		bus.set_irq ( irq, wanted );

	}

}