/*
 * z_Linux_util.c -- platform specific routines.
 * $Revision: 43473 $
 * $Date: 2014-09-26 15:02:57 -0500 (Fri, 26 Sep 2014) $
 */


//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#include "kmp.h"
#include "kmp_wrapper_getpid.h"
#include "kmp_itt.h"
#include "kmp_str.h"
#include "kmp_i18n.h"
#include "kmp_io.h"
#include "kmp_stats.h"
#include "kmp_wait_release.h"

#if !KMP_OS_FREEBSD
# include <alloca.h>
#endif
#include <unistd.h>
#include <math.h>               // HUGE_VAL.
#include <sys/time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#if KMP_OS_LINUX && !KMP_OS_CNK
# include <sys/sysinfo.h>
# if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM)
// We should really include <futex.h>, but that causes compatibility problems on different
// Linux* OS distributions that either require that you include (or break when you try to include)
// <pci/types.h>.
// Since all we need is the two macros below (which are part of the kernel ABI, so can't change)
// we just define the constants here and don't include <futex.h>
#  ifndef FUTEX_WAIT
#   define FUTEX_WAIT    0
#  endif
#  ifndef FUTEX_WAKE
#   define FUTEX_WAKE    1
#  endif
# endif
#elif KMP_OS_DARWIN
# include <sys/sysctl.h>
# include <mach/mach.h>
#elif KMP_OS_FREEBSD
# include <sys/sysctl.h>
# include <pthread_np.h>
#endif


#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>

// For non-x86 architecture
#if KMP_COMPILER_GCC && !(KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_PPC64)
# include <stdbool.h>
# include <ffi.h>
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

struct kmp_sys_timer {
    struct timespec     start;
};

// Convert timespec to nanoseconds.
#define TS2NS(timespec) (((timespec).tv_sec * 1e9) + (timespec).tv_nsec)

static struct kmp_sys_timer __kmp_sys_timer_data;

#if KMP_HANDLE_SIGNALS
    typedef void                            (* sig_func_t )( int );
    STATIC_EFI2_WORKAROUND struct sigaction    __kmp_sighldrs[ NSIG ];
    static sigset_t                            __kmp_sigset;
#endif

static int __kmp_init_runtime   = FALSE;

static int __kmp_fork_count = 0;

static pthread_condattr_t  __kmp_suspend_cond_attr;
static pthread_mutexattr_t __kmp_suspend_mutex_attr;

static kmp_cond_align_t    __kmp_wait_cv;
static kmp_mutex_align_t   __kmp_wait_mx;

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#ifdef DEBUG_SUSPEND
static void
__kmp_print_cond( char *buffer, kmp_cond_align_t *cond )
{
    sprintf( buffer, "(cond (lock (%ld, %d)), (descr (%p)))",
                      cond->c_cond.__c_lock.__status, cond->c_cond.__c_lock.__spinlock,
                      cond->c_cond.__c_waiting );
}
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if ( KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED)

/*
 * Affinity support
 */

/*
 * On some of the older OS's that we build on, these constants aren't present
 * in <asm/unistd.h> #included from <sys.syscall.h>.  They must be the same on
 * all systems of the same arch where they are defined, and they cannot change.
 * stone forever.
 */

#  if KMP_ARCH_X86 || KMP_ARCH_ARM
#   ifndef __NR_sched_setaffinity
#    define __NR_sched_setaffinity  241
#   elif __NR_sched_setaffinity != 241
#    error Wrong code for setaffinity system call.
#   endif /* __NR_sched_setaffinity */
#   ifndef __NR_sched_getaffinity
#    define __NR_sched_getaffinity  242
#   elif __NR_sched_getaffinity != 242
#    error Wrong code for getaffinity system call.
#   endif /* __NR_sched_getaffinity */

#  elif KMP_ARCH_X86_64
#   ifndef __NR_sched_setaffinity
#    define __NR_sched_setaffinity  203
#   elif __NR_sched_setaffinity != 203
#    error Wrong code for setaffinity system call.
#   endif /* __NR_sched_setaffinity */
#   ifndef __NR_sched_getaffinity
#    define __NR_sched_getaffinity  204
#   elif __NR_sched_getaffinity != 204
#    error Wrong code for getaffinity system call.
#   endif /* __NR_sched_getaffinity */

#  elif KMP_ARCH_PPC64
#   ifndef __NR_sched_setaffinity
#    define __NR_sched_setaffinity  222
#   elif __NR_sched_setaffinity != 222
#    error Wrong code for setaffinity system call.
#   endif /* __NR_sched_setaffinity */
#   ifndef __NR_sched_getaffinity
#    define __NR_sched_getaffinity  223
#   elif __NR_sched_getaffinity != 223
#    error Wrong code for getaffinity system call.
#   endif /* __NR_sched_getaffinity */


#  else
#   error Unknown or unsupported architecture

#  endif /* KMP_ARCH_* */

int
__kmp_set_system_affinity( kmp_affin_mask_t const *mask, int abort_on_error )
{
    KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
      "Illegal set affinity operation when not capable");

    int retval = syscall( __NR_sched_setaffinity, 0, __kmp_affin_mask_size, mask );
    if (retval >= 0) {
        return 0;
    }
    int error = errno;
    if (abort_on_error) {
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( FatalSysError ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
    return error;
}

int
__kmp_get_system_affinity( kmp_affin_mask_t *mask, int abort_on_error )
{
    KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
      "Illegal get affinity operation when not capable");

    int retval = syscall( __NR_sched_getaffinity, 0, __kmp_affin_mask_size, mask );
    if (retval >= 0) {
        return 0;
    }
    int error = errno;
    if (abort_on_error) {
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( FatalSysError ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
    return error;
}

void
__kmp_affinity_bind_thread( int which )
{
    KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
      "Illegal set affinity operation when not capable");

    kmp_affin_mask_t *mask = (kmp_affin_mask_t *)alloca(__kmp_affin_mask_size);
    KMP_CPU_ZERO(mask);
    KMP_CPU_SET(which, mask);
    __kmp_set_system_affinity(mask, TRUE);
}

/*
 * Determine if we can access affinity functionality on this version of
 * Linux* OS by checking __NR_sched_{get,set}affinity system calls, and set
 * __kmp_affin_mask_size to the appropriate value (0 means not capable).
 */
void
__kmp_affinity_determine_capable(const char *env_var)
{
    //
    // Check and see if the OS supports thread affinity.
    //

# define KMP_CPU_SET_SIZE_LIMIT          (1024*1024)

    int gCode;
    int sCode;
    kmp_affin_mask_t *buf;
    buf = ( kmp_affin_mask_t * ) KMP_INTERNAL_MALLOC( KMP_CPU_SET_SIZE_LIMIT );

    // If Linux* OS:
    // If the syscall fails or returns a suggestion for the size,
    // then we don't have to search for an appropriate size.
    gCode = syscall( __NR_sched_getaffinity, 0, KMP_CPU_SET_SIZE_LIMIT, buf );
    KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
       "initial getaffinity call returned %d errno = %d\n",
       gCode, errno));

    //if ((gCode < 0) && (errno == ENOSYS))
    if (gCode < 0) {
        //
        // System call not supported
        //
        if (__kmp_affinity_verbose || (__kmp_affinity_warnings
          && (__kmp_affinity_type != affinity_none)
          && (__kmp_affinity_type != affinity_default)
          && (__kmp_affinity_type != affinity_disabled))) {
            int error = errno;
            __kmp_msg(
                kmp_ms_warning,
                KMP_MSG( GetAffSysCallNotSupported, env_var ),
                KMP_ERR( error ),
                __kmp_msg_null
            );
        }
        __kmp_affin_mask_size = 0;  // should already be 0
        KMP_INTERNAL_FREE(buf);
        return;
    }
    if (gCode > 0) { // Linux* OS only
        // The optimal situation: the OS returns the size of the buffer
        // it expects.
        //
        // A verification of correct behavior is that Isetaffinity on a NULL
        // buffer with the same size fails with errno set to EFAULT.
        sCode = syscall( __NR_sched_setaffinity, 0, gCode, NULL );
        KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
           "setaffinity for mask size %d returned %d errno = %d\n",
           gCode, sCode, errno));
        if (sCode < 0) {
            if (errno == ENOSYS) {
                if (__kmp_affinity_verbose || (__kmp_affinity_warnings
                  && (__kmp_affinity_type != affinity_none)
                  && (__kmp_affinity_type != affinity_default)
                  && (__kmp_affinity_type != affinity_disabled))) {
                    int error = errno;
                    __kmp_msg(
                        kmp_ms_warning,
                        KMP_MSG( SetAffSysCallNotSupported, env_var ),
                        KMP_ERR( error ),
                        __kmp_msg_null
                    );
                }
                __kmp_affin_mask_size = 0;  // should already be 0
                KMP_INTERNAL_FREE(buf);
            }
            if (errno == EFAULT) {
                __kmp_affin_mask_size = gCode;
                KA_TRACE(10, ( "__kmp_affinity_determine_capable: "
                  "affinity supported (mask size %d)\n",
                  (int)__kmp_affin_mask_size));
                KMP_INTERNAL_FREE(buf);
                return;
            }
        }
    }

    //
    // Call the getaffinity system call repeatedly with increasing set sizes
    // until we succeed, or reach an upper bound on the search.
    //
    KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
      "searching for proper set size\n"));
    int size;
    for (size = 1; size <= KMP_CPU_SET_SIZE_LIMIT; size *= 2) {
        gCode = syscall( __NR_sched_getaffinity, 0,  size, buf );
        KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
          "getaffinity for mask size %d returned %d errno = %d\n", size,
            gCode, errno));

        if (gCode < 0) {
            if ( errno == ENOSYS )
            {
                //
                // We shouldn't get here
                //
                KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
                  "inconsistent OS call behavior: errno == ENOSYS for mask size %d\n",
                   size));
                if (__kmp_affinity_verbose || (__kmp_affinity_warnings
                  && (__kmp_affinity_type != affinity_none)
                  && (__kmp_affinity_type != affinity_default)
                  && (__kmp_affinity_type != affinity_disabled))) {
                    int error = errno;
                    __kmp_msg(
                        kmp_ms_warning,
                        KMP_MSG( GetAffSysCallNotSupported, env_var ),
                        KMP_ERR( error ),
                        __kmp_msg_null
                    );
                }
                __kmp_affin_mask_size = 0;  // should already be 0
                KMP_INTERNAL_FREE(buf);
                return;
            }
            continue;
        }

        sCode = syscall( __NR_sched_setaffinity, 0, gCode, NULL );
        KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
           "setaffinity for mask size %d returned %d errno = %d\n",
           gCode, sCode, errno));
        if (sCode < 0) {
            if (errno == ENOSYS) { // Linux* OS only
                //
                // We shouldn't get here
                //
                KA_TRACE(30, ( "__kmp_affinity_determine_capable: "
                  "inconsistent OS call behavior: errno == ENOSYS for mask size %d\n",
                   size));
                if (__kmp_affinity_verbose || (__kmp_affinity_warnings
                  && (__kmp_affinity_type != affinity_none)
                  && (__kmp_affinity_type != affinity_default)
                  && (__kmp_affinity_type != affinity_disabled))) {
                    int error = errno;
                    __kmp_msg(
                        kmp_ms_warning,
                        KMP_MSG( SetAffSysCallNotSupported, env_var ),
                        KMP_ERR( error ),
                        __kmp_msg_null
                    );
                }
                __kmp_affin_mask_size = 0;  // should already be 0
                KMP_INTERNAL_FREE(buf);
                return;
            }
            if (errno == EFAULT) {
                __kmp_affin_mask_size = gCode;
                KA_TRACE(10, ( "__kmp_affinity_determine_capable: "
                  "affinity supported (mask size %d)\n",
                   (int)__kmp_affin_mask_size));
                KMP_INTERNAL_FREE(buf);
                return;
            }
        }
    }
    //int error = errno;  // save uncaught error code
    KMP_INTERNAL_FREE(buf);
    // errno = error;  // restore uncaught error code, will be printed at the next KMP_WARNING below

    //
    // Affinity is not supported
    //
    __kmp_affin_mask_size = 0;
    KA_TRACE(10, ( "__kmp_affinity_determine_capable: "
      "cannot determine mask size - affinity not supported\n"));
    if (__kmp_affinity_verbose || (__kmp_affinity_warnings
      && (__kmp_affinity_type != affinity_none)
      && (__kmp_affinity_type != affinity_default)
      && (__kmp_affinity_type != affinity_disabled))) {
        KMP_WARNING( AffCantGetMaskSize, env_var );
    }
}


