	org 0xE000

rom_segment         equ 0xF000
service_port        equ 0x0E00
frame_port          equ 0x0E04
request_port        equ 0x0E06
data_port           equ 0x0E08
reset_port          equ 0x0CF9

entry:
	cli
	cld
	xor ax, ax
	mov ss, ax
	mov sp, 0x7000
	mov ds, ax
	mov es, ax
	sti
	int 0x19

wait_forever:
	sti
	hlt
	jmp wait_forever

no_boot:
	sti
	xor ax, ax
	int 0x16
	int 0x19
	jmp wait_forever

vector_09:
	db 0x66
	push bx
	mov bx, 0x09
	jmp service

vector_10:
	db 0x66
	push bx
	mov bx, 0x10
	jmp service

vector_11:
	db 0x66
	push bx
	mov bx, 0x11
	jmp service

vector_12:
	db 0x66
	push bx
	mov bx, 0x12
	jmp service

vector_13:
	db 0x66
	push bx
	mov bx, 0x13
	jmp service

vector_14:
	db 0x66
	push bx
	mov bx, 0x14
	jmp service

vector_15:
	db 0x66
	push bx
	mov bx, 0x15
	jmp service

vector_16:
	db 0x66
	push bx
	mov bx, 0x16
	jmp service

vector_17:
	db 0x66
	push bx
	mov bx, 0x17
	jmp service

vector_18:
	db 0x66
	push bx
	mov bx, 0x18
	jmp service

vector_19:
	db 0x66
	push bx
	mov bx, 0x19
	jmp service

vector_1a:
	db 0x66
	push bx
	mov bx, 0x1A
	jmp service

vector_74:
	db 0x66
	push bx
	mov bx, 0x74
	jmp service

service:
	db 0x66
	push ax
	db 0x66
	push cx
	db 0x66
	push dx
	db 0x66
	push si
	db 0x66
	push di
	db 0x66
	push bp
	push ds
	push es
	mov bp, sp

service_send:
	mov dx, frame_port
	xor si, si
	mov cx, 19

service_frame_out:
	mov ax, [bp+si]
	out dx, ax
	add si, 2
	loop service_frame_out

service_execute:
	mov dx, service_port
	mov ax, bx
	out dx, ax

service_request:
	mov dx, request_port
	in ax, dx
	cmp ax, 1
	je service_input
	cmp ax, 2
	je service_output
	cmp ax, 3
	je service_wait
	cmp ax, 4
	je service_mouse

service_receive:
	mov dx, frame_port
	xor si, si
	mov cx, 19

service_frame_in:
	in ax, dx
	mov [bp+si], ax
	add si, 2
	loop service_frame_in
	sti
	nop
	cli

service_done:
	pop es
	pop ds
	db 0x66
	pop bp
	db 0x66
	pop di
	db 0x66
	pop si
	db 0x66
	pop dx
	db 0x66
	pop cx
	db 0x66
	pop ax
	db 0x66
	pop bx
	iret

service_input:
	in ax, dx
	mov es, ax
	in ax, dx
	mov di, ax
	in ax, dx
	mov cx, ax
	mov dx, data_port

service_input_loop:
	es
	mov al, [di]
	out dx, al
	inc di
	loop service_input_loop
	jmp service_execute

service_output:
	in ax, dx
	mov es, ax
	in ax, dx
	mov di, ax
	in ax, dx
	mov cx, ax
	mov dx, data_port

service_output_loop:
	in al, dx
	es
	mov [di], al
	inc di
	loop service_output_loop
	jmp service_request

service_wait:
	sti
	hlt
	cli
	jmp service_send

service_mouse:
	mov ax, 0x40
	mov ds, ax
	mov ds, word [0x0E]
	push word [0x24]
	push word [0x26]
	push word [0x28]
	push word [0x2A]
	sti
	call far [0x20]
	cli
	add sp, 8
	jmp service_receive

vector_08:
	push ds
	push ax
	mov ax, 0x40
	mov ds, ax
	add word [0x6C], 1
	adc word [0x6E], 0
	cmp word [0x6E], 0x18
	jb timer_chain
	cmp word [0x6C], 0xB0
	jb timer_chain
	mov word [0x6C], 0
	mov word [0x6E], 0
	mov byte [0x70], 1

timer_chain:
	int 0x1C
	cli
	mov al, 0x20
	out 0x20, al
	pop ax
	pop ds
	iret

vector_70:
	push ax
	mov al, 0x0C
	out 0x70, al
	in al, 0x71
	mov al, 0x20
	out 0xA0, al
	out 0x20, al
	pop ax
	iret

irq_master:
	push ax
	mov al, 0x20
	out 0x20, al
	pop ax
	iret

irq_slave:
	push ax
	mov al, 0x20
	out 0xA0, al
	out 0x20, al
	pop ax
	iret

dummy_iret:
	iret

reset_entry:
	cli
	mov dx, reset_port
	mov al, 0x02
	out dx, al
	mov al, 0x06
	out dx, al

reset_wait:
	hlt
	jmp reset_wait

config_table:
	dw 8
	db 0xFC, 0x01, 0x00, 0x74, 0x40, 0x00, 0x00, 0x00

disk_table:
	db 0xDF, 0x02, 0x25, 0x02, 0x12, 0x1B, 0xFF, 0x6C, 0xF6, 0x0F, 0x08

video_table:
	times 64 db 0

	org 0xFF53
	iret

	org 0xFFF0
	jmp rom_segment:reset_entry

	org 0xFFF5
	db "11/01/26"

	org 0xFFFE
	db 0xFC
	db 0x00