#include "app/audio.hh"
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace app {

	static const GUID float_format = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
	static const GUID pcm_format = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

	static bool same_guid ( const GUID& a, const GUID& b ) {

		const uint8_t* x = reinterpret_cast < const uint8_t* > ( &a );
		const uint8_t* y = reinterpret_cast < const uint8_t* > ( &b );

		for ( int32_t i = 0; i < 16; i++ ) {

			if ( x [ i ] != y [ i ] )
				return false;

		}

		return true;

	}

	bool audio::start ( ) {

		if ( thread )
			return rate != 0;

		quit = 0;
		ready = CreateEventW ( nullptr, TRUE, FALSE, nullptr );
		thread = CreateThread ( nullptr, 0, &loop, this, 0, nullptr );

		if ( !thread )
			return false;

		WaitForSingleObject ( ready, 2000 );

		return rate != 0;

	}

	void audio::stop ( ) {

		if ( !thread )
			return;

		InterlockedExchange ( &quit, 1 );
		WaitForSingleObject ( thread, 2000 );
		CloseHandle ( thread );
		CloseHandle ( ready );
		thread = nullptr;
		ready = nullptr;

	}

	void audio::push ( const float* samples, int32_t total ) {

		AcquireSRWLockExclusive ( &lock );

		for ( int32_t i = 0; i < total; i++ ) {

			if ( count == ring_size ) {

				head = ( head + 1 ) % ring_size;
				count--;

			}

			ring [ ( head + count ) % ring_size ] = samples [ i ];
			count++;

		}

		uint32_t limit = rate / 6;

		if ( limit && count > limit ) {

			head = ( head + count - limit / 2 ) % ring_size;
			count = limit / 2;

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	void audio::push_stereo ( const float* frames, int32_t total, uint32_t source_rate ) {

		if ( !rate || !source_rate )
			return;

		double step = static_cast < double > ( source_rate ) / static_cast < double > ( rate );

		AcquireSRWLockExclusive ( &lock );

		for ( int32_t i = 0; i < total; i++ ) {

			const float* frame = frames + i * 2;

			while ( phase <= 1.0 ) {

				float mix = static_cast < float > ( phase );

				if ( stereo_count == ring_size ) {

					stereo_head = ( stereo_head + 1 ) % ring_size;
					stereo_count--;

				}

				uint32_t slot = ( stereo_head + stereo_count ) % ring_size;

				stereo [ slot * 2 ] = previous [ 0 ] + ( frame [ 0 ] - previous [ 0 ] ) * mix;
				stereo [ slot * 2 + 1 ] = previous [ 1 ] + ( frame [ 1 ] - previous [ 1 ] ) * mix;
				stereo_count++;
				phase += step;

			}

			phase -= 1.0;
			previous [ 0 ] = frame [ 0 ];
			previous [ 1 ] = frame [ 1 ];

		}

		uint32_t limit = rate / 5;

		if ( limit && stereo_count > limit ) {

			stereo_head = ( stereo_head + stereo_count - limit / 2 ) % ring_size;
			stereo_count = limit / 2;

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	int32_t audio::take_stereo ( float* out, int32_t wanted ) {

		AcquireSRWLockExclusive ( &lock );

		int32_t taken = static_cast < int32_t > ( base::min ( static_cast < uint32_t > ( wanted ), stereo_count ) );

		for ( int32_t i = 0; i < taken; i++ ) {

			uint32_t slot = ( stereo_head + static_cast < uint32_t > ( i ) ) % ring_size;

			out [ i * 2 ] = stereo [ slot * 2 ];
			out [ i * 2 + 1 ] = stereo [ slot * 2 + 1 ];

		}

		stereo_head = ( stereo_head + static_cast < uint32_t > ( taken ) ) % ring_size;
		stereo_count -= static_cast < uint32_t > ( taken );
		ReleaseSRWLockExclusive ( &lock );

		return taken;

	}

	int32_t audio::take ( float* out, int32_t wanted ) {

		AcquireSRWLockExclusive ( &lock );

		int32_t taken = static_cast < int32_t > ( base::min ( static_cast < uint32_t > ( wanted ), count ) );

		for ( int32_t i = 0; i < taken; i++ )
			out [ i ] = ring [ ( head + static_cast < uint32_t > ( i ) ) % ring_size ];

		head = ( head + static_cast < uint32_t > ( taken ) ) % ring_size;
		count -= static_cast < uint32_t > ( taken );
		ReleaseSRWLockExclusive ( &lock );

		return taken;

	}

	DWORD __stdcall audio::loop ( void* self ) {

		static_cast < audio* > ( self )->run ( );

		return 0;

	}

	void audio::run ( ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );

		IMMDeviceEnumerator* devices = nullptr;
		IMMDevice* device = nullptr;
		IAudioClient* client = nullptr;
		IAudioRenderClient* render = nullptr;
		WAVEFORMATEX* format = nullptr;
		bool floating = false;
		uint32_t channels = 0;
		UINT32 frames = 0;

		bool ok = SUCCEEDED ( CoCreateInstance ( __uuidof ( MMDeviceEnumerator ), nullptr, CLSCTX_ALL, __uuidof ( IMMDeviceEnumerator ), reinterpret_cast < void** > ( &devices ) ) );

		ok = ok && SUCCEEDED ( devices->GetDefaultAudioEndpoint ( eRender, eConsole, &device ) );
		ok = ok && SUCCEEDED ( device->Activate ( __uuidof ( IAudioClient ), CLSCTX_ALL, nullptr, reinterpret_cast < void** > ( &client ) ) );
		ok = ok && SUCCEEDED ( client->GetMixFormat ( &format ) );

		if ( ok ) {

			channels = format->nChannels;

			if ( format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32 )
				floating = true;
			else if ( format->wFormatTag == WAVE_FORMAT_EXTENSIBLE ) {

				WAVEFORMATEXTENSIBLE* extended = reinterpret_cast < WAVEFORMATEXTENSIBLE* > ( format );

				if ( same_guid ( extended->SubFormat, float_format ) && format->wBitsPerSample == 32 )
					floating = true;
				else if ( !same_guid ( extended->SubFormat, pcm_format ) || format->wBitsPerSample != 16 )
					ok = false;

			} else if ( format->wFormatTag != WAVE_FORMAT_PCM || format->wBitsPerSample != 16 )
				ok = false;

		}

		ok = ok && SUCCEEDED ( client->Initialize ( AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, nullptr ) );
		ok = ok && SUCCEEDED ( client->GetBufferSize ( &frames ) );
		ok = ok && SUCCEEDED ( client->GetService ( __uuidof ( IAudioRenderClient ), reinterpret_cast < void** > ( &render ) ) );

		if ( ok )
			rate = format->nSamplesPerSec;

		SetEvent ( ready );

		if ( ok && SUCCEEDED ( client->Start ( ) ) ) {

			float* block = static_cast < float* > ( runtime_allocate ( sizeof ( float ) * frames ) );
			float* pairs = static_cast < float* > ( runtime_allocate ( sizeof ( float ) * frames * 2 ) );
			float previous_in = 0.0f;
			float previous_out = 0.0f;
			float last = 0.0f;

			while ( !quit ) {

				Sleep ( 5 );

				UINT32 padding = 0;

				if ( FAILED ( client->GetCurrentPadding ( &padding ) ) )
					break;

				UINT32 room = frames - padding;
				UINT32 wanted = base::min ( room, rate / 50 );

				if ( !wanted )
					continue;

				BYTE* data = nullptr;

				if ( FAILED ( render->GetBuffer ( wanted, &data ) ) )
					break;

				int32_t got = take ( block, static_cast < int32_t > ( wanted ) );
				int32_t paired = take_stereo ( pairs, static_cast < int32_t > ( wanted ) );

				for ( UINT32 i = 0; i < wanted; i++ ) {

					float input = static_cast < int32_t > ( i ) < got ? block [ i ] : last;
					float output = input - previous_in + 0.995f * previous_out;

					last = input;
					previous_in = input;
					previous_out = output;
					output *= 0.3f;

					for ( uint32_t c = 0; c < channels; c++ ) {

						float value = output;

						if ( static_cast < int32_t > ( i ) < paired )
							value += pairs [ i * 2 + ( c & 1 ) ];

						value = value > 1.0f ? 1.0f : ( value < -1.0f ? -1.0f : value );

						if ( floating )
							reinterpret_cast < float* > ( data ) [ i * channels + c ] = value;
						else
							reinterpret_cast < int16_t* > ( data ) [ i * channels + c ] = static_cast < int16_t > ( value * 32767.0f );

					}

				}

				render->ReleaseBuffer ( wanted, 0 );

			}

			client->Stop ( );
			runtime_release ( block );
			runtime_release ( pairs );

		}

		if ( format )
			CoTaskMemFree ( format );

		if ( render )
			render->Release ( );

		if ( client )
			client->Release ( );

		if ( device )
			device->Release ( );

		if ( devices )
			devices->Release ( );

		CoUninitialize ( );

	}

}