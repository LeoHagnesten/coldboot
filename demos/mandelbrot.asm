org 0x7C00

	cli
	xor ax, ax
	mov ds, ax
	mov ss, ax
	mov sp, 0x7C00
	sti
	cld
	mov ax, 0x0004
	int 0x10
	mov ax, 0xB800
	mov es, ax
	mov word [ci], -4776

next_row:
	mov al, [row]
	call row_offset
	mov [top], di
	mov al, 199
	sub al, [row]
	call row_offset
	mov [bottom], di
	mov bp, -9533
	mov byte [bytes], 80
.byte:
	mov byte [pixels], 4
.pixel:
	call mandel
	mov ah, [acc]
	shl ah, 2
	or ah, al
	mov [acc], ah
	add bp, 40
	dec byte [pixels]
	jnz .pixel
	mov al, [acc]
	mov di, [top]
	stosb
	mov [top], di
	mov di, [bottom]
	stosb
	mov [bottom], di
	dec byte [bytes]
	jnz .byte
	add word [ci], 48
	inc byte [row]
	cmp byte [row], 100
	jb next_row

idle:
	xor ah, ah
	int 0x16
	inc byte [look]
	mov ah, 0x0B
	mov bh, 1
	mov bl, [look]
	and bl, 1
	xor bl, 1
	int 0x10
	mov ah, 0x0B
	mov bh, 0
	mov bl, [look]
	not bl
	and bl, 2
	shl bl, 3
	int 0x10
	jmp idle

row_offset:
	mov dl, al
	shr al, 1
	mov ah, 80
	mul ah
	test dl, 1
	jz .even
	add ax, 0x2000
.even:
	mov di, ax
	ret

mandel:
	mov si, bp
	mov di, [ci]
	mov cx, 32
.iterate:
	cmp si, 8192
	jg .escape
	cmp si, -8192
	jl .escape
	cmp di, 8192
	jg .escape
	cmp di, -8192
	jl .escape
	mov ax, si
	imul ax
	rol ax, 4
	and ax, 15
	shl dx, 4
	or ax, dx
	mov bx, ax
	mov ax, di
	imul ax
	rol ax, 4
	and ax, 15
	shl dx, 4
	or ax, dx
	mov dx, bx
	add dx, ax
	cmp dx, 16384
	ja .escape
	sub bx, ax
	mov ax, si
	imul di
	rol ax, 4
	and ax, 15
	shl dx, 4
	or ax, dx
	shl ax, 1
	add ax, [ci]
	mov di, ax
	mov si, bx
	add si, bp
	loop .iterate
.escape:
	mov al, 32
	sub al, cl
	mov bx, shades
	xlatb
	ret

shades:
	db 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 1, 1, 2, 2, 3, 3
	db 1, 1, 2, 2, 3, 3, 1, 1, 2, 2, 3, 3, 1, 1, 2, 2, 0

ci: dw 0
top: dw 0
bottom: dw 0
row: db 0
bytes: db 0
pixels: db 0
acc: db 0
look: db 0

times 510 - ( $ - $$ ) db 0
dw 0xAA55