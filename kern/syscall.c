/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e100.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
    static void
sys_cputs(const char *s, size_t len)
{
    // Check that the user has permission to read memory [s, s+len).
    // Destroy the environment if not.

    // LAB 3: Your code here.
    user_mem_assert(curenv, (const void *)s, len, PTE_P);

    // Print the string supplied by the user.
    cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
    static int
sys_cgetc(void)
{
    return cons_getc();
}

// Returns the current environment's envid.
    static envid_t
sys_getenvid(void)
{
    return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_destroy(envid_t envid)
{
    int r;
    struct Env *e;

    if ((r = envid2env(envid, &e, 1)) < 0)
        return r;
    env_destroy(e);
    return 0;
}

// Deschedule current environment and pick a different one to run.
    static void
sys_yield(void)
{
    sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
    static envid_t
sys_exofork(void)
{
    // Create the new environment with env_alloc(), from kern/env.c.
    // It should be left as env_alloc created it, except that
    // status is set to ENV_NOT_RUNNABLE, and the register set is copied
    // from the current environment -- but tweaked so sys_exofork
    // will appear to return 0.

    // LAB 4: Your code here.
    struct Env *env = NULL;
    if (0 != env_alloc(&env, curenv->env_id))
        return -E_NO_FREE_ENV;
    env->env_status = ENV_NOT_RUNNABLE;
    env->env_tf = curenv->env_tf;
    env->env_tf.tf_regs.reg_eax = 0;
    return env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
    static int
sys_env_set_status(envid_t envid, int status)
{
    // Hint: Use the 'envid2env' function from kern/env.c to translate an
    // envid to a struct Env.
    // You should set envid2env's third argument to 1, which will
    // check whether the current environment has permission to set
    // envid's status.

    // LAB 4: Your code here.
    struct Env *env;
    if (0 != envid2env(envid, &env, true))
        return -E_BAD_ENV;

    if (ENV_RUNNABLE != env->env_status && ENV_NOT_RUNNABLE != env->env_status)
        return -E_INVAL;

    env->env_status = status;
    return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
    // LAB 5: Your code here.
    // Remember to check whether the user has supplied us with a good
    // address!

    struct Env *env;
    if (0 != envid2env(envid, &env, true))
        return -E_BAD_ENV;

    user_mem_assert(env, (const void *)tf, 0, PTE_P);
    env->env_tf = *tf;

    return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
    // LAB 4: Your code here.
    struct Env *env;
    if (0 != envid2env(envid, &env, true))
        return -E_BAD_ENV;
    env->env_pgfault_upcall = func;
    return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_USER in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
    static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
    // Hint: This function is a wrapper around page_alloc() and
    //   page_insert() from kern/pmap.c.
    //   Most of the new code you write should be to check the
    //   parameters for correctness.
    //   If page_insert() fails, remember to free the page you
    //   allocated!

    // LAB 4: Your code here.
    struct Env *env = NULL;
    struct Page *pag = NULL;
    if ((void *)UTOP < va || 0 != (unsigned int)va % PGSIZE)
        return -E_INVAL;
    if (0 != envid2env(envid, &env, true))
        return -E_BAD_ENV;
    if ((PTE_U | PTE_P) != ((PTE_U | PTE_P) & perm))
        return -E_INVAL;
    if (0 != page_alloc(&pag))
        return -E_NO_MEM;
    memset(page2kva(pag), 0, PGSIZE);
    if (0 != page_insert(env->env_pgdir, pag, va, perm))
        return -E_NO_MEM;
    return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
    static int
sys_page_map(envid_t srcenvid, void *srcva,
        envid_t dstenvid, void *dstva, int perm)
{
    // Hint: This function is a wrapper around page_lookup() and
    //   page_insert() from kern/pmap.c.
    //   Again, most of the new code you write should be to check the
    //   parameters for correctness.
    //   Use the third argument to page_lookup() to
    //   check the current permissions on the page.

    // LAB 4: Your code here.
    struct Env *srcenv, *dstenv;
    if ((0 != envid2env(srcenvid, &srcenv, true)) || (0 != envid2env(dstenvid, &dstenv, true)))
        return -E_BAD_ENV;

    if ((void *)UTOP < srcva || ((unsigned int)srcva % PGSIZE != 0) || (void *)UTOP < dstva || ((unsigned int)dstva % PGSIZE) != 0)
        return -E_INVAL;

    struct Page * pag;
    pte_t *pte;
    pag = page_lookup(srcenv->env_pgdir, srcva, &pte);

    if (NULL == pag)
        return -E_INVAL;

    if ((PTE_P | PTE_U) != ((PTE_U | PTE_P) & *pte))
        return -E_INVAL;

    if ((PTE_W == (PTE_W & perm)) && (PTE_W != (PTE_W & *pte)))
        return -E_INVAL;

    if (0 != page_insert(dstenv->env_pgdir, pag, dstva, perm))
        return -E_NO_MEM;

    return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
    static int
sys_page_unmap(envid_t envid, void *va)
{
    // Hint: This function is a wrapper around page_remove().

    // LAB 4: Your code here.
    struct Env *env;
    struct Page *pag;
    pte_t *pte = NULL;
    if ((void *)UTOP < va || (unsigned int)va % PGSIZE != 0)
        return -E_INVAL;
    if (0 != envid2env(envid, &env, true))
        return -E_BAD_ENV;
    pag = page_lookup(env->env_pgdir, va, &pte);
    if (NULL != pag && *pte) {
        *pte = 0;
        page_decref(pag);
    }
    return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
    static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
    // LAB 4: Your code here.
    struct Env *dstenv;
    struct Page *pag;
    pte_t *pte;

    if (0 != envid2env(envid, &dstenv, false))
        return -E_BAD_ENV;

    if ((ENV_NOT_RUNNABLE != dstenv->env_status) || (false == dstenv->env_ipc_recving))
        return -E_IPC_NOT_RECV;

    if ((void *)UTOP > srcva && (void *)UTOP > dstenv->env_ipc_dstva) {
        if (0 != (unsigned)srcva % PGSIZE)
            return -E_INVAL;
        pag = page_lookup(curenv->env_pgdir, srcva, &pte);
        if (NULL == pag || 0 == *pte)
            return -E_INVAL;
        if ((PTE_U | PTE_P) != ((PTE_U | PTE_P) & *pte))
            return -E_INVAL;
        if ((PTE_W == (perm & PTE_W)) && (PTE_W != (PTE_W & *pte)))
            return -E_INVAL;
        if (0 != page_insert(dstenv->env_pgdir, pag, dstenv->env_ipc_dstva, perm))
            return -E_NO_MEM;

        dstenv->env_ipc_perm = perm;
    }
    else
        dstenv->env_ipc_perm = 0;

    dstenv->env_ipc_value = value;
    dstenv->env_ipc_recving = false;
    dstenv->env_ipc_from = curenv->env_id;
    dstenv->env_tf.tf_regs.reg_eax = 0;
    dstenv->env_status = ENV_RUNNABLE;

    return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
    static int
sys_ipc_recv(void *dstva)
{
    // LAB 4: Your code here.

    if ((void *)UTOP > dstva) {
        if (0 != (unsigned)dstva % PGSIZE)
            return -E_INVAL;
        curenv->env_ipc_dstva = dstva;
    }
    else
        curenv->env_ipc_dstva = (void *)UTOP;

    curenv->env_ipc_recving = true;
    curenv->env_status = ENV_NOT_RUNNABLE;

    return 0;
}

// Return the current time.
    static int
sys_time_msec(void) 
{
    // LAB 6: Your code here.
    return time_msec();
}

//Send data from user 
int
sys_net_send(void * src, size_t len)
{
    uint16_t status;
    union Tcb *ptcb = NULL;

    if (NULL == tcb_cu) {
        tcb_cu = cbl_ka;
        noptcb_cu = tcb_cu;
    }
    else {
        ptcb = tcb_cu;
        tcb_cu = e100_next_tcb(tcb_cu);
    }

    if (!e100_tcb_complete(tcb_cu->tcb_transmit.cb.status))
        return -E_RETRY;

    user_mem_assert(curenv, (const void *)src, len, PTE_P);
    e100_cu_int(tcb_cu);
    memmove(tcb_cu->tcb_transmit.data, src, len);
    tcb_cu->tcb_transmit.cb.status = 0;
    tcb_cu->tcb_transmit.tcbbc = len;

    if (NULL != ptcb)
        e100_cu_res(ptcb);

    status = e100_cu_status();
    if (TCB_STATUS_IDLE == status) {
        e100_write_scbgp(PADDR(cbl_ka));
        e100_write_cmd_cu(TCB_CMD_CS);
    }
    else if (TCB_STATUS_SUSPENDED == status) {
        e100_cu_res(noptcb_cu);
        if (noptcb_cu == tcb_cu) {
            noptcb_cu = e100_prev_tcb(tcb_cu);
            e100_cu_int(noptcb_cu);
        }
        else
            noptcb_cu = tcb_cu;
        e100_write_cmd_cu(TCB_CMD_CC);
    }

    return 0;
}

//recieve data from net
int
sys_net_recv(char * dst, size_t len)
{
    int n;

    if (NULL == tcb_ru) {
        tcb_ru = rfa_ka;
        noptcb_ru = e100_prev_tcb(tcb_ru);
        noptcb_ru->tcb_recieve.cb.cmd = TCB_CMD_NOP;
        e100_write_scbgp(PADDR(tcb_ru));
        e100_write_cmd_ru(TCB_CMD_RS);
    }

    if (!e100_tcb_complete(tcb_ru->tcb_recieve.cb.status))
        return -E_RETRY;

    if (0x03 == e100_ru_resault(tcb_ru->tcb_recieve.actualcount)) {
        n = e100_ru_count(tcb_ru->tcb_recieve.actualcount);
        n = MIN(len, n);
        user_mem_assert(curenv, (const void *)dst, n, PTE_P);
        memmove(dst, tcb_ru->tcb_recieve.data, n);
    }
    else
        n = 0;

    uint16_t status = e100_read_status();
    if (e100_ru_sus(status)) {
        e100_ru_res(noptcb_ru);
        noptcb_ru = (tcb_ru == noptcb_ru) ? e100_prev_tcb(tcb_ru) : tcb_ru;

        noptcb_ru->tcb_recieve.cb.cmd = TCB_CMD_RI;
        noptcb_ru->tcb_recieve.cb.status = 0;
        noptcb_ru->tcb_recieve.actualcount = 0;
        e100_write_cmd_ru(TCB_CMD_RC);
    }

    tcb_ru->tcb_recieve.actualcount = 0;
    tcb_ru->tcb_recieve.cb.status = 0;
    tcb_ru = e100_next_tcb(tcb_ru);

    return n;
}

// Dispatches to the correct kernel function, passing the arguments.
    int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    // Call the function corresponding to the 'syscallno' parameter.
    // Return any appropriate return value.
    // LAB 3: Your code here.

    switch (syscallno) {
        case SYS_cputs:
            sys_cputs((const char *)a1, (size_t)a2);
            return 0;
            break;
        case SYS_getenvid:
            return sys_getenvid();
            break;
        case SYS_cgetc:
            return sys_cgetc();
            break;
        case SYS_env_destroy:
            return sys_env_destroy((envid_t)a1);
            break;
        case SYS_yield:
            sys_yield();
            return 0;
            break;
        case SYS_page_map:
            return sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
            break;
        case SYS_page_unmap:
            return sys_page_unmap((envid_t)a1, (void *)a2);
            break;
        case SYS_exofork:
            return sys_exofork();
            break;
        case SYS_env_set_status:
            return sys_env_set_status((envid_t)a1, (int)a2);
            break;
        case SYS_page_alloc:
            return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
            break;
        case SYS_env_set_pgfault_upcall:
            return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
            break;
        case SYS_ipc_try_send:
            return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
            break;
        case SYS_ipc_recv:
            return sys_ipc_recv((void *)a1);
            break;
        case SYS_env_set_trapframe:
            return sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
            break;
        case SYS_net_send:
            return sys_net_send((void *)a1, (size_t)a2);
            break;
        case SYS_net_recv:
            return sys_net_recv((void *)a1, (size_t)a2);
            break;
        case SYS_time_msec:
            return sys_time_msec();
            break;

        default:
            return -E_INVAL;
    }
}

