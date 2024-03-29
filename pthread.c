#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dis-asm.h>
#include <sched.h>
#include <dlfcn.h>
#include <pthread.h>
#include <abt.h>
#include <map>
#include <set>
#include <stack>
#include "common.h"
#include "real_pthread.h"

#define MAX_CORE (4)
#define MAX_TH (1024)

//#define __PTHREAD_VERBOSE__ (1)

int n_abt_thread = 0;
ABT_xstream abt_xstreams[MAX_CORE];
//ABT_thread abt_threads[MAX_TH];
ABT_pool abt_pools[MAX_CORE];
ABT_mutex mutex_map_mutex;
ABT_mutex cond_map_mutex;

typedef int tid_t;
typedef struct {
  ABT_thread abt_thread;
  tid_t tid;
} myth_t;

static std::map<tid_t, myth_t *> th_map;
static std::stack<myth_t *> myth_pool;
static std::map<pthread_cond_t *, ABT_cond *> cond_map;
static std::map<pthread_mutex_t *, ABT_mutex *> mutex_map;
static std::map<pthread_key_t, ABT_key *> key_map;
//tbb::concurrent_hash_map<pthread_cond_t *, ABT_cond *> cond_map;
//tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *> mutex_map2;
//tbb::concurrent_hash_map<pthread_key_t *, ABT_key *> key_map;


#define NEW_ABT_INIT (0)

static void
my_abt_init()
{
  ABT_init(0, NULL);
}

__attribute__((constructor(0xffff))) static void
//static void
ensure_abt_initialized()
{
  if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
    int ret;
    pthread_t pth;
#if NEW_ABT_INIT
    real_pthread_create(&pth, NULL, my_abt_init, NULL);
    real_pthread_join(pth, NULL);
#else
    ret = ABT_init(0, NULL);
#endif
    for (int i=0; i<MAX_CORE; i++) {
      ret = ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
      ret = ABT_xstream_get_main_pools(abt_xstreams[i], 1, &abt_pools[i]);
    }
    ABT_mutex_create(&mutex_map_mutex);
    ABT_mutex_create(&cond_map_mutex);
  }
}

int pthread_create(pthread_t *pth, const pthread_attr_t *attr,
		   void *(*start_routine) (void *), void *arg) {

  myth_t *myth;
  if (myth_pool.empty()) {
    myth = (myth_t *)malloc(sizeof(myth_t));
    myth->tid = n_abt_thread++;
  } else {
    myth = myth_pool.top();
    myth_pool.pop();
  }
  int ret = ABT_thread_create(abt_pools[myth->tid % MAX_CORE], start_routine, arg,
			      ABT_THREAD_ATTR_NULL, &myth->abt_thread);
  *(tid_t *)pth = myth->tid;
  th_map[myth->tid] = myth;
  //printf("%p %p %p\n", pth, myth, &myth->abt_thread);
#if __PTHREAD_VERBOSE__
  printf("%s %d %d %p\n", __func__, __LINE__, ret, arg);
#endif
  if (ret) {
    printf("error %d\n", ret);
  }
  assert(ret == 0);
  return ret;
}

#define NEW_JOIN (1)

void
my_join(void *arg)
{
  myth_t *myth = (myth_t *)arg;
  ABT_thread_join(myth->abt_thread);
  myth_pool.push(myth);
}

int pthread_join(pthread_t pth, void **retval) {
  assert(retval == NULL);
#if NEW_JOIN
  pthread_t pth_for_join;
  tid_t tid = (tid_t)pth;
  //printf("%p %p %p\n", &pth, th_map[pth], &(th_map[pth]->abt_thread));
  real_pthread_create(&pth_for_join, NULL, my_join, th_map[tid]);
  real_pthread_join(pth_for_join, NULL);
#else
  ABT_thread_join(abt_threads[pth]);
#endif
  return 0;
}

#if 1
int sched_yield() {
  uint64_t id;
  int ret2 = ABT_self_get_thread_id(&id);
  //printf("%s %d %lu %d\n", __func__, __LINE__, id, ret2);
  
  return ABT_thread_yield();
}
#endif


