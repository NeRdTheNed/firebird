#ifdef __unix__

#define _GNU_SOURCE

#include <sys/mman.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include "os.h"

#include "../debug.h"
#include "../mmu.h"

void *os_reserve(size_t size)
{
    void *ptr = mmap((void*)0x70000000, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    msync(ptr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}

void *os_commit(void *addr, size_t size)
{
    void *ptr = mmap(addr, size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED|MAP_ANON, -1, 0);
	msync(addr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}

void *os_sparse_commit(void *page, size_t size)
{
    void *ptr = mmap(page, size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED|MAP_ANON, -1, 0);
	msync(page, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}

void os_sparse_decommit(void *page, size_t size)
{
	// instead of unmapping the address, we're just gonna trick
	// the TLB to mark this as a new mapped area which, due to
	// demand paging, will not be committed until used.
    mmap(page, size, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0);
    msync(page, size, MS_SYNC|MS_INVALIDATE);
}

void *os_alloc_executable(size_t size)
{
    void *ptr = mmap((void*)0x30000000, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_SHARED|MAP_ANON, -1, 0);
	msync(ptr, size, MS_SYNC|MS_INVALIDATE);
	return ptr;
}

void os_query_time(os_time_t *t)
{
	gettimeofday(t, NULL);
}

double os_time_diff(os_time_t x, os_time_t y)
{
	double t;
	t = (y.tv_sec - x.tv_sec) * 1000.0;	// sec to ms
	t += (y.tv_usec - x.tv_usec) / 1000.0;	// us to ms
	return t;
}

long long os_frequency_hz(os_frequency_t f)
{
    return f;
}

void os_query_frequency(os_frequency_t *f)
{
    *f = 2*1000*1000*1000; // 2GHz, good guess
    FILE *freq = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if(!freq)
        return;

    fscanf(freq, "%Lu", f);
    fclose(freq);
}

void throttle_timer_on()
{
	/*
	hTimerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        uTimerID = timeSetEvent(throttle_delay, throttle_delay, (LPTIMECALLBACK)hTimerEvent, 0,
                                TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
        if (uTimerID == 0) {
                printf("timeSetEvent failed\n");
                exit(1);
        }
	*/
}

void throttle_timer_wait()
{
	//WaitForSingleObject(hTimerEvent, INFINITE);
}

void throttle_timer_off()
{
	/*
        if (uTimerID != 0) {
                timeKillEvent(uTimerID);
                uTimerID = 0;
                CloseHandle(hTimerEvent);
        }
	*/
}

static void addr_cache_exception(int sig, siginfo_t *si, void *uctx)
{
    (void) sig;

    ucontext_t *u = (ucontext_t*) uctx;

#ifdef __i386__
    emuprintf("Got SIGSEGV trying to access 0x%lx (EIP=0x%x)\n", (long) si->si_addr, u->uc_mcontext.gregs[REG_EIP]);
#elif defined(__x86_64__)
    emuprintf("Got SIGSEGV trying to access 0x%lx (RIP=0x%x)\n", (long) si->si_addr, u->uc_mcontext.gregs[REG_RIP]);
#endif

    if(!addr_cache_pagefault((uint8_t*)si->si_addr))
      exit(1);
}

void make_writable(void *addr)
{
    uintptr_t ps = sysconf(_SC_PAGE_SIZE);
    void *prev = (void*)((uintptr_t)(addr) & (~(ps - 1)));
    if(mprotect(prev, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
      emuprintf("mprotect failed.\n");
}

void addr_cache_init(os_exception_frame_t *frame)
{
    (void) frame;
    addr_cache = mmap((void*)0x40000000, AC_NUM_ENTRIES * sizeof(ac_entry), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);

    setbuf(stdout, NULL);

    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = addr_cache_exception;
    if(sigaction(SIGSEGV, &sa, NULL) == -1)
    {
        emuprintf("Failed to initialize SEGV handler.\n");
        exit(1);
    }

    unsigned int i;
    for(i = 0; i < AC_NUM_ENTRIES; ++i)
    {
        AC_SET_ENTRY_INVALID(addr_cache[i], i >> 1 << 10);
    }

	// Relocate the assembly code that wants addr_cache at a fixed address
    extern uint32_t *ac_reloc_start[] __asm__("ac_reloc_start"), *ac_reloc_end[] __asm__("ac_reloc_end");
    uint32_t **reloc;
    for(reloc = ac_reloc_start; reloc != ac_reloc_end; reloc++)
	{
		make_writable(*reloc);
        **reloc += (uintptr_t)addr_cache;
	}
}

#endif
