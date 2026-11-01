service_exit:
	pop ax
	pop bx
	pop cx
	pop dx
	pop si
	pop di
	pop ds
	pop es
	pop bp
	iret

set_carry:
	or byte [bp+frame_flags], 0x01
	ret

clear_carry:
	and byte [bp+frame_flags], 0xFE
	ret

set_zero:
	or byte [bp+frame_flags], 0x40
	ret

clear_zero:
	and byte [bp+frame_flags], 0xBF
	ret

dummy_iret:
	iret

int02:
	iret

int05:
	iret

int08:
	sti
	push ax
	push dx
	push ds
	mov ax, bda
	mov ds, ax
	add word [ticks_low], 1
	adc word [ticks_high], 0
	cmp word [ticks_high], 0x0018
	jnz .motor
	cmp word [ticks_low], 0x00B0
	jnz .motor
	xor ax, ax
	mov [ticks_low], ax
	mov [ticks_high], ax
	mov byte [ticks_rollover], 1
.motor:
	cmp byte [disk_motor_count], 0
	jz .user
	dec byte [disk_motor_count]
	jnz .user
	and byte [disk_motor_status], 0xF0
	mov al, 0x0C
	mov dx, fdc_output
	out dx, al
.user:
	int 0x1C
	mov al, 0x20
	out pic_command, al
	pop ds
	pop dx
	pop ax
	iret

int0e:
	push ax
	push ds
	mov ax, bda
	mov ds, ax
	or byte [disk_seek_status], 0x80
	mov al, 0x20
	out pic_command, al
	pop ds
	pop ax
	iret

int11:
	push ds
	mov ax, bda
	mov ds, ax
	mov ax, [bda_equipment]
	pop ds
	iret

int12:
	push ds
	mov ax, bda
	mov ds, ax
	mov ax, [bda_memory]
	pop ds
	iret

int14:
	mov ah, 0x80
	iret

int15:
	push bp
	mov bp, sp
	mov ah, 0x86
	or byte [bp+6], 0x01
	pop bp
	iret

int17:
	mov ah, 0x01
	iret

int1a:
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
	cmp ah, 0x01
	jz .write
	call set_carry
	jmp service_exit
.read:
	cli
	mov ax, [ticks_low]
	mov [bp+frame_dx], ax
	mov ax, [ticks_high]
	mov [bp+frame_cx], ax
	mov al, [ticks_rollover]
	mov [bp+frame_al], al
	mov byte [ticks_rollover], 0
	sti
	call clear_carry
	jmp service_exit
.write:
	cli
	mov [ticks_low], dx
	mov [ticks_high], cx
	mov byte [ticks_rollover], 0
	sti
	call clear_carry
	jmp service_exit

wait_ticks:
	push ax
	push bx
	push ds
	mov ax, bda
	mov ds, ax
	mov bx, [ticks_low]
.loop:
	sti
	hlt
	mov ax, [ticks_low]
	sub ax, bx
	cmp ax, cx
	jb .loop
	pop ds
	pop bx
	pop ax
	ret

beep:
	push ax
	push cx
	mov al, 0xB6
	out pit_control, al
	mov ax, 1193
	out pit_counter2, al
	mov al, ah
	out pit_counter2, al
	in al, ppi_b
	push ax
	or al, 0x03
	out ppi_b, al
	mov cx, 2
	call wait_ticks
	pop ax
	out ppi_b, al
	pop cx
	pop ax
	ret

print_cs:
	push ax
	push bx
	push si
.loop:
	mov al, [cs:si]
	inc si
	test al, al
	jz .done
	mov ah, 0x0E
	mov bx, 0x0007
	int 0x10
	jmp .loop
.done:
	pop si
	pop bx
	pop ax
	ret

int18:
	sti
	mov si, text_no_boot
	call print_cs
	xor ah, ah
	int 0x16
	mov si, text_newline
	call print_cs
	int 0x19

int19:
	sti
	mov ax, bda
	mov ds, ax
	mov dl, [boot_drive]
	xor ax, ax
	mov es, ax
	mov cx, 4
.try:
	push cx
	push dx
	xor ah, ah
	int 0x13
	pop dx
	push dx
	mov ax, 0x0201
	mov bx, 0x7C00
	mov cx, 0x0001
	xor dh, dh
	int 0x13
	pop dx
	pop cx
	jnc .loaded
	loop .try
	int 0x18
.loaded:
	xor ax, ax
	mov ds, ax
	cmp word [0x7C00], 0
	jnz .start
	int 0x18
.start:
	cli
	xor ax, ax
	mov ss, ax
	mov sp, 0x7C00
	mov es, ax
	sti
	jmp 0x0000:0x7C00

text_no_boot:
	db 13, 10, "No bootable disk. Insert a disk and press any key.", 0
text_newline:
	db 13, 10, 0