int pthread_cond_init(pthread_cond_t *cond,
		      const pthread_condattr_t *attr) {
  ABT_cond *abt_cond = (ABT_cond *)malloc(sizeof(ABT_cond));
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  int ret = ABT_cond_create(abt_cond);
  ABT_mutex_lock(cond_map_mutex);
  cond_map[cond] = abt_cond;
  ABT_mutex_unlock(cond_map_mutex);
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  return ret;
}

int pthread_cond_signal(pthread_cond_t *cond) {
  return ABT_cond_signal(*(cond_map[cond]));
}

int pthread_cond_destroy(pthread_cond_t *cond) {
  return ABT_cond_free(cond_map[cond]);
}

int pthread_cond_wait(pthread_cond_t *cond,
		      pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_cond_wait(*(cond_map[cond]), *abt_mutex);
#else
  auto it = cond_map.find(cond);
  if (it == cond_map.end())
    pthread_cond_init(cond, NULL);
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  return ABT_cond_wait(*(cond_map[cond]), *(mutex_map[mutex]));
#endif
}

int pthread_cond_timedwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex,
			   const struct timespec *abstime) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_cond_timedwait(*(cond_map[cond]), *abt_mutex, abstime);
#else
  return ABT_cond_timedwait(*(cond_map[cond]), *(mutex_map[mutex]), abstime);
#endif
}

int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr) {
  ABT_mutex *abt_mutex = (ABT_mutex *)malloc(sizeof(ABT_mutex));
  int ret = ABT_mutex_create(abt_mutex);
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::accessor ac;
  mutex_map2.insert(ac, mutex);
  ac->second = abt_mutex;
  ac.release();
#endif
  ABT_mutex_lock(mutex_map_mutex);
  mutex_map[mutex] = abt_mutex;
  ABT_mutex_unlock(mutex_map_mutex);
  //printf("%s %d %p %p\n", __func__, __LINE__, mutex, abt_mutex);
  return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
#if 0
  printf("%s %d %p\n", __func__, __LINE__, mutex);
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::accessor ac;
  bool found = mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex;
  if (found) {
    abt_mutex = ac->second;
    ac.release();
  } else {
    abt_mutex = (ABT_mutex *)malloc(sizeof(ABT_mutex));
    int ret = ABT_mutex_create(abt_mutex);
    mutex_map.insert(ac, mutex);
    ac->second = abt_mutex;
    ac.release();
  }
  printf("%s %d %p\n", __func__, __LINE__, mutex);
  return ABT_mutex_lock(*abt_mutex);
#else
  auto it = mutex_map.find(mutex);
  if (it == mutex_map.end())
    return pthread_mutex_init(mutex, NULL);
#if 1//__PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, mutex);
#endif
  return ABT_mutex_lock(*(mutex_map[mutex]));
#endif
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_mutex_trylock(*abt_mutex);
#else
  auto it = mutex_map.find(mutex);
  if (it == mutex_map.end())
    pthread_mutex_init(mutex, NULL);
  return ABT_mutex_trylock(*(mutex_map[mutex]));
#endif
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_mutex_unlock(*abt_mutex);
#else
  return ABT_mutex_unlock(*(mutex_map[mutex]));
#endif
}

int pthread_attr_init(pthread_attr_t *attr) {
  return 0;
};

int pthread_attr_setdetachstate(pthread_attr_t *attr, int) {
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
  return 0;
};

#if 0
int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
  ABT_key *abt_key = (ABT_key *)malloc(sizeof(ABT_key));
  printf("%s %d %p %p\n", __func__, __LINE__, key, abt_key);
  int ret = ABT_key_create(destructor, abt_key);
  key_map[*key] = abt_key;
  return ret;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, key);
#endif
  return ABT_key_set(*(key_map[key]), value);
}

void * pthread_getspecific(pthread_key_t key) {
  void *ret;
  ABT_key_get(*(key_map[key]), &ret);
  return ret;
}
#endif
int pthread_setname_np(pthread_t thread, const char *name) {
  return 0;
}







//#define SUPPLEMENTAL__REWRITTEN_ADDR_CHECK 1

#ifdef SUPPLEMENTAL__REWRITTEN_ADDR_CHECK

