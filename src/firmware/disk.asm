int13:
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
	test dl, 0x80
	jnz .fixed
	cmp ah, 0x00
	jz disk_reset
	cmp ah, 0x01
	jz disk_last_status
	cmp ah, 0x02
	jz disk_read
	cmp ah, 0x03
	jz disk_write
	cmp ah, 0x04
	jz disk_verify
	cmp ah, 0x05
	jz disk_format
	cmp ah, 0x08
	jz disk_parameters
	cmp ah, 0x15
	jz disk_kind
	mov ah, 0x01
	jmp disk_finish
.fixed:
	cmp ah, 0x15
	jz .no_fixed_kind
	cmp ah, 0x08
	jnz .no_fixed
	mov byte [bp+frame_dl], 0
.no_fixed:
	mov ah, 0x01
	jmp disk_finish
.no_fixed_kind:
	mov byte [bp+frame_ah], 0
	call clear_carry
	jmp service_exit

disk_reset:
	call fdc_reset
	jmp disk_finish

disk_last_status:
	mov ah, [disk_status]
	mov [bp+frame_ah], ah
	call clear_carry
	test ah, ah
	jz .done
	call set_carry
.done:
	jmp service_exit

disk_parameters:
	call drive_count
	mov [bp+frame_dl], al
	cmp dl, al
	jb .present
	mov word [bp+frame_ax], 0
	mov word [bp+frame_bx], 0
	mov word [bp+frame_cx], 0
	mov byte [bp+frame_dh], 0
	call clear_carry
	jmp service_exit
.present:
	mov word [bp+frame_ax], 0
	mov word [bp+frame_bx], 0x0004
	mov word [bp+frame_cx], 0x4F12
	mov byte [bp+frame_dh], 1
	mov word [bp+frame_es], rom_segment
	mov word [bp+frame_di], disk_table
	mov byte [disk_status], 0
	call clear_carry
	jmp service_exit

disk_kind:
	call drive_count
	mov ah, 0x00
	cmp dl, al
	jnb .done
	mov ah, 0x01
.done:
	mov [bp+frame_ah], ah
	call clear_carry
	jmp service_exit

drive_count:
	mov al, [bda_equipment]
	test al, 0x01
	jz .none
	mov cl, 6
	shr al, cl
	inc al
	ret
.none:
	xor al, al
	ret

disk_read:
	mov byte [disk_command], 0xE6
	mov byte [disk_dma_mode], 0x46
	jmp disk_transfer

disk_write:
	mov byte [disk_command], 0xC5
	mov byte [disk_dma_mode], 0x4A
	jmp disk_transfer

disk_verify:
	mov byte [disk_command], 0xE6
	mov byte [disk_dma_mode], 0x42
	jmp disk_transfer

disk_format:
	mov byte [disk_command], 0x4D
	mov byte [disk_dma_mode], 0x4A

disk_transfer:
	mov [disk_count], al
	mov [disk_drive], dl
	mov [disk_cylinder], ch
	mov al, dh
	and al, 0x01
	mov [disk_head], al
	mov al, cl
	and al, 0x3F
	mov [disk_sector], al
	mov byte [bp+frame_al], 0
	mov ah, 0x80
	cmp dl, 2
	jnb .finish
	mov ah, 0x01
	cmp byte [disk_count], 0
	jz .finish
	mov byte [disk_detected], 0
.attempt:
	call disk_attempt
	test ah, ah
	jz .success
	cmp ah, 0x04
	jz .redetect
	cmp ah, 0x02
	jnz .finish
.redetect:
	cmp byte [disk_detected], 0
	jnz .finish
	mov byte [disk_detected], 1
	mov bl, [disk_drive]
	mov bh, 0
	mov byte [bx+disk_spt], 0
	jmp .attempt
.success:
	mov al, [disk_count]
	mov [bp+frame_al], al
.finish:
	jmp disk_finish

disk_finish:
	mov [disk_status], ah
	mov [bp+frame_ah], ah
	push ax
	push es
	xor si, si
	mov es, si
	les si, [es:0x78]
	mov al, [es:si+2]
	mov [disk_motor_count], al
	pop es
	pop ax
	call clear_carry
	test ah, ah
	jz .done
	call set_carry
.done:
	jmp service_exit

disk_attempt:
	call motor_start
	mov cl, [disk_drive]
	mov al, 1
	shl al, cl
	test [disk_seek_status], al
	jnz .calibrated
	call recalibrate
	test ah, ah
	jnz .exit
	mov cl, [disk_drive]
	mov al, 1
	shl al, cl
	or [disk_seek_status], al
	mov bl, [disk_drive]
	mov bh, 0
	mov byte [bx+disk_track], 0
.calibrated:
	call seek_cylinder
	test ah, ah
	jnz .exit
	cmp byte [disk_command], 0x4D
	jz .transfer
	mov bl, [disk_drive]
	mov bh, 0
	cmp byte [bx+disk_spt], 0
	jnz .transfer
	call detect_media
	test ah, ah
	jnz .exit