/*
 * Change thread to the affinity mask pointed to by affin_mask argument
 * and return a pointer to the old value in the old_mask argument, if argument
 * is non-NULL.
 */

void
__kmp_change_thread_affinity_mask( int gtid, kmp_affin_mask_t *new_mask,
                                   kmp_affin_mask_t *old_mask )
{
    KMP_DEBUG_ASSERT( gtid == __kmp_get_gtid() );
    if ( KMP_AFFINITY_CAPABLE() ) {
        int status;
        kmp_info_t  *th = __kmp_threads[ gtid ];

        KMP_DEBUG_ASSERT( new_mask != NULL );

        if ( old_mask != NULL ) {
            status = __kmp_get_system_affinity( old_mask, TRUE );
            int error = errno;
            if ( status != 0 ) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( ChangeThreadAffMaskError ),
                    KMP_ERR( error ),
                    __kmp_msg_null
                );
            }
        }

        __kmp_set_system_affinity( new_mask, TRUE );

        if (__kmp_affinity_verbose) {
            char old_buf[KMP_AFFIN_MASK_PRINT_LEN];
            char new_buf[KMP_AFFIN_MASK_PRINT_LEN];
            __kmp_affinity_print_mask(old_buf, KMP_AFFIN_MASK_PRINT_LEN, old_mask);
            __kmp_affinity_print_mask(new_buf, KMP_AFFIN_MASK_PRINT_LEN, new_mask);
            KMP_INFORM( ChangeAffMask, "KMP_AFFINITY (Bind)", gtid, old_buf, new_buf );

        }

        /* Make sure old value is correct in thread data structures */
        KMP_DEBUG_ASSERT( old_mask != NULL && (memcmp(old_mask,
          th->th.th_affin_mask, __kmp_affin_mask_size) == 0) );
        KMP_CPU_COPY( th->th.th_affin_mask, new_mask );
    }
}

#endif // KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM) && !KMP_OS_CNK

int
__kmp_futex_determine_capable()
{
    int loc = 0;
    int rc = syscall( __NR_futex, &loc, FUTEX_WAKE, 1, NULL, NULL, 0 );
    int retval = ( rc == 0 ) || ( errno != ENOSYS );

    KA_TRACE(10, ( "__kmp_futex_determine_capable: rc = %d errno = %d\n", rc,
      errno ) );
    KA_TRACE(10, ( "__kmp_futex_determine_capable: futex syscall%s supported\n",
        retval ? "" : " not" ) );

    return retval;
}

#endif // KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM) && !KMP_OS_CNK

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if (KMP_ARCH_X86 || KMP_ARCH_X86_64) && (! KMP_ASM_INTRINS)
/*
 * Only 32-bit "add-exchange" instruction on IA-32 architecture causes us to
 * use compare_and_store for these routines
 */

kmp_int32
__kmp_test_then_or32( volatile kmp_int32 *p, kmp_int32 d )
{
    kmp_int32 old_value, new_value;

    old_value = TCR_4( *p );
    new_value = old_value | d;

    while ( ! KMP_COMPARE_AND_STORE_REL32 ( p, old_value, new_value ) )
    {
        KMP_CPU_PAUSE();
        old_value = TCR_4( *p );
        new_value = old_value | d;
    }
    return old_value;
}

kmp_int32
__kmp_test_then_and32( volatile kmp_int32 *p, kmp_int32 d )
{
    kmp_int32 old_value, new_value;

    old_value = TCR_4( *p );
    new_value = old_value & d;

    while ( ! KMP_COMPARE_AND_STORE_REL32 ( p, old_value, new_value ) )
    {
        KMP_CPU_PAUSE();
        old_value = TCR_4( *p );
        new_value = old_value & d;
    }
    return old_value;
}

# if KMP_ARCH_X86 || KMP_ARCH_PPC64
kmp_int64
__kmp_test_then_add64( volatile kmp_int64 *p, kmp_int64 d )
{
    kmp_int64 old_value, new_value;

    old_value = TCR_8( *p );
    new_value = old_value + d;

    while ( ! KMP_COMPARE_AND_STORE_REL64 ( p, old_value, new_value ) )
    {
        KMP_CPU_PAUSE();
        old_value = TCR_8( *p );
        new_value = old_value + d;
    }
    return old_value;
}
# endif /* KMP_ARCH_X86 */

kmp_int64
__kmp_test_then_or64( volatile kmp_int64 *p, kmp_int64 d )
{
    kmp_int64 old_value, new_value;

    old_value = TCR_8( *p );
    new_value = old_value | d;
    while ( ! KMP_COMPARE_AND_STORE_REL64 ( p, old_value, new_value ) )
    {
        KMP_CPU_PAUSE();
        old_value = TCR_8( *p );
        new_value = old_value | d;
    }
    return old_value;
}

kmp_int64
__kmp_test_then_and64( volatile kmp_int64 *p, kmp_int64 d )
{
    kmp_int64 old_value, new_value;

    old_value = TCR_8( *p );
    new_value = old_value & d;
    while ( ! KMP_COMPARE_AND_STORE_REL64 ( p, old_value, new_value ) )
    {
        KMP_CPU_PAUSE();
        old_value = TCR_8( *p );
        new_value = old_value & d;
    }
    return old_value;
}

#endif /* (KMP_ARCH_X86 || KMP_ARCH_X86_64) && (! KMP_ASM_INTRINS) */

void
__kmp_terminate_thread( int gtid )
{
    int status;
    kmp_info_t  *th = __kmp_threads[ gtid ];

    if ( !th ) return;

    #ifdef KMP_CANCEL_THREADS
        KA_TRACE( 10, ("__kmp_terminate_thread: kill (%d)\n", gtid ) );
        status = pthread_cancel( th->th.th_info.ds.ds_thread );
        if ( status != 0 && status != ESRCH ) {
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( CantTerminateWorkerThread ),
                KMP_ERR( status ),
                __kmp_msg_null
            );
        }; // if
    #endif
    __kmp_yield( TRUE );
} //

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Set thread stack info according to values returned by
 * pthread_getattr_np().
 * If values are unreasonable, assume call failed and use
 * incremental stack refinement method instead.
 * Returns TRUE if the stack parameters could be determined exactly,
 * FALSE if incremental refinement is necessary.
 */
static kmp_int32
__kmp_set_stack_info( int gtid, kmp_info_t *th )
{
    int            stack_data;
#if KMP_OS_LINUX || KMP_OS_FREEBSD
    /* Linux* OS only -- no pthread_getattr_np support on OS X* */
    pthread_attr_t attr;
    int            status;
    size_t         size = 0;
    void *         addr = 0;

    /* Always do incremental stack refinement for ubermaster threads since the initial
       thread stack range can be reduced by sibling thread creation so pthread_attr_getstack
       may cause thread gtid aliasing */
    if ( ! KMP_UBER_GTID(gtid) ) {

        /* Fetch the real thread attributes */
        status = pthread_attr_init( &attr );
        KMP_CHECK_SYSFAIL( "pthread_attr_init", status );
#if KMP_OS_FREEBSD
        status = pthread_attr_get_np( pthread_self(), &attr );
        KMP_CHECK_SYSFAIL( "pthread_attr_get_np", status );
#else
        status = pthread_getattr_np( pthread_self(), &attr );
        KMP_CHECK_SYSFAIL( "pthread_getattr_np", status );
#endif
        status = pthread_attr_getstack( &attr, &addr, &size );
        KMP_CHECK_SYSFAIL( "pthread_attr_getstack", status );
        KA_TRACE( 60, ( "__kmp_set_stack_info: T#%d pthread_attr_getstack returned size: %lu, "
                        "low addr: %p\n",
                        gtid, size, addr ));

        status = pthread_attr_destroy( &attr );
        KMP_CHECK_SYSFAIL( "pthread_attr_destroy", status );
    }

    if ( size != 0 && addr != 0 ) {     /* was stack parameter determination successful? */
        /* Store the correct base and size */
        TCW_PTR(th->th.th_info.ds.ds_stackbase, (((char *)addr) + size));
        TCW_PTR(th->th.th_info.ds.ds_stacksize, size);
        TCW_4(th->th.th_info.ds.ds_stackgrow, FALSE);
        return TRUE;
    }
#endif /* KMP_OS_LINUX || KMP_OS_FREEBSD */
    /* Use incremental refinement starting from initial conservative estimate */
    TCW_PTR(th->th.th_info.ds.ds_stacksize, 0);
    TCW_PTR(th -> th.th_info.ds.ds_stackbase, &stack_data);
    TCW_4(th->th.th_info.ds.ds_stackgrow, TRUE);
    return FALSE;
}

static void*
__kmp_launch_worker( void *thr )
{
    int status, old_type, old_state;
#ifdef KMP_BLOCK_SIGNALS
    sigset_t    new_set, old_set;
#endif /* KMP_BLOCK_SIGNALS */
    void *exit_val;
    void *padding = 0;
    int gtid;
    int error;

    gtid = ((kmp_info_t*)thr) -> th.th_info.ds.ds_gtid;
    __kmp_gtid_set_specific( gtid );
#ifdef KMP_TDATA_GTID
    __kmp_gtid = gtid;
#endif
#if KMP_STATS_ENABLED
    // set __thread local index to point to thread-specific stats
    __kmp_stats_thread_ptr = ((kmp_info_t*)thr)->th.th_stats;
#endif

#if USE_ITT_BUILD
    __kmp_itt_thread_name( gtid );
#endif /* USE_ITT_BUILD */

#if KMP_AFFINITY_SUPPORTED
    __kmp_affinity_set_init_mask( gtid, FALSE );
#endif

#ifdef KMP_CANCEL_THREADS
    status = pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, & old_type );
    KMP_CHECK_SYSFAIL( "pthread_setcanceltype", status );
    /* josh todo: isn't PTHREAD_CANCEL_ENABLE default for newly-created threads? */
    status = pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, & old_state );
    KMP_CHECK_SYSFAIL( "pthread_setcancelstate", status );
#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    //
    // Set the FP control regs to be a copy of
    // the parallel initialization thread's.
    //
    __kmp_clear_x87_fpu_status_word();
    __kmp_load_x87_fpu_control_word( &__kmp_init_x87_fpu_control_word );
    __kmp_load_mxcsr( &__kmp_init_mxcsr );
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#ifdef KMP_BLOCK_SIGNALS
    status = sigfillset( & new_set );
    KMP_CHECK_SYSFAIL_ERRNO( "sigfillset", status );
    status = pthread_sigmask( SIG_BLOCK, & new_set, & old_set );
    KMP_CHECK_SYSFAIL( "pthread_sigmask", status );
#endif /* KMP_BLOCK_SIGNALS */

