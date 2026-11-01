int09:
	sti
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push ds
	push es
	cld
	mov ax, bda
	mov ds, ax
	in al, ppi_a
	mov ah, al
	in al, ppi_b
	mov bl, al
	or al, 0x80
	out ppi_b, al
	mov al, bl
	out ppi_b, al
	mov al, ah
	cmp al, 0xFF
	jz key_done
	test byte [key_flags2], 0x08
	jz .running
	test al, 0x80
	jnz key_done
	cmp al, 0x45
	jz key_done
	and byte [key_flags2], 0xF7
	jmp key_done
.running:
	mov bl, al
	and bl, 0x7F
	mov bh, al
	and bh, 0x80
	cmp bl, 0x36
	jz .right_shift
	cmp bl, 0x2A
	jz .left_shift
	cmp bl, 0x1D
	jz .control
	cmp bl, 0x38
	jz .alternate
	cmp bl, 0x3A
	jz .caps
	cmp bl, 0x45
	jz .num
	cmp bl, 0x46
	jz .scroll
	test bh, bh
	jz key_translate
	cmp bl, 0x52
	jnz key_done
	and byte [key_flags2], 0x7F
	jmp key_done
.right_shift:
	mov al, 0x01
	jmp .modifier
.left_shift:
	mov al, 0x02
	jmp .modifier
.control:
	mov al, 0x04
	jmp .modifier
.alternate:
	mov al, 0x08
	test bh, bh
	jz .modifier
	and byte [key_flags], 0xF7
	mov al, [key_alt_entry]
	mov byte [key_alt_entry], 0
	test al, al
	jz key_done
	mov ah, 0
	jmp key_store
.modifier:
	test bh, bh
	jnz .modifier_up
	or [key_flags], al
	jmp key_done
.modifier_up:
	not al
	and [key_flags], al
	jmp key_done
.caps:
	mov al, 0x40
	jmp .toggle
.num:
	test byte [key_flags], 0x04
	jz .num_lock
	test bh, bh
	jnz key_done
	or byte [key_flags2], 0x08
	mov al, 0x20
	out pic_command, al
.pause:
	sti
	hlt
	test byte [key_flags2], 0x08
	jnz .pause
	jmp key_exit
.num_lock:
	mov al, 0x20
	jmp .toggle
.scroll:
	test byte [key_flags], 0x04
	jz .scroll_lock
	test bh, bh
	jnz key_done
	mov ax, [key_start]
	mov [key_head], ax
	mov [key_tail], ax
	mov byte [break_flag], 0x80
	int 0x1B
	xor ax, ax
	jmp key_store
.scroll_lock:
	mov al, 0x10
.toggle:
	test bh, bh
	jnz .toggle_up
	test [key_flags2], al
	jnz key_done
	or [key_flags2], al
	xor [key_flags], al
	jmp key_done
.toggle_up:
	not al
	and [key_flags2], al
	jmp key_done

key_translate:
	cmp bl, 0x54
	jnb key_done
	mov al, [key_flags]
	test al, 0x08
	jnz key_alt
	test al, 0x04
	jnz key_control
	cmp bl, 0x3B
	jb .not_function
	cmp bl, 0x44
	ja .not_function
	mov ah, bl
	test al, 0x03
	jz .function
	add ah, 0x54 - 0x3B
.function:
	xor al, al
	jmp key_store
.not_function:
	cmp bl, 0x47
	jb .main
	cmp bl, 0x4A
	jz .main
	cmp bl, 0x4E
	jz .main
	mov cl, al
	and cl, 0x20
	test al, 0x03
	jz .keypad
	xor cl, 0x20
.keypad:
	test cl, cl
	jnz .digit
	cmp bl, 0x52
	jnz .cursor
	test byte [key_flags2], 0x80
	jnz key_done
	or byte [key_flags2], 0x80
	xor byte [key_flags], 0x80
.cursor:
	cmp bl, 0x4C
	jz key_done
	mov ah, bl
	xor al, al
	jmp key_store
.digit:
	mov bh, 0
	mov al, [cs:bx + keypad_digits - 0x47]
	mov ah, bl
	jmp key_store
.main:
	mov bh, 0
	test byte [key_flags], 0x03
	jz .normal
	cmp bl, 0x0F
	jnz .not_tab
	mov ax, 0x0F00
	jmp key_store
.not_tab:
	cmp bl, 0x37
	jnz .shifted
	int 0x05
	jmp key_done
.shifted:
	mov al, [cs:bx + key_shift]
	jmp .letter
.normal:
	mov al, [cs:bx + key_normal]
.letter:
	call caps_invert
	test al, al
	jz key_done
	mov ah, bl
	jmp key_store

key_control:
	cmp bl, 0x3B
	jb .not_function
	cmp bl, 0x44
	ja .not_function
	mov ah, bl
	add ah, 0x5E - 0x3B
	xor al, al
	jmp key_store
.not_function:
	cmp bl, 0x47
	jb .table
	mov bh, 0
	mov ah, [cs:bx + control_keypad - 0x47]
	test ah, ah
	jz key_done
	xor al, al
	jmp key_store
.table:
	cmp bl, 0x03
	jnz .not_nul
	mov ax, 0x0300
	jmp key_store
.not_nul:
	cmp bl, 0x37
	jnz .lookup
	mov ax, 0x7200
	jmp key_store
.lookup:
	mov bh, 0
	mov al, [cs:bx + key_control_table]
	cmp al, 0xFF
	jz key_done
	mov ah, bl
	jmp key_store