.transfer:
	call dma_setup
	test ah, ah
	jnz .exit
	call send_command
	test ah, ah
	jnz .exit
	call wait_interrupt
	test ah, ah
	jnz .timeout
	call read_results
	test ah, ah
	jnz .exit
	call evaluate_results
.exit:
	ret
.timeout:
	push ax
	call fdc_reset
	pop ax
	ret

motor_start:
	mov cl, [disk_drive]
	mov byte [disk_motor_count], 0xFF
	mov bl, 1
	shl bl, cl
	test [disk_motor_status], bl
	jnz .running
	and byte [disk_motor_status], 0xF0
	or [disk_motor_status], bl
	mov al, 0x10
	shl al, cl
	or al, cl
	or al, 0x0C
	mov dx, fdc_output
	out dx, al
	push es
	xor si, si
	mov es, si
	les si, [es:0x78]
	mov al, [es:si+10]
	pop es
	mov ah, 0
	shl ax, 1
	mov cx, ax
	call wait_ticks
.running:
	ret

fdc_reset:
	cli
	mov al, [disk_motor_status]
	and al, 0x0F
	mov cl, 4
	shl al, cl
	or al, 0x08
	mov dx, fdc_output
	out dx, al
	and byte [disk_seek_status], 0x70
	nop
	nop
	or al, 0x04
	out dx, al
	sti
	call wait_interrupt
	test ah, ah
	jnz .failed
	mov cx, 4
.sense:
	push cx
	call sense_interrupt
	pop cx
	loop .sense
	call specify
	mov word [disk_spt], 0
	mov word [disk_track], 0xFFFF
	xor ah, ah
	ret
.failed:
	mov ah, 0x20
	ret

specify:
	push es
	xor si, si
	mov es, si
	les si, [es:0x78]
	mov al, 0x03
	call fdc_out
	mov al, [es:si]
	call fdc_out
	mov al, [es:si+1]
	call fdc_out
	pop es
	ret

recalibrate:
	and byte [disk_seek_status], 0x7F
	mov al, 0x07
	call fdc_out
	jc .fail
	mov al, [disk_drive]
	call fdc_out
	jc .fail
	call wait_interrupt
	test ah, ah
	jnz .fail
	call sense_interrupt
	mov al, [disk_results]
	and al, 0xF0
	cmp al, 0x20
	jnz .seek_error
	xor ah, ah
	ret
.seek_error:
	mov ah, 0x40
	ret
.fail:
	mov ah, 0x20
	ret

seek_cylinder:
	mov bl, [disk_drive]
	mov bh, 0
	mov al, [disk_cylinder]
	cmp al, [bx+disk_track]
	jz .done
	and byte [disk_seek_status], 0x7F
	mov al, 0x0F
	call fdc_out
	jc .fail
	call head_drive
	call fdc_out
	jc .fail
	mov al, [disk_cylinder]
	call fdc_out
	jc .fail
	call wait_interrupt
	test ah, ah
	jnz .fail
	call sense_interrupt
	mov al, [disk_results]
	and al, 0xE0
	cmp al, 0x20
	jnz .seek_error
	mov al, [disk_results+1]
	cmp al, [disk_cylinder]
	jnz .seek_error
	mov bl, [disk_drive]
	mov bh, 0
	mov [bx+disk_track], al
.done:
	xor ah, ah
	ret
.seek_error:
	mov bl, [disk_drive]
	mov bh, 0
	mov byte [bx+disk_track], 0xFF
	mov ah, 0x40
	ret
.fail:
	mov ah, 0x20
	ret

head_drive:
	mov al, [disk_head]
	shl al, 1
	shl al, 1
	or al, [disk_drive]
	ret

detect_media:
	mov si, media_sizes
.next:
	mov al, [cs:si]
	test al, al
	jz .unknown
	push si
	mov [disk_probe], al
	cli
	mov al, 0x06
	out 0x0A, al
	out 0x0C, al
	mov al, 0x42
	out 0x0B, al
	xor al, al
	out 0x04, al
	out 0x04, al
	out dma2_page, al
	mov al, 0xFF
	out 0x05, al
	mov al, 0x01
	out 0x05, al
	sti
	mov al, 0x02
	out 0x0A, al
	and byte [disk_seek_status], 0x7F
	mov al, 0x66
	call fdc_out
	mov al, [disk_drive]
	call fdc_out
	mov al, [disk_cylinder]
	call fdc_out
	xor al, al
	call fdc_out
	mov al, [disk_probe]
	call fdc_out
	mov al, 0x02
	call fdc_out
	mov al, [disk_probe]
	call fdc_out
	mov al, 0x1B
	call fdc_out
	mov al, 0xFF
	call fdc_out
	call wait_interrupt
	test ah, ah
	jnz .timeout
	call read_results
	pop si
	inc si
	test byte [disk_results], 0xC0
	jnz .next
	mov al, [disk_probe]
	mov bl, [disk_drive]
	mov bh, 0
	mov [bx+disk_spt], al
	xor ah, ah
	ret
.timeout:
	pop si
	push ax
	call fdc_reset
	pop ax
	ret
.unknown:
	mov ah, 0x02
	ret