#if KMP_OS_LINUX || KMP_OS_FREEBSD
    if ( __kmp_stkoffset > 0 && gtid > 0 ) {
        padding = alloca( gtid * __kmp_stkoffset );
    }
#endif

    KMP_MB();
    __kmp_set_stack_info( gtid, (kmp_info_t*)thr );

    __kmp_check_stack_overlap( (kmp_info_t*)thr );

    exit_val = __kmp_launch_thread( (kmp_info_t *) thr );

#ifdef KMP_BLOCK_SIGNALS
    status = pthread_sigmask( SIG_SETMASK, & old_set, NULL );
    KMP_CHECK_SYSFAIL( "pthread_sigmask", status );
#endif /* KMP_BLOCK_SIGNALS */

    return exit_val;
}


/* The monitor thread controls all of the threads in the complex */

static void*
__kmp_launch_monitor( void *thr )
{
    int         status, old_type, old_state;
#ifdef KMP_BLOCK_SIGNALS
    sigset_t    new_set;
#endif /* KMP_BLOCK_SIGNALS */
    struct timespec  interval;
    int yield_count;
    int yield_cycles = 0;
    int error;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ("__kmp_launch_monitor: #1 launched\n" ) );

    /* register us as the monitor thread */
    __kmp_gtid_set_specific( KMP_GTID_MONITOR );
#ifdef KMP_TDATA_GTID
    __kmp_gtid = KMP_GTID_MONITOR;
#endif

    KMP_MB();

#if USE_ITT_BUILD
    __kmp_itt_thread_ignore();    // Instruct Intel(R) Threading Tools to ignore monitor thread.
#endif /* USE_ITT_BUILD */

    __kmp_set_stack_info( ((kmp_info_t*)thr)->th.th_info.ds.ds_gtid, (kmp_info_t*)thr );

    __kmp_check_stack_overlap( (kmp_info_t*)thr );

#ifdef KMP_CANCEL_THREADS
    status = pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, & old_type );
    KMP_CHECK_SYSFAIL( "pthread_setcanceltype", status );
    /* josh todo: isn't PTHREAD_CANCEL_ENABLE default for newly-created threads? */
    status = pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, & old_state );
    KMP_CHECK_SYSFAIL( "pthread_setcancelstate", status );
#endif

    #if KMP_REAL_TIME_FIX
    // This is a potential fix which allows application with real-time scheduling policy work.
    // However, decision about the fix is not made yet, so it is disabled by default.
    { // Are program started with real-time scheduling policy?
        int sched = sched_getscheduler( 0 );
        if ( sched == SCHED_FIFO || sched == SCHED_RR ) {
            // Yes, we are a part of real-time application. Try to increase the priority of the
            // monitor.
            struct sched_param param;
            int    max_priority = sched_get_priority_max( sched );
            int    rc;
            KMP_WARNING( RealTimeSchedNotSupported );
            sched_getparam( 0, & param );
            if ( param.sched_priority < max_priority ) {
                param.sched_priority += 1;
                rc = sched_setscheduler( 0, sched, & param );
                if ( rc != 0 ) {
                    int error = errno;
                  __kmp_msg(
                      kmp_ms_warning,
                      KMP_MSG( CantChangeMonitorPriority ),
                      KMP_ERR( error ),
                      KMP_MSG( MonitorWillStarve ),
                      __kmp_msg_null
                  );
                }; // if
            } else {
                // We cannot abort here, because number of CPUs may be enough for all the threads,
                // including the monitor thread, so application could potentially work...
                __kmp_msg(
                    kmp_ms_warning,
                    KMP_MSG( RunningAtMaxPriority ),
                    KMP_MSG( MonitorWillStarve ),
                    KMP_HNT( RunningAtMaxPriority ),
                    __kmp_msg_null
                );
            }; // if
        }; // if
        TCW_4( __kmp_global.g.g_time.dt.t_value, 0 );  // AC: free thread that waits for monitor started
    }
    #endif // KMP_REAL_TIME_FIX

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    if ( __kmp_monitor_wakeups == 1 ) {
        interval.tv_sec  = 1;
        interval.tv_nsec = 0;
    } else {
        interval.tv_sec  = 0;
        interval.tv_nsec = (NSEC_PER_SEC / __kmp_monitor_wakeups);
    }

    KA_TRACE( 10, ("__kmp_launch_monitor: #2 monitor\n" ) );

    if (__kmp_yield_cycle) {
        __kmp_yielding_on = 0;  /* Start out with yielding shut off */
        yield_count = __kmp_yield_off_count;
    } else {
        __kmp_yielding_on = 1;  /* Yielding is on permanently */
    }

    while( ! TCR_4( __kmp_global.g.g_done ) ) {
        struct timespec  now;
        struct timeval   tval;

        /*  This thread monitors the state of the system */

        KA_TRACE( 15, ( "__kmp_launch_monitor: update\n" ) );

        status = gettimeofday( &tval, NULL );
        KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
        TIMEVAL_TO_TIMESPEC( &tval, &now );

        now.tv_sec  += interval.tv_sec;
        now.tv_nsec += interval.tv_nsec;

        if (now.tv_nsec >= NSEC_PER_SEC) {
            now.tv_sec  += 1;
            now.tv_nsec -= NSEC_PER_SEC;
        }

        status = pthread_mutex_lock( & __kmp_wait_mx.m_mutex );
        KMP_CHECK_SYSFAIL( "pthread_mutex_lock", status );
        // AC: the monitor should not fall asleep if g_done has been set
        if ( !TCR_4(__kmp_global.g.g_done) ) {  // check once more under mutex
            status = pthread_cond_timedwait( &__kmp_wait_cv.c_cond, &__kmp_wait_mx.m_mutex, &now );
            if ( status != 0 ) {
                if ( status != ETIMEDOUT && status != EINTR ) {
                    KMP_SYSFAIL( "pthread_cond_timedwait", status );
                };
            };
        };
        status = pthread_mutex_unlock( & __kmp_wait_mx.m_mutex );
        KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );

        if (__kmp_yield_cycle) {
            yield_cycles++;
            if ( (yield_cycles % yield_count) == 0 ) {
                if (__kmp_yielding_on) {
                    __kmp_yielding_on = 0;   /* Turn it off now */
                    yield_count = __kmp_yield_off_count;
                } else {
                    __kmp_yielding_on = 1;   /* Turn it on now */
                    yield_count = __kmp_yield_on_count;
                }
                yield_cycles = 0;
            }
        } else {
            __kmp_yielding_on = 1;
        }

        TCW_4( __kmp_global.g.g_time.dt.t_value,
          TCR_4( __kmp_global.g.g_time.dt.t_value ) + 1 );

        KMP_MB();       /* Flush all pending memory write invalidates.  */
    }

    KA_TRACE( 10, ("__kmp_launch_monitor: #3 cleanup\n" ) );

#ifdef KMP_BLOCK_SIGNALS
    status = sigfillset( & new_set );
    KMP_CHECK_SYSFAIL_ERRNO( "sigfillset", status );
    status = pthread_sigmask( SIG_UNBLOCK, & new_set, NULL );
    KMP_CHECK_SYSFAIL( "pthread_sigmask", status );
#endif /* KMP_BLOCK_SIGNALS */

    KA_TRACE( 10, ("__kmp_launch_monitor: #4 finished\n" ) );

    if( __kmp_global.g.g_abort != 0 ) {
        /* now we need to terminate the worker threads  */
        /* the value of t_abort is the signal we caught */

        int gtid;

        KA_TRACE( 10, ("__kmp_launch_monitor: #5 terminate sig=%d\n", __kmp_global.g.g_abort ) );

        /* terminate the OpenMP worker threads */
        /* TODO this is not valid for sibling threads!!
         * the uber master might not be 0 anymore.. */
        for (gtid = 1; gtid < __kmp_threads_capacity; ++gtid)
            __kmp_terminate_thread( gtid );

        __kmp_cleanup();

        KA_TRACE( 10, ("__kmp_launch_monitor: #6 raise sig=%d\n", __kmp_global.g.g_abort ) );

        if (__kmp_global.g.g_abort > 0)
            raise( __kmp_global.g.g_abort );

    }

    KA_TRACE( 10, ("__kmp_launch_monitor: #7 exit\n" ) );

    return thr;
}

void
__kmp_create_worker( int gtid, kmp_info_t *th, size_t stack_size )
{
    pthread_t      handle;
    pthread_attr_t thread_attr;
    int            status;


    th->th.th_info.ds.ds_gtid = gtid;

#if KMP_STATS_ENABLED
    // sets up worker thread stats
    __kmp_acquire_tas_lock(&__kmp_stats_lock, gtid);

    // th->th.th_stats is used to transfer thread specific stats-pointer to __kmp_launch_worker
    // So when thread is created (goes into __kmp_launch_worker) it will
    // set it's __thread local pointer to th->th.th_stats
    th->th.th_stats = __kmp_stats_list.push_back(gtid);
    if(KMP_UBER_GTID(gtid)) {
        __kmp_stats_start_time = tsc_tick_count::now();
        __kmp_stats_thread_ptr = th->th.th_stats;
        __kmp_stats_init();
        KMP_START_EXPLICIT_TIMER(OMP_serial);
        KMP_START_EXPLICIT_TIMER(OMP_start_end);
    }
    __kmp_release_tas_lock(&__kmp_stats_lock, gtid);

#endif // KMP_STATS_ENABLED

    if ( KMP_UBER_GTID(gtid) ) {
        KA_TRACE( 10, ("__kmp_create_worker: uber thread (%d)\n", gtid ) );
        th -> th.th_info.ds.ds_thread = pthread_self();
        __kmp_set_stack_info( gtid, th );
        __kmp_check_stack_overlap( th );
        return;
    }; // if

    KA_TRACE( 10, ("__kmp_create_worker: try to create thread (%d)\n", gtid ) );

    KMP_MB();       /* Flush all pending memory write invalidates.  */

#ifdef KMP_THREAD_ATTR
        {
            status = pthread_attr_init( &thread_attr );
            if ( status != 0 ) {
                __kmp_msg(
                          kmp_ms_fatal,
                          KMP_MSG( CantInitThreadAttrs ),
                          KMP_ERR( status ),
                          __kmp_msg_null
                          );
            }; // if
            status = pthread_attr_setdetachstate( & thread_attr, PTHREAD_CREATE_JOINABLE );
            if ( status != 0 ) {
                __kmp_msg(
                          kmp_ms_fatal,
                          KMP_MSG( CantSetWorkerState ),
                          KMP_ERR( status ),
                          __kmp_msg_null
                          );
            }; // if

            /* Set stack size for this thread now. */
            stack_size += gtid * __kmp_stkoffset;

            KA_TRACE( 10, ( "__kmp_create_worker: T#%d, default stacksize = %lu bytes, "
                            "__kmp_stksize = %lu bytes, final stacksize = %lu bytes\n",
                            gtid, KMP_DEFAULT_STKSIZE, __kmp_stksize, stack_size ) );

# ifdef _POSIX_THREAD_ATTR_STACKSIZE
                status = pthread_attr_setstacksize( & thread_attr, stack_size );
#  ifdef KMP_BACKUP_STKSIZE
            if ( status != 0 ) {
                if ( ! __kmp_env_stksize ) {
                    stack_size = KMP_BACKUP_STKSIZE + gtid * __kmp_stkoffset;
                    __kmp_stksize = KMP_BACKUP_STKSIZE;
                    KA_TRACE( 10, ("__kmp_create_worker: T#%d, default stacksize = %lu bytes, "
                                   "__kmp_stksize = %lu bytes, (backup) final stacksize = %lu "
                                   "bytes\n",
                                   gtid, KMP_DEFAULT_STKSIZE, __kmp_stksize, stack_size )
                              );
                    status = pthread_attr_setstacksize( &thread_attr, stack_size );
                }; // if
            }; // if
#  endif /* KMP_BACKUP_STKSIZE */
            if ( status != 0 ) {
                __kmp_msg(
                          kmp_ms_fatal,
                          KMP_MSG( CantSetWorkerStackSize, stack_size ),
                          KMP_ERR( status ),
                          KMP_HNT( ChangeWorkerStackSize  ),
                          __kmp_msg_null
                          );
            }; // if
# endif /* _POSIX_THREAD_ATTR_STACKSIZE */
        }
#endif /* KMP_THREAD_ATTR */

        {
            status = pthread_create( & handle, & thread_attr, __kmp_launch_worker, (void *) th );
            if ( status != 0 || ! handle ) { // ??? Why do we check handle??
#ifdef _POSIX_THREAD_ATTR_STACKSIZE
                if ( status == EINVAL ) {
                    __kmp_msg(
                              kmp_ms_fatal,
                              KMP_MSG( CantSetWorkerStackSize, stack_size ),
                              KMP_ERR( status ),
                              KMP_HNT( IncreaseWorkerStackSize ),
                              __kmp_msg_null
                              );
                };
                if ( status == ENOMEM ) {
                    __kmp_msg(
                              kmp_ms_fatal,
                              KMP_MSG( CantSetWorkerStackSize, stack_size ),
                              KMP_ERR( status ),
                              KMP_HNT( DecreaseWorkerStackSize ),
                              __kmp_msg_null
                              );
                };
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
                if ( status == EAGAIN ) {
                    __kmp_msg(
                              kmp_ms_fatal,
                              KMP_MSG( NoResourcesForWorkerThread ),
                              KMP_ERR( status ),
                              KMP_HNT( Decrease_NUM_THREADS ),
                              __kmp_msg_null
                              );
                }; // if
                KMP_SYSFAIL( "pthread_create", status );
            }; // if

            th->th.th_info.ds.ds_thread = handle;
        }

#ifdef KMP_THREAD_ATTR
        {
            status = pthread_attr_destroy( & thread_attr );
            if ( status ) {
                __kmp_msg(
                          kmp_ms_warning,
                          KMP_MSG( CantDestroyThreadAttrs ),
                          KMP_ERR( status ),
                          __kmp_msg_null
                          );
            }; // if
        }
#endif /* KMP_THREAD_ATTR */

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ("__kmp_create_worker: done creating thread (%d)\n", gtid ) );

} // __kmp_create_worker


