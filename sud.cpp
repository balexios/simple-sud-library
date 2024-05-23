/*
 * SUD as a library concept
*/

#include <sys/prctl.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sched.h>
#include "sud.h"

// taken from https://github.com/ColinIanKing/stress-ng/blob/master/stress-usersyscall.c
#ifndef SYS_USER_DISPATCH
# define SYS_USER_DISPATCH 2 /* syscall user dispatch triggered */
#endif

#ifndef CLONE_CLEAR_SIGHAND
#define CLONE_CLEAR_SIGHAND 0x100000000ULL
#endif

extern "C" void* (syscall_dispatcher_start)(void);
extern "C" void* (syscall_dispatcher_end)(void);
extern "C" long enter_syscall(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

char sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;

template<size_t NUM>
static constexpr long long SYSCALL_ARG(const gregset_t gregs) {
	static_assert(NUM > 0 && NUM <= 6);
	return (long long[]){gregs[REG_RDI], gregs[REG_RSI], gregs[REG_RDX], gregs[REG_R10], gregs[REG_R8], gregs[REG_R9]}[NUM-1];
}

void ____asm_impl(void) {
	/*
	 * enter_syscall triggers a kernel-space system call
	 */
	asm volatile (
		".globl enter_syscall \n\t"
		"enter_syscall: \n\t"
		"movq %rdi, %rax \n\t"
		"movq %rsi, %rdi \n\t"
		"movq %rdx, %rsi \n\t"
		"movq %rcx, %rdx \n\t"
		"movq %r8, %r10 \n\t"
		"movq %r9, %r8 \n\t"
		"movq 8(%rsp),%r9 \n\t"
		"syscall \n\t"
		"ret \n\t"
	);
}

static void handle_sigsys(int sig, siginfo_t* info, void* ucontextv) {
	sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;

	assert(sig == SIGSYS);
	assert(info->si_signo == SIGSYS);
	assert(info->si_code == SYS_USER_DISPATCH);
	assert(info->si_errno == 0);

	// emulate the system call
	const auto uctxt = (ucontext_t*)ucontextv;
	const auto gregs = uctxt->uc_mcontext.gregs;
	assert(gregs[REG_RAX] == info->si_syscall);

	fprintf(stderr, "SUD trapping (syscall number = %d)\n", (int)info->si_syscall);
	gregs[REG_RAX] = enter_syscall(info->si_syscall, SYSCALL_ARG<1>(gregs), SYSCALL_ARG<2>(gregs), SYSCALL_ARG<3>(gregs), SYSCALL_ARG<4>(gregs), SYSCALL_ARG<5>(gregs), SYSCALL_ARG<6>(gregs));

	sud_selector = SYSCALL_DISPATCH_FILTER_BLOCK;

	asm volatile (
		"movq $0xf, %%rax \n\t"
		"leaveq \n\t"
		"add $0x8, %%rsp \n\t"
		".globl syscalll_dispatcher_start \n\t"
		"syscall_dispatcher_start: \n\t"
		"syscall \n\t"
		"nop \n\t"
		".globl syscall_dispatcher_end \n\t"
		"syscall_dispatcher_end: \n\t"
		:
		:
		: "rsp", "rax", "cc" // for some stupid reason adding rsp in the clobbers is considered deprecated ... so we need to use -Wno-deprecated flag
	);
}

static void sud_setup() {
	struct sigaction act = {};
	act.sa_sigaction = handle_sigsys;
	act.sa_flags = SA_SIGINFO;

	ASSERT_ELSE_PERROR(sigemptyset(&act.sa_mask) == 0);
	ASSERT_ELSE_PERROR(sigaction(SIGSYS, &act, NULL) == 0);

	ASSERT_ELSE_PERROR(prctl(PR_SET_SYSCALL_USER_DISPATCH,
							 PR_SYS_DISPATCH_ON,
							 syscall_dispatcher_start,
							 ((int64_t)syscall_dispatcher_end - (int64_t)syscall_dispatcher_start + 1),
							 &sud_selector) == 0);

	sud_selector = SYSCALL_DISPATCH_FILTER_BLOCK;
}

//
// (To build) Use make or cmake
// (To run) LD_PRELOAD=./libsud.so <program>
// (Tested with) -O0, -O3, clang++, g++
// (Tested on) /bin/ls, /bin/pwd
//
__attribute__((constructor(0xffff)))
static void __sud_init(void) {
	sud_setup();
}
