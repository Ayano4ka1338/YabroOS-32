# System call entry path - (c) Ayano4ka1338, 2026
.section .text
.code64

.global syscall_entry_asm
.global enter_usermode
.global exec_enter_usermode
.global syscall_handler
.global syscall_isr_stub

.extern syscall_dispatch
.extern kernel_rsp
.extern kernel_cr3
.extern syscall_user_rsp
.extern syscall_user_cr3
.extern syscall_saved_rax
.extern syscall_saved_r12
.extern syscall_saved_r13
.extern syscall_user_rip
.extern syscall_user_rflags
.extern syscall_return_frame
.extern user_return_rsp
.extern user_exit_pending
.extern user_exec_pending
.extern task_switch_pending
.extern task_return_ctx
.extern vmm_user_switch
.extern syscall_debug_iret_rip
.extern syscall_debug_iret_rsp
.extern syscall_debug_iret_cr3

.equ CTX_RAX,      0
.equ CTX_RBX,      8
.equ CTX_RCX,     16
.equ CTX_RDX,     24
.equ CTX_RSI,     32
.equ CTX_RDI,     40
.equ CTX_RBP,     48
.equ CTX_R8,      56
.equ CTX_R9,      64
.equ CTX_R10,     72
.equ CTX_R11,     80
.equ CTX_R12,     88
.equ CTX_R13,     96
.equ CTX_R14,    104
.equ CTX_R15,    112
.equ CTX_RSP,    120
.equ CTX_RIP,    128
.equ CTX_RFLAGS, 136
.equ CTX_CR3,    144
.equ CTX_FS_BASE, 152
.equ CTX_GS_BASE, 160

syscall_entry_asm:
	cld

	movq %r12, syscall_saved_r12(%rip)
	movq %r13, syscall_saved_r13(%rip)
	movq %rsp, %r12
	movq %cr3, %r13
	movq %r12, syscall_user_rsp(%rip)
	movq %r13, syscall_user_cr3(%rip)
	movq %rcx, syscall_user_rip(%rip)
	movq %r11, syscall_user_rflags(%rip)
	movq %rax, syscall_saved_rax(%rip)

	movq kernel_cr3(%rip), %rax
	movq %rax, %cr3
	movq kernel_rsp(%rip), %rsp

	movq syscall_saved_rax(%rip), %rax
	movq syscall_saved_r12(%rip), %r12
	movq syscall_saved_r13(%rip), %r13

	pushq syscall_user_cr3(%rip)
	pushq syscall_user_rsp(%rip)
	pushq syscall_user_rflags(%rip)
	pushq syscall_user_rip(%rip)
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax

	movq %rsp, %rdi
	call syscall_dispatch

.check_exec:
	cmpq $0, user_exec_pending(%rip)
	je .check_task_switch
	movq $0, user_exec_pending(%rip)
	movq syscall_user_rip(%rip), %rdi
	movq syscall_user_rsp(%rip), %rsi
	movq syscall_user_cr3(%rip), %rdx



	movq %rdi, syscall_debug_iret_rip(%rip)
	movq %rsi, syscall_debug_iret_rsp(%rip)
	movq %rdx, syscall_debug_iret_cr3(%rip)

	jmp exec_enter_usermode_from_syscall

.check_task_switch:
	cmpq $0, task_switch_pending(%rip)
	je .check_exit
	movq $0, task_switch_pending(%rip)

	movq task_return_ctx(%rip), %r15

	subq $40, %rsp
	movq CTX_RIP(%r15), %rax
	movq %rax, 0(%rsp)
	movq $0x23, %rax
	movq %rax, 8(%rsp)
	movq CTX_RFLAGS(%r15), %rax
	movq %rax, 16(%rsp)
	movq CTX_RSP(%r15), %rax
	movq %rax, 24(%rsp)
	movq $0x2b, %rax
	movq %rax, 32(%rsp)

	movq CTX_FS_BASE(%r15), %rax
	movl %eax, %eax
	movq %rax, %r10
	shrq $32, %r10
	movl %r10d, %edx
	movl $0xC0000100, %ecx
	wrmsr

	movq CTX_GS_BASE(%r15), %rax
	movl %eax, %eax
	movq %rax, %r10
	shrq $32, %r10
	movl %r10d, %edx
	movl $0xC0000101, %ecx
	wrmsr

	movq CTX_CR3(%r15), %r14
	movq %r14, %cr3

	movq CTX_RAX(%r15), %rax
	movq CTX_RBX(%r15), %rbx
	movq CTX_RCX(%r15), %rcx
	movq CTX_RDX(%r15), %rdx
	movq CTX_RSI(%r15), %rsi
	movq CTX_RDI(%r15), %rdi
	movq CTX_RBP(%r15), %rbp
	movq CTX_R8(%r15),  %r8
	movq CTX_R9(%r15),  %r9
	movq CTX_R10(%r15), %r10
	movq CTX_R11(%r15), %r11
	movq CTX_R12(%r15), %r12
	movq CTX_R13(%r15), %r13
	movq CTX_R14(%r15), %r14
	movq CTX_R15(%r15), %r15
	iretq

.check_exit:
	cmpq $0, user_exit_pending(%rip)
	je .normal_return
	movq $0, user_exit_pending(%rip)
	movq kernel_cr3(%rip), %rax
	movq %rax, %cr3
	movq user_return_rsp(%rip), %rsp
	sti
	retq

.normal_return:

	movq %rsp, %r15

	movq 120(%r15), %rax
	movq %rax, 152(%r15)
	movq $0x23, %rax
	movq %rax, 160(%r15)
	movq 128(%r15), %rax
	movq %rax, 168(%r15)
	movq 136(%r15), %rax
	movq %rax, 176(%r15)
	movq $0x2b, %rax
	movq %rax, 184(%r15)

	movq 144(%r15), %rax
	movq %rax, %cr3

	movq   0(%r15), %rax
	movq   8(%r15), %rbx
	movq  16(%r15), %rcx
	movq  24(%r15), %rdx
	movq  32(%r15), %rsi
	movq  40(%r15), %rdi
	movq  48(%r15), %rbp
	movq  56(%r15), %r8
	movq  64(%r15), %r9
	movq  72(%r15), %r10
	movq  80(%r15), %r11
	movq  88(%r15), %r12
	movq  96(%r15), %r13
	movq 104(%r15), %r14

	leaq 152(%r15), %rsp
	movq 112(%r15), %r15
	iretq

syscall_isr_stub:
	cld
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax
	movq %rsp, %rdi
	call syscall_dispatch

	popq %rax
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15
	iretq

exec_enter_usermode:

	cli
	movq %rsp, user_return_rsp(%rip)
	jmp exec_enter_usermode_common

exec_enter_usermode_from_syscall:
	cli

exec_enter_usermode_common:

	pushq $0x2b
	pushq %rsi
	pushq $0x202
	pushq $0x23
	pushq %rdi

	movq $0x2b, %rax
	movq %rax, %ds
	movq %rax, %es
	movq %rax, %fs
	movq %rax, %gs

	movq %rdx, %rax
	movq %rax, %cr3
	iretq

enter_usermode:

	cli
	movq %rsp, user_return_rsp(%rip)

	pushq $0x2b
	pushq %rsi
	pushq $0x202
	pushq $0x23
	pushq %rdi

	movq $0x2b, %rax
	movq %rax, %ds
	movq %rax, %es
	movq %rax, %fs
	movq %rax, %gs

	movq %rdx, %cr3
	iretq

.exec_cr3_invalid:
	cli
	hlt
	jmp .exec_cr3_invalid