void
__kmp_create_monitor( kmp_info_t *th )
{
    pthread_t           handle;
    pthread_attr_t      thread_attr;
    size_t              size;
    int                 status;
    int                 caller_gtid = __kmp_get_gtid();
    int                 auto_adj_size = FALSE;

    KA_TRACE( 10, ("__kmp_create_monitor: try to create monitor\n" ) );

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    th->th.th_info.ds.ds_tid  = KMP_GTID_MONITOR;
    th->th.th_info.ds.ds_gtid = KMP_GTID_MONITOR;
    #if KMP_REAL_TIME_FIX
        TCW_4( __kmp_global.g.g_time.dt.t_value, -1 ); // Will use it for synchronization a bit later.
    #else
        TCW_4( __kmp_global.g.g_time.dt.t_value, 0 );
    #endif // KMP_REAL_TIME_FIX

    #ifdef KMP_THREAD_ATTR
        if ( __kmp_monitor_stksize == 0 ) {
            __kmp_monitor_stksize = KMP_DEFAULT_MONITOR_STKSIZE;
            auto_adj_size = TRUE;
        }
        status = pthread_attr_init( &thread_attr );
        if ( status != 0 ) {
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( CantInitThreadAttrs ),
                KMP_ERR( status ),
                __kmp_msg_null
            );
        }; // if
        status = pthread_attr_setdetachstate( & thread_attr, PTHREAD_CREATE_JOINABLE );
        if ( status != 0 ) {
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( CantSetMonitorState ),
                KMP_ERR( status ),
                __kmp_msg_null
            );
        }; // if

        #ifdef _POSIX_THREAD_ATTR_STACKSIZE
            status = pthread_attr_getstacksize( & thread_attr, & size );
            KMP_CHECK_SYSFAIL( "pthread_attr_getstacksize", status );
        #else
            size = __kmp_sys_min_stksize;
        #endif /* _POSIX_THREAD_ATTR_STACKSIZE */
    #endif /* KMP_THREAD_ATTR */

    if ( __kmp_monitor_stksize == 0 ) {
        __kmp_monitor_stksize = KMP_DEFAULT_MONITOR_STKSIZE;
    }
    if ( __kmp_monitor_stksize < __kmp_sys_min_stksize ) {
        __kmp_monitor_stksize = __kmp_sys_min_stksize;
    }

    KA_TRACE( 10, ( "__kmp_create_monitor: default stacksize = %lu bytes,"
                    "requested stacksize = %lu bytes\n",
                    size, __kmp_monitor_stksize ) );

    retry:

    /* Set stack size for this thread now. */

    #ifdef _POSIX_THREAD_ATTR_STACKSIZE
        KA_TRACE( 10, ( "__kmp_create_monitor: setting stacksize = %lu bytes,",
                        __kmp_monitor_stksize ) );
        status = pthread_attr_setstacksize( & thread_attr, __kmp_monitor_stksize );
        if ( status != 0 ) {
            if ( auto_adj_size ) {
                __kmp_monitor_stksize *= 2;
                goto retry;
            }
            __kmp_msg(
                kmp_ms_warning,  // should this be fatal?  BB
                KMP_MSG( CantSetMonitorStackSize, (long int) __kmp_monitor_stksize ),
                KMP_ERR( status ),
                KMP_HNT( ChangeMonitorStackSize ),
                __kmp_msg_null
            );
        }; // if
    #endif /* _POSIX_THREAD_ATTR_STACKSIZE */

    status = pthread_create( &handle, & thread_attr, __kmp_launch_monitor, (void *) th );

    if ( status != 0 ) {
        #ifdef _POSIX_THREAD_ATTR_STACKSIZE
            if ( status == EINVAL ) {
                if ( auto_adj_size  && ( __kmp_monitor_stksize < (size_t)0x40000000 ) ) {
                    __kmp_monitor_stksize *= 2;
                    goto retry;
                }
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( CantSetMonitorStackSize, __kmp_monitor_stksize ),
                    KMP_ERR( status ),
                    KMP_HNT( IncreaseMonitorStackSize ),
                    __kmp_msg_null
                );
            }; // if
            if ( status == ENOMEM ) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( CantSetMonitorStackSize, __kmp_monitor_stksize ),
                    KMP_ERR( status ),
                    KMP_HNT( DecreaseMonitorStackSize ),
                    __kmp_msg_null
                );
            }; // if
        #endif /* _POSIX_THREAD_ATTR_STACKSIZE */
        if ( status == EAGAIN ) {
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( NoResourcesForMonitorThread ),
                KMP_ERR( status ),
                KMP_HNT( DecreaseNumberOfThreadsInUse ),
                __kmp_msg_null
            );
        }; // if
        KMP_SYSFAIL( "pthread_create", status );
    }; // if

    th->th.th_info.ds.ds_thread = handle;

    #if KMP_REAL_TIME_FIX
        // Wait for the monitor thread is really started and set its *priority*.
        KMP_DEBUG_ASSERT( sizeof( kmp_uint32 ) == sizeof( __kmp_global.g.g_time.dt.t_value ) );
        __kmp_wait_yield_4(
            (kmp_uint32 volatile *) & __kmp_global.g.g_time.dt.t_value, -1, & __kmp_neq_4, NULL
        );
    #endif // KMP_REAL_TIME_FIX

    #ifdef KMP_THREAD_ATTR
        status = pthread_attr_destroy( & thread_attr );
        if ( status != 0 ) {
            __kmp_msg(    //
                kmp_ms_warning,
                KMP_MSG( CantDestroyThreadAttrs ),
                KMP_ERR( status ),
                __kmp_msg_null
            );
        }; // if
    #endif

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ( "__kmp_create_monitor: monitor created %#.8lx\n", th->th.th_info.ds.ds_thread ) );

} // __kmp_create_monitor

void
__kmp_exit_thread(
    int exit_status
) {
    pthread_exit( (void *)(intptr_t) exit_status );
} // __kmp_exit_thread

void __kmp_resume_monitor();

void
__kmp_reap_monitor( kmp_info_t *th )
{
    int          status, i;
    void        *exit_val;

    KA_TRACE( 10, ("__kmp_reap_monitor: try to reap monitor thread with handle %#.8lx\n",
                   th->th.th_info.ds.ds_thread ) );

    // If monitor has been created, its tid and gtid should be KMP_GTID_MONITOR.
    // If both tid and gtid are 0, it means the monitor did not ever start.
    // If both tid and gtid are KMP_GTID_DNE, the monitor has been shut down.
    KMP_DEBUG_ASSERT( th->th.th_info.ds.ds_tid == th->th.th_info.ds.ds_gtid );
    if ( th->th.th_info.ds.ds_gtid != KMP_GTID_MONITOR ) {
        return;
    }; // if

    KMP_MB();       /* Flush all pending memory write invalidates.  */


    /* First, check to see whether the monitor thread exists.  This could prevent a hang,
       but if the monitor dies after the pthread_kill call and before the pthread_join
       call, it will still hang. */

    status = pthread_kill( th->th.th_info.ds.ds_thread, 0 );
    if (status == ESRCH) {

        KA_TRACE( 10, ("__kmp_reap_monitor: monitor does not exist, returning\n") );

    } else
    {
        __kmp_resume_monitor();   // Wake up the monitor thread
        status = pthread_join( th->th.th_info.ds.ds_thread, & exit_val);
        if (exit_val != th) {
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( ReapMonitorError ),
                KMP_ERR( status ),
                __kmp_msg_null
            );
        }
    }

    th->th.th_info.ds.ds_tid  = KMP_GTID_DNE;
    th->th.th_info.ds.ds_gtid = KMP_GTID_DNE;

    KA_TRACE( 10, ("__kmp_reap_monitor: done reaping monitor thread with handle %#.8lx\n",
                   th->th.th_info.ds.ds_thread ) );

    KMP_MB();       /* Flush all pending memory write invalidates.  */

}

void
__kmp_reap_worker( kmp_info_t *th )
{
    int          status;
    void        *exit_val;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ("__kmp_reap_worker: try to reap T#%d\n", th->th.th_info.ds.ds_gtid ) );

    /* First, check to see whether the worker thread exists.  This could prevent a hang,
       but if the worker dies after the pthread_kill call and before the pthread_join
       call, it will still hang. */

        {
            status = pthread_kill( th->th.th_info.ds.ds_thread, 0 );
            if (status == ESRCH) {
                KA_TRACE( 10, ("__kmp_reap_worker: worker T#%d does not exist, returning\n",
                               th->th.th_info.ds.ds_gtid ) );
            }
            else {
                KA_TRACE( 10, ("__kmp_reap_worker: try to join with worker T#%d\n",
                               th->th.th_info.ds.ds_gtid ) );

                status = pthread_join( th->th.th_info.ds.ds_thread, & exit_val);
#ifdef KMP_DEBUG
                /* Don't expose these to the user until we understand when they trigger */
                if ( status != 0 ) {
                    __kmp_msg(
                              kmp_ms_fatal,
                              KMP_MSG( ReapWorkerError ),
                              KMP_ERR( status ),
                              __kmp_msg_null
                              );
                }
                if ( exit_val != th ) {
                    KA_TRACE( 10, ( "__kmp_reap_worker: worker T#%d did not reap properly, "
                                    "exit_val = %p\n",
                                    th->th.th_info.ds.ds_gtid, exit_val ) );
                }
#endif /* KMP_DEBUG */
            }
        }

    KA_TRACE( 10, ("__kmp_reap_worker: done reaping T#%d\n", th->th.th_info.ds.ds_gtid ) );

    KMP_MB();       /* Flush all pending memory write invalidates.  */
}


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if KMP_HANDLE_SIGNALS


