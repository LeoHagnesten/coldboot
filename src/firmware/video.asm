int10:
	sti
	cld
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
	cmp ah, 0x0E
	jz video_teletype
	cmp ah, 0x00
	jz video_set_mode
	cmp ah, 0x01
	jz video_cursor_type
	cmp ah, 0x02
	jz video_set_cursor
	cmp ah, 0x03
	jz video_get_cursor
	cmp ah, 0x04
	jz video_light_pen
	cmp ah, 0x05
	jz video_select_page
	cmp ah, 0x06
	jz video_scroll_up
	cmp ah, 0x07
	jz video_scroll_down
	cmp ah, 0x08
	jz video_read_cell
	cmp ah, 0x09
	jz video_write_cell
	cmp ah, 0x0A
	jz video_write_char
	cmp ah, 0x0B
	jz video_palette_set
	cmp ah, 0x0C
	jz video_write_pixel
	cmp ah, 0x0D
	jz video_read_pixel
	cmp ah, 0x0F
	jz video_get_mode
	jmp service_exit

graphics_mode:
	cmp byte [video_mode], 4
	jb .text
	cmp byte [video_mode], 7
	jnb .text
	cmp byte [video_mode], 0
	ret
.text:
	cmp al, al
	ret

text_offset:
	push ax
	push dx
	mov al, bh
	mov ah, 0
	mul word [video_page_size]
	mov di, ax
	pop dx
	push dx
	mov al, dh
	mov ah, 0
	mul byte [video_columns]
	mov dh, 0
	add ax, dx
	shl ax, 1
	add di, ax
	pop dx
	pop ax
	ret

update_cursor:
	push ax
	push bx
	push dx
	mov bl, [video_page]
	mov bh, 0
	shl bx, 1
	mov dx, [bx+video_cursor]
	mov al, dh
	mov ah, 0
	mul byte [video_columns]
	mov dh, 0
	add ax, dx
	mov bx, [video_page_start]
	shr bx, 1
	add bx, ax
	mov dx, [video_port]
	mov al, 0x0E
	out dx, al
	inc dx
	mov al, bh
	out dx, al
	dec dx
	mov al, 0x0F
	out dx, al
	inc dx
	mov al, bl
	out dx, al
	pop dx
	pop bx
	pop ax
	ret

video_set_mode:
	and al, 0x7F
	cmp al, 7
	jb .known
	mov al, 3
.known:
	mov [video_mode], al
	mov bl, al
	mov bh, 0
	mov al, [cs:bx+video_table_columns]
	mov ah, 0
	mov [video_columns], ax
	mov al, [cs:bx+video_table_modes]
	mov [video_mode_reg], al
	mov al, [cs:bx+video_table_sets]
	mov ah, 0
	mov si, ax
	shl si, 1
	mov dx, [cs:si+video_table_sizes]
	mov [video_page_size], dx
	mov cl, 4
	shl ax, cl
	add ax, video_parameters
	mov si, ax
	xor ax, ax
	mov [video_page_start], ax
	mov [video_page], al
	mov byte [video_rows], screen_rows - 1
	mov word [video_points], 8
	mov di, video_cursor
	mov cx, 8
.cursors:
	mov [di], ax
	add di, 2
	loop .cursors
	mov word [video_port], crtc_index
	mov dx, cga_mode
	mov al, [video_mode_reg]
	and al, 0xF7
	out dx, al
	mov dx, crtc_index
	xor ah, ah
	mov cx, 16
.crtc:
	mov al, ah
	out dx, al
	inc dx
	mov al, [cs:si]
	out dx, al
	dec dx
	inc si
	inc ah
	loop .crtc
	mov word [video_cursor_shape], 0x0607
	mov si, 0xB800
	mov es, si
	xor di, di
	mov cx, 0x2000
	mov ax, 0x0720
	call graphics_mode
	jz .fill
	xor ax, ax
.fill:
	rep stosw
	mov al, 0x30
	cmp byte [video_mode], 6
	jnz .palette
	mov al, 0x3F
.palette:
	mov [video_palette], al
	mov dx, cga_color
	out dx, al
	mov al, [video_mode_reg]
	mov dx, cga_mode
	out dx, al
	jmp service_exit

video_cursor_type:
	mov [video_cursor_shape], cx
	mov dx, [video_port]
	mov al, 0x0A
	out dx, al
	inc dx
	mov al, ch
	out dx, al
	dec dx
	mov al, 0x0B
	out dx, al
	inc dx
	mov al, cl
	out dx, al
	jmp service_exit