/*
 * SUPPLEMENTAL: rewritten address check
 *
 * NOTE: this ifdef section is supplemental.
 *       if you wish to quicly know the core
 *       mechanism of zpoline, please skip here.
 *
 * the objective of this part is to terminate
 * a null pointer function call.
 *
 * an example is shown below.
 * --
 * void (*null_fn)(void) = NULL;
 *
 * int main(void) {
 *   null_fn();
 *   return 0;
 * }
 * --
 *
 * usually, the code above will cause a segmentation
 * fault because no memory is mapped to address 0 (NULL).
 *
 * however, zpoline maps memory to address 0. therefore, the
 * code above continues to run without causing the fault.
 *
 * this behavior is unusual, thus, we wish to avoid this.
 *
 * our approach here is:
 *
 *   1. during the binrary rewriting phase, record
 *      the addresses of the rewritten syscall/sysenter
 *      instructions (record_replaced_instruction_addr).
 *
 *   2. in the hook function, we check wheter the caller's
 *      address is the one that we conducted the rewriting
 *      or not (is_replaced_instruction_addr).
 *
 *      if not, it means that the program reaches the hook
 *      funtion without going through our replaced callq *%rax.
 *      this typically occurs the program was like the example
 *      code above. after we detect this type of irregular hook
 *      entry, we terminate the program.
 *
 * assuming 0xffffffffffff (256TB : ((1UL << 48) - 1)) as max virtual address (48-bit address)
 *
 */

#define BM_SIZE ((1UL << 48) >> 3)
static char *bm_mem = NULL;

static void bitmap_set(char bm[], unsigned long val)
{
	bm[val >> 3] |= (1 << (val & 7));
}

static bool is_bitmap_set(char bm[], unsigned long val)
{
	return (bm[val >> 3] & (1 << (val & 7)) ? true : false);
}

static void record_replaced_instruction_addr(uintptr_t addr)
{
	assert(addr < (1UL << 48));
	bitmap_set(bm_mem, addr);
}

static bool is_replaced_instruction_addr(uintptr_t addr)
{
	assert(addr < (1UL << 48));
	return is_bitmap_set(bm_mem, addr);
}

#endif