static void
__kmp_null_handler( int signo )
{
    //  Do nothing, for doing SIG_IGN-type actions.
} // __kmp_null_handler


static void
__kmp_team_handler( int signo )
{
    if ( __kmp_global.g.g_abort == 0 ) {
        /* Stage 1 signal handler, let's shut down all of the threads */
        #ifdef KMP_DEBUG
            __kmp_debug_printf( "__kmp_team_handler: caught signal = %d\n", signo );
        #endif
        switch ( signo ) {
            case SIGHUP  :
            case SIGINT  :
            case SIGQUIT :
            case SIGILL  :
            case SIGABRT :
            case SIGFPE  :
            case SIGBUS  :
            case SIGSEGV :
            #ifdef SIGSYS
                case SIGSYS :
            #endif
            case SIGTERM :
                if ( __kmp_debug_buf ) {
                    __kmp_dump_debug_buffer( );
                }; // if
                KMP_MB();       // Flush all pending memory write invalidates.
                TCW_4( __kmp_global.g.g_abort, signo );
                KMP_MB();       // Flush all pending memory write invalidates.
                TCW_4( __kmp_global.g.g_done, TRUE );
                KMP_MB();       // Flush all pending memory write invalidates.
                break;
            default:
                #ifdef KMP_DEBUG
                    __kmp_debug_printf( "__kmp_team_handler: unknown signal type" );
                #endif
                break;
        }; // switch
    }; // if
} // __kmp_team_handler


static
void __kmp_sigaction( int signum, const struct sigaction * act, struct sigaction * oldact ) {
    int rc = sigaction( signum, act, oldact );
    KMP_CHECK_SYSFAIL_ERRNO( "sigaction", rc );
}


static void
__kmp_install_one_handler( int sig, sig_func_t handler_func, int parallel_init )
{
    KMP_MB();       // Flush all pending memory write invalidates.
    KB_TRACE( 60, ( "__kmp_install_one_handler( %d, ..., %d )\n", sig, parallel_init ) );
    if ( parallel_init ) {
        struct sigaction new_action;
        struct sigaction old_action;
        new_action.sa_handler = handler_func;
        new_action.sa_flags   = 0;
        sigfillset( & new_action.sa_mask );
        __kmp_sigaction( sig, & new_action, & old_action );
        if ( old_action.sa_handler == __kmp_sighldrs[ sig ].sa_handler ) {
            sigaddset( & __kmp_sigset, sig );
        } else {
            // Restore/keep user's handler if one previously installed.
            __kmp_sigaction( sig, & old_action, NULL );
        }; // if
    } else {
        // Save initial/system signal handlers to see if user handlers installed.
        __kmp_sigaction( sig, NULL, & __kmp_sighldrs[ sig ] );
    }; // if
    KMP_MB();       // Flush all pending memory write invalidates.
} // __kmp_install_one_handler


static void
__kmp_remove_one_handler( int sig )
{
    KB_TRACE( 60, ( "__kmp_remove_one_handler( %d )\n", sig ) );
    if ( sigismember( & __kmp_sigset, sig ) ) {
        struct sigaction old;
        KMP_MB();       // Flush all pending memory write invalidates.
        __kmp_sigaction( sig, & __kmp_sighldrs[ sig ], & old );
        if ( ( old.sa_handler != __kmp_team_handler ) && ( old.sa_handler != __kmp_null_handler ) ) {
            // Restore the users signal handler.
            KB_TRACE( 10, ( "__kmp_remove_one_handler: oops, not our handler, restoring: sig=%d\n", sig ) );
            __kmp_sigaction( sig, & old, NULL );
        }; // if
        sigdelset( & __kmp_sigset, sig );
        KMP_MB();       // Flush all pending memory write invalidates.
    }; // if
} // __kmp_remove_one_handler


void
__kmp_install_signals( int parallel_init )
{
    KB_TRACE( 10, ( "__kmp_install_signals( %d )\n", parallel_init ) );
    if ( __kmp_handle_signals || ! parallel_init ) {
        // If ! parallel_init, we do not install handlers, just save original handlers.
        // Let us do it even __handle_signals is 0.
        sigemptyset( & __kmp_sigset );
        __kmp_install_one_handler( SIGHUP,  __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGINT,  __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGQUIT, __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGILL,  __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGABRT, __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGFPE,  __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGBUS,  __kmp_team_handler, parallel_init );
        __kmp_install_one_handler( SIGSEGV, __kmp_team_handler, parallel_init );
        #ifdef SIGSYS
            __kmp_install_one_handler( SIGSYS,  __kmp_team_handler, parallel_init );
        #endif // SIGSYS
        __kmp_install_one_handler( SIGTERM, __kmp_team_handler, parallel_init );
        #ifdef SIGPIPE
            __kmp_install_one_handler( SIGPIPE, __kmp_team_handler, parallel_init );
        #endif // SIGPIPE
    }; // if
} // __kmp_install_signals


void
__kmp_remove_signals( void )
{
    int    sig;
    KB_TRACE( 10, ( "__kmp_remove_signals()\n" ) );
    for ( sig = 1; sig < NSIG; ++ sig ) {
        __kmp_remove_one_handler( sig );
    }; // for sig
} // __kmp_remove_signals


#endif // KMP_HANDLE_SIGNALS

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_enable( int new_state )
{
    #ifdef KMP_CANCEL_THREADS
        int status, old_state;
        status = pthread_setcancelstate( new_state, & old_state );
        KMP_CHECK_SYSFAIL( "pthread_setcancelstate", status );
        KMP_DEBUG_ASSERT( old_state == PTHREAD_CANCEL_DISABLE );
    #endif
}