video_set_cursor:
	mov bl, bh
	and bx, 0x0007
	shl bx, 1
	mov [bx+video_cursor], dx
	shr bx, 1
	cmp bl, [video_page]
	jnz .done
	call update_cursor
.done:
	jmp service_exit

video_get_cursor:
	mov bl, bh
	and bx, 0x0007
	shl bx, 1
	mov ax, [bx+video_cursor]
	mov [bp+frame_dx], ax
	mov ax, [video_cursor_shape]
	mov [bp+frame_cx], ax
	jmp service_exit

video_light_pen:
	mov byte [bp+frame_ah], 0
	jmp service_exit

video_select_page:
	call graphics_mode
	jnz .done
	and al, 0x07
	mov [video_page], al
	mov ah, 0
	mul word [video_page_size]
	mov [video_page_start], ax
	shr ax, 1
	mov bx, ax
	mov dx, [video_port]
	mov al, 0x0C
	out dx, al
	inc dx
	mov al, bh
	out dx, al
	dec dx
	mov al, 0x0D
	out dx, al
	inc dx
	mov al, bl
	out dx, al
	call update_cursor
.done:
	jmp service_exit

video_scroll_up:
	mov byte [video_down], 0
	jmp video_scroll

video_scroll_down:
	mov byte [video_down], 1

video_scroll:
	call scroll_window
	jmp service_exit

scroll_window:
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push es
	push ds
	mov [video_lines], al
	mov [video_attr], bh
	mov al, [video_columns]
	dec al
	cmp dl, al
	jbe .right
	mov dl, al
.right:
	cmp dh, screen_rows - 1
	jbe .bottom
	mov dh, screen_rows - 1
.bottom:
	cmp ch, dh
	ja .exit
	cmp cl, dl
	ja .exit
	mov [video_top], ch
	mov [video_left], cl
	mov [video_bottom], dh
	mov [video_right], dl
	mov al, dh
	sub al, ch
	inc al
	mov [video_height], al
	mov al, dl
	sub al, cl
	inc al
	mov [video_width], al
	mov al, [video_lines]
	test al, al
	jz .whole
	cmp al, [video_height]
	jb .lines
.whole:
	mov al, [video_height]
	mov [video_lines], al
.lines:
	call graphics_mode
	jnz .graphics
	call text_scroll
	jmp .exit
.graphics:
	call graphics_scroll
.exit:
	pop ds
	pop es
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	ret

text_scroll:
	push bp
	mov ax, [video_columns]
	shl ax, 1
	mov bp, ax
	mov bh, [video_page]
	mov dh, [video_top]
	mov dl, [video_left]
	cmp byte [video_down], 0
	jz .origin
	mov dh, [video_bottom]
	neg bp
.origin:
	call text_offset
	mov al, [video_lines]
	cbw
	imul bp
	mov si, di
	add si, ax
	mov cl, [video_height]
	sub cl, [video_lines]
	mov ch, 0
	mov bl, [video_width]
	mov bh, 0
	mov dl, [video_lines]
	mov dh, [video_attr]
	mov ax, 0xB800
	mov es, ax
	mov ds, ax
	jcxz .clear
.move:
	push cx
	push si
	push di
	mov cx, bx
	rep movsw
	pop di
	pop si
	pop cx
	add si, bp
	add di, bp
	loop .move
.clear:
	mov al, 0x20
	mov ah, dh
	mov cl, dl
	mov ch, 0
.blank:
	push cx
	push di
	mov cx, bx
	rep stosw
	pop di
	pop cx
	add di, bp
	loop .blank
	pop bp
	ret

graphics_scroll:
	mov bl, 1
	cmp byte [video_mode], 6
	jz .width
	mov bl, 2
.width:
	mov al, [video_left]
	mul bl
	mov [graphics_left], al
	mov al, [video_width]
	mul bl
	mov [graphics_width], al
	mov si, 0xB800
	mov es, si
	mov cl, [video_height]
	sub cl, [video_lines]
	mov ch, 0
	mov al, [video_top]
	mov ah, 1
	cmp byte [video_down], 0
	jz .move_start
	mov al, [video_bottom]
	mov ah, 0xFF
.move_start:
	jcxz .clear_start
.move:
	push ax
	push cx
	mov bl, al
	mov bh, al
	cmp ah, 1
	jnz .source_up
	add bh, [video_lines]
	jmp .copy
.source_up:
	sub bh, [video_lines]
.copy:
	call graphics_copy_row
	pop cx
	pop ax
	add al, ah
	loop .move
