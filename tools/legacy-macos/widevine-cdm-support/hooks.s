// Canonicalizing objc_msgSend hooks (x86_64).
//
// The Widevine CDM passes embedded C-string method names as selectors; old
// libobjc runtimes only match selectors by pointer and abort. The hooks
// register the name (a no-op for already-canonical selectors) and tail-call
// the real functions. All argument registers (including xmm0-7 for
// floating-point variadic arguments) must survive the canonicalization call.
//
// %al carries the number of XMM registers holding variadic arguments; a
// variadic method implementation reads it from va_start, so it is saved
// across the call and restored after %rax has been consumed as the
// canonical selector. objc_msgSend_stret takes the return buffer in %rdi,
// so for it the selector is %rdx, not %rsi.

.macro SAVE_ARGS FRAME
  subq  \FRAME, %rsp
  movq  %rdi, -0x08(%rbp)
  movq  %rsi, -0x10(%rbp)
  movq  %rdx, -0x18(%rbp)
  movq  %rcx, -0x20(%rbp)
  movq  %r8,  -0x28(%rbp)
  movq  %r9,  -0x30(%rbp)
  movdqu %xmm0, -0x40(%rbp)
  movdqu %xmm1, -0x50(%rbp)
  movdqu %xmm2, -0x60(%rbp)
  movdqu %xmm3, -0x70(%rbp)
  movdqu %xmm4, -0x80(%rbp)
  movdqu %xmm5, -0x90(%rbp)
  movdqu %xmm6, -0xA0(%rbp)
  movdqu %xmm7, -0xB0(%rbp)
  movb  %al, -0xB8(%rbp)
.endm

.macro RESTORE_ARGS
  movq  -0x08(%rbp), %rdi
  movq  -0x18(%rbp), %rdx
  movq  -0x20(%rbp), %rcx
  movq  -0x28(%rbp), %r8
  movq  -0x30(%rbp), %r9
  movdqu -0x40(%rbp), %xmm0
  movdqu -0x50(%rbp), %xmm1
  movdqu -0x60(%rbp), %xmm2
  movdqu -0x70(%rbp), %xmm3
  movdqu -0x80(%rbp), %xmm4
  movdqu -0x90(%rbp), %xmm5
  movdqu -0xA0(%rbp), %xmm6
  movdqu -0xB0(%rbp), %xmm7
.endm

// Canonicalize the selector in \SELREG and leave it back in \SELREG; %rsi is
// not restored, so only hooks whose selector is %rsi may use this.
.macro CANON_SEL SELREG
  movq  \SELREG, %rdi
  callq _canon_sel_c
  RESTORE_ARGS
  movq  %rax, \SELREG
  movb  -0xB8(%rbp), %al
.endm

.text
.globl _objc_msgSend
_objc_msgSend:
  pushq %rbp
  movq  %rsp, %rbp
  SAVE_ARGS $0xC0
  CANON_SEL %rsi
  movq  %rbp, %rsp
  popq  %rbp
  jmp   *_real_msgSend(%rip)

.globl _objc_msgSend_fpret
_objc_msgSend_fpret:
  pushq %rbp
  movq  %rsp, %rbp
  SAVE_ARGS $0xC0
  CANON_SEL %rsi
  movq  %rbp, %rsp
  popq  %rbp
  jmp   *_real_msgSend_fpret(%rip)

.globl _objc_msgSendSuper
_objc_msgSendSuper:
  pushq %rbp
  movq  %rsp, %rbp
  SAVE_ARGS $0xC0
  CANON_SEL %rsi
  movq  %rbp, %rsp
  popq  %rbp
  jmp   *_real_msgSendSuper(%rip)

// stret ABI: %rdi return buffer, %rsi receiver, %rdx selector.
.globl _objc_msgSend_stret
_objc_msgSend_stret:
  pushq %rbp
  movq  %rsp, %rbp
  SAVE_ARGS $0xC0
  movq  %rdx, %rdi
  callq _canon_sel_c
  RESTORE_ARGS
  movq  -0x10(%rbp), %rsi
  movq  %rax, %rdx
  movb  -0xB8(%rbp), %al
  movq  %rbp, %rsp
  popq  %rbp
  jmp   *_real_msgSend_stret(%rip)