void
__kmp_disable( int * old_state )
{
    #ifdef KMP_CANCEL_THREADS
        int status;
        status = pthread_setcancelstate( PTHREAD_CANCEL_DISABLE, old_state );
        KMP_CHECK_SYSFAIL( "pthread_setcancelstate", status );
    #endif
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

static void
__kmp_atfork_prepare (void)
{
    /*  nothing to do  */
}

static void
__kmp_atfork_parent (void)
{
    /*  nothing to do  */
}

/*
    Reset the library so execution in the child starts "all over again" with
    clean data structures in initial states.  Don't worry about freeing memory
    allocated by parent, just abandon it to be safe.
*/
static void
__kmp_atfork_child (void)
{
    /* TODO make sure this is done right for nested/sibling */
    // ATT:  Memory leaks are here? TODO: Check it and fix.
    /* KMP_ASSERT( 0 ); */

    ++__kmp_fork_count;

    __kmp_init_runtime = FALSE;
    __kmp_init_monitor = 0;
    __kmp_init_parallel = FALSE;
    __kmp_init_middle = FALSE;
    __kmp_init_serial = FALSE;
    TCW_4(__kmp_init_gtid, FALSE);
    __kmp_init_common = FALSE;

    TCW_4(__kmp_init_user_locks, FALSE);
    __kmp_user_lock_table.used = 1;
    __kmp_user_lock_table.allocated = 0;
    __kmp_user_lock_table.table = NULL;
    __kmp_lock_blocks = NULL;

    __kmp_all_nth = 0;
    TCW_4(__kmp_nth, 0);

    /* Must actually zero all the *cache arguments passed to __kmpc_threadprivate here
       so threadprivate doesn't use stale data */
    KA_TRACE( 10, ( "__kmp_atfork_child: checking cache address list %p\n",
                 __kmp_threadpriv_cache_list ) );

    while ( __kmp_threadpriv_cache_list != NULL ) {

        if ( *__kmp_threadpriv_cache_list -> addr != NULL ) {
            KC_TRACE( 50, ( "__kmp_atfork_child: zeroing cache at address %p\n",
                        &(*__kmp_threadpriv_cache_list -> addr) ) );

            *__kmp_threadpriv_cache_list -> addr = NULL;
        }
        __kmp_threadpriv_cache_list = __kmp_threadpriv_cache_list -> next;
    }

    __kmp_init_runtime = FALSE;

    /* reset statically initialized locks */
    __kmp_init_bootstrap_lock( &__kmp_initz_lock );
    __kmp_init_bootstrap_lock( &__kmp_stdio_lock );
    __kmp_init_bootstrap_lock( &__kmp_console_lock );

    /* This is necessary to make sure no stale data is left around */
    /* AC: customers complain that we use unsafe routines in the atfork
       handler. Mathworks: dlsym() is unsafe. We call dlsym and dlopen
       in dynamic_link when check the presence of shared tbbmalloc library.
       Suggestion is to make the library initialization lazier, similar
       to what done for __kmpc_begin(). */
    // TODO: synchronize all static initializations with regular library
    //       startup; look at kmp_global.c and etc.
    //__kmp_internal_begin ();

}

void
__kmp_register_atfork(void) {
    if ( __kmp_need_register_atfork ) {
        int status = pthread_atfork( __kmp_atfork_prepare, __kmp_atfork_parent, __kmp_atfork_child );
        KMP_CHECK_SYSFAIL( "pthread_atfork", status );
        __kmp_need_register_atfork = FALSE;
    }
}

void
__kmp_suspend_initialize( void )
{
    int status;
    status = pthread_mutexattr_init( &__kmp_suspend_mutex_attr );
    KMP_CHECK_SYSFAIL( "pthread_mutexattr_init", status );
    status = pthread_condattr_init( &__kmp_suspend_cond_attr );
    KMP_CHECK_SYSFAIL( "pthread_condattr_init", status );
}

static void
__kmp_suspend_initialize_thread( kmp_info_t *th )
{
    if ( th->th.th_suspend_init_count <= __kmp_fork_count ) {
        /* this means we haven't initialized the suspension pthread objects for this thread
           in this instance of the process */
        int     status;
        status = pthread_cond_init( &th->th.th_suspend_cv.c_cond, &__kmp_suspend_cond_attr );
        KMP_CHECK_SYSFAIL( "pthread_cond_init", status );
        status = pthread_mutex_init( &th->th.th_suspend_mx.m_mutex, & __kmp_suspend_mutex_attr );
        KMP_CHECK_SYSFAIL( "pthread_mutex_init", status );
        *(volatile int*)&th->th.th_suspend_init_count = __kmp_fork_count + 1;
    };
}

void
__kmp_suspend_uninitialize_thread( kmp_info_t *th )
{
    if(th->th.th_suspend_init_count > __kmp_fork_count) {
        /* this means we have initialize the suspension pthread objects for this thread
           in this instance of the process */
        int status;

        status = pthread_cond_destroy( &th->th.th_suspend_cv.c_cond );
        if ( status != 0 && status != EBUSY ) {
            KMP_SYSFAIL( "pthread_cond_destroy", status );
        };
        status = pthread_mutex_destroy( &th->th.th_suspend_mx.m_mutex );
        if ( status != 0 && status != EBUSY ) {
            KMP_SYSFAIL( "pthread_mutex_destroy", status );
        };
        --th->th.th_suspend_init_count;
        KMP_DEBUG_ASSERT(th->th.th_suspend_init_count == __kmp_fork_count);
    }
}

/* This routine puts the calling thread to sleep after setting the
 * sleep bit for the indicated flag variable to true.
 */
template <class C>
static inline void __kmp_suspend_template( int th_gtid, C *flag )
{
    KMP_TIME_BLOCK(USER_suspend);
    kmp_info_t *th = __kmp_threads[th_gtid];
    int status;
    typename C::flag_t old_spin;

    KF_TRACE( 30, ("__kmp_suspend_template: T#%d enter for flag = %p\n", th_gtid, flag->get() ) );

    __kmp_suspend_initialize_thread( th );

    status = pthread_mutex_lock( &th->th.th_suspend_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_lock", status );

    KF_TRACE( 10, ( "__kmp_suspend_template: T#%d setting sleep bit for spin(%p)\n",
                    th_gtid, flag->get() ) );

    /* TODO: shouldn't this use release semantics to ensure that __kmp_suspend_initialize_thread
       gets called first?
    */
    old_spin = flag->set_sleeping();

    KF_TRACE( 5, ( "__kmp_suspend_template: T#%d set sleep bit for spin(%p)==%d\n",
                   th_gtid, flag->get(), *(flag->get()) ) );

    if ( flag->done_check_val(old_spin) ) {
        old_spin = flag->unset_sleeping();
        KF_TRACE( 5, ( "__kmp_suspend_template: T#%d false alarm, reset sleep bit for spin(%p)\n",
                       th_gtid, flag->get()) );
    } else {
        /* Encapsulate in a loop as the documentation states that this may
         * "with low probability" return when the condition variable has
         * not been signaled or broadcast
         */
        int deactivated = FALSE;
        TCW_PTR(th->th.th_sleep_loc, (void *)flag);
        while ( flag->is_sleeping() ) {
#ifdef DEBUG_SUSPEND
            char buffer[128];
            __kmp_suspend_count++;
            __kmp_print_cond( buffer, &th->th.th_suspend_cv );
            __kmp_printf( "__kmp_suspend_template: suspending T#%d: %s\n", th_gtid, buffer );
#endif
            // Mark the thread as no longer active (only in the first iteration of the loop).
            if ( ! deactivated ) {
                th->th.th_active = FALSE;
                if ( th->th.th_active_in_pool ) {
                    th->th.th_active_in_pool = FALSE;
                    KMP_TEST_THEN_DEC32(
                      (kmp_int32 *) &__kmp_thread_pool_active_nth );
                    KMP_DEBUG_ASSERT( TCR_4(__kmp_thread_pool_active_nth) >= 0 );
                }
                deactivated = TRUE;


            }

#if USE_SUSPEND_TIMEOUT
            struct timespec  now;
            struct timeval   tval;
            int msecs;

            status = gettimeofday( &tval, NULL );
            KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
            TIMEVAL_TO_TIMESPEC( &tval, &now );

            msecs = (4*__kmp_dflt_blocktime) + 200;
            now.tv_sec  += msecs / 1000;
            now.tv_nsec += (msecs % 1000)*1000;

            KF_TRACE( 15, ( "__kmp_suspend_template: T#%d about to perform pthread_cond_timedwait\n",
                            th_gtid ) );
            status = pthread_cond_timedwait( &th->th.th_suspend_cv.c_cond, &th->th.th_suspend_mx.m_mutex, & now );
#else
            KF_TRACE( 15, ( "__kmp_suspend_template: T#%d about to perform pthread_cond_wait\n",
                               th_gtid ) );

            status = pthread_cond_wait( &th->th.th_suspend_cv.c_cond, &th->th.th_suspend_mx.m_mutex );
#endif

            if ( (status != 0) && (status != EINTR) && (status != ETIMEDOUT) ) {
                KMP_SYSFAIL( "pthread_cond_wait", status );
            }
#ifdef KMP_DEBUG
            if (status == ETIMEDOUT) {
                if ( flag->is_sleeping() ) {
                    KF_TRACE( 100, ( "__kmp_suspend_template: T#%d timeout wakeup\n", th_gtid ) );
                } else {
                    KF_TRACE( 2, ( "__kmp_suspend_template: T#%d timeout wakeup, sleep bit not set!\n",
                                   th_gtid ) );
                }
            } else if ( flag->is_sleeping() ) {
                KF_TRACE( 100, ( "__kmp_suspend_template: T#%d spurious wakeup\n", th_gtid ) );
            }
#endif
        } // while

        // Mark the thread as active again (if it was previous marked as inactive)
        if ( deactivated ) {
            th->th.th_active = TRUE;
            if ( TCR_4(th->th.th_in_pool) ) {
                KMP_TEST_THEN_INC32( (kmp_int32 *) &__kmp_thread_pool_active_nth );
                th->th.th_active_in_pool = TRUE;
            }
        }
    }

#ifdef DEBUG_SUSPEND
    {
        char buffer[128];
        __kmp_print_cond( buffer, &th->th.th_suspend_cv);
        __kmp_printf( "__kmp_suspend_template: T#%d has awakened: %s\n", th_gtid, buffer );
    }
#endif


    status = pthread_mutex_unlock( &th->th.th_suspend_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );

    KF_TRACE( 30, ("__kmp_suspend_template: T#%d exit\n", th_gtid ) );
}

void __kmp_suspend_32(int th_gtid, kmp_flag_32 *flag) {
    __kmp_suspend_template(th_gtid, flag);
}
void __kmp_suspend_64(int th_gtid, kmp_flag_64 *flag) {
    __kmp_suspend_template(th_gtid, flag);
}
void __kmp_suspend_oncore(int th_gtid, kmp_flag_oncore *flag) {
    __kmp_suspend_template(th_gtid, flag);
}


/* This routine signals the thread specified by target_gtid to wake up
 * after setting the sleep bit indicated by the flag argument to FALSE.
 * The target thread must already have called __kmp_suspend_template()
 */
template <class C>
static inline void __kmp_resume_template( int target_gtid, C *flag )
{
    kmp_info_t *th = __kmp_threads[target_gtid];
    int status;

#ifdef KMP_DEBUG
    int gtid = TCR_4(__kmp_init_gtid) ? __kmp_get_gtid() : -1;
#endif

    KF_TRACE( 30, ( "__kmp_resume_template: T#%d wants to wakeup T#%d enter\n", gtid, target_gtid ) );
    KMP_DEBUG_ASSERT( gtid != target_gtid );

    __kmp_suspend_initialize_thread( th );

    status = pthread_mutex_lock( &th->th.th_suspend_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_lock", status );

    if (!flag) {
        flag = (C *)th->th.th_sleep_loc;
    }

    if (!flag) {
        KF_TRACE( 5, ( "__kmp_resume_template: T#%d exiting, thread T#%d already awake: flag(%p)\n",
                       gtid, target_gtid, NULL ) );
        status = pthread_mutex_unlock( &th->th.th_suspend_mx.m_mutex );
        KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );
        return;
    }
    else {
        typename C::flag_t old_spin = flag->unset_sleeping();
        if ( ! flag->is_sleeping_val(old_spin) ) {
            KF_TRACE( 5, ( "__kmp_resume_template: T#%d exiting, thread T#%d already awake: flag(%p): "
                           "%u => %u\n",
                           gtid, target_gtid, flag->get(), old_spin, *flag->get() ) );

            status = pthread_mutex_unlock( &th->th.th_suspend_mx.m_mutex );
            KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );
            return;
        }
        KF_TRACE( 5, ( "__kmp_resume_template: T#%d about to wakeup T#%d, reset sleep bit for flag's loc(%p): "
                       "%u => %u\n",
                       gtid, target_gtid, flag->get(), old_spin, *flag->get() ) );
    }
    TCW_PTR(th->th.th_sleep_loc, NULL);


#ifdef DEBUG_SUSPEND
    {
        char buffer[128];
        __kmp_print_cond( buffer, &th->th.th_suspend_cv );
        __kmp_printf( "__kmp_resume_template: T#%d resuming T#%d: %s\n", gtid, target_gtid, buffer );
    }
#endif


    status = pthread_cond_signal( &th->th.th_suspend_cv.c_cond );
    KMP_CHECK_SYSFAIL( "pthread_cond_signal", status );
    status = pthread_mutex_unlock( &th->th.th_suspend_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );
    KF_TRACE( 30, ( "__kmp_resume_template: T#%d exiting after signaling wake up for T#%d\n",
                    gtid, target_gtid ) );
}

void __kmp_resume_32(int target_gtid, kmp_flag_32 *flag) {
    __kmp_resume_template(target_gtid, flag);
}
void __kmp_resume_64(int target_gtid, kmp_flag_64 *flag) {
    __kmp_resume_template(target_gtid, flag);
}
void __kmp_resume_oncore(int target_gtid, kmp_flag_oncore *flag) {
    __kmp_resume_template(target_gtid, flag);
}

void
__kmp_resume_monitor()
{
    KMP_TIME_BLOCK(USER_resume);
    int status;
#ifdef KMP_DEBUG
    int gtid = TCR_4(__kmp_init_gtid) ? __kmp_get_gtid() : -1;
    KF_TRACE( 30, ( "__kmp_resume_monitor: T#%d wants to wakeup T#%d enter\n",
                    gtid, KMP_GTID_MONITOR ) );
    KMP_DEBUG_ASSERT( gtid != KMP_GTID_MONITOR );
#endif
    status = pthread_mutex_lock( &__kmp_wait_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_lock", status );
#ifdef DEBUG_SUSPEND
    {
        char buffer[128];
        __kmp_print_cond( buffer, &__kmp_wait_cv.c_cond );
        __kmp_printf( "__kmp_resume_monitor: T#%d resuming T#%d: %s\n", gtid, KMP_GTID_MONITOR, buffer );
    }
#endif
    status = pthread_cond_signal( &__kmp_wait_cv.c_cond );
    KMP_CHECK_SYSFAIL( "pthread_cond_signal", status );
    status = pthread_mutex_unlock( &__kmp_wait_mx.m_mutex );
    KMP_CHECK_SYSFAIL( "pthread_mutex_unlock", status );
    KF_TRACE( 30, ( "__kmp_resume_monitor: T#%d exiting after signaling wake up for T#%d\n",
                    gtid, KMP_GTID_MONITOR ) );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_yield( int cond )
{
    if (cond && __kmp_yielding_on) {
        sched_yield();
    }
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_gtid_set_specific( int gtid )
{
    int status;
    KMP_ASSERT( __kmp_init_runtime );
    status = pthread_setspecific( __kmp_gtid_threadprivate_key, (void*)(intptr_t)(gtid+1) );
    KMP_CHECK_SYSFAIL( "pthread_setspecific", status );
}

int
__kmp_gtid_get_specific()
{
    int gtid;
    if ( !__kmp_init_runtime ) {
        KA_TRACE( 50, ("__kmp_get_specific: runtime shutdown, returning KMP_GTID_SHUTDOWN\n" ) );
        return KMP_GTID_SHUTDOWN;
    }
    gtid = (int)(size_t)pthread_getspecific( __kmp_gtid_threadprivate_key );
    if ( gtid == 0 ) {
        gtid = KMP_GTID_DNE;
    }
    else {
        gtid--;
    }
    KA_TRACE( 50, ("__kmp_gtid_get_specific: key:%d gtid:%d\n",
               __kmp_gtid_threadprivate_key, gtid ));
    return gtid;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

double
__kmp_read_cpu_time( void )
{
    /*clock_t   t;*/
    struct tms  buffer;

    /*t =*/  times( & buffer );

    return (buffer.tms_utime + buffer.tms_cutime) / (double) CLOCKS_PER_SEC;
}

int
__kmp_read_system_info( struct kmp_sys_info *info )
{
    int status;
    struct rusage r_usage;

    memset( info, 0, sizeof( *info ) );

    status = getrusage( RUSAGE_SELF, &r_usage);
    KMP_CHECK_SYSFAIL_ERRNO( "getrusage", status );

    info->maxrss  = r_usage.ru_maxrss;  /* the maximum resident set size utilized (in kilobytes)     */
    info->minflt  = r_usage.ru_minflt;  /* the number of page faults serviced without any I/O        */
    info->majflt  = r_usage.ru_majflt;  /* the number of page faults serviced that required I/O      */
    info->nswap   = r_usage.ru_nswap;   /* the number of times a process was "swapped" out of memory */
    info->inblock = r_usage.ru_inblock; /* the number of times the file system had to perform input  */
    info->oublock = r_usage.ru_oublock; /* the number of times the file system had to perform output */
    info->nvcsw   = r_usage.ru_nvcsw;   /* the number of times a context switch was voluntarily      */
    info->nivcsw  = r_usage.ru_nivcsw;  /* the number of times a context switch was forced           */

    return (status != 0);
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */


void
__kmp_read_system_time( double *delta )
{
    double              t_ns;
    struct timeval      tval;
    struct timespec     stop;
    int status;

    status = gettimeofday( &tval, NULL );
    KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
    TIMEVAL_TO_TIMESPEC( &tval, &stop );
    t_ns = TS2NS(stop) - TS2NS(__kmp_sys_timer_data.start);
    *delta = (t_ns * 1e-9);
}

void
__kmp_clear_system_time( void )
{
    struct timeval tval;
    int status;
    status = gettimeofday( &tval, NULL );
    KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
    TIMEVAL_TO_TIMESPEC( &tval, &__kmp_sys_timer_data.start );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#ifdef BUILD_TV

void
__kmp_tv_threadprivate_store( kmp_info_t *th, void *global_addr, void *thread_addr )
{
    struct tv_data *p;

    p = (struct tv_data *) __kmp_allocate( sizeof( *p ) );

    p->u.tp.global_addr = global_addr;
    p->u.tp.thread_addr = thread_addr;

    p->type = (void *) 1;

    p->next =  th->th.th_local.tv_data;
    th->th.th_local.tv_data = p;

    if ( p->next == 0 ) {
        int rc = pthread_setspecific( __kmp_tv_key, p );
        KMP_CHECK_SYSFAIL( "pthread_setspecific", rc );
    }
}

#endif /* BUILD_TV */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

static int
__kmp_get_xproc( void ) {

    int r = 0;

    #if KMP_OS_LINUX

        r = sysconf( _SC_NPROCESSORS_ONLN );

    #elif KMP_OS_DARWIN

        // Bug C77011 High "OpenMP Threads and number of active cores".

        // Find the number of available CPUs.
        kern_return_t          rc;
        host_basic_info_data_t info;
        mach_msg_type_number_t num = HOST_BASIC_INFO_COUNT;
        rc = host_info( mach_host_self(), HOST_BASIC_INFO, (host_info_t) & info, & num );
        if ( rc == 0 && num == HOST_BASIC_INFO_COUNT ) {
            // Cannot use KA_TRACE() here because this code works before trace support is
            // initialized.
            r = info.avail_cpus;
        } else {
            KMP_WARNING( CantGetNumAvailCPU );
            KMP_INFORM( AssumedNumCPU );
        }; // if

    #elif KMP_OS_FREEBSD

        int mib[] = { CTL_HW, HW_NCPU };
        size_t len = sizeof( r );
        if ( sysctl( mib, 2, &r, &len, NULL, 0 ) < 0 ) {
             r = 0;
             KMP_WARNING( CantGetNumAvailCPU );
             KMP_INFORM( AssumedNumCPU );
        }

    #else

        #error "Unknown or unsupported OS."

    #endif

    return r > 0 ? r : 2; /* guess value of 2 if OS told us 0 */

} // __kmp_get_xproc

int
__kmp_read_from_file( char const *path, char const *format, ... )
{
    int result;
    va_list args;

    va_start(args, format);
    FILE *f = fopen(path, "rb");
    if ( f == NULL )
        return 0;
    result = vfscanf(f, format, args);
    fclose(f);

    return result;
}

void
__kmp_runtime_initialize( void )
{
    int status;
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t  cond_attr;

    if ( __kmp_init_runtime ) {
        return;
    }; // if

    #if ( KMP_ARCH_X86 || KMP_ARCH_X86_64 )
        if ( ! __kmp_cpuinfo.initialized ) {
            __kmp_query_cpuid( &__kmp_cpuinfo );
        }; // if
    #endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

    __kmp_xproc = __kmp_get_xproc();

    if ( sysconf( _SC_THREADS ) ) {

        /* Query the maximum number of threads */
        __kmp_sys_max_nth = sysconf( _SC_THREAD_THREADS_MAX );
        if ( __kmp_sys_max_nth == -1 ) {
            /* Unlimited threads for NPTL */
            __kmp_sys_max_nth = INT_MAX;
        }
        else if ( __kmp_sys_max_nth <= 1 ) {
            /* Can't tell, just use PTHREAD_THREADS_MAX */
            __kmp_sys_max_nth = KMP_MAX_NTH;
        }

        /* Query the minimum stack size */
        __kmp_sys_min_stksize = sysconf( _SC_THREAD_STACK_MIN );
        if ( __kmp_sys_min_stksize <= 1 ) {
            __kmp_sys_min_stksize = KMP_MIN_STKSIZE;
        }
    }

    /* Set up minimum number of threads to switch to TLS gtid */
    __kmp_tls_gtid_min = KMP_TLS_GTID_MIN;


    #ifdef BUILD_TV
        {
            int rc = pthread_key_create( & __kmp_tv_key, 0 );
            KMP_CHECK_SYSFAIL( "pthread_key_create", rc );
        }
    #endif

    status = pthread_key_create( &__kmp_gtid_threadprivate_key, __kmp_internal_end_dest );
    KMP_CHECK_SYSFAIL( "pthread_key_create", status );
    status = pthread_mutexattr_init( & mutex_attr );
    KMP_CHECK_SYSFAIL( "pthread_mutexattr_init", status );
    status = pthread_mutex_init( & __kmp_wait_mx.m_mutex, & mutex_attr );
    KMP_CHECK_SYSFAIL( "pthread_mutex_init", status );
    status = pthread_condattr_init( & cond_attr );
    KMP_CHECK_SYSFAIL( "pthread_condattr_init", status );
    status = pthread_cond_init( & __kmp_wait_cv.c_cond, & cond_attr );
    KMP_CHECK_SYSFAIL( "pthread_cond_init", status );
#if USE_ITT_BUILD
    __kmp_itt_initialize();
#endif /* USE_ITT_BUILD */

    __kmp_init_runtime = TRUE;
}

void
__kmp_runtime_destroy( void )
{
    int status;

    if ( ! __kmp_init_runtime ) {
        return; // Nothing to do.
    };

#if USE_ITT_BUILD
    __kmp_itt_destroy();
#endif /* USE_ITT_BUILD */

    status = pthread_key_delete( __kmp_gtid_threadprivate_key );
    KMP_CHECK_SYSFAIL( "pthread_key_delete", status );
    #ifdef BUILD_TV
        status = pthread_key_delete( __kmp_tv_key );
        KMP_CHECK_SYSFAIL( "pthread_key_delete", status );
    #endif

    status = pthread_mutex_destroy( & __kmp_wait_mx.m_mutex );
    if ( status != 0 && status != EBUSY ) {
        KMP_SYSFAIL( "pthread_mutex_destroy", status );
    }
    status = pthread_cond_destroy( & __kmp_wait_cv.c_cond );
    if ( status != 0 && status != EBUSY ) {
        KMP_SYSFAIL( "pthread_cond_destroy", status );
    }
    #if KMP_AFFINITY_SUPPORTED
        __kmp_affinity_uninitialize();
    #endif

    __kmp_init_runtime = FALSE;
}


/* Put the thread to sleep for a time period */
/* NOTE: not currently used anywhere */
void
__kmp_thread_sleep( int millis )
{
    sleep(  ( millis + 500 ) / 1000 );
}

/* Calculate the elapsed wall clock time for the user */
void
__kmp_elapsed( double *t )
{
    int status;
# ifdef FIX_SGI_CLOCK
    struct timespec ts;

    status = clock_gettime( CLOCK_PROCESS_CPUTIME_ID, &ts );
    KMP_CHECK_SYSFAIL_ERRNO( "clock_gettime", status );
    *t = (double) ts.tv_nsec * (1.0 / (double) NSEC_PER_SEC) +
        (double) ts.tv_sec;
# else
    struct timeval tv;

    status = gettimeofday( & tv, NULL );
    KMP_CHECK_SYSFAIL_ERRNO( "gettimeofday", status );
    *t = (double) tv.tv_usec * (1.0 / (double) USEC_PER_SEC) +
        (double) tv.tv_sec;
# endif
}

/* Calculate the elapsed wall clock tick for the user */
void
__kmp_elapsed_tick( double *t )
{
    *t = 1 / (double) CLOCKS_PER_SEC;
}

/*
    Determine whether the given address is mapped into the current address space.
*/

int
__kmp_is_address_mapped( void * addr ) {

    int found = 0;
    int rc;

    #if KMP_OS_LINUX

        /*
            On Linux* OS, read the /proc/<pid>/maps pseudo-file to get all the address ranges mapped
            into the address space.
        */

        char * name = __kmp_str_format( "/proc/%d/maps", getpid() );
        FILE * file  = NULL;

        file = fopen( name, "r" );
        KMP_ASSERT( file != NULL );

        for ( ; ; ) {

            void * beginning = NULL;
            void * ending    = NULL;
            char   perms[ 5 ];

            rc = fscanf( file, "%p-%p %4s %*[^\n]\n", & beginning, & ending, perms );
            if ( rc == EOF ) {
                break;
            }; // if
            KMP_ASSERT( rc == 3 && strlen( perms ) == 4 ); // Make sure all fields are read.

            // Ending address is not included in the region, but beginning is.
            if ( ( addr >= beginning ) && ( addr < ending ) ) {
                perms[ 2 ] = 0;    // 3th and 4th character does not matter.
                if ( strcmp( perms, "rw" ) == 0 ) {
                    // Memory we are looking for should be readable and writable.
                    found = 1;
                }; // if
                break;
            }; // if

        }; // forever

        // Free resources.
        fclose( file );
        KMP_INTERNAL_FREE( name );

    #elif KMP_OS_DARWIN

        /*
            On OS X*, /proc pseudo filesystem is not available. Try to read memory using vm
            interface.
        */

        int       buffer;
        vm_size_t count;
        rc =
            vm_read_overwrite(
                mach_task_self(),           // Task to read memory of.
                (vm_address_t)( addr ),     // Address to read from.
                1,                          // Number of bytes to be read.
                (vm_address_t)( & buffer ), // Address of buffer to save read bytes in.
                & count                     // Address of var to save number of read bytes in.
            );
        if ( rc == 0 ) {
            // Memory successfully read.
            found = 1;
        }; // if

    #elif KMP_OS_FREEBSD

        // FIXME(FreeBSD*): Implement this
        found = 1;

    #else

        #error "Unknown or unsupported OS"

    #endif

    return found;

} // __kmp_is_address_mapped

#ifdef USE_LOAD_BALANCE


# if KMP_OS_DARWIN

// The function returns the rounded value of the system load average
// during given time interval which depends on the value of
// __kmp_load_balance_interval variable (default is 60 sec, other values
// may be 300 sec or 900 sec).
// It returns -1 in case of error.
int
__kmp_get_load_balance( int max )
{
    double averages[3];
    int ret_avg = 0;

    int res = getloadavg( averages, 3 );

    //Check __kmp_load_balance_interval to determine which of averages to use.
    // getloadavg() may return the number of samples less than requested that is
    // less than 3.
    if ( __kmp_load_balance_interval < 180 && ( res >= 1 ) ) {
        ret_avg = averages[0];// 1 min
    } else if ( ( __kmp_load_balance_interval >= 180
                  && __kmp_load_balance_interval < 600 ) && ( res >= 2 ) ) {
        ret_avg = averages[1];// 5 min
    } else if ( ( __kmp_load_balance_interval >= 600 ) && ( res == 3 ) ) {
        ret_avg = averages[2];// 15 min
    } else {// Error occurred
        return -1;
    }

    return ret_avg;
}

# else // Linux* OS

// The fuction returns number of running (not sleeping) threads, or -1 in case of error.
// Error could be reported if Linux* OS kernel too old (without "/proc" support).
// Counting running threads stops if max running threads encountered.
int
__kmp_get_load_balance( int max )
{
    static int permanent_error = 0;

    static int     glb_running_threads          = 0;  /* Saved count of the running threads for the thread balance algortihm */
    static double  glb_call_time = 0;  /* Thread balance algorithm call time */

    int running_threads = 0;              // Number of running threads in the system.

    DIR  *          proc_dir   = NULL;    // Handle of "/proc/" directory.
    struct dirent * proc_entry = NULL;

    kmp_str_buf_t   task_path;            // "/proc/<pid>/task/<tid>/" path.
    DIR  *          task_dir   = NULL;    // Handle of "/proc/<pid>/task/<tid>/" directory.
    struct dirent * task_entry = NULL;
    int             task_path_fixed_len;

    kmp_str_buf_t   stat_path;            // "/proc/<pid>/task/<tid>/stat" path.
    int             stat_file = -1;
    int             stat_path_fixed_len;

    int total_processes = 0;              // Total number of processes in system.
    int total_threads   = 0;              // Total number of threads in system.

    double call_time = 0.0;

    __kmp_str_buf_init( & task_path );
    __kmp_str_buf_init( & stat_path );

     __kmp_elapsed( & call_time );

    if ( glb_call_time &&
            ( call_time - glb_call_time < __kmp_load_balance_interval ) ) {
        running_threads = glb_running_threads;
        goto finish;
    }

    glb_call_time = call_time;

    // Do not spend time on scanning "/proc/" if we have a permanent error.
    if ( permanent_error ) {
        running_threads = -1;
        goto finish;
    }; // if

    if ( max <= 0 ) {
        max = INT_MAX;
    }; // if

    // Open "/proc/" directory.
    proc_dir = opendir( "/proc" );
    if ( proc_dir == NULL ) {
        // Cannot open "/prroc/". Probably the kernel does not support it. Return an error now and
        // in subsequent calls.
        running_threads = -1;
        permanent_error = 1;
        goto finish;
    }; // if

    // Initialize fixed part of task_path. This part will not change.
    __kmp_str_buf_cat( & task_path, "/proc/", 6 );
    task_path_fixed_len = task_path.used;    // Remember number of used characters.

    proc_entry = readdir( proc_dir );
    while ( proc_entry != NULL ) {
        // Proc entry is a directory and name starts with a digit. Assume it is a process'
        // directory.
        if ( proc_entry->d_type == DT_DIR && isdigit( proc_entry->d_name[ 0 ] ) ) {

            ++ total_processes;
            // Make sure init process is the very first in "/proc", so we can replace
            // strcmp( proc_entry->d_name, "1" ) == 0 with simpler total_processes == 1.
            // We are going to check that total_processes == 1 => d_name == "1" is true (where
            // "=>" is implication). Since C++ does not have => operator, let us replace it with its
            // equivalent: a => b == ! a || b.
            KMP_DEBUG_ASSERT( total_processes != 1 || strcmp( proc_entry->d_name, "1" ) == 0 );

            // Construct task_path.
            task_path.used = task_path_fixed_len;    // Reset task_path to "/proc/".
            __kmp_str_buf_cat( & task_path, proc_entry->d_name, strlen( proc_entry->d_name ) );
            __kmp_str_buf_cat( & task_path, "/task", 5 );

            task_dir = opendir( task_path.str );
            if ( task_dir == NULL ) {
                // Process can finish between reading "/proc/" directory entry and opening process'
                // "task/" directory. So, in general case we should not complain, but have to skip
                // this process and read the next one.
                // But on systems with no "task/" support we will spend lot of time to scan "/proc/"
                // tree again and again without any benefit. "init" process (its pid is 1) should
                // exist always, so, if we cannot open "/proc/1/task/" directory, it means "task/"
                // is not supported by kernel. Report an error now and in the future.
                if ( strcmp( proc_entry->d_name, "1" ) == 0 ) {
                    running_threads = -1;
                    permanent_error = 1;
                    goto finish;
                }; // if
            } else {
                 // Construct fixed part of stat file path.
                __kmp_str_buf_clear( & stat_path );
                __kmp_str_buf_cat( & stat_path, task_path.str, task_path.used );
                __kmp_str_buf_cat( & stat_path, "/", 1 );
                stat_path_fixed_len = stat_path.used;

                task_entry = readdir( task_dir );
                while ( task_entry != NULL ) {
                    // It is a directory and name starts with a digit.
                    if ( proc_entry->d_type == DT_DIR && isdigit( task_entry->d_name[ 0 ] ) ) {

                        ++ total_threads;

                        // Consruct complete stat file path. Easiest way would be:
                        //  __kmp_str_buf_print( & stat_path, "%s/%s/stat", task_path.str, task_entry->d_name );
                        // but seriae of __kmp_str_buf_cat works a bit faster.
                        stat_path.used = stat_path_fixed_len;    // Reset stat path to its fixed part.
                        __kmp_str_buf_cat( & stat_path, task_entry->d_name, strlen( task_entry->d_name ) );
                        __kmp_str_buf_cat( & stat_path, "/stat", 5 );

                        // Note: Low-level API (open/read/close) is used. High-level API
                        // (fopen/fclose)  works ~ 30 % slower.
                        stat_file = open( stat_path.str, O_RDONLY );
                        if ( stat_file == -1 ) {
                            // We cannot report an error because task (thread) can terminate just
                            // before reading this file.
                        } else {
                            /*
                                Content of "stat" file looks like:

                                    24285 (program) S ...

                                It is a single line (if program name does not include fanny
                                symbols). First number is a thread id, then name of executable file
                                name in paretheses, then state of the thread. We need just thread
                                state.

                                Good news: Length of program name is 15 characters max. Longer
                                names are truncated.

                                Thus, we need rather short buffer: 15 chars for program name +
                                2 parenthesis, + 3 spaces + ~7 digits of pid = 37.

                                Bad news: Program name may contain special symbols like space,
                                closing parenthesis, or even new line. This makes parsing "stat"
                                file not 100 % reliable. In case of fanny program names parsing
                                may fail (report incorrect thread state).

                                Parsing "status" file looks more promissing (due to different
                                file structure and escaping special symbols) but reading and
                                parsing of "status" file works slower.

                                -- ln
                            */
                            char buffer[ 65 ];
                            int len;
                            len = read( stat_file, buffer, sizeof( buffer ) - 1 );
                            if ( len >= 0 ) {
                                buffer[ len ] = 0;
                                // Using scanf:
                                //     sscanf( buffer, "%*d (%*s) %c ", & state );
                                // looks very nice, but searching for a closing parenthesis works a
                                // bit faster.
                                char * close_parent = strstr( buffer, ") " );
                                if ( close_parent != NULL ) {
                                    char state = * ( close_parent + 2 );
                                    if ( state == 'R' ) {
                                        ++ running_threads;
                                        if ( running_threads >= max ) {
                                            goto finish;
                                        }; // if
                                    }; // if
                                }; // if
                            }; // if
                            close( stat_file );
                            stat_file = -1;
                        }; // if
                    }; // if
                    task_entry = readdir( task_dir );
                }; // while
                closedir( task_dir );
                task_dir = NULL;
            }; // if
        }; // if
        proc_entry = readdir( proc_dir );
    }; // while

    //
    // There _might_ be a timing hole where the thread executing this
    // code get skipped in the load balance, and running_threads is 0.
    // Assert in the debug builds only!!!
    //
    KMP_DEBUG_ASSERT( running_threads > 0 );
    if ( running_threads <= 0 ) {
        running_threads = 1;
    }

    finish: // Clean up and exit.
        if ( proc_dir != NULL ) {
            closedir( proc_dir );
        }; // if
        __kmp_str_buf_free( & task_path );
        if ( task_dir != NULL ) {
            closedir( task_dir );
        }; // if
        __kmp_str_buf_free( & stat_path );
        if ( stat_file != -1 ) {
            close( stat_file );
        }; // if

    glb_running_threads = running_threads;

    return running_threads;

} // __kmp_get_load_balance

# endif // KMP_OS_DARWIN

#endif // USE_LOAD_BALANCE


#if KMP_COMPILER_GCC && !(KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_PPC64)

int __kmp_invoke_microtask( microtask_t pkfn, int gtid, int tid, int argc,
        void *p_argv[] )
{
    int argc_full = argc + 2;
    int i;
    ffi_cif cif;
    ffi_type *types[argc_full];
    void *args[argc_full];
    void *idp[2];

    /* We're only passing pointers to the target. */
    for (i = 0; i < argc_full; i++)
        types[i] = &ffi_type_pointer;

    /* Ugly double-indirection, but that's how it goes... */
    idp[0] = &gtid;
    idp[1] = &tid;
    args[0] = &idp[0];
    args[1] = &idp[1];

    for (i = 0; i < argc; i++)
        args[2 + i] = &p_argv[i];

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc_full,
                &ffi_type_void, types) != FFI_OK)
        abort();

    ffi_call(&cif, (void (*)(void))pkfn, NULL, args);

    return 1;
}

#endif // KMP_COMPILER_GCC && !(KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_PPC64)

#if KMP_ARCH_PPC64

// we really only need the case with 1 argument, because CLANG always build
// a struct of pointers to shared variables referenced in the outlined function
int
__kmp_invoke_microtask( microtask_t pkfn,
                        int gtid, int tid,
                        int argc, void *p_argv[] ) {
  switch (argc) {
  default:
    fprintf(stderr, "Too many args to microtask: %d!\n", argc);
    fflush(stderr);
    exit(-1);
  case 0:
    (*pkfn)(&gtid, &tid);
    break;
  case 1:
    (*pkfn)(&gtid, &tid, p_argv[0]);
    break;
  case 2:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1]);
    break;
  case 3:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2]);
    break;
  case 4:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3]);
    break;
  case 5:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4]);
    break;
  case 6:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5]);
    break;
  case 7:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6]);
    break;
  case 8:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7]);
    break;
  case 9:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8]);
    break;
  case 10:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9]);
    break;
  case 11:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9], p_argv[10]);
    break;
  case 12:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9], p_argv[10],
            p_argv[11]);
    break;
  case 13:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9], p_argv[10],
            p_argv[11], p_argv[12]);
    break;
  case 14:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9], p_argv[10],
            p_argv[11], p_argv[12], p_argv[13]);
    break;
  case 15:
    (*pkfn)(&gtid, &tid, p_argv[0], p_argv[1], p_argv[2], p_argv[3], p_argv[4],
            p_argv[5], p_argv[6], p_argv[7], p_argv[8], p_argv[9], p_argv[10],
            p_argv[11], p_argv[12], p_argv[13], p_argv[14]);
    break;
  }

  return 1;
}

#endif

// end of file //