extern "C" void syscall_addr(void);
extern "C" long enter_syscall(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
extern "C" void asm_syscall_hook(void);


extern "C" {
void ____asm_impl(void)
{
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
	".globl syscall_addr \n\t"
	"syscall_addr: \n\t"
	"syscall \n\t"
	"ret \n\t"
	);

	/*
	 * asm_syscall_hook is the address where the
	 * trampoline code first lands.
	 *
	 * the procedure below calls the C function
	 * named syscall_hook.
	 *
	 * at the entry point of this,
	 * the register values follow the calling convention
	 * of the system calls.
	 *
	 * this part is a bit complicated.
	 * commit e5afaba has a bit simpler versoin.
	 *
	 */
	asm volatile (
	".globl asm_syscall_hook \n\t"
	"asm_syscall_hook: \n\t"
	"popq %rax \n\t" /* restore %rax saved in the trampoline code */

	/* discard pushed 0x90 for 0xeb 0x6a 0x90 if rax is n * 3 + 1 */
	"pushq %rdi \n\t"
	"pushq %rax \n\t"
	"movabs $0xaaaaaaaaaaaaaaab, %rdi \n\t"
	"imul %rdi, %rax \n\t"
	"cmp %rdi, %rax \n\t"
	"popq %rax \n\t"
	"popq %rdi \n\t"
	"jb skip_pop \n\t"
	"addq $8, %rsp \n\t"
	"skip_pop: \n\t"

	"cmpq $15, %rax \n\t" // rt_sigreturn
	"je do_rt_sigreturn \n\t"
	"pushq %rbp \n\t"
	"movq %rsp, %rbp \n\t"

	/*
	 * NOTE: for xmm register operations such as movaps
	 * stack is expected to be aligned to a 16 byte boundary.
	 */

	"andq $-16, %rsp \n\t" // 16 byte stack alignment

	/* assuming callee preserves r12-r15 and rbx  */

	"pushq %r11 \n\t"
	"pushq %r9 \n\t"
	"pushq %r8 \n\t"
	"pushq %rdi \n\t"
	"pushq %rsi \n\t"
	"pushq %rdx \n\t"
	"pushq %rcx \n\t"

	/* arguments for syscall_hook */

	"pushq 8(%rbp) \n\t"	// return address
	"pushq %rax \n\t"
	"pushq %r10 \n\t"

	/* up to here, stack has to be 16 byte aligned */

	"callq syscall_hook \n\t"

	"popq %r10 \n\t"
	"addq $16, %rsp \n\t"	// discard arg7 and arg8

	"popq %rcx \n\t"
	"popq %rdx \n\t"
	"popq %rsi \n\t"
	"popq %rdi \n\t"
	"popq %r8 \n\t"
	"popq %r9 \n\t"
	"popq %r11 \n\t"

	"leaveq \n\t"

	"retq \n\t"

	"do_rt_sigreturn:"
	"addq $8, %rsp \n\t"
	"jmp syscall_addr \n\t"
	);
}

  
static long (*hook_fn)(int64_t a1, int64_t a2, int64_t a3,
		       int64_t a4, int64_t a5, int64_t a6,
		       int64_t a7) = enter_syscall;

extern void (*debug_print)(int, int, int);
long syscall_hook(int64_t rdi, int64_t rsi,
		  int64_t rdx, int64_t __rcx __attribute__((unused)),
		  int64_t r8, int64_t r9,
		  int64_t r10_on_stack /* 4th arg for syscall */,
		  int64_t rax_on_stack,
		  int64_t retptr)
{
#ifdef SUPPLEMENTAL__REWRITTEN_ADDR_CHECK
	/*
	 * retptr is the caller's address, namely.
	 * "supposedly", it should be callq *%rax that we replaced.
	 */
	if (!is_replaced_instruction_addr(retptr - 2 /* 2 is the size of syscall/sysenter */)) {
		/*
		 * here, we detected that the program comes here
		 * without going through our replaced callq *%rax.
		 *
		 * this can should a bug of the program.
		 *
		 * therefore, we stop the program by int3.
		 */
		asm volatile ("int3");
	}
#endif

	if (rax_on_stack == __NR_clone3)
		return -ENOSYS; /* workaround to trigger the fallback to clone */

	if (rax_on_stack == __NR_clone) {
		if (rdi & CLONE_VM) { // pthread creation
			/* push return address to the stack */
			rsi -= sizeof(uint64_t);
			*((uint64_t *) rsi) = retptr;
		}
	}

	return hook_fn(rax_on_stack, rdi, rsi, rdx, r10_on_stack, r8, r9);
}

struct disassembly_state {
	char *code;
	size_t off;
};

/*
 * this actually rewrites the code.
 * this is called by the disassembler.
 */
#ifdef NEW_DIS_ASM
static int do_rewrite(void *data, enum disassembler_style style ATTRIBUTE_UNUSED, const char *fmt, ...)
#else
static int do_rewrite(void *data, const char *fmt, ...)
#endif
{
	struct disassembly_state *s = (struct disassembly_state *) data;
	char buf[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(buf, fmt, arg);
	/* replace syscall and sysenter with callq *%rax */
	if (!strncmp(buf, "syscall", 7) || !strncmp(buf, "sysenter", 8)) {
		uint8_t *ptr = (uint8_t *)(((uintptr_t) s->code) + s->off);
		if ((uintptr_t) ptr == (uintptr_t) syscall_addr) {
			/*
			 * skip the syscall replacement for
			 * our system call hook (enter_syscall)
			 * so that it can issue system calls.
			 */
			goto skip;
		}
		ptr[0] = 0xff; // callq
		ptr[1] = 0xd0; // *%rax
#ifdef SUPPLEMENTAL__REWRITTEN_ADDR_CHECK
		record_replaced_instruction_addr((uintptr_t) ptr);
#endif
	}
skip:
	va_end(arg);
	return 0;
}

/* find syscall and sysenter using the disassembler, and rewrite them */
static void disassemble_and_rewrite(char *code, size_t code_size, int mem_prot)
{
	struct disassembly_state s = { 0 };
	/* add PROT_WRITE to rewrite the code */
	assert(!mprotect(code, code_size, PROT_WRITE | PROT_READ | PROT_EXEC));
	disassemble_info disasm_info = { 0 };
#ifdef NEW_DIS_ASM
	init_disassemble_info(&disasm_info, &s, (fprintf_ftype) printf, do_rewrite);
#else
	init_disassemble_info(&disasm_info, &s, do_rewrite);
#endif
	disasm_info.arch = bfd_arch_i386;
	disasm_info.mach = bfd_mach_x86_64;
	disasm_info.buffer = (bfd_byte *) code;
	disasm_info.buffer_length = code_size;
	disassemble_init_for_target(&disasm_info);
	disassembler_ftype disasm;
	disasm = disassembler(bfd_arch_i386, false, bfd_mach_x86_64, NULL);
	s.code = code;
	while (s.off < code_size)
		s.off += disasm(s.off, &disasm_info);
	/* restore the memory protection */
	assert(!mprotect(code, code_size, mem_prot));
}

/* entry point for binary rewriting */
static void rewrite_code(void)
{
	FILE *fp;
	/* get memory mapping information from procfs */
	assert((fp = fopen("/proc/self/maps", "r")) != NULL);
	{
		char buf[4096];
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			/* we do not touch stack and vsyscall memory */
			if (((strstr(buf, "stack") == NULL) && (strstr(buf, "vsyscall") == NULL))) {
				int i = 0;
				char addr[65] = { 0 };
				char *c = strtok(buf, " ");
				while (c != NULL) {
					switch (i) {
					case 0:
						strncpy(addr, c, sizeof(addr) - 1);
						break;
					case 1:
						{
							int mem_prot = 0;
							{
								size_t j;
								for (j = 0; j < strlen(c); j++) {
									if (c[j] == 'r')
										mem_prot |= PROT_READ;
									if (c[j] == 'w')
										mem_prot |= PROT_WRITE;
									if (c[j] == 'x')
										mem_prot |= PROT_EXEC;
								}
							}
							/* rewrite code if the memory is executable */
							if (mem_prot & PROT_EXEC) {
								size_t k;
								for (k = 0; k < strlen(addr); k++) {
									if (addr[k] == '-') {
										addr[k] = '\0';
										break;
									}
								}
								{
									int64_t from, to;
									from = strtol(&addr[0], NULL, 16);
									if (from == 0) {
										/*
										 * this is trampoline code.
										 * so skip it.
										 */
										break;
									}
									to = strtol(&addr[k + 1], NULL, 16);
									disassemble_and_rewrite((char *) from,
											(size_t) to - from,
											mem_prot);
								}
							}
						}
						break;
					}
					if (i == 1)
						break;
					c = strtok(NULL, " ");
					i++;
				}
			}
		}
	}
	fclose(fp);
}

#define NR_syscalls (512) // bigger than max syscall number

static void setup_trampoline(void)
{
	void *mem;

	/* allocate memory at virtual address 0 */
	mem = mmap(0 /* virtual address 0 */, 0x1000,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
			-1, 0);
	if (mem == MAP_FAILED) {
		fprintf(stderr, "map failed\n");
		fprintf(stderr, "NOTE: /proc/sys/vm/mmap_min_addr should be set 0\n");
		exit(1);
	}

	{
		/*
		 * optimized instructions to slide down
		 * repeat of 0xeb 0x6a 0x90
		 *
		 * case 1 : jmp to n * 3 + 0
		 * jmp 0x6a
		 * nop
		 * jmp 0x6a
		 * nop
		 *
		 * case 2 : jmp to n * 3 + 1
		 * push 0x90
		 * jmp 0x6a
		 * nop
		 * jmp 0x6a
		 *
		 * case 3 : jmp to n * 3 + 2
		 * nop
		 * jmp 0x6a
		 * nop
		 * jmp 0x6a
		 *
		 * for case 2, we discard 0x90 pushed to stack
		 *
		 */
		int i;
		for (i = 0; i < NR_syscalls; i++) {
			if (NR_syscalls - 0x6a - 2 < i)
				((uint8_t *) mem)[i] = 0x90;
			else {
				int x = i % 3;
				switch (x) {
				case 0:
					((uint8_t *) mem)[i] = 0xeb;
					break;
				case 1:
					((uint8_t *) mem)[i] = 0x6a;
					break;
				case 2:
					((uint8_t *) mem)[i] = 0x90;
					break;
				}
			}
		}
	}

	/* 
	 * put code for jumping to asm_syscall_hook.
	 *
	 * here we embed the following code.
	 *
	 * push   %rax
	 * movabs [asm_syscall_hook],%rax
	 * jmpq   *%rax
	 *
	 */

	/*
	 * save %rax on stack before overwriting
	 * with "movabs [asm_syscall_hook],%rax",
	 * and the saved value is resumed in asm_syscall_hook.
	 */
	// 50                      push   %rax
	((uint8_t *) mem)[NR_syscalls + 0x0] = 0x50;

	// 48 b8 [64-bit addr (8-byte)]   movabs [asm_syscall_hook],%rax
	((uint8_t *) mem)[NR_syscalls + 0x1] = 0x48;
	((uint8_t *) mem)[NR_syscalls + 0x2] = 0xb8;
	((uint8_t *) mem)[NR_syscalls + 0x3] = ((uint64_t) asm_syscall_hook >> (8 * 0)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x4] = ((uint64_t) asm_syscall_hook >> (8 * 1)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x5] = ((uint64_t) asm_syscall_hook >> (8 * 2)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x6] = ((uint64_t) asm_syscall_hook >> (8 * 3)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x7] = ((uint64_t) asm_syscall_hook >> (8 * 4)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x8] = ((uint64_t) asm_syscall_hook >> (8 * 5)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0x9] = ((uint64_t) asm_syscall_hook >> (8 * 6)) & 0xff;
	((uint8_t *) mem)[NR_syscalls + 0xa] = ((uint64_t) asm_syscall_hook >> (8 * 7)) & 0xff;

	// ff e0                   jmpq   *%rax
	((uint8_t *) mem)[NR_syscalls + 0xb] = 0xff;
	((uint8_t *) mem)[NR_syscalls + 0xc] = 0xe0;

	/*
	 * mprotect(PROT_EXEC without PROT_READ), executed
	 * on CPUs supporting Memory Protection Keys for Userspace (PKU),
	 * configures this memory region as eXecute-Only-Memory (XOM).
	 * this enables to cause a segmentation fault for a NULL pointer access.
	 */
	assert(!mprotect(0, 0x1000, PROT_EXEC));
}

extern "C" int __hook_init(long placeholder __attribute__((unused)),
			   void *sys_call_hook_ptr);

static void load_hook_lib(void)
{
#if 0
	void *handle;
	{
		const char *filename;
		filename = getenv("LIBZPHOOK");
		if (!filename) {
			fprintf(stderr, "env LIBZPHOOK is empty, so skip to load a hook library\n");
			return;
		}

		handle = dlmopen(LM_ID_NEWLM, filename, RTLD_NOW | RTLD_LOCAL);
		if (!handle) {
			fprintf(stderr, "dlmopen failed: %s\n\n", dlerror());
			fprintf(stderr, "NOTE: this may occur when the compilation of your hook function library misses some specifications in LDFLAGS. or if you are using a C++ compiler, dlmopen may fail to find a symbol, and adding 'extern \"C\"' to the definition may resolve the issue.\n");
			exit(1);
		}
	}
	{
		int (*hook_init)(long, ...);
		hook_init = dlsym(handle, "__hook_init");
		assert(hook_init);
#ifdef SUPPLEMENTAL__REWRITTEN_ADDR_CHECK
		assert(hook_init(0, &hook_fn, bm_mem) == 0);
#else
		assert(hook_init(0, &hook_fn) == 0);
#endif
	}
#else
	assert(__hook_init(0, &hook_fn) == 0);
#endif
}

__attribute__((constructor(0xffff))) static void __zpoline_init(void)
{
#ifdef SUPPLEMENTAL__REWRITTEN_ADDR_CHECK
	assert((bm_mem = mmap(NULL, BM_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
			-1, 0)) != MAP_FAILED);
#endif
	setup_trampoline();
	rewrite_code();
	load_hook_lib();
}
}