.clear_start:
	mov cl, [video_lines]
	mov ch, 0
.clear:
	push ax
	push cx
	mov bl, al
	call graphics_clear_row
	pop cx
	pop ax
	add al, ah
	loop .clear
	ret

graphics_line_offset:
	push bx
	push dx
	mov ah, 0
	shl ax, 1
	shl ax, 1
	mov bl, dl
	shr bl, 1
	mov bh, 0
	add ax, bx
	mov bx, 80
	mul bx
	pop dx
	push dx
	test dl, 1
	jz .even
	add ax, 0x2000
.even:
	mov bl, [graphics_left]
	mov bh, 0
	add ax, bx
	pop dx
	pop bx
	ret

graphics_copy_row:
	push ax
	push cx
	push dx
	push si
	push di
	xor dx, dx
.line:
	mov al, bh
	call graphics_line_offset
	mov si, ax
	mov al, bl
	call graphics_line_offset
	mov di, ax
	mov cl, [graphics_width]
	mov ch, 0
.byte:
	mov al, [es:si]
	mov [es:di], al
	inc si
	inc di
	loop .byte
	inc dl
	cmp dl, 8
	jb .line
	pop di
	pop si
	pop dx
	pop cx
	pop ax
	ret

graphics_clear_row:
	push ax
	push cx
	push dx
	push di
	xor dx, dx
.line:
	mov al, bl
	call graphics_line_offset
	mov di, ax
	mov cl, [graphics_width]
	mov ch, 0
	mov al, [video_attr]
.byte:
	mov [es:di], al
	inc di
	loop .byte
	inc dl
	cmp dl, 8
	jb .line
	pop di
	pop dx
	pop cx
	pop ax
	ret

video_read_cell:
	call graphics_mode
	jnz .graphics
	mov si, 0xB800
	mov es, si
	mov bl, bh
	and bx, 0x0007
	shl bx, 1
	mov dx, [bx+video_cursor]
	shr bx, 1
	mov bh, bl
	call text_offset
	mov ax, [es:di]
	mov [bp+frame_ax], ax
	jmp service_exit
.graphics:
	mov word [bp+frame_ax], 0
	jmp service_exit

video_write_cell:
	mov ah, 1
	jmp video_write

video_write_char:
	mov ah, 0

video_write:
	jcxz .done
	push ax
	call graphics_mode
	pop ax
	jnz .graphics
	mov si, 0xB800
	mov es, si
	and bh, 0x07
	mov dl, bh
	mov dh, 0
	mov si, dx
	shl si, 1
	mov dx, [si+video_cursor]
	call text_offset
	test ah, ah
	jz .chars
	mov ah, bl
	rep stosw
	jmp .done
.chars:
	mov [es:di], al
	add di, 2
	loop .chars
.done:
	jmp service_exit
.graphics:
	mov dx, [video_cursor]
.glyphs:
	push cx
	push dx
	call draw_glyph
	pop dx
	pop cx
	inc dl
	loop .glyphs
	jmp service_exit

video_teletype:
	mov bl, [video_page]
	mov bh, 0
	shl bx, 1
	mov dx, [bx+video_cursor]
	mov bx, [bp+frame_bx]
	cmp al, 0x07
	jz .bell
	cmp al, 0x08
	jz .backspace
	cmp al, 0x0D
	jz .return
	cmp al, 0x0A
	jz .line
	call graphics_mode
	jnz .graphics
	mov si, 0xB800
	mov es, si
	push bx
	mov bh, [video_page]
	call text_offset
	pop bx
	mov [es:di], al
	jmp .advance
.graphics:
	call draw_glyph
.advance:
	inc dl
	cmp dl, [video_columns]
	jb .store
	mov dl, 0
.line:
	inc dh
	cmp dh, screen_rows
	jb .store
	mov dh, screen_rows - 1
	call teletype_scroll
	jmp .store
.return:
	mov dl, 0
	jmp .store
.backspace:
	test dl, dl
	jz .store
	dec dl
	jmp .store
.bell:
	call beep
	jmp service_exit
.store:
	mov bl, [video_page]
	mov bh, 0
	shl bx, 1
	mov [bx+video_cursor], dx
	call update_cursor
	jmp service_exit

teletype_scroll:
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push es
	mov bh, 0
	call graphics_mode
	jnz .scroll
	mov si, 0xB800
	mov es, si
	mov bh, [video_page]
	call text_offset
	mov bh, [es:di+1]
