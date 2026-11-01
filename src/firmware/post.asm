post_start:
	cli
	cld
	xor al, al
	out nmi_port, al
	mov al, 0x01
	out post_port, al
	xor ax, ax
	mov ss, ax
	mov sp, 0x0800
	mov ds, ax
	mov es, ax
	mov bp, [0x0472]

	mov al, 0x02
	out post_port, al
	mov si, 0xC000
	mov cx, 0x4000
	xor ah, ah
.checksum:
	add ah, [cs:si]
	inc si
	loop .checksum
	push ax

	mov al, 0x03
	out post_port, al
	out 0x0D, al
	mov al, 0x58
	out 0x0B, al
	mov al, 0x41
	out 0x0B, al
	mov al, 0x42
	out 0x0B, al
	mov al, 0x43
	out 0x0B, al
	xor al, al
	out 0x0C, al
	out 0x00, al
	out 0x00, al
	mov al, 0xFF
	out 0x01, al
	out 0x01, al
	xor al, al
	out 0x08, al
	out 0x0A, al
	mov al, 0x54
	out pit_control, al
	mov al, 18
	out pit_counter1, al

	mov al, 0x04
	out post_port, al
	mov al, 0x36
	out pit_control, al
	xor al, al
	out pit_counter0, al
	out pit_counter0, al
	mov al, 0x99
	out ppi_control, al
	mov al, 0x48
	out ppi_b, al

	mov al, 0x05
	out post_port, al
	mov al, 0x13
	out pic_command, al
	mov al, 0x08
	out pic_data, al
	mov al, 0x09
	out pic_data, al
	mov al, 0xFF
	out pic_data, al

	mov al, 0x06
	out post_port, al
	xor di, di
	mov cx, 0x0280
	xor ax, ax
	rep stosw
	xor di, di
	mov cx, 256
.vectors:
	mov ax, dummy_iret
	stosw
	mov ax, rom_segment
	stosw
	loop .vectors
	mov di, 0x08 * 4
	mov si, vector_table
	mov cx, 24
.table:
	mov ax, [cs:si]
	add si, 2
	stosw
	mov ax, rom_segment
	stosw
	loop .table
	mov word [es:0x02 * 4], int02
	mov word [es:0x05 * 4], int05

	mov al, 0x07
	out post_port, al
	mov ax, bda
	mov ds, ax
	mov [reset_flag], bp
	mov word [key_head], key_buffer
	mov word [key_tail], key_buffer
	mov word [key_start], key_buffer
	mov word [key_end], key_buffer_end
	mov word [disk_track], 0xFFFF
	pop ax
	mov [post_rom], ah

	mov al, 0x40
	out ppi_b, al
	in al, ppi_c
	and al, 0x0F
	mov ah, al
	mov al, 0x48
	out ppi_b, al
	in al, ppi_c
	and al, 0x0F
	mov cl, 4
	shl al, cl
	or al, ah
	mov ah, 0
	mov [bda_equipment], ax

	mov bx, 0x1000
.probe:
	cmp bx, 0xA000
	jnb .sized
	mov es, bx
	mov ax, [es:0]
	mov word [es:0], 0xA55A
	cmp word [es:0], 0xA55A
	mov [es:0], ax
	jnz .sized
	add bx, 0x0400
	jmp .probe
.sized:
	mov ax, bx
	mov cl, 6
	shr ax, cl
	mov [bda_memory], ax
	call drive_count
	mov [post_drives], al

	mov al, 0x08
	out post_port, al
	mov al, [bda_equipment]
	and al, 0x30
	mov bl, 3
	cmp al, 0x10
	jnz .mode
	mov bl, 1
.mode:
	mov al, bl
	mov ah, 0
	int 0x10
	mov cx, 0x2000
	mov ah, 0x01
	int 0x10
	call post_screen

	mov al, 0x09
	out post_port, al
	mov al, 0x08
	out ppi_b, al
	mov cx, 0x2000
.hold:
	loop .hold
	mov al, 0xC8
	out ppi_b, al
	mov al, 0x48
	out ppi_b, al
	mov byte [post_keyboard], 0
	xor cx, cx
.answer:
	in al, ppi_a
	cmp al, 0xAA
	jz .keyboard_ok
	loop .answer
	jmp .keyboard_done
.keyboard_ok:
	mov byte [post_keyboard], 1