key_alt:
	test byte [key_flags], 0x04
	jz .no_control
	cmp bl, 0x53
	jnz .no_control
	mov word [reset_flag], 0x1234
	jmp rom_segment:post_entry
.no_control:
	cmp bl, 0x47
	jb .not_keypad
	mov bh, 0
	mov al, [cs:bx + keypad_digits - 0x47]
	cmp al, '0'
	jb key_done
	cmp al, '9'
	ja key_done
	sub al, '0'
	mov cl, al
	mov al, [key_alt_entry]
	mov ah, 10
	mul ah
	add al, cl
	mov [key_alt_entry], al
	jmp key_done
.not_keypad:
	cmp bl, 0x3B
	jb .not_function
	cmp bl, 0x44
	ja .not_function
	mov ah, bl
	add ah, 0x68 - 0x3B
	xor al, al
	jmp key_store
.not_function:
	cmp bl, 0x02
	jb key_done
	cmp bl, 0x0D
	ja .letters
	mov ah, bl
	add ah, 0x76
	xor al, al
	jmp key_store
.letters:
	mov bh, 0
	mov al, [cs:bx + key_normal]
	cmp al, 'a'
	jb .space
	cmp al, 'z'
	ja key_done
	mov ah, bl
	xor al, al
	jmp key_store
.space:
	cmp bl, 0x39
	jnz key_done
	mov ax, 0x3920

key_store:
	call stuff_key

key_done:
	mov al, 0x20
	out pic_command, al

key_exit:
	pop es
	pop ds
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	iret

caps_invert:
	test byte [key_flags], 0x40
	jz .done
	cmp al, 'a'
	jb .upper
	cmp al, 'z'
	jbe .flip
.upper:
	cmp al, 'A'
	jb .done
	cmp al, 'Z'
	ja .done
.flip:
	xor al, 0x20
.done:
	ret

stuff_key:
	push bx
	push si
	mov bx, [key_tail]
	mov si, bx
	add si, 2
	cmp si, [key_end]
	jb .room
	mov si, [key_start]
.room:
	cmp si, [key_head]
	jz .full
	mov [bx], ax
	mov [key_tail], si
	pop si
	pop bx
	clc
	ret
.full:
	pop si
	pop bx
	stc
	ret

int16:
	sti
	push bp
	push es
	push ds
	push di
	push si
	push dx
	push cx
	push bx
	push ax
	mov bp, sp
	mov si, bda
	mov ds, si
	cmp ah, 0x00
	jz .read
	cmp ah, 0x10
	jz .read
	cmp ah, 0x01
	jz .peek
	cmp ah, 0x11
	jz .peek
	cmp ah, 0x02
	jz .flags
	cmp ah, 0x12
	jz .flags
	cmp ah, 0x05
	jz .store
	jmp service_exit
.read:
	cli
	mov bx, [key_head]
	cmp bx, [key_tail]
	jnz .take
	sti
	hlt
	jmp .read
.take:
	mov ax, [bx]
	add bx, 2
	cmp bx, [key_end]
	jb .keep
	mov bx, [key_start]
.keep:
	mov [key_head], bx
	sti
	mov [bp+frame_ax], ax
	jmp service_exit
.peek:
	cli
	mov bx, [key_head]
	cmp bx, [key_tail]
	jz .empty
	mov ax, [bx]
	sti
	mov [bp+frame_ax], ax
	call clear_zero
	jmp service_exit
.empty:
	sti
	call set_zero
	jmp service_exit
.flags:
	mov al, [key_flags]
	mov [bp+frame_al], al
	cmp ah, 0x12
	jnz .flags_done
	mov al, [key_flags2]
	mov [bp+frame_ah], al
.flags_done:
	jmp service_exit
.store:
	mov ax, cx
	call stuff_key
	mov byte [bp+frame_al], 0
	jnc .stored
	mov byte [bp+frame_al], 1
.stored:
	jmp service_exit

key_normal:
	db 0, 0x1B, "1234567890-=", 0x08, 0x09
	db "qwertyuiop[]", 0x0D, 0
	db "asdfghjkl;", 0x27, "`", 0, 0x5C
	db "zxcvbnm,./", 0, "*", 0, " ", 0
	db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	db 0, 0
	db 0, 0, 0, "-", 0, 0, 0, "+", 0, 0, 0, 0, 0

key_shift:
	db 0, 0x1B, "!@#$%^&*()_+", 0x08, 0
	db "QWERTYUIOP{}", 0x0D, 0
	db "ASDFGHJKL:", 0x22, "~", 0, "|"
	db "ZXCVBNM<>?", 0, 0, 0, " ", 0
	db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	db 0, 0
	db 0, 0, 0, "-", 0, 0, 0, "+", 0, 0, 0, 0, 0

key_control_table:
	db 0xFF, 0x1B, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x1E, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0x7F, 0xFF
	db 0x11, 0x17, 0x05, 0x12, 0x14, 0x19, 0x15, 0x09, 0x0F, 0x10, 0x1B, 0x1D, 0x0A, 0xFF
	db 0x01, 0x13, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0xFF, 0xFF, 0xFF, 0xFF, 0x1C
	db 0x1A, 0x18, 0x03, 0x16, 0x02, 0x0E, 0x0D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0xFF
	db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	db 0xFF, 0xFF

control_keypad:
	db 0x77, 0, 0x84, 0, 0x73, 0, 0x74, 0, 0x75, 0, 0x76, 0, 0

keypad_digits:
	db "789-456+1230."