.scroll:
	mov al, 1
	xor cx, cx
	mov dh, screen_rows - 1
	mov dl, [video_columns]
	dec dl
	mov byte [video_down], 0
	call scroll_window
	pop es
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	ret

video_get_mode:
	mov al, [video_mode]
	mov ah, [video_columns]
	mov [bp+frame_ax], ax
	mov al, [video_page]
	mov [bp+frame_bh], al
	jmp service_exit

video_palette_set:
	mov al, [video_palette]
	test bh, bh
	jnz .select
	and al, 0xE0
	and bl, 0x1F
	or al, bl
	jmp .apply
.select:
	and al, 0xDF
	test bl, 1
	jz .apply
	or al, 0x20
.apply:
	mov [video_palette], al
	mov dx, cga_color
	out dx, al
	jmp service_exit

pixel_address:
	mov si, 0xB800
	mov es, si
	mov ax, dx
	shr ax, 1
	mov si, 80
	push dx
	mul si
	pop dx
	mov si, ax
	test dl, 1
	jz .even
	add si, 0x2000
.even:
	mov ax, cx
	cmp byte [video_mode], 6
	jz .mono
	mov cl, al
	shr ax, 1
	shr ax, 1
	add si, ax
	and cl, 3
	xor cl, 3
	shl cl, 1
	mov ch, 0x03
	ret
.mono:
	mov cl, al
	shr ax, 1
	shr ax, 1
	shr ax, 1
	add si, ax
	and cl, 7
	xor cl, 7
	mov ch, 0x01
	ret

video_write_pixel:
	call graphics_mode
	jz .done
	push ax
	call pixel_address
	pop ax
	mov ah, al
	and al, ch
	shl al, cl
	mov bl, ch
	shl bl, cl
	test ah, 0x80
	jnz .flip
	not bl
	and [es:si], bl
	or [es:si], al
	jmp .done
.flip:
	xor [es:si], al
.done:
	jmp service_exit

video_read_pixel:
	call graphics_mode
	jz .none
	call pixel_address
	mov al, [es:si]
	shr al, cl
	and al, ch
	mov [bp+frame_al], al
	jmp service_exit
.none:
	mov byte [bp+frame_al], 0
	jmp service_exit

draw_glyph:
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push es
	mov [video_color], bl
	mov [video_top], dh
	mov ch, al
	mov bl, 1
	cmp byte [video_mode], 6
	jz .bytes
	mov bl, 2
.bytes:
	mov al, dl
	mul bl
	mov [graphics_left], al
	mov al, ch
	cmp al, 0x80
	jnb .high
	mov ah, 0
	mov cl, 3
	shl ax, cl
	add ax, font_low
	mov si, ax
	mov di, video_glyph
	mov cx, 8
.copy_low:
	mov al, [cs:si]
	mov [di], al
	inc si
	inc di
	loop .copy_low
	jmp .draw
.high:
	sub al, 0x80
	mov ah, 0
	mov cl, 3
	shl ax, cl
	xor si, si
	mov es, si
	les si, [es:0x7C]
	add si, ax
	mov di, video_glyph
	mov cx, 8
.copy_high:
	mov al, [es:si]
	mov [di], al
	inc si
	inc di
	loop .copy_high
.draw:
	mov si, 0xB800
	mov es, si
	xor dx, dx
.line:
	mov al, [video_top]
	call graphics_line_offset
	mov di, ax
	mov bl, dl
	mov bh, 0
	mov al, [bx+video_glyph]
	cmp byte [video_mode], 6
	jz .mono
	call expand_glyph
	test byte [video_color], 0x80
	jnz .flip_wide
	mov [es:di], ah
	mov [es:di+1], al
	jmp .next
.flip_wide:
	xor [es:di], ah
	xor [es:di+1], al
	jmp .next
.mono:
	test byte [video_color], 0x7F
	jnz .lit
	xor al, al
.lit:
	test byte [video_color], 0x80
	jnz .flip_narrow
	mov [es:di], al
	jmp .next
.flip_narrow:
	xor [es:di], al
.next:
	inc dl
	cmp dl, 8
	jb .line
	pop es
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	ret

expand_glyph:
	push bx
	push cx
	mov bl, al
	mov bh, [video_color]
	and bh, 0x03
	xor ax, ax
	mov cx, 8
.bit:
	shl ax, 1
	shl ax, 1
	shl bl, 1
	jnc .clear
	or al, bh
.clear:
	loop .bit
	pop cx
	pop bx
	ret

video_table_sets:
	db 0, 0, 1, 1, 2, 2, 2, 1