.keyboard_done:
	mov al, 0xC8
	out ppi_b, al
	mov al, 0x48
	out ppi_b, al

	mov al, 0x0A
	out post_port, al
	call memory_test

	mov al, 0x0B
	out post_port, al
	mov al, 0xBC
	out pic_data, al
	sti
	xor ax, ax
	xor dx, dx
	int 0x13
	call post_results
	call beep

	mov al, 0x0C
	out post_port, al
	call boot_menu
	mov cx, 0x0607
	mov ah, 0x01
	int 0x10
	mov ah, 0x02
	xor bh, bh
	mov dx, 0x0D00
	int 0x10
	mov word [reset_flag], 0
	mov al, 0x0D
	out post_port, al
	int 0x19

screen_offset:
	push ax
	push dx
	mov al, dh
	mul byte [video_columns]
	mov dh, 0
	add ax, dx
	shl ax, 1
	mov di, ax
	pop dx
	pop ax
	ret

put_string:
	push ax
	push di
	push es
	mov ax, 0xB800
	mov es, ax
	call screen_offset
.next:
	mov al, [cs:si]
	inc si
	test al, al
	jz .done
	mov ah, bl
	stosw
	inc dl
	jmp .next
.done:
	pop es
	pop di
	pop ax
	ret

put_repeat:
	push ax
	push cx
	push di
	push es
	mov di, 0xB800
	mov es, di
	call screen_offset
	mov ah, bl
	rep stosw
	pop es
	pop di
	pop cx
	pop ax
	ret

put_number:
	push ax
	push bx
	push cx
	push dx
	push di
	push es
	push bp
	mov bp, 0xB800
	mov es, bp
	call screen_offset
	mov ch, 0
	mov bp, 10
.divide:
	xor dx, dx
	div bp
	push dx
	inc ch
	test ax, ax
	jnz .divide
	mov al, cl
	sub al, ch
	jbe .digits
.pad:
	push ax
	mov al, ' '
	mov ah, bl
	stosw
	pop ax
	dec al
	jnz .pad
.digits:
	pop ax
	add al, '0'
	mov ah, bl
	stosw
	dec ch
	jnz .digits
	pop bp
	pop es
	pop di
	pop dx
	pop cx
	pop bx
	pop ax
	ret

post_label:
	mov dl, 2
	mov bl, attr_label
	jmp put_string

post_value:
	mov dl, 14
	mov bl, attr_value
	jmp put_string

post_screen:
	mov dh, 1
	mov dl, 2
	mov bl, attr_title
	mov si, text_title
	call put_string
	mov dl, [video_columns]
	sub dl, 13
	mov bl, attr_dim
	mov si, text_version
	call put_string
	mov dh, 2
	mov dl, 2
	mov al, 0xC4
	mov cl, [video_columns]
	sub cl, 4
	mov ch, 0
	mov bl, attr_dim
	call put_repeat
	mov dh, 4
	mov si, text_processor
	call post_label
	mov si, text_cpu
	call post_value
	mov dh, 5
	mov si, text_memory
	call post_label
	mov dh, 6
	mov si, text_video
	call post_label
	mov si, text_cga80
	cmp byte [video_columns], 80
	jz .wide
	mov si, text_cga40
.wide:
	call post_value
	mov dh, 7
	mov si, text_floppy
	call post_label
	mov si, text_no_drives
	cmp byte [post_drives], 0
	jz .drives
	mov si, text_one_drive
	cmp byte [post_drives], 1
	jz .drives
	mov si, text_two_drives
.drives:
	call post_value
	mov dh, 8
	mov si, text_keyboard
	call post_label
	mov dh, 9
	mov si, text_rom
	call post_label
	xor ax, ax
	call show_memory
	ret

show_memory:
	push bx
	push cx
	push dx
	push si
	mov dh, 5
	mov dl, 14
	mov cl, 3
	mov bl, attr_value
	call put_number
	mov dl, 17
	mov si, text_kb
	call put_string
	pop si
	pop dx
	pop cx
	pop bx
	ret

memory_test:
	push es
	mov cx, [bda_memory]
	cmp word [reset_flag], 0x1234
	jz .final
	xor bx, bx
	xor dx, dx
.block:
	cmp bx, cx
	jnb .final
	push cx
	push dx
	push bx
	mov es, dx
	xor di, di
	mov cx, 0x2000
	test dx, dx
	jnz .fill
	mov di, 0x0800
	mov cx, ( 0x4000 - 0x0800 ) / 2
.fill:
	push cx
	push di
	mov ax, 0xAA55
	rep stosw
	pop di
	pop cx
	push cx
	push di
	repe scasw
	pop di
	pop cx
	jnz .error
	xor ax, ax
	rep stosw
	pop bx
	pop dx
	pop cx
	add bx, 16
	add dx, 0x0400
	mov ax, bx
	call show_memory
	jmp .block