media_sizes:
	db 18, 15, 9, 8, 0

dma_setup:
	mov ax, [bp+frame_es]
	mov dx, ax
	mov cl, 12
	shr dx, cl
	mov cl, 4
	shl ax, cl
	add ax, [bp+frame_bx]
	adc dl, 0
	mov di, ax
	mov al, [disk_count]
	mov ah, 0
	cmp byte [disk_command], 0x4D
	jz .format
	mov cl, 9
	shl ax, cl
	jmp .length
.format:
	shl ax, 1
	shl ax, 1
.length:
	dec ax
	mov cx, ax
	cmp byte [disk_dma_mode], 0x42
	jz .program
	mov ax, di
	add ax, cx
	jnc .program
	mov ah, 0x09
	ret
.program:
	cli
	mov al, 0x06
	out 0x0A, al
	out 0x0C, al
	mov al, [disk_dma_mode]
	out 0x0B, al
	mov ax, di
	out 0x04, al
	mov al, ah
	out 0x04, al
	mov al, dl
	and al, 0x0F
	out dma2_page, al
	mov ax, cx
	out 0x05, al
	mov al, ah
	out 0x05, al
	sti
	mov al, 0x02
	out 0x0A, al
	xor ah, ah
	ret

send_command:
	push es
	xor si, si
	mov es, si
	les si, [es:0x78]
	and byte [disk_seek_status], 0x7F
	mov al, [disk_command]
	call fdc_out
	jc .fail
	call head_drive
	call fdc_out
	jc .fail
	cmp byte [disk_command], 0x4D
	jz .format
	mov al, [disk_cylinder]
	call fdc_out
	jc .fail
	mov al, [disk_head]
	call fdc_out
	jc .fail
	mov al, [disk_sector]
	call fdc_out
	jc .fail
	mov al, 0x02
	call fdc_out
	jc .fail
	mov bl, [disk_drive]
	mov bh, 0
	mov al, [bx+disk_spt]
	test al, al
	jnz .eot
	mov al, [es:si+4]
.eot:
	call fdc_out
	jc .fail
	mov al, [es:si+5]
	call fdc_out
	jc .fail
	mov al, [es:si+6]
	call fdc_out
	jc .fail
	pop es
	xor ah, ah
	ret
.format:
	mov al, 0x02
	call fdc_out
	jc .fail
	mov al, [disk_count]
	call fdc_out
	jc .fail
	mov al, [es:si+7]
	call fdc_out
	jc .fail
	mov al, [es:si+8]
	call fdc_out
	jc .fail
	pop es
	xor ah, ah
	ret
.fail:
	pop es
	mov ah, 0x20
	ret

fdc_out:
	push cx
	push dx
	push ax
	mov dx, fdc_status
	xor cx, cx
.wait:
	in al, dx
	and al, 0xC0
	cmp al, 0x80
	jz .send
	loop .wait
	pop ax
	pop dx
	pop cx
	stc
	ret
.send:
	pop ax
	inc dx
	out dx, al
	pop dx
	pop cx
	clc
	ret

sense_interrupt:
	mov al, 0x08
	call fdc_out
	jmp read_results

read_results:
	push cx
	push dx
	push di
	mov di, disk_results
	mov dx, fdc_status
	mov bl, 0
.next:
	xor cx, cx
.wait:
	in al, dx
	test al, 0x80
	jnz .ready
	loop .wait
	mov ah, 0x20
	jmp .done
.ready:
	test al, 0x40
	jz .finished
	cmp bl, 7
	jnb .finished
	inc dx
	in al, dx
	dec dx
	mov [di], al
	inc di
	inc bl
	jmp .next
.finished:
	xor ah, ah
.done:
	pop di
	pop dx
	pop cx
	ret

wait_interrupt:
	push bx
	push cx
	mov bx, [ticks_low]
.loop:
	cli
	test byte [disk_seek_status], 0x80
	jnz .got
	mov cx, [ticks_low]
	sub cx, bx
	cmp cx, 37
	jnb .timeout
	sti
	hlt
	jmp .loop
.got:
	and byte [disk_seek_status], 0x7F
	sti
	xor ah, ah
	pop cx
	pop bx
	ret
.timeout:
	sti
	mov ah, 0x80
	pop cx
	pop bx
	ret

evaluate_results:
	mov al, [disk_results]
	test al, 0xC0
	jz .good
	mov ah, 0x80
	test al, 0x08
	jnz .done
	mov al, [disk_results+1]
	mov ah, 0x04
	test al, 0x80
	jnz .done
	mov ah, 0x10
	test al, 0x20
	jnz .done
	mov ah, 0x08
	test al, 0x10
	jnz .done
	mov ah, 0x04
	test al, 0x04
	jnz .done
	mov ah, 0x03
	test al, 0x02
	jnz .done
	mov ah, 0x02
	test al, 0x01
	jnz .done
	mov al, [disk_results+2]
	mov ah, 0x10
	test al, 0x20
	jnz .done
	mov ah, 0x40
	test al, 0x10
	jnz .done
	mov ah, 0x20
.done:
	ret
.good:
	xor ah, ah
	ret