#pragma once

#include "net/nat.hh"
#include <windows.h>

namespace pc {

	enum tools_message : uint32_t {

		TOOLS_HELLO = 1,
		TOOLS_POLICY,
		TOOLS_TEXT,
		TOOLS_FILES_BEGIN,
		TOOLS_FILE,
		TOOLS_DATA,
		TOOLS_FILES_END,
		TOOLS_FOLDER,

	};

	enum tools_share : uint32_t {

		SHARE_TEXT_IN = 1,
		SHARE_TEXT_OUT = 2,
		SHARE_FILES_IN = 4,
		SHARE_FILES_OUT = 8,

	};

	enum tools_purpose : uint32_t {

		PURPOSE_CLIPBOARD,
		PURPOSE_SENT,

	};

	enum tools_event_kind : uint8_t {

		TOOLS_NONE,
		TOOLS_CONNECTED,
		TOOLS_DISCONNECTED,
		TOOLS_GOT_TEXT,
		TOOLS_GOT_FILES,
		TOOLS_SENT,
		TOOLS_REFUSED,

	};

	static constexpr uint32_t tools_version = 1;
	static constexpr uint32_t tools_chunk = 60000;
	static constexpr uint32_t tools_frame_limit = 0x1000000;
	static constexpr uint64_t clipboard_file_limit = 1ull << 30;

	struct tools_entry {

		wchar_t source [ MAX_PATH ] = { };
		wchar_t name [ 260 ] = { };
		uint64_t size = 0;
		bool folder = false;

	};

	struct tools_event {

		tools_event_kind kind = TOOLS_NONE;
		uint32_t purpose = 0;
		uint32_t count = 0;
		base::list < wchar_t > text;
		base::list < wchar_t > items;

	};

	bool tools_clean_name ( const wchar_t* name, uint32_t length, wchar_t* out, uint32_t room );

	bool tools_collect ( const wchar_t* source, const wchar_t* name, base::list < tools_entry >& out, uint64_t& total );

	class guest_tools : public net::service {

	public:

		guest_tools ( ) = default;

		~guest_tools ( ) override;

		void set_policy ( uint32_t flags );

		void set_inbox ( const wchar_t* folder );

		bool connected ( );

		bool busy ( );

		bool send_text ( const wchar_t* text, uint32_t length );

		bool send_files ( const wchar_t ( *paths ) [ MAX_PATH ], int32_t count, uint32_t purpose, uint64_t limit );

		bool next_event ( tools_event& out );

		void opened ( ) override;

		void received ( const uint8_t* data, uint32_t size ) override;

		uint32_t produce ( uint8_t* out, uint32_t room ) override;

		void closed ( ) override;

		uint32_t current_policy ( ) {

			AcquireSRWLockShared ( &lock );

			uint32_t value = policy;

			ReleaseSRWLockShared ( &lock );

			return value;

		}

		net::nat* router = nullptr;
		wchar_t guest_name [ 64 ] = { };
		bool agent = false;

	private:

		void frame ( uint32_t type, const void* data, uint32_t size );

		void handle ( uint32_t type, const uint8_t* data, uint32_t size );

		void push_event ( tools_event&& item );

		void finish_receive ( bool keep );

		void stop_sending ( );

		SRWLOCK lock = SRWLOCK_INIT;
		uint32_t policy = 0;
		bool linked = false;
		base::list < uint8_t > inbound;
		base::list < uint8_t > outbound;
		uint32_t outbound_head = 0;
		base::list < tools_entry > sending;
		uint32_t send_index = 0;
		uint32_t send_purpose = 0;
		uint64_t send_total = 0;
		HANDLE send_file = INVALID_HANDLE_VALUE;
		bool send_started = false;
		bool send_active = false;
		uint8_t* chunk = nullptr;
		wchar_t inbox [ MAX_PATH ] = { };
		uint32_t inbox_serial = 0;
		bool receiving = false;
		bool receive_allowed = false;
		uint32_t receive_purpose = 0;
		uint64_t receive_total = 0;
		uint64_t received_bytes = 0;
		wchar_t receive_folder [ MAX_PATH ] = { };
		HANDLE receive_file = INVALID_HANDLE_VALUE;
		uint64_t receive_left = 0;
		base::list < wchar_t > receive_items;
		uint32_t receive_count = 0;
		base::list < tools_event > events;

	};

}