.error:
	pop bx
	pop dx
	pop cx
	mov [bda_memory], bx
	mov ax, bx
	call show_memory
	mov dh, 5
	mov dl, 20
	mov bl, attr_bad
	mov si, text_memory_bad
	call put_string
	pop es
	ret
.final:
	mov ax, [bda_memory]
	call show_memory
	mov dh, 5
	mov dl, 21
	mov bl, attr_good
	mov si, text_ok
	call put_string
	pop es
	ret

post_results:
	mov dh, 8
	mov dl, 14
	mov bl, attr_good
	mov si, text_ok
	cmp byte [post_keyboard], 0
	jnz .keyboard
	mov bl, attr_bad
	mov si, text_failed
.keyboard:
	call put_string
	mov dh, 9
	mov dl, 14
	mov bl, attr_good
	mov si, text_checksum_ok
	cmp byte [post_rom], 0
	jz .rom
	mov bl, attr_bad
	mov si, text_checksum_bad
.rom:
	call put_string
	ret

clear_status:
	push ax
	push cx
	push dx
	mov dh, 11
.row:
	mov dl, 0
	mov al, ' '
	mov cl, [video_columns]
	mov ch, 0
	mov bl, attr_label
	call put_repeat
	inc dh
	cmp dh, 16
	jb .row
	pop dx
	pop cx
	pop ax
	ret

boot_menu:
	mov byte [boot_drive], 0
	mov dh, 11
	mov dl, 2
	mov bl, attr_key
	mov si, text_escape
	call put_string
	mov bl, attr_dim
	mov si, text_menu_hint
	call put_string
	mov bx, [ticks_low]
.wait:
	mov ah, 0x01
	int 0x16
	jz .quiet
	xor ah, ah
	int 0x16
	cmp ah, 0x01
	jz .menu
.quiet:
	mov ax, [ticks_low]
	sub ax, bx
	cmp ax, 18
	jnb .boot
	hlt
	jmp .wait
.menu:
	call clear_status
	mov dh, 11
	mov dl, 2
	mov bl, attr_title
	mov si, text_boot_from
	call put_string
	mov dh, 12
	mov dl, 2
	mov bl, attr_key
	mov si, text_key_a
	call put_string
	mov bl, attr_label
	mov si, text_drive_a
	call put_string
	mov dh, 13
	mov dl, 2
	mov bl, attr_key
	mov si, text_key_b
	call put_string
	mov bl, attr_label
	mov si, text_drive_b
	call put_string
	mov dh, 15
	mov dl, 2
	mov bl, attr_dim
	mov si, text_choose
	call put_string
.choose:
	xor ah, ah
	int 0x16
	cmp al, 0x0D
	jz .boot
	cmp al, 0x1B
	jz .boot
	or al, 0x20
	cmp al, 'a'
	jz .boot
	cmp al, 'b'
	jnz .choose
	mov byte [boot_drive], 1
.boot:
	call clear_status
	mov dh, 11
	mov dl, 2
	mov bl, attr_label
	mov si, text_starting_a
	cmp byte [boot_drive], 0
	jz .starting
	mov si, text_starting_b
.starting:
	call put_string
	ret

text_title:
	db "Coldboot BIOS", 0
text_version:
	db "Version 1.0", 0
text_processor:
	db "Processor", 0
text_memory:
	db "Memory", 0
text_video:
	db "Video", 0
text_floppy:
	db "Floppy", 0
text_keyboard:
	db "Keyboard", 0
text_rom:
	db "ROM", 0
text_cpu:
	db "Intel 8088, 4.77 MHz", 0
text_cga80:
	db "CGA, 80 x 25 color", 0
text_cga40:
	db "CGA, 40 x 25 color", 0
text_one_drive:
	db "1 drive", 0
text_two_drives:
	db "2 drives", 0
text_no_drives:
	db "None", 0
text_kb:
	db " KB", 0
text_ok:
	db "OK", 0
text_failed:
	db "Not responding", 0
text_checksum_ok:
	db "Checksum OK", 0
text_checksum_bad:
	db "Checksum error", 0
text_memory_bad:
	db "Error", 0
text_escape:
	db " Esc ", 0
text_menu_hint:
	db "  Boot menu", 0
text_boot_from:
	db "Boot from", 0
text_key_a:
	db " A ", 0
text_key_b:
	db " B ", 0
text_drive_a:
	db "  Floppy drive A", 0
text_drive_b:
	db "  Floppy drive B", 0
text_choose:
	db "Press A or B", 0
text_starting_a:
	db "Starting from drive A", 0
text_starting_b:
	db "Starting from drive B", 0