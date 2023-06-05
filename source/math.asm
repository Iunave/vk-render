%ifndef __MATH_ASM__
%define __MATH_ASM__

section .bss

    struc transform_t
        .rotw resd 1
        .rotx resd 1
        .roty resd 1
        .rotz resd 1
        .posx resd 1
        .posy resd 1
        .posz resd 1
        .sclx resd 1
        .scly resd 1
        .sclz resd 1
    endstruc

    struc packed_transform_t
        .rot_wx resd 1
        .rot_yz resd 1
        .posx resd 1
        .posy resd 1
        .posz resd 1
        .sclx resd 1
        .scly resd 1
        .sclz resd 1
    endstruc

section .data

    align 32
    load_mask: dd 0, 0, -1, -1, -1, -1, -1, -1
    shuffle_mask: db 0, 1, 4, 5, 8, 9, 12, 13, 128, 128, 128, 128, 128, 128, 128, 128
    int16_max: dd 32767.0

section .text

    global upload_entity_transforms ; *dst *src count
upload_entity_transforms:
    test rdx, rdx
    jz .done
    vbroadcastss xmm0, dword[rel int16_max]
    vmovdqa xmm1, oword[rel shuffle_mask]
    vmovaps ymm2, yword[rel load_mask]
.next_upload:
    vmovups xmm3, oword[rsi + transform_t.rotw]
    vmulps xmm3, xmm0
    vcvtps2dq xmm3, xmm3
    vpshufb xmm3, xmm3, xmm1
    vmaskmovps ymm4, ymm2, yword[rsi + transform_t.roty]
    vorps ymm3, ymm3, ymm4
    vmovdqu yword[rdi], ymm3
    add rdi, packed_transform_t_size
    add rsi, transform_t_size
    dec rdx
    jnz .next_upload
.done:
    ret

	global count_mipmap ;uint32 width, uint32 height -> uint32
count_mipmap:
    cmp edi, esi
    cmova edi, esi
    xor esi, esi
    cmp edi, 1
    setbe sil
    add edi, esi ;edi is now >= 1
    bsr eax, edi
    ret

	global abs_flt64
abs_flt64:
	vpcmpeqq ymm1, ymm1
	vpsrlq ymm1, 1
	vpand ymm0, ymm1
	ret

	global abs_flt32
abs_flt32:
	vpcmpeqq ymm1, ymm1
	vpsrld ymm1, 1
	vpand ymm0, ymm1
	ret
	
	global abs_int64
abs_int64:
	mov rax, rdi
	neg rdi
	test rax, rax
	cmovs rax, rdi
	ret
	
	global randnormal; -> float64 (0 to 1)
randnormal:
	rdrand rax
	jnc randnormal
	mov rdi, 0x7FFFFFFFFFFFFFFF
	and rax, rdi
	vcvtsi2sd xmm0, rax
	vcvtsi2sd xmm1, rdi
	vdivsd xmm0, xmm1
	ret

	global randrange_flt ; float64, float64 -> float64
randrange_flt:
	vmovsd xmm2, xmm0
	vmovsd xmm3, xmm1
	call randnormal
	vsubsd xmm3, xmm2
	vfmadd132sd xmm0, xmm2, xmm3
	ret
	
	global randrange_int ; int64, int64 -> int64
randrange_int:
	vcvtsi2sd xmm2, rdi
	vcvtsi2sd xmm3, rsi
	call randnormal
	vsubsd xmm3, xmm2
	vfmadd132sd xmm0, xmm2, xmm3
	vcvtsd2si rax, xmm0
	ret
%endif


