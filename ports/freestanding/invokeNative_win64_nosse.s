# wasmmod OWN Win64 WAMR trampoline. Same argv slots as WAMR
# invokeNative_mingw_x64.s; no XMM (UEFI is -mno-sse).
# rcx = func, rdx = argv, r8 = n_stacks.
	.text
	.align 2
	.globl invokeNative
invokeNative:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rcx, %r10
	movq	%rdx, %rax
	movq	%r8, %rcx
	cmpq	$0, %rcx
	je	cycle_end
	movq	%rsp, %rdx
	andq	$15, %rdx
	jz	no_abort
	int	$3
no_abort:
	movq	%rcx, %rdx
	andq	$1, %rdx
	shlq	$3, %rdx
	subq	%rdx, %rsp
	leaq	56(%rax,%rcx,8), %r9
	subq	%rsp, %r9
cycle:
	pushq	(%rsp,%r9)
	loop	cycle
cycle_end:
	movq	32(%rax), %rcx
	movq	40(%rax), %rdx
	movq	48(%rax), %r8
	movq	56(%rax), %r9
	subq	$32, %rsp
	call	*%r10
	leave
	ret
