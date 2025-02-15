/*
 * kmp_settings.c -- Initialize environment variables
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
#include "kmp_environment.h"
#include "kmp_atomic.h"
#include "kmp_itt.h"
#include "kmp_str.h"
#include "kmp_settings.h"
#include "kmp_i18n.h"
#include "kmp_io.h"


static int __kmp_env_isDefined( char const * name );
static int __kmp_env_toPrint( char const * name, int flag );

bool __kmp_env_format = 0; // 0 - old format; 1 - new format
// -------------------------------------------------------------------------------------------------
// Helper string functions. Subject to move to kmp_str.
// -------------------------------------------------------------------------------------------------

static double
__kmp_convert_to_double( char const * s )
{
    double result;

    if ( sscanf( s, "%lf", &result ) < 1 ) {
        result = 0.0;
    }

    return result;
}

static unsigned int
__kmp_readstr_with_sentinel(char *dest, char const * src, size_t len, char sentinel) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        if ((*src == '\0') || (*src == sentinel)) {
            break;
        }
        *(dest++) = *(src++);
    }
    *dest = '\0';
    return i;
}

static int
__kmp_match_with_sentinel( char const * a, char const * b, size_t len, char sentinel ) {
    size_t l = 0;

    if(a == NULL)
        a = "";
    if(b == NULL)
        b = "";
    while(*a && *b && *b != sentinel) {
        char ca = *a, cb = *b;

        if(ca >= 'a' && ca <= 'z')
            ca -= 'a' - 'A';
        if(cb >= 'a' && cb <= 'z')
            cb -= 'a' - 'A';
        if(ca != cb)
            return FALSE;
        ++l;
        ++a;
        ++b;
    }
    return l >= len;
}

//
// Expected usage:
//     token is the token to check for.
//     buf is the string being parsed.
//     *end returns the char after the end of the token.
//        it is not modified unless a match occurs.
//
//
// Example 1:
//
//     if (__kmp_match_str("token", buf, *end) {
//         <do something>
//         buf = end;
//     }
//
//  Example 2:
//
//     if (__kmp_match_str("token", buf, *end) {
//         char *save = **end;
//         **end = sentinel;
//         <use any of the __kmp*_with_sentinel() functions>
//         **end = save;
//         buf = end;
//     }
//

static int
__kmp_match_str( char const *token, char const *buf, const char **end) {

    KMP_ASSERT(token != NULL);
    KMP_ASSERT(buf != NULL);
    KMP_ASSERT(end != NULL);

    while (*token && *buf) {
        char ct = *token, cb = *buf;

        if(ct >= 'a' && ct <= 'z')
            ct -= 'a' - 'A';
        if(cb >= 'a' && cb <= 'z')
            cb -= 'a' - 'A';
        if (ct != cb)
            return FALSE;
        ++token;
        ++buf;
    }
    if (*token) {
        return FALSE;
    }
    *end = buf;
    return TRUE;
}

static char *
__kmp_strip_quotes( char *target, int len) {
    char *end = target + len - 1;

    while(*target == '"' || *target == '\'') {
        if(end <= target || (*end != '"' && *end != '\''))
            return NULL;
        *end = 0;
        --end;
        *target = 0;
        ++target;
    }
    return target;
}


static size_t
__kmp_round4k( size_t size ) {
    size_t _4k = 4 * 1024;
    if ( size & ( _4k - 1 ) ) {
        size &= ~ ( _4k - 1 );
        if ( size <= KMP_SIZE_T_MAX - _4k ) {
            size += _4k;    // Round up if there is no overflow.
        }; // if
    }; // if
    return size;
} // __kmp_round4k


static int
__kmp_convert_to_seconds( char const * data )
{
    int nvalues, value, factor;
    char mult, extra;

    if (data == NULL) return (0);
    value = 0;
    mult = '\0';
    nvalues = sscanf (data, "%d%c%c", &value, &mult, &extra);
    if (nvalues < 1) return (0);
    if (nvalues == 1) mult = '\0';
    if (nvalues == 3) return (-1);

    switch (mult) {
    case 's': case 'S':
        factor = 1;
        break;
    case '\0':
        factor = 60;
        break;
    case 'm': case 'M':
        factor = 60;
        break;
    case 'h': case 'H':
        factor = 60 * 60;
        break;
    case 'd': case 'D':
        factor = 24 * 60 * 60;
        break;
    default:
        return (-1);
    }

    if (value > (INT_MAX / factor))
        value = INT_MAX;
    else
        value *= factor;

    return value;
}

/*
    Here, multipliers are like __kmp_convert_to_seconds, but floating-point
    values are allowed, and the return value is in milliseconds.  The default
    multiplier is milliseconds.  Returns INT_MAX only if the value specified
    matches "infinit*".  Returns -1 if specified string is invalid.
*/
int
__kmp_convert_to_milliseconds( char const * data )
{
    int ret, nvalues, factor;
    char mult, extra;
    double value;

    if (data == NULL) return (-1);
    if ( __kmp_str_match( "infinit", -1, data)) return (INT_MAX);
    value = (double) 0.0;
    mult = '\0';
    nvalues = sscanf (data, "%lf%c%c", &value, &mult, &extra);
    if (nvalues < 1) return (-1);
    if (nvalues == 1) mult = '\0';
    if (nvalues == 3) return (-1);

    if (value < 0)    return (-1);

    switch (mult) {
    case '\0':
        /*  default is milliseconds  */
        factor = 1;
        break;
    case 's': case 'S':
        factor = 1000;
        break;
    case 'm': case 'M':
        factor = 1000 * 60;
        break;
    case 'h': case 'H':
        factor = 1000 * 60 * 60;
        break;
    case 'd': case 'D':
        factor = 1000 * 24 * 60 * 60;
        break;
    default:
        return (-1);
    }

    if ( value >= ( (INT_MAX-1) / factor) )
        ret = INT_MAX-1;        /* Don't allow infinite value here */
    else
        ret = (int) (value * (double) factor);  /* truncate to int  */

    return ret;
}

static kmp_uint64
__kmp_convert_to_nanoseconds(         // R: Time in nanoseconds, or ~0 in case of error.
    char const * str                  // I: String representing time.
) {

    double     value;    // Parsed value.
    char       unit;     // Unit: 's', 'm', 'u', or 'n'.
    char       extra;    // Buffer for extra character (if any).
    int        rc;       // Return code of sscanf().
    double     factor;   // Numeric factor corresponding to unit.
    kmp_uint64 result;

    if ( str == NULL || str[ 0 ] == 0 ) {    // No string or empty string.
        return 0;                            // Default value.
    }; // if
    rc = sscanf( str, "%lf%c%c", &value, &unit, &extra );
    switch ( rc ) {
        case 0: {             // Value is not parsed.
            return ~ 0;
        } break;
        case 1: {             // One value parsed, no unit is specified.
            unit = 's';       // Use default unit.
        } break;
        case 2: {             // Value and unit are parsed.
            // Do nothing.
        } break;
        case 3: {             // Extra characters is specified.
            return ~ 0;
        } break;
    }; // switch
    switch ( unit ) {
        case 's': {
            factor = 1.0E+9;
        } break;
        case 'm': {
            factor = 1.0E+6;
        } break;
        case 'u': {
            factor = 1.0E+3;
        } break;
        case 'n': {
            factor = 1.0;
        } break;
        default: {                           // Illegal unit.
            return ~ 0;                      // Return error.
        } break;
    }; // switch
    result = (kmp_uint64)( value * factor );
    return result;

}; // func __kmp_convert_to_nanoseconds


static int
__kmp_strcasecmp_with_sentinel( char const * a, char const * b, char sentinel ) {
    if(a == NULL)
        a = "";
    if(b == NULL)
        b = "";
    while(*a && *b && *b != sentinel) {
        char ca = *a, cb = *b;

        if(ca >= 'a' && ca <= 'z')
            ca -= 'a' - 'A';
        if(cb >= 'a' && cb <= 'z')
            cb -= 'a' - 'A';
        if(ca != cb)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        ++a;
        ++b;
    }
    return *a ?
        (*b && *b != sentinel) ? (int)(unsigned char)*a - (int)(unsigned char)*b : 1 :
        (*b && *b != sentinel) ? -1 : 0;
}


// =================================================================================================
// Table structures and helper functions.
// =================================================================================================

typedef struct __kmp_setting        kmp_setting_t;
typedef struct __kmp_stg_ss_data    kmp_stg_ss_data_t;
typedef struct __kmp_stg_wp_data    kmp_stg_wp_data_t;
typedef struct __kmp_stg_fr_data    kmp_stg_fr_data_t;

typedef void ( * kmp_stg_parse_func_t )( char const * name, char const * value, void * data );
typedef void ( * kmp_stg_print_func_t )( kmp_str_buf_t * buffer, char const * name, void * data );

struct __kmp_setting {
    char const *         name;        // Name of setting (environment variable).
    kmp_stg_parse_func_t parse;       // Parser function.
    kmp_stg_print_func_t print;       // Print function.
    void *               data;        // Data passed to parser and printer.
    int                  set;         // Variable set during this "session"
                                      //     (__kmp_env_initialize() or kmp_set_defaults() call).
    int                  defined;     // Variable set in any "session".
}; // struct __kmp_setting

struct __kmp_stg_ss_data {
    size_t             factor;  // Default factor: 1 for KMP_STACKSIZE, 1024 for others.
    kmp_setting_t * *  rivals;  // Array of pointers to rivals (including itself).
}; // struct __kmp_stg_ss_data

struct __kmp_stg_wp_data {
    int                omp;     // 0 -- KMP_LIBRARY, 1 -- OMP_WAIT_POLICY.
    kmp_setting_t * *  rivals;  // Array of pointers to rivals (including itself).
}; // struct __kmp_stg_wp_data

struct __kmp_stg_fr_data {
    int                force;  // 0 -- KMP_DETERMINISTIC_REDUCTION, 1 -- KMP_FORCE_REDUCTION.
    kmp_setting_t * *  rivals;  // Array of pointers to rivals (including itself).
}; // struct __kmp_stg_fr_data

static int
__kmp_stg_check_rivals(          // 0 -- Ok, 1 -- errors found.
    char const *       name,     // Name of variable.
    char const *       value,    // Value of the variable.
    kmp_setting_t * *  rivals    // List of rival settings (the list must include current one).
);


// -------------------------------------------------------------------------------------------------
// Helper parse functions.
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_bool(
    char const * name,
    char const * value,
    int *        out
) {
    if ( __kmp_str_match_true( value ) ) {
        * out = TRUE;
    } else if (__kmp_str_match_false( value ) ) {
        * out = FALSE;
    } else {
        __kmp_msg(
            kmp_ms_warning,
            KMP_MSG( BadBoolValue, name, value ),
            KMP_HNT( ValidBoolValues ),
            __kmp_msg_null
        );
    }; // if
} // __kmp_stg_parse_bool

static void
__kmp_stg_parse_size(
    char const * name,
    char const * value,
    size_t       size_min,
    size_t       size_max,
    int *        is_specified,
    size_t *     out,
    size_t       factor
) {
    char const * msg = NULL;
    #if KMP_OS_DARWIN
        size_min = __kmp_round4k( size_min );
        size_max = __kmp_round4k( size_max );
    #endif // KMP_OS_DARWIN
    if ( value ) {
        if ( is_specified != NULL ) {
            * is_specified = 1;
        }; // if
        __kmp_str_to_size( value, out, factor, & msg );
        if ( msg == NULL ) {
            if ( * out > size_max ) {
                * out = size_max;
                msg = KMP_I18N_STR( ValueTooLarge );
            } else if ( * out < size_min ) {
                * out = size_min;
                msg = KMP_I18N_STR( ValueTooSmall );
            } else {
                #if KMP_OS_DARWIN
                    size_t round4k = __kmp_round4k( * out );
                    if ( * out != round4k ) {
                        * out = round4k;
                        msg = KMP_I18N_STR( NotMultiple4K );
                    }; // if
                #endif
            }; // if
        } else {
            // If integer overflow occurred, * out == KMP_SIZE_T_MAX. Cut it to size_max silently.
            if ( * out < size_min ) {
                * out = size_max;
            }
            else if ( * out >  size_max ) {
                * out = size_max;
            }; // if
        }; // if
        if ( msg != NULL ) {
            // Message is not empty. Print warning.
            kmp_str_buf_t buf;
            __kmp_str_buf_init( & buf );
            __kmp_str_buf_print_size( & buf, * out );
            KMP_WARNING( ParseSizeIntWarn, name, value, msg );
            KMP_INFORM( Using_str_Value, name, buf.str );
            __kmp_str_buf_free( & buf );
        }; // if
    }; // if
} // __kmp_stg_parse_size

static void
__kmp_stg_parse_str(
    char const *      name,
    char const *      value,
    char const * *    out
) {
    KMP_INTERNAL_FREE( (void *) * out );
    * out = __kmp_str_format( "%s", value );
} // __kmp_stg_parse_str


static void
__kmp_stg_parse_int(
    char const * name,   // I: Name of environment variable (used in warning messages).
    char const * value,  // I: Value of environment variable to parse.
    int          min,    // I: Miminal allowed value.
    int          max,    // I: Maximum allowed value.
    int *        out     // O: Output (parsed) value.
) {
    char const * msg  = NULL;
    kmp_uint64   uint = * out;
    __kmp_str_to_uint( value, & uint, & msg );
    if ( msg == NULL ) {
        if ( uint < (unsigned int)min ) {
            msg = KMP_I18N_STR( ValueTooSmall );
            uint = min;
        } else if ( uint > (unsigned int)max ) {
            msg = KMP_I18N_STR( ValueTooLarge );
            uint = max;
        }; // if
    } else {
        // If overflow occurred msg contains error message and uint is very big. Cut tmp it
        // to INT_MAX.
        if ( uint < (unsigned int)min ) {
            uint = min;
        }
        else if ( uint > (unsigned int)max ) {
            uint = max;
        }; // if
    }; // if
    if ( msg != NULL ) {
        // Message is not empty. Print warning.
        kmp_str_buf_t buf;
        KMP_WARNING( ParseSizeIntWarn, name, value, msg );
        __kmp_str_buf_init( & buf );
        __kmp_str_buf_print( &buf, "%" KMP_UINT64_SPEC "", uint );
        KMP_INFORM( Using_uint64_Value, name, buf.str );
        __kmp_str_buf_free( &buf );
    }; // if
    * out = uint;
} // __kmp_stg_parse_int


static void
__kmp_stg_parse_file(
    char const * name,
    char const * value,
    char *       suffix,
    char * *     out
) {
    char buffer[256];
    char *t;
    int hasSuffix;
    KMP_INTERNAL_FREE( (void *) * out );
    t = (char *) strrchr(value, '.');
    hasSuffix = t && __kmp_str_eqf( t, suffix );
    t = __kmp_str_format( "%s%s", value, hasSuffix ? "" : suffix );
    __kmp_expand_file_name( buffer, sizeof(buffer), t);
    KMP_INTERNAL_FREE(t);
    * out = __kmp_str_format( "%s", buffer );
} // __kmp_stg_parse_file

#ifdef KMP_DEBUG
static char * par_range_to_print = NULL;

static void
__kmp_stg_parse_par_range(
    char const * name,
    char const * value,
    int *        out_range,
    char *       out_routine,
    char *       out_file,
    int *        out_lb,
    int *        out_ub
) {
    size_t len = strlen( value + 1 );
    par_range_to_print = (char *) KMP_INTERNAL_MALLOC( len +1 );
    strncpy( par_range_to_print, value, len + 1);
    __kmp_par_range = +1;
    __kmp_par_range_lb = 0;
    __kmp_par_range_ub = INT_MAX;
    for (;;) {
        unsigned int len;
        if (( value == NULL ) || ( *value == '\0' )) {
            break;
        }
        if ( ! __kmp_strcasecmp_with_sentinel( "routine", value, '=' )) {
            value = strchr( value, '=' ) + 1;
            len = __kmp_readstr_with_sentinel( out_routine,
              value, KMP_PAR_RANGE_ROUTINE_LEN - 1, ',' );
            if ( len == 0 ) {
                goto par_range_error;
            }
            value = strchr( value, ',' );
            if ( value != NULL ) {
                value++;
            }
            continue;
        }
        if ( ! __kmp_strcasecmp_with_sentinel( "filename", value, '=' )) {
            value = strchr( value, '=' ) + 1;
            len = __kmp_readstr_with_sentinel( out_file,
              value, KMP_PAR_RANGE_FILENAME_LEN - 1, ',' );
            if ( len == 0) {
                goto par_range_error;
            }
            value = strchr( value, ',' );
            if ( value != NULL ) {
                value++;
            }
            continue;
        }
        if (( ! __kmp_strcasecmp_with_sentinel( "range", value, '=' ))
          || ( ! __kmp_strcasecmp_with_sentinel( "incl_range", value, '=' ))) {
            value = strchr( value, '=' ) + 1;
            if ( sscanf( value, "%d:%d", out_lb, out_ub ) != 2 ) {
                goto par_range_error;
            }
            *out_range = +1;
            value = strchr( value, ',' );
            if ( value != NULL ) {
                value++;
            }
            continue;
        }
        if ( ! __kmp_strcasecmp_with_sentinel( "excl_range", value, '=' )) {
            value = strchr( value, '=' ) + 1;
            if ( sscanf( value, "%d:%d", out_lb, out_ub) != 2 ) {
                goto par_range_error;
            }
            *out_range = -1;
            value = strchr( value, ',' );
            if ( value != NULL ) {
                value++;
            }
            continue;
        }
        par_range_error:
        KMP_WARNING( ParRangeSyntax, name );
        __kmp_par_range = 0;
        break;
    }
} // __kmp_stg_parse_par_range
#endif

int
__kmp_initial_threads_capacity( int req_nproc )
{
    int nth = 32;

    /* MIN( MAX( 32, 4 * $OMP_NUM_THREADS, 4 * omp_get_num_procs() ), __kmp_max_nth) */
    if (nth < (4 * req_nproc))
        nth = (4 * req_nproc);
    if (nth < (4 * __kmp_xproc))
        nth = (4 * __kmp_xproc);

    if (nth > __kmp_max_nth)
        nth = __kmp_max_nth;

    return nth;
}


int
__kmp_default_tp_capacity( int req_nproc, int max_nth, int all_threads_specified) {
    int nth = 128;

    if(all_threads_specified)
        return max_nth;
    /* MIN( MAX (128, 4 * $OMP_NUM_THREADS, 4 * omp_get_num_procs() ), __kmp_max_nth ) */
    if (nth < (4 * req_nproc))
        nth = (4 * req_nproc);
    if (nth < (4 * __kmp_xproc))
        nth = (4 * __kmp_xproc);

    if (nth > __kmp_max_nth)
        nth = __kmp_max_nth;

    return nth;
}


// -------------------------------------------------------------------------------------------------
// Helper print functions.
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_print_bool( kmp_str_buf_t * buffer, char const * name, int value ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_BOOL;
    } else {
        __kmp_str_buf_print( buffer, "   %s=%s\n", name, value ? "true" : "false" );
    }
} // __kmp_stg_print_bool

static void
__kmp_stg_print_int( kmp_str_buf_t * buffer, char const * name, int value ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_INT;
    } else {
        __kmp_str_buf_print( buffer, "   %s=%d\n", name, value );
    }
} // __kmp_stg_print_int

static void
__kmp_stg_print_uint64( kmp_str_buf_t * buffer, char const * name, kmp_uint64 value ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_UINT64;
    } else {
        __kmp_str_buf_print( buffer, "   %s=%" KMP_UINT64_SPEC "\n", name, value );
    }
} // __kmp_stg_print_uint64

static void
__kmp_stg_print_str( kmp_str_buf_t * buffer, char const * name, char const * value ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_STR;
    } else {
        __kmp_str_buf_print( buffer, "   %s=%s\n", name, value );
    }
} // __kmp_stg_print_str

static void
__kmp_stg_print_size( kmp_str_buf_t * buffer, char const * name, size_t value ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
        __kmp_str_buf_print_size( buffer, value );
        __kmp_str_buf_print( buffer, "'\n" );
    } else {
        __kmp_str_buf_print( buffer, "   %s=", name );
        __kmp_str_buf_print_size( buffer, value );
        __kmp_str_buf_print( buffer, "\n" );
        return;
    }
} // __kmp_stg_print_size


// =================================================================================================
// Parse and print functions.
// =================================================================================================

// -------------------------------------------------------------------------------------------------
// KMP_ALL_THREADS, KMP_MAX_THREADS, OMP_THREAD_LIMIT
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_all_threads( char const * name, char const * value, void * data ) {

    kmp_setting_t * * rivals = (kmp_setting_t * *) data;
    int               rc;
    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }; // if
    if ( ! __kmp_strcasecmp_with_sentinel( "all", value, 0 ) ) {
        __kmp_max_nth = __kmp_xproc;
        __kmp_allThreadsSpecified = 1;
    } else {
        __kmp_stg_parse_int( name, value, 1, __kmp_sys_max_nth, & __kmp_max_nth );
        __kmp_allThreadsSpecified = 0;
    }
    K_DIAG( 1, ( "__kmp_max_nth == %d\n", __kmp_max_nth ) );

} // __kmp_stg_parse_all_threads

static void
__kmp_stg_print_all_threads( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_max_nth );
} // __kmp_stg_print_all_threads

// -------------------------------------------------------------------------------------------------
// KMP_BLOCKTIME
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_blocktime( char const * name, char const * value, void * data ) {
    __kmp_dflt_blocktime = __kmp_convert_to_milliseconds( value );
    if ( __kmp_dflt_blocktime < 0 ) {
        __kmp_dflt_blocktime = KMP_DEFAULT_BLOCKTIME;
        __kmp_msg( kmp_ms_warning, KMP_MSG( InvalidValue, name, value ), __kmp_msg_null );
        KMP_INFORM( Using_int_Value, name, __kmp_dflt_blocktime );
        __kmp_env_blocktime = FALSE;  // Revert to default as if var not set.
    } else {
        if ( __kmp_dflt_blocktime < KMP_MIN_BLOCKTIME ) {
            __kmp_dflt_blocktime = KMP_MIN_BLOCKTIME;
            __kmp_msg( kmp_ms_warning, KMP_MSG( SmallValue, name, value ), __kmp_msg_null );
            KMP_INFORM( MinValueUsing, name, __kmp_dflt_blocktime );
        } else if ( __kmp_dflt_blocktime > KMP_MAX_BLOCKTIME ) {
            __kmp_dflt_blocktime = KMP_MAX_BLOCKTIME;
            __kmp_msg( kmp_ms_warning, KMP_MSG( LargeValue, name, value ), __kmp_msg_null );
            KMP_INFORM( MaxValueUsing, name, __kmp_dflt_blocktime );
        }; // if
        __kmp_env_blocktime = TRUE;    // KMP_BLOCKTIME was specified.
    }; // if
    // calculate number of monitor thread wakeup intervals corresonding to blocktime.
    __kmp_monitor_wakeups = KMP_WAKEUPS_FROM_BLOCKTIME( __kmp_dflt_blocktime, __kmp_monitor_wakeups );
    __kmp_bt_intervals = KMP_INTERVALS_FROM_BLOCKTIME( __kmp_dflt_blocktime, __kmp_monitor_wakeups );
    K_DIAG( 1, ( "__kmp_env_blocktime == %d\n", __kmp_env_blocktime ) );
    if ( __kmp_env_blocktime ) {
        K_DIAG( 1, ( "__kmp_dflt_blocktime == %d\n", __kmp_dflt_blocktime ) );
    }
} // __kmp_stg_parse_blocktime

static void
__kmp_stg_print_blocktime( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_dflt_blocktime );
} // __kmp_stg_print_blocktime

// -------------------------------------------------------------------------------------------------
// KMP_DUPLICATE_LIB_OK
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_duplicate_lib_ok( char const * name, char const * value, void * data ) {
    /* actually this variable is not supported,
       put here for compatibility with earlier builds and for static/dynamic combination */
    __kmp_stg_parse_bool( name, value, & __kmp_duplicate_library_ok );
} // __kmp_stg_parse_duplicate_lib_ok

static void
__kmp_stg_print_duplicate_lib_ok( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_duplicate_library_ok );
} // __kmp_stg_print_duplicate_lib_ok

// -------------------------------------------------------------------------------------------------
// KMP_INHERIT_FP_CONTROL
// -------------------------------------------------------------------------------------------------

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

static void
__kmp_stg_parse_inherit_fp_control( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_inherit_fp_control );
} // __kmp_stg_parse_inherit_fp_control

static void
__kmp_stg_print_inherit_fp_control( kmp_str_buf_t * buffer, char const * name, void * data ) {
#if KMP_DEBUG
    __kmp_stg_print_bool( buffer, name, __kmp_inherit_fp_control );
#endif /* KMP_DEBUG */
} // __kmp_stg_print_inherit_fp_control

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

// -------------------------------------------------------------------------------------------------
// KMP_LIBRARY, OMP_WAIT_POLICY
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_wait_policy( char const * name, char const * value, void * data ) {

    kmp_stg_wp_data_t * wait = (kmp_stg_wp_data_t *) data;
    int                 rc;

    rc = __kmp_stg_check_rivals( name, value, wait->rivals );
    if ( rc ) {
        return;
    }; // if

    if ( wait->omp ) {
        if ( __kmp_str_match( "ACTIVE", 1, value ) ) {
           __kmp_library = library_turnaround;
        } else if ( __kmp_str_match( "PASSIVE", 1, value ) ) {
           __kmp_library = library_throughput;
        } else {
            KMP_WARNING( StgInvalidValue, name, value );
        }; // if
    } else {
        if ( __kmp_str_match( "serial", 1, value ) ) {             /* S */
           __kmp_library = library_serial;
        } else if ( __kmp_str_match( "throughput", 2, value ) ) {  /* TH */
           __kmp_library = library_throughput;
        } else if ( __kmp_str_match( "turnaround", 2, value ) ) {  /* TU */
           __kmp_library = library_turnaround;
        } else if ( __kmp_str_match( "dedicated", 1, value ) ) {   /* D */
           __kmp_library = library_turnaround;
        } else if ( __kmp_str_match( "multiuser", 1, value ) ) {   /* M */
           __kmp_library = library_throughput;
        } else {
            KMP_WARNING( StgInvalidValue, name, value );
        }; // if
    }; // if
    __kmp_aux_set_library( __kmp_library );

} // __kmp_stg_parse_wait_policy

static void
__kmp_stg_print_wait_policy( kmp_str_buf_t * buffer, char const * name, void * data ) {

    kmp_stg_wp_data_t * wait = (kmp_stg_wp_data_t *) data;
    char const *        value = NULL;

    if ( wait->omp ) {
        switch ( __kmp_library ) {
            case library_turnaround : {
                value = "ACTIVE";
            } break;
            case library_throughput : {
                value = "PASSIVE";
            } break;
        }; // switch
    } else {
        switch ( __kmp_library ) {
            case library_serial : {
                value = "serial";
            } break;
            case library_turnaround : {
                value = "turnaround";
            } break;
            case library_throughput : {
                value = "throughput";
            } break;
        }; // switch
    }; // if
    if ( value != NULL ) {
        __kmp_stg_print_str( buffer, name, value );
    }; // if

} // __kmp_stg_print_wait_policy

// -------------------------------------------------------------------------------------------------
// KMP_MONITOR_STACKSIZE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_monitor_stacksize( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_size(
        name,
        value,
        __kmp_sys_min_stksize,
        KMP_MAX_STKSIZE,
        NULL,
        & __kmp_monitor_stksize,
        1
    );
} // __kmp_stg_parse_monitor_stacksize

static void
__kmp_stg_print_monitor_stacksize( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if( __kmp_env_format ) {
        if (  __kmp_monitor_stksize > 0 )
            KMP_STR_BUF_PRINT_NAME_EX(name);
        else
            KMP_STR_BUF_PRINT_NAME;
    } else {
        __kmp_str_buf_print( buffer, "   %s", name );
    }
    if (  __kmp_monitor_stksize > 0 ) {
        __kmp_str_buf_print_size( buffer, __kmp_monitor_stksize );
    } else {
        __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
    }
    if( __kmp_env_format && __kmp_monitor_stksize ) {
            __kmp_str_buf_print( buffer, "'\n");
    }

} // __kmp_stg_print_monitor_stacksize

// -------------------------------------------------------------------------------------------------
// KMP_SETTINGS
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_settings( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_settings );
} // __kmp_stg_parse_settings

static void
__kmp_stg_print_settings( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_settings );
} // __kmp_stg_print_settings

// -------------------------------------------------------------------------------------------------
// KMP_STACKPAD
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_stackpad( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int(
        name,                             // Env var name
        value,                            // Env var value
        KMP_MIN_STKPADDING,               // Min value
        KMP_MAX_STKPADDING,               // Max value
        & __kmp_stkpadding                // Var to initialize
    );
} // __kmp_stg_parse_stackpad

static void
__kmp_stg_print_stackpad( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_stkpadding );
} // __kmp_stg_print_stackpad

// -------------------------------------------------------------------------------------------------
// KMP_STACKOFFSET
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_stackoffset( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_size(
        name,                             // Env var name
        value,                            // Env var value
        KMP_MIN_STKOFFSET,                // Min value
        KMP_MAX_STKOFFSET,                // Max value
        NULL,                             //
        & __kmp_stkoffset,                // Var to initialize
        1
    );
} // __kmp_stg_parse_stackoffset

static void
__kmp_stg_print_stackoffset( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_size( buffer, name, __kmp_stkoffset );
} // __kmp_stg_print_stackoffset

// -------------------------------------------------------------------------------------------------
// KMP_STACKSIZE, OMP_STACKSIZE, GOMP_STACKSIZE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_stacksize( char const * name, char const * value, void * data ) {

    kmp_stg_ss_data_t *  stacksize = (kmp_stg_ss_data_t *) data;
    int                  rc;

    rc = __kmp_stg_check_rivals( name, value, stacksize->rivals );
    if ( rc ) {
        return;
    }; // if
    __kmp_stg_parse_size(
        name,                     // Env var name
        value,                    // Env var value
        __kmp_sys_min_stksize,    // Min value
        KMP_MAX_STKSIZE,          // Max value
        & __kmp_env_stksize,      //
        & __kmp_stksize,          // Var to initialize
        stacksize->factor
    );

} // __kmp_stg_parse_stacksize

// This function is called for printing both KMP_STACKSIZE (factor is 1) and OMP_STACKSIZE (factor is 1024).
// Currently it is not possible to print OMP_STACKSIZE value in bytes. We can consider adding this
// possibility by a customer request in future.
static void
__kmp_stg_print_stacksize( kmp_str_buf_t * buffer, char const * name, void * data ) {
    kmp_stg_ss_data_t *  stacksize = (kmp_stg_ss_data_t *) data;
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
        __kmp_str_buf_print_size( buffer, (__kmp_stksize % 1024) ? __kmp_stksize / stacksize->factor : __kmp_stksize );
        __kmp_str_buf_print( buffer, "'\n" );
    } else {
        __kmp_str_buf_print( buffer, "   %s=", name );
        __kmp_str_buf_print_size( buffer, (__kmp_stksize % 1024) ? __kmp_stksize / stacksize->factor : __kmp_stksize );
        __kmp_str_buf_print( buffer, "\n" );
    }
} // __kmp_stg_print_stacksize

// -------------------------------------------------------------------------------------------------
// KMP_VERSION
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_version( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_version );
} // __kmp_stg_parse_version

static void
__kmp_stg_print_version( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_version );
} // __kmp_stg_print_version

// -------------------------------------------------------------------------------------------------
// KMP_WARNINGS
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_warnings( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_generate_warnings );
    if (__kmp_generate_warnings != kmp_warnings_off) {   // AC: we have only 0/1 values documented,
        __kmp_generate_warnings = kmp_warnings_explicit; //     so reset it to explicit in order to
    }                                                    //     distinguish from default setting
} // __kmp_env_parse_warnings

static void
__kmp_stg_print_warnings( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_generate_warnings ); // AC: TODO: change to print_int?
} // __kmp_env_print_warnings                                      //     (needs documentation change)...

// -------------------------------------------------------------------------------------------------
// OMP_NESTED, OMP_NUM_THREADS
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_nested( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_dflt_nested );
} // __kmp_stg_parse_nested

static void
__kmp_stg_print_nested( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_dflt_nested );
} // __kmp_stg_print_nested

static void
__kmp_parse_nested_num_threads( const char *var, const char *env, kmp_nested_nthreads_t *nth_array )
{
    const char *next = env;
    const char *scan = next;

    int total = 0;          // Count elements that were set. It'll be used as an array size
    int prev_comma = FALSE; // For correct processing sequential commas

    // Count the number of values in the env. var string
    for ( ; ; ) {
        SKIP_WS( next );

        if ( *next == '\0' ) {
            break;
        }
        // Next character is not an integer or not a comma => end of list
        if ( ( ( *next < '0' ) || ( *next > '9' ) ) && ( *next !=',') ) {
            KMP_WARNING( NthSyntaxError, var, env );
            return;
        }
        // The next character is ','
        if ( *next == ',' ) {
            // ',' is the fisrt character
            if ( total == 0 || prev_comma ) {
                total++;
            }
            prev_comma = TRUE;
            next++; //skip ','
            SKIP_WS( next );
        }
        // Next character is a digit
        if ( *next >= '0' && *next <= '9' ) {
            prev_comma = FALSE;
            SKIP_DIGITS( next );
            total++;
            const char *tmp = next;
            SKIP_WS( tmp );
            if ( ( *next == ' ' || *next == '\t' ) && ( *tmp >= '0' && *tmp <= '9' ) ) {
                KMP_WARNING( NthSpacesNotAllowed, var, env );
                return;
            }
        }
    }
    KMP_DEBUG_ASSERT( total > 0 );
    if( total <= 0 ) {
        KMP_WARNING( NthSyntaxError, var, env );
        return;
    }

    // Check if the nested nthreads array exists
    if ( ! nth_array->nth ) {
        // Allocate an array of double size
        nth_array->nth = ( int * )KMP_INTERNAL_MALLOC( sizeof( int ) * total * 2 );
        if ( nth_array->nth == NULL ) {
            KMP_FATAL( MemoryAllocFailed );
        }
        nth_array->size = total * 2;
    } else {
        if ( nth_array->size < total ) {
            // Increase the array size
            do {
                nth_array->size *= 2;
            } while ( nth_array->size < total );

            nth_array->nth = (int *) KMP_INTERNAL_REALLOC(
                nth_array->nth, sizeof( int ) * nth_array->size );
            if ( nth_array->nth == NULL ) {
		KMP_FATAL( MemoryAllocFailed );
            }
        }
    }
    nth_array->used = total;
    int i = 0;

    prev_comma = FALSE;
    total = 0;
    // Save values in the array
    for ( ; ; ) {
        SKIP_WS( scan );
        if ( *scan == '\0' ) {
            break;
        }
        // The next character is ','
        if ( *scan == ',' ) {
            // ',' in the beginning of the list
            if ( total == 0 ) {
                // The value is supposed to be equal to __kmp_avail_proc but it is unknown at the moment.
                // So let's put a placeholder (#threads = 0) to correct it later.
                nth_array->nth[i++] = 0;
                total++;
            }else if ( prev_comma ) {
                // Num threads is inherited from the previous level
                nth_array->nth[i] = nth_array->nth[i - 1];
                i++;
                total++;
            }
            prev_comma = TRUE;
            scan++; //skip ','
            SKIP_WS( scan );
        }
        // Next character is a digit
        if ( *scan >= '0' && *scan <= '9' ) {
            int num;
            const char *buf = scan;
            char const * msg  = NULL;
            prev_comma = FALSE;
            SKIP_DIGITS( scan );
            total++;

            num = __kmp_str_to_int( buf, *scan );
            if ( num < KMP_MIN_NTH ) {
                msg = KMP_I18N_STR( ValueTooSmall );
                num = KMP_MIN_NTH;
            } else if ( num > __kmp_sys_max_nth ) {
                msg = KMP_I18N_STR( ValueTooLarge );
                num = __kmp_sys_max_nth;
            }
            if ( msg != NULL ) {
                // Message is not empty. Print warning.
                KMP_WARNING( ParseSizeIntWarn, var, env, msg );
                KMP_INFORM( Using_int_Value, var, num );
            }
            nth_array->nth[i++] = num;
        }
    }
}

static void
__kmp_stg_parse_num_threads( char const * name, char const * value, void * data ) {
    // TODO: Remove this option. OMP_NUM_THREADS is a list of positive integers!
    if ( ! __kmp_strcasecmp_with_sentinel( "all", value, 0 ) ) {
        // The array of 1 element
        __kmp_nested_nth.nth = ( int* )KMP_INTERNAL_MALLOC( sizeof( int ) );
        __kmp_nested_nth.size = __kmp_nested_nth.used = 1;
        __kmp_nested_nth.nth[0] = __kmp_dflt_team_nth = __kmp_dflt_team_nth_ub = __kmp_xproc;
    } else {
        __kmp_parse_nested_num_threads( name, value, & __kmp_nested_nth );
        if ( __kmp_nested_nth.nth ) {
            __kmp_dflt_team_nth = __kmp_nested_nth.nth[0];
            if ( __kmp_dflt_team_nth_ub < __kmp_dflt_team_nth ) {
                __kmp_dflt_team_nth_ub = __kmp_dflt_team_nth;
            }
        }
    }; // if
    K_DIAG( 1, ( "__kmp_dflt_team_nth == %d\n", __kmp_dflt_team_nth ) );
} // __kmp_stg_parse_num_threads

static void
__kmp_stg_print_num_threads( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME;
    } else {
        __kmp_str_buf_print( buffer, "   %s", name );
    }
    if ( __kmp_nested_nth.used ) {
        kmp_str_buf_t buf;
        __kmp_str_buf_init( &buf );
        for ( int i = 0; i < __kmp_nested_nth.used; i++) {
            __kmp_str_buf_print( &buf, "%d", __kmp_nested_nth.nth[i] );
            if ( i < __kmp_nested_nth.used - 1 ) {
                __kmp_str_buf_print( &buf, "," );
            }
        }
        __kmp_str_buf_print( buffer, "='%s'\n", buf.str );
        __kmp_str_buf_free(&buf);
    } else {
        __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
    }
} // __kmp_stg_print_num_threads

// -------------------------------------------------------------------------------------------------
// OpenMP 3.0: KMP_TASKING, OMP_MAX_ACTIVE_LEVELS,
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_tasking( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, 0, (int)tskm_max, (int *)&__kmp_tasking_mode );
} // __kmp_stg_parse_tasking

static void
__kmp_stg_print_tasking( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_tasking_mode );
} // __kmp_stg_print_tasking

static void
__kmp_stg_parse_task_stealing( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, 0, 1, (int *)&__kmp_task_stealing_constraint );
} // __kmp_stg_parse_task_stealing

static void
__kmp_stg_print_task_stealing( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_task_stealing_constraint );
} // __kmp_stg_print_task_stealing

static void
__kmp_stg_parse_max_active_levels( char const * name, char const * value, void * data ) {
	 __kmp_stg_parse_int( name, value, 0, KMP_MAX_ACTIVE_LEVELS_LIMIT, & __kmp_dflt_max_active_levels );
} // __kmp_stg_parse_max_active_levels

static void
__kmp_stg_print_max_active_levels( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_dflt_max_active_levels );
} // __kmp_stg_print_max_active_levels

#if KMP_NESTED_HOT_TEAMS
// -------------------------------------------------------------------------------------------------
// KMP_HOT_TEAMS_MAX_LEVEL, KMP_HOT_TEAMS_MODE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_hot_teams_level( char const * name, char const * value, void * data ) {
    if ( TCR_4(__kmp_init_parallel) ) {
        KMP_WARNING( EnvParallelWarn, name );
        return;
    }   // read value before first parallel only
    __kmp_stg_parse_int( name, value, 0, KMP_MAX_ACTIVE_LEVELS_LIMIT, & __kmp_hot_teams_max_level );
} // __kmp_stg_parse_hot_teams_level

static void
__kmp_stg_print_hot_teams_level( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_hot_teams_max_level );
} // __kmp_stg_print_hot_teams_level

static void
__kmp_stg_parse_hot_teams_mode( char const * name, char const * value, void * data ) {
    if ( TCR_4(__kmp_init_parallel) ) {
        KMP_WARNING( EnvParallelWarn, name );
        return;
    }   // read value before first parallel only
    __kmp_stg_parse_int( name, value, 0, KMP_MAX_ACTIVE_LEVELS_LIMIT, & __kmp_hot_teams_mode );
} // __kmp_stg_parse_hot_teams_mode

static void
__kmp_stg_print_hot_teams_mode( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_hot_teams_mode );
} // __kmp_stg_print_hot_teams_mode

#endif // KMP_NESTED_HOT_TEAMS

// -------------------------------------------------------------------------------------------------
// KMP_HANDLE_SIGNALS
// -------------------------------------------------------------------------------------------------

#if KMP_HANDLE_SIGNALS

static void
__kmp_stg_parse_handle_signals( char const * name, char const * value, void * data ) {
	__kmp_stg_parse_bool( name, value, & __kmp_handle_signals );
} // __kmp_stg_parse_handle_signals

static void
__kmp_stg_print_handle_signals( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_handle_signals );
} // __kmp_stg_print_handle_signals

#endif // KMP_HANDLE_SIGNALS

// -------------------------------------------------------------------------------------------------
// KMP_X_DEBUG, KMP_DEBUG, KMP_DEBUG_BUF_*, KMP_DIAG
// -------------------------------------------------------------------------------------------------

#ifdef KMP_DEBUG

#define KMP_STG_X_DEBUG( x )                                                                            \
    static void __kmp_stg_parse_##x##_debug( char const * name, char const * value, void * data ) {     \
	__kmp_stg_parse_int( name, value, 0, INT_MAX, & kmp_##x##_debug );                              \
    } /* __kmp_stg_parse_x_debug */                                                                     \
    static void __kmp_stg_print_##x##_debug( kmp_str_buf_t * buffer, char const * name, void * data ) { \
	__kmp_stg_print_int( buffer, name, kmp_##x##_debug );                                           \
    } /* __kmp_stg_print_x_debug */

KMP_STG_X_DEBUG( a )
KMP_STG_X_DEBUG( b )
KMP_STG_X_DEBUG( c )
KMP_STG_X_DEBUG( d )
KMP_STG_X_DEBUG( e )
KMP_STG_X_DEBUG( f )

#undef KMP_STG_X_DEBUG

static void
__kmp_stg_parse_debug( char const * name, char const * value, void * data ) {
    int debug = 0;
    __kmp_stg_parse_int( name, value, 0, INT_MAX, & debug );
    if ( kmp_a_debug < debug ) {
	kmp_a_debug = debug;
    }; // if
    if ( kmp_b_debug < debug ) {
	kmp_b_debug = debug;
    }; // if
    if ( kmp_c_debug < debug ) {
	kmp_c_debug = debug;
    }; // if
    if ( kmp_d_debug < debug ) {
	kmp_d_debug = debug;
    }; // if
    if ( kmp_e_debug < debug ) {
	kmp_e_debug = debug;
    }; // if
    if ( kmp_f_debug < debug ) {
	kmp_f_debug = debug;
    }; // if
} // __kmp_stg_parse_debug

static void
__kmp_stg_parse_debug_buf( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_debug_buf );
    // !!! TODO: Move buffer initialization of of this file! It may works incorrectly if
    // KMP_DEBUG_BUF is parsed before KMP_DEBUG_BUF_LINES or KMP_DEBUG_BUF_CHARS.
    if ( __kmp_debug_buf ) {
	int i;
	int elements = __kmp_debug_buf_lines * __kmp_debug_buf_chars;

	/* allocate and initialize all entries in debug buffer to empty */
	__kmp_debug_buffer = (char *) __kmp_page_allocate( elements * sizeof( char ) );
	for ( i = 0; i < elements; i += __kmp_debug_buf_chars )
	   __kmp_debug_buffer[i] = '\0';

	__kmp_debug_count = 0;
    }
    K_DIAG( 1, ( "__kmp_debug_buf = %d\n", __kmp_debug_buf ) );
} // __kmp_stg_parse_debug_buf

static void
__kmp_stg_print_debug_buf( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_debug_buf );
} // __kmp_stg_print_debug_buf

static void
__kmp_stg_parse_debug_buf_atomic( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_debug_buf_atomic );
} // __kmp_stg_parse_debug_buf_atomic

static void
__kmp_stg_print_debug_buf_atomic( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_debug_buf_atomic );
} // __kmp_stg_print_debug_buf_atomic

static void
__kmp_stg_parse_debug_buf_chars( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int(
	name,
	value,
	KMP_DEBUG_BUF_CHARS_MIN,
	INT_MAX,
	& __kmp_debug_buf_chars
    );
} // __kmp_stg_debug_parse_buf_chars

static void
__kmp_stg_print_debug_buf_chars( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_debug_buf_chars );
} // __kmp_stg_print_debug_buf_chars

static void
__kmp_stg_parse_debug_buf_lines( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int(
	name,
	value,
	KMP_DEBUG_BUF_LINES_MIN,
	INT_MAX,
	& __kmp_debug_buf_lines
    );
} // __kmp_stg_parse_debug_buf_lines

static void
__kmp_stg_print_debug_buf_lines( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_debug_buf_lines );
} // __kmp_stg_print_debug_buf_lines

static void
__kmp_stg_parse_diag( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, 0, INT_MAX, & kmp_diag );
} // __kmp_stg_parse_diag

static void
__kmp_stg_print_diag( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, kmp_diag );
} // __kmp_stg_print_diag

#endif // KMP_DEBUG

// -------------------------------------------------------------------------------------------------
// KMP_ALIGN_ALLOC
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_align_alloc( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_size(
        name,
        value,
        CACHE_LINE,
        INT_MAX,
        NULL,
        & __kmp_align_alloc,
        1
    );
} // __kmp_stg_parse_align_alloc

static void
__kmp_stg_print_align_alloc( kmp_str_buf_t * buffer, char const * name, void * data ) {
        __kmp_stg_print_size( buffer, name, __kmp_align_alloc );
} // __kmp_stg_print_align_alloc

// -------------------------------------------------------------------------------------------------
// KMP_PLAIN_BARRIER, KMP_FORKJOIN_BARRIER, KMP_REDUCTION_BARRIER
// -------------------------------------------------------------------------------------------------

// TODO: Remove __kmp_barrier_branch_bit_env_name varibale, remove loops from parse and print
//       functions, pass required info through data argument.

static void
__kmp_stg_parse_barrier_branch_bit( char const * name, char const * value, void * data ) {
    const char *var;

    /* ---------- Barrier branch bit control ------------ */
    for ( int i=bs_plain_barrier; i<bs_last_barrier; i++ ) {
        var = __kmp_barrier_branch_bit_env_name[ i ];
        if ( ( strcmp( var, name) == 0 ) && ( value != 0 ) ) {
            char *comma;

            comma = (char *) strchr( value, ',' );
            __kmp_barrier_gather_branch_bits[ i ] = ( kmp_uint32 ) __kmp_str_to_int( value, ',' );
            /* is there a specified release parameter? */
            if ( comma == NULL ) {
                __kmp_barrier_release_branch_bits[ i ] = __kmp_barrier_release_bb_dflt;
            } else {
                __kmp_barrier_release_branch_bits[ i ] = (kmp_uint32) __kmp_str_to_int( comma + 1, 0 );

                if ( __kmp_barrier_release_branch_bits[ i ] > KMP_MAX_BRANCH_BITS ) {
                    __kmp_msg( kmp_ms_warning, KMP_MSG( BarrReleaseValueInvalid, name, comma + 1 ), __kmp_msg_null );
                    __kmp_barrier_release_branch_bits[ i ] = __kmp_barrier_release_bb_dflt;
                }
            }
            if ( __kmp_barrier_gather_branch_bits[ i ] > KMP_MAX_BRANCH_BITS ) {
                    KMP_WARNING( BarrGatherValueInvalid, name, value );
                    KMP_INFORM( Using_uint_Value, name, __kmp_barrier_gather_bb_dflt );
                __kmp_barrier_gather_branch_bits[ i ] =  __kmp_barrier_gather_bb_dflt;
            }
        }
        K_DIAG(1, ("%s == %d,%d\n", __kmp_barrier_branch_bit_env_name[ i ], \
                   __kmp_barrier_gather_branch_bits [ i ], \
                   __kmp_barrier_release_branch_bits [ i ]))
    }
} // __kmp_stg_parse_barrier_branch_bit

static void
__kmp_stg_print_barrier_branch_bit( kmp_str_buf_t * buffer, char const * name, void * data ) {
    const char *var;
    for ( int i=bs_plain_barrier; i<bs_last_barrier; i++ ) {
        var = __kmp_barrier_branch_bit_env_name[ i ];
        if ( strcmp( var, name) == 0  ) {
            if( __kmp_env_format ) {
                KMP_STR_BUF_PRINT_NAME_EX(__kmp_barrier_branch_bit_env_name[ i ]);
            } else {
                __kmp_str_buf_print( buffer, "   %s='", __kmp_barrier_branch_bit_env_name[ i ] );
            }
            __kmp_str_buf_print( buffer, "%d,%d'\n", __kmp_barrier_gather_branch_bits [ i ], __kmp_barrier_release_branch_bits [ i ]);
        }
    }
} // __kmp_stg_print_barrier_branch_bit


// -------------------------------------------------------------------------------------------------
// KMP_PLAIN_BARRIER_PATTERN, KMP_FORKJOIN_BARRIER_PATTERN, KMP_REDUCTION_BARRIER_PATTERN
// -------------------------------------------------------------------------------------------------

// TODO: Remove __kmp_barrier_pattern_name variable, remove loops from parse and print functions,
//       pass required data to functions through data argument.

static void
__kmp_stg_parse_barrier_pattern( char const * name, char const * value, void * data ) {
    const char *var;
    /* ---------- Barrier method control ------------ */

    for ( int i=bs_plain_barrier; i<bs_last_barrier; i++ ) {
        var = __kmp_barrier_pattern_env_name[ i ];

        if ( ( strcmp ( var, name ) == 0 ) && ( value != 0 ) ) {
            int j;
            char *comma = (char *) strchr( value, ',' );

            /* handle first parameter: gather pattern */
            for ( j = bp_linear_bar; j<bp_last_bar; j++ ) {
                if (__kmp_match_with_sentinel( __kmp_barrier_pattern_name[j], value, 1, ',' )) {
                   __kmp_barrier_gather_pattern[ i ] = (kmp_bar_pat_e) j;
                   break;
                }
            }
            if ( j == bp_last_bar ) {
                KMP_WARNING( BarrGatherValueInvalid, name, value );
                KMP_INFORM( Using_str_Value, name, __kmp_barrier_pattern_name[ bp_linear_bar ] );
            }

            /* handle second parameter: release pattern */
            if ( comma != NULL ) {
                for ( j = bp_linear_bar; j < bp_last_bar; j++ ) {
                    if ( __kmp_str_match( __kmp_barrier_pattern_name[j], 1, comma + 1 ) ) {
                       __kmp_barrier_release_pattern[ i ] = (kmp_bar_pat_e) j;
                       break;
                    }
                }
                if (j == bp_last_bar) {
                    __kmp_msg( kmp_ms_warning, KMP_MSG( BarrReleaseValueInvalid, name, comma + 1 ), __kmp_msg_null );
                    KMP_INFORM( Using_str_Value, name, __kmp_barrier_pattern_name[ bp_linear_bar ] );
                }
            }
        }
    }
} // __kmp_stg_parse_barrier_pattern

static void
__kmp_stg_print_barrier_pattern( kmp_str_buf_t * buffer, char const * name, void * data ) {
    const char *var;
    for ( int i=bs_plain_barrier; i<bs_last_barrier; i++ ) {
        var = __kmp_barrier_pattern_env_name[ i ];
        if ( strcmp ( var, name ) == 0 ) {
            int j = __kmp_barrier_gather_pattern [ i ];
            int k = __kmp_barrier_release_pattern [ i ];
            if( __kmp_env_format ) {
                KMP_STR_BUF_PRINT_NAME_EX(__kmp_barrier_pattern_env_name[ i ]);
            } else {
                __kmp_str_buf_print( buffer, "   %s='", __kmp_barrier_pattern_env_name[ i ] );
            }
            __kmp_str_buf_print( buffer, "%s,%s'\n", __kmp_barrier_pattern_name [ j ], __kmp_barrier_pattern_name [ k ]);
        }
    }
} // __kmp_stg_print_barrier_pattern

// -------------------------------------------------------------------------------------------------
// KMP_ABORT_DELAY
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_abort_delay( char const * name, char const * value, void * data ) {
    // Units of KMP_DELAY_ABORT are seconds, units of __kmp_abort_delay is milliseconds.
    int delay = __kmp_abort_delay / 1000;
    __kmp_stg_parse_int( name, value, 0, INT_MAX / 1000, & delay );
    __kmp_abort_delay = delay * 1000;
} // __kmp_stg_parse_abort_delay

static void
__kmp_stg_print_abort_delay( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_abort_delay );
} // __kmp_stg_print_abort_delay

// -------------------------------------------------------------------------------------------------
// KMP_CPUINFO_FILE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_cpuinfo_file( char const * name, char const * value, void * data ) {
    #if KMP_AFFINITY_SUPPORTED
        __kmp_stg_parse_str( name, value, & __kmp_cpuinfo_file );
        K_DIAG( 1, ( "__kmp_cpuinfo_file == %s\n", __kmp_cpuinfo_file ) );
    #endif
} //__kmp_stg_parse_cpuinfo_file

static void
__kmp_stg_print_cpuinfo_file( kmp_str_buf_t * buffer, char const * name, void * data ) {
    #if KMP_AFFINITY_SUPPORTED
        if( __kmp_env_format ) {
            KMP_STR_BUF_PRINT_NAME;
        } else {
            __kmp_str_buf_print( buffer, "   %s", name );
        }
        if ( __kmp_cpuinfo_file ) {
            __kmp_str_buf_print( buffer, "='%s'\n", __kmp_cpuinfo_file );
        } else {
            __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
        }
    #endif
} //__kmp_stg_print_cpuinfo_file

// -------------------------------------------------------------------------------------------------
// KMP_FORCE_REDUCTION, KMP_DETERMINISTIC_REDUCTION
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_force_reduction( char const * name, char const * value, void * data )
{
    kmp_stg_fr_data_t * reduction = (kmp_stg_fr_data_t *) data;
    int                 rc;

    rc = __kmp_stg_check_rivals( name, value, reduction->rivals );
    if ( rc ) {
        return;
    }; // if
    if ( reduction->force ) {
        if( value != 0 ) {
            if( __kmp_str_match( "critical", 0, value ) )
               __kmp_force_reduction_method = critical_reduce_block;
            else if( __kmp_str_match( "atomic", 0, value ) )
               __kmp_force_reduction_method = atomic_reduce_block;
            else if( __kmp_str_match( "tree", 0, value ) )
               __kmp_force_reduction_method = tree_reduce_block;
            else {
                KMP_FATAL( UnknownForceReduction, name, value );
            }
        }
    } else {
        __kmp_stg_parse_bool( name, value, & __kmp_determ_red );
        if( __kmp_determ_red ) {
            __kmp_force_reduction_method = tree_reduce_block;
        } else {
            __kmp_force_reduction_method = reduction_method_not_defined;
        }
    }
    K_DIAG( 1, ( "__kmp_force_reduction_method == %d\n", __kmp_force_reduction_method ) );
} // __kmp_stg_parse_force_reduction

static void
__kmp_stg_print_force_reduction( kmp_str_buf_t * buffer, char const * name, void * data ) {

    kmp_stg_fr_data_t * reduction = (kmp_stg_fr_data_t *) data;
    char const *        value = NULL;
    if ( reduction->force ) {
        if( __kmp_force_reduction_method == critical_reduce_block) {
            __kmp_stg_print_str( buffer, name, "critical");
        } else if ( __kmp_force_reduction_method == atomic_reduce_block ) {
            __kmp_stg_print_str( buffer, name, "atomic");
        } else if ( __kmp_force_reduction_method == tree_reduce_block ) {
            __kmp_stg_print_str( buffer, name, "tree");
        } else {
            if( __kmp_env_format ) {
                KMP_STR_BUF_PRINT_NAME;
            } else {
                __kmp_str_buf_print( buffer, "   %s", name );
            }
            __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
        }
    } else {
        __kmp_stg_print_bool( buffer, name, __kmp_determ_red );
    }


} // __kmp_stg_print_force_reduction

// -------------------------------------------------------------------------------------------------
// KMP_STORAGE_MAP
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_storage_map( char const * name, char const * value, void * data ) {
    if ( __kmp_str_match(  "verbose", 1, value ) ) {
        __kmp_storage_map         = TRUE;
        __kmp_storage_map_verbose = TRUE;
        __kmp_storage_map_verbose_specified = TRUE;

    } else {
        __kmp_storage_map_verbose = FALSE;
        __kmp_stg_parse_bool( name, value, & __kmp_storage_map ); // !!!
    }; // if
} // __kmp_stg_parse_storage_map

static void
__kmp_stg_print_storage_map( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if ( __kmp_storage_map_verbose || __kmp_storage_map_verbose_specified ) {
        __kmp_stg_print_str( buffer, name, "verbose" );
    } else {
        __kmp_stg_print_bool( buffer, name, __kmp_storage_map );
    }
} // __kmp_stg_print_storage_map

// -------------------------------------------------------------------------------------------------
// KMP_ALL_THREADPRIVATE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_all_threadprivate( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, __kmp_allThreadsSpecified ? __kmp_max_nth : 1, __kmp_max_nth,
        & __kmp_tp_capacity );
} // __kmp_stg_parse_all_threadprivate

static void
__kmp_stg_print_all_threadprivate( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_tp_capacity );

}

// -------------------------------------------------------------------------------------------------
// KMP_FOREIGN_THREADS_THREADPRIVATE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_foreign_threads_threadprivate( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_foreign_tp );
} // __kmp_stg_parse_foreign_threads_threadprivate

static void
__kmp_stg_print_foreign_threads_threadprivate( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_foreign_tp );
} // __kmp_stg_print_foreign_threads_threadprivate


// -------------------------------------------------------------------------------------------------
// KMP_AFFINITY, GOMP_CPU_AFFINITY, KMP_TOPOLOGY_METHOD
// -------------------------------------------------------------------------------------------------

#if KMP_AFFINITY_SUPPORTED
//
// Parse the proc id list.  Return TRUE if successful, FALSE otherwise.
//
static int
__kmp_parse_affinity_proc_id_list( const char *var, const char *env,
    const char **nextEnv, char **proclist )
{
    const char *scan = env;
    const char *next = scan;
    int empty = TRUE;

    *proclist = NULL;

    for (;;) {
        int start, end, stride;

        SKIP_WS(scan);
        next = scan;
        if (*next == '\0') {
            break;
        }

        if (*next == '{') {
            int num;
            next++;     // skip '{'
            SKIP_WS(next);
            scan = next;

            //
            // Read the first integer in the set.
            //
            if ((*next < '0') || (*next > '9')) {
                KMP_WARNING( AffSyntaxError, var );
                return FALSE;
            }
            SKIP_DIGITS(next);
            num = __kmp_str_to_int(scan, *next);
            KMP_ASSERT(num >= 0);

            for (;;) {
                //
                // Check for end of set.
                //
                SKIP_WS(next);
                if (*next == '}') {
                    next++;     // skip '}'
                    break;
                }

                //
                // Skip optional comma.
                //
                if (*next == ',') {
                    next++;
                }
                SKIP_WS(next);

                //
                // Read the next integer in the set.
                //
                scan = next;
                if ((*next < '0') || (*next > '9')) {
                    KMP_WARNING( AffSyntaxError, var );
                    return FALSE;
                }

                SKIP_DIGITS(next);
                num = __kmp_str_to_int(scan, *next);
                KMP_ASSERT(num >= 0);
            }
            empty = FALSE;

            SKIP_WS(next);
            if (*next == ',') {
                next++;
            }
            scan = next;
            continue;
        }

        //
        // Next character is not an integer => end of list
        //
        if ((*next < '0') || (*next > '9')) {
            if (empty) {
                KMP_WARNING( AffSyntaxError, var );
                return FALSE;
            }
            break;
        }

        //
        // Read the first integer.
        //
        SKIP_DIGITS(next);
        start = __kmp_str_to_int(scan, *next);
        KMP_ASSERT(start >= 0);
        SKIP_WS(next);

        //
        // If this isn't a range, then go on.
        //
        if (*next != '-') {
            empty = FALSE;

            //
            // Skip optional comma.
            //
            if (*next == ',') {
                next++;
            }
            scan = next;
            continue;
        }

        //
        // This is a range.  Skip over the '-' and read in the 2nd int.
        //
        next++;         // skip '-'
        SKIP_WS(next);
        scan = next;
        if ((*next < '0') || (*next > '9')) {
            KMP_WARNING( AffSyntaxError, var );
            return FALSE;
        }
        SKIP_DIGITS(next);
        end = __kmp_str_to_int(scan, *next);
        KMP_ASSERT(end >= 0);

        //
        // Check for a stride parameter
        //
        stride = 1;
        SKIP_WS(next);
        if (*next == ':') {
            //
            // A stride is specified.  Skip over the ':" and read the 3rd int.
            //
            int sign = +1;
            next++;         // skip ':'
            SKIP_WS(next);
            scan = next;
            if (*next == '-') {
                sign = -1;
                next++;
                SKIP_WS(next);
                scan = next;
            }
            if ((*next < '0') || (*next > '9')) {
                KMP_WARNING( AffSyntaxError, var );
                return FALSE;
            }
            SKIP_DIGITS(next);
            stride = __kmp_str_to_int(scan, *next);
            KMP_ASSERT(stride >= 0);
            stride *= sign;
        }

        //
        // Do some range checks.
        //
        if (stride == 0) {
            KMP_WARNING( AffZeroStride, var );
            return FALSE;
        }
        if (stride > 0) {
            if (start > end) {
                KMP_WARNING( AffStartGreaterEnd, var, start, end );
                return FALSE;
            }
        }
        else {
            if (start < end) {
                KMP_WARNING( AffStrideLessZero, var, start, end );
                return FALSE;
            }
        }
        if ((end - start) / stride > 65536 ) {
            KMP_WARNING( AffRangeTooBig, var, end, start, stride );
            return FALSE;
        }

        empty = FALSE;

        //
        // Skip optional comma.
        //
        SKIP_WS(next);
        if (*next == ',') {
            next++;
        }
        scan = next;
    }

    *nextEnv = next;

    {
        int len = next - env;
        char *retlist = (char *)__kmp_allocate((len + 1) * sizeof(char));
        memcpy(retlist, env, len * sizeof(char));
        retlist[len] = '\0';
        *proclist = retlist;
    }
    return TRUE;
}


//
// If KMP_AFFINITY is specified without a type, then
// __kmp_affinity_notype should point to its setting.
//
static kmp_setting_t *__kmp_affinity_notype = NULL;

static void
__kmp_parse_affinity_env( char const * name, char const * value,
    enum affinity_type  * out_type,
    char                ** out_proclist,
    int                 * out_verbose,
    int                 * out_warn,
    int                 * out_respect,
    enum affinity_gran  * out_gran,
    int                 * out_gran_levels,
    int                 * out_dups,
    int                 * out_compact,
    int                 * out_offset
)
{
    char * buffer = NULL;    // Copy of env var value.
    char * buf    = NULL;    // Buffer for strtok_r() function.
    char * next = NULL;      // end of token / start of next.
    const char * start;      // start of current token (for err msgs)
    int    count  = 0;       // Counter of parsed integer numbers.
    int    number[ 2 ];      // Parsed numbers.

    // Guards.
    int type         = 0;
    int proclist     = 0;
    int max_proclist = 0;
    int verbose      = 0;
    int warnings     = 0;
    int respect      = 0;
    int gran         = 0;
    int dups         = 0;

    KMP_ASSERT( value != NULL );

    if ( TCR_4(__kmp_init_middle) ) {
        KMP_WARNING( EnvMiddleWarn, name );
        __kmp_env_toPrint( name, 0 );
        return;
    }
    __kmp_env_toPrint( name, 1 );

    buffer = __kmp_str_format( "%s", value );         // Copy env var to keep original intact.
    buf = buffer;
    SKIP_WS(buf);

    // Helper macros.

    //
    // If we see a parse error, emit a warning and scan to the next ",".
    //
    // FIXME - there's got to be a better way to print an error
    // message, hopefully without overwritting peices of buf.
    //
    #define EMIT_WARN(skip,errlist) \
        {                                                                     \
            char ch;                                                          \
            if (skip) {                                                       \
                SKIP_TO(next, ',');                                           \
            }                                                                 \
            ch = *next;                                                       \
            *next = '\0';                                                     \
            KMP_WARNING errlist;                                              \
            *next = ch;                                                       \
            if (skip) {                                                       \
                if (ch == ',') next++;                                        \
            }                                                                 \
            buf = next;                                                       \
        }

    #define _set_param(_guard,_var,_val)                                      \
        {                                                                     \
            if ( _guard == 0 ) {                                              \
                _var = _val;                                                  \
            } else {                                                          \
                EMIT_WARN( FALSE, ( AffParamDefined, name, start ) );         \
            };                                                                \
            ++ _guard;                                                        \
        }

    #define set_type(val)          _set_param( type,     *out_type,        val )
    #define set_verbose(val)       _set_param( verbose,  *out_verbose,     val )
    #define set_warnings(val)      _set_param( warnings, *out_warn,        val )
    #define set_respect(val)       _set_param( respect,  *out_respect,     val )
    #define set_dups(val)          _set_param( dups,     *out_dups,        val )
    #define set_proclist(val)      _set_param( proclist, *out_proclist,    val )

    #define set_gran(val,levels)                                              \
        {                                                                     \
            if ( gran == 0 ) {                                                \
                *out_gran = val;                                              \
                *out_gran_levels = levels;                                    \
            } else {                                                          \
                EMIT_WARN( FALSE, ( AffParamDefined, name, start ) );         \
            };                                                                \
            ++ gran;                                                          \
        }

# if OMP_40_ENABLED
    KMP_DEBUG_ASSERT( ( __kmp_nested_proc_bind.bind_types != NULL )
      && ( __kmp_nested_proc_bind.used > 0 ) );
# endif

    while ( *buf != '\0' ) {
        start = next = buf;

        if (__kmp_match_str("none", buf, (const char **)&next)) {
            set_type( affinity_none );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_false;
# endif
            buf = next;
        } else if (__kmp_match_str("scatter", buf, (const char **)&next)) {
            set_type( affinity_scatter );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
            buf = next;
        } else if (__kmp_match_str("compact", buf, (const char **)&next)) {
            set_type( affinity_compact );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
            buf = next;
        } else if (__kmp_match_str("logical", buf, (const char **)&next)) {
            set_type( affinity_logical );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
            buf = next;
        } else if (__kmp_match_str("physical", buf, (const char **)&next)) {
            set_type( affinity_physical );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
            buf = next;
        } else if (__kmp_match_str("explicit", buf, (const char **)&next)) {
            set_type( affinity_explicit );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
            buf = next;
# if KMP_MIC
        } else if (__kmp_match_str("balanced", buf, (const char **)&next)) {
            set_type( affinity_balanced );
#  if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
#  endif
            buf = next;
# endif
        } else if (__kmp_match_str("disabled", buf, (const char **)&next)) {
            set_type( affinity_disabled );
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_false;
# endif
            buf = next;
        } else if (__kmp_match_str("verbose", buf, (const char **)&next)) {
            set_verbose( TRUE );
            buf = next;
        } else if (__kmp_match_str("noverbose", buf, (const char **)&next)) {
            set_verbose( FALSE );
            buf = next;
        } else if (__kmp_match_str("warnings", buf, (const char **)&next)) {
            set_warnings( TRUE );
            buf = next;
        } else if (__kmp_match_str("nowarnings", buf, (const char **)&next)) {
            set_warnings( FALSE );
            buf = next;
        } else if (__kmp_match_str("respect", buf, (const char **)&next)) {
            set_respect( TRUE );
            buf = next;
        } else if (__kmp_match_str("norespect", buf, (const char **)&next)) {
            set_respect( FALSE );
            buf = next;
        } else if (__kmp_match_str("duplicates", buf, (const char **)&next)
          || __kmp_match_str("dups", buf, (const char **)&next)) {
            set_dups( TRUE );
            buf = next;
        } else if (__kmp_match_str("noduplicates", buf, (const char **)&next)
          || __kmp_match_str("nodups", buf, (const char **)&next)) {
            set_dups( FALSE );
            buf = next;
        } else if (__kmp_match_str("granularity", buf, (const char **)&next)
          || __kmp_match_str("gran", buf, (const char **)&next)) {
            SKIP_WS(next);
            if (*next != '=') {
                EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
                continue;
            }
            next++;      // skip '='
            SKIP_WS(next);

            buf = next;
            if (__kmp_match_str("fine", buf, (const char **)&next)) {
                set_gran( affinity_gran_fine, -1 );
                buf = next;
            } else if (__kmp_match_str("thread", buf, (const char **)&next)) {
                set_gran( affinity_gran_thread, -1 );
                buf = next;
            } else if (__kmp_match_str("core", buf, (const char **)&next)) {
                set_gran( affinity_gran_core, -1 );
                buf = next;
            } else if (__kmp_match_str("package", buf, (const char **)&next)) {
                set_gran( affinity_gran_package, -1 );
                buf = next;
            } else if (__kmp_match_str("node", buf, (const char **)&next)) {
                set_gran( affinity_gran_node, -1 );
                buf = next;
# if KMP_OS_WINDOWS && KMP_ARCH_X86_64
            } else if (__kmp_match_str("group", buf, (const char **)&next)) {
                set_gran( affinity_gran_group, -1 );
                buf = next;
# endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
            } else if ((*buf >= '0') && (*buf <= '9')) {
                int n;
                next = buf;
                SKIP_DIGITS(next);
                n = __kmp_str_to_int( buf, *next );
                KMP_ASSERT(n >= 0);
                buf = next;
                set_gran( affinity_gran_default, n );
            } else {
                EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
                continue;
            }
        } else if (__kmp_match_str("proclist", buf, (const char **)&next)) {
            char *temp_proclist;

            SKIP_WS(next);
            if (*next != '=') {
                EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
                continue;
            }
            next++;      // skip '='
            SKIP_WS(next);
            if (*next != '[') {
                EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
                continue;
            }
            next++;      // skip '['
            buf = next;
            if (! __kmp_parse_affinity_proc_id_list(name, buf,
              (const char **)&next, &temp_proclist)) {
                //
                // warning already emitted.
                //
                SKIP_TO(next, ']');
                if (*next == ']') next++;
                SKIP_TO(next, ',');
                if (*next == ',') next++;
                buf = next;
                continue;
            }
            if (*next != ']') {
                EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
                continue;
            }
            next++;      // skip ']'
            set_proclist( temp_proclist );
        } else if ((*buf >= '0') && (*buf <= '9')) {
            // Parse integer numbers -- permute and offset.
            int n;
            next = buf;
            SKIP_DIGITS(next);
            n = __kmp_str_to_int( buf, *next );
            KMP_ASSERT(n >= 0);
            buf = next;
            if ( count < 2 ) {
                number[ count ] = n;
            } else {
                KMP_WARNING( AffManyParams, name, start );
            }; // if
            ++ count;
        } else {
            EMIT_WARN( TRUE, ( AffInvalidParam, name, start ) );
            continue;
        }

        SKIP_WS(next);
        if (*next == ',') {
            next++;
            SKIP_WS(next);
        }
        else if (*next != '\0') {
            const char *temp = next;
            EMIT_WARN( TRUE, ( ParseExtraCharsWarn, name, temp ) );
            continue;
        }
        buf = next;
    } // while

    #undef EMIT_WARN
    #undef _set_param
    #undef set_type
    #undef set_verbose
    #undef set_warnings
    #undef set_respect
    #undef set_granularity

    KMP_INTERNAL_FREE( buffer );

    if ( proclist ) {
        if ( ! type ) {
            KMP_WARNING( AffProcListNoType, name );
            __kmp_affinity_type = affinity_explicit;
        }
        else if ( __kmp_affinity_type != affinity_explicit ) {
            KMP_WARNING( AffProcListNotExplicit, name );
            KMP_ASSERT( *out_proclist != NULL );
            KMP_INTERNAL_FREE( *out_proclist );
            *out_proclist = NULL;
        }
    }
    switch ( *out_type ) {
        case affinity_logical:
        case affinity_physical: {
            if ( count > 0 ) {
                *out_offset = number[ 0 ];
            }; // if
            if ( count > 1 ) {
                KMP_WARNING( AffManyParamsForLogic, name, number[ 1 ] );
            }; // if
        } break;
# if KMP_MIC
        case affinity_balanced: {
            if ( count > 0 ) {
                *out_compact = number[ 0 ];
            }; // if
            if ( count > 1 ) {
                *out_offset = number[ 1 ];
            }; // if

            // If granularity is neither thread nor core let it be default value=fine
            if( __kmp_affinity_gran != affinity_gran_default && __kmp_affinity_gran != affinity_gran_fine
                && __kmp_affinity_gran != affinity_gran_thread && __kmp_affinity_gran != affinity_gran_core ) {
                if( __kmp_affinity_verbose || __kmp_affinity_warnings ) {
                    KMP_WARNING( AffGranUsing, "KMP_AFFINITY", "core" );
                }
                __kmp_affinity_gran = affinity_gran_fine;
            }
        } break;
# endif
        case affinity_scatter:
        case affinity_compact: {
            if ( count > 0 ) {
                *out_compact = number[ 0 ];
            }; // if
            if ( count > 1 ) {
                *out_offset = number[ 1 ];
            }; // if
        } break;
        case affinity_explicit: {
            if ( *out_proclist == NULL ) {
                KMP_WARNING( AffNoProcList, name );
                __kmp_affinity_type = affinity_none;
            }
            if ( count > 0 ) {
                KMP_WARNING( AffNoParam, name, "explicit" );
            }
        } break;
        case affinity_none: {
            if ( count > 0 ) {
                KMP_WARNING( AffNoParam, name, "none" );
            }; // if
        } break;
        case affinity_disabled: {
            if ( count > 0 ) {
                KMP_WARNING( AffNoParam, name, "disabled" );
            }; // if
        } break;
        case affinity_default: {
            if ( count > 0 ) {
                KMP_WARNING( AffNoParam, name, "default" );
            }; // if
        } break;
        default: {
            KMP_ASSERT( 0 );
        };
    }; // switch
} // __kmp_parse_affinity_env

static void
__kmp_stg_parse_affinity( char const * name, char const * value, void * data )
{
    kmp_setting_t **rivals = (kmp_setting_t **) data;
    int rc;

    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }

    __kmp_parse_affinity_env( name, value, & __kmp_affinity_type,
      & __kmp_affinity_proclist, & __kmp_affinity_verbose,
      & __kmp_affinity_warnings, & __kmp_affinity_respect_mask,
      & __kmp_affinity_gran, & __kmp_affinity_gran_levels,
      & __kmp_affinity_dups, & __kmp_affinity_compact,
      & __kmp_affinity_offset );

} // __kmp_stg_parse_affinity

static void
__kmp_stg_print_affinity( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
    } else {
        __kmp_str_buf_print( buffer, "   %s='", name );
    }
    if ( __kmp_affinity_verbose ) {
        __kmp_str_buf_print( buffer, "%s,", "verbose");
    } else {
        __kmp_str_buf_print( buffer, "%s,", "noverbose");
    }
    if ( __kmp_affinity_warnings ) {
        __kmp_str_buf_print( buffer, "%s,", "warnings");
    } else {
        __kmp_str_buf_print( buffer, "%s,", "nowarnings");
    }
    if ( KMP_AFFINITY_CAPABLE() ) {
        if ( __kmp_affinity_respect_mask ) {
            __kmp_str_buf_print( buffer, "%s,", "respect");
        } else {
            __kmp_str_buf_print( buffer, "%s,", "norespect");
        }
        switch ( __kmp_affinity_gran ) {
            case affinity_gran_default:
                __kmp_str_buf_print( buffer, "%s", "granularity=default,");
                break;
            case affinity_gran_fine:
                __kmp_str_buf_print( buffer, "%s", "granularity=fine,");
                break;
            case affinity_gran_thread:
                __kmp_str_buf_print( buffer, "%s", "granularity=thread,");
                break;
            case affinity_gran_core:
                __kmp_str_buf_print( buffer, "%s", "granularity=core,");
                break;
            case affinity_gran_package:
                __kmp_str_buf_print( buffer, "%s", "granularity=package,");
                break;
            case affinity_gran_node:
                __kmp_str_buf_print( buffer, "%s", "granularity=node,");
                break;
# if KMP_OS_WINDOWS && KMP_ARCH_X86_64
            case affinity_gran_group:
                __kmp_str_buf_print( buffer, "%s", "granularity=group,");
                break;
# endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
        }
        if ( __kmp_affinity_dups ) {
            __kmp_str_buf_print( buffer, "%s,", "duplicates");
        } else {
            __kmp_str_buf_print( buffer, "%s,", "noduplicates");
        }
    }
    if ( ! KMP_AFFINITY_CAPABLE() ) {
        __kmp_str_buf_print( buffer, "%s", "disabled" );
    }
    else switch ( __kmp_affinity_type ){
        case affinity_none:
            __kmp_str_buf_print( buffer, "%s", "none");
            break;
        case affinity_physical:
            __kmp_str_buf_print( buffer, "%s,%d", "physical",
              __kmp_affinity_offset );
            break;
        case affinity_logical:
            __kmp_str_buf_print( buffer, "%s,%d", "logical",
              __kmp_affinity_offset );
            break;
        case affinity_compact:
            __kmp_str_buf_print( buffer, "%s,%d,%d", "compact",
              __kmp_affinity_compact, __kmp_affinity_offset );
            break;
        case affinity_scatter:
            __kmp_str_buf_print( buffer, "%s,%d,%d", "scatter",
              __kmp_affinity_compact, __kmp_affinity_offset );
            break;
        case affinity_explicit:
            __kmp_str_buf_print( buffer, "%s=[%s],%s", "proclist",
              __kmp_affinity_proclist, "explicit" );
            break;
# if KMP_MIC
        case affinity_balanced:
            __kmp_str_buf_print( buffer, "%s,%d,%d", "balanced",
              __kmp_affinity_compact, __kmp_affinity_offset );
            break;
# endif
        case affinity_disabled:
            __kmp_str_buf_print( buffer, "%s", "disabled");
            break;
        case affinity_default:
            __kmp_str_buf_print( buffer, "%s", "default");
            break;
        default:
            __kmp_str_buf_print( buffer, "%s", "<unknown>");
            break;
    }
        __kmp_str_buf_print( buffer, "'\n" );
} //__kmp_stg_print_affinity

# ifdef KMP_GOMP_COMPAT

static void
__kmp_stg_parse_gomp_cpu_affinity( char const * name, char const * value, void * data )
{
    const char * next = NULL;
    char * temp_proclist;
    kmp_setting_t **rivals = (kmp_setting_t **) data;
    int rc;

    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }

    if ( TCR_4(__kmp_init_middle) ) {
        KMP_WARNING( EnvMiddleWarn, name );
        __kmp_env_toPrint( name, 0 );
        return;
    }

    __kmp_env_toPrint( name, 1 );

    if ( __kmp_parse_affinity_proc_id_list( name, value, &next,
      &temp_proclist )) {
        SKIP_WS(next);
        if (*next == '\0') {
            //
            // GOMP_CPU_AFFINITY => granularity=fine,explicit,proclist=...
            //
            __kmp_affinity_proclist = temp_proclist;
            __kmp_affinity_type = affinity_explicit;
            __kmp_affinity_gran = affinity_gran_fine;
# if OMP_40_ENABLED
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
# endif
        }
        else {
            KMP_WARNING( AffSyntaxError, name );
            if (temp_proclist != NULL) {
                KMP_INTERNAL_FREE((void *)temp_proclist);
            }
        }
    }
    else {
        //
        // Warning already emitted
        //
        __kmp_affinity_type = affinity_none;
    }
} // __kmp_stg_parse_gomp_cpu_affinity

# endif /* KMP_GOMP_COMPAT */


# if OMP_40_ENABLED

/*-----------------------------------------------------------------------------

The OMP_PLACES proc id list parser. Here is the grammar:

place_list := place
place_list := place , place_list
place := num
place := place : num
place := place : num : signed
place := { subplacelist }
place := ! place                  // (lowest priority)
subplace_list := subplace
subplace_list := subplace , subplace_list
subplace := num
subplace := num : num
subplace := num : num : signed
signed := num
signed := + signed
signed := - signed

-----------------------------------------------------------------------------*/

static int
__kmp_parse_subplace_list( const char *var, const char **scan )
{
    const char *next;

    for (;;) {
        int start, count, stride;

        //
        // Read in the starting proc id
        //
        SKIP_WS(*scan);
        if ((**scan < '0') || (**scan > '9')) {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        next = *scan;
        SKIP_DIGITS(next);
        start = __kmp_str_to_int(*scan, *next);
        KMP_ASSERT(start >= 0);
        *scan = next;

        //
        // valid follow sets are ',' ':' and '}'
        //
        SKIP_WS(*scan);
        if (**scan == '}') {
            break;
        }
        if (**scan == ',') {
            (*scan)++;  // skip ','
            continue;
        }
        if (**scan != ':') {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        (*scan)++;      // skip ':'

        //
        // Read count parameter
        //
        SKIP_WS(*scan);
        if ((**scan < '0') || (**scan > '9')) {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        next = *scan;
        SKIP_DIGITS(next);
        count = __kmp_str_to_int(*scan, *next);
        KMP_ASSERT(count >= 0);
        *scan = next;

        //
        // valid follow sets are ',' ':' and '}'
        //
        SKIP_WS(*scan);
        if (**scan == '}') {
            break;
        }
        if (**scan == ',') {
            (*scan)++;  // skip ','
            continue;
        }
        if (**scan != ':') {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        (*scan)++;      // skip ':'

        //
        // Read stride parameter
        //
        int sign = +1;
        for (;;) {
            SKIP_WS(*scan);
            if (**scan == '+') {
                (*scan)++; // skip '+'
                continue;
            }
            if (**scan == '-') {
                sign *= -1;
                (*scan)++; // skip '-'
                continue;
            }
            break;
        }
        SKIP_WS(*scan);
        if ((**scan < '0') || (**scan > '9')) {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        next = *scan;
        SKIP_DIGITS(next);
        stride = __kmp_str_to_int(*scan, *next);
        KMP_ASSERT(stride >= 0);
        *scan = next;
        stride *= sign;

        //
        // valid follow sets are ',' and '}'
        //
        SKIP_WS(*scan);
        if (**scan == '}') {
            break;
        }
        if (**scan == ',') {
            (*scan)++;  // skip ','
            continue;
        }

        KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
        return FALSE;
    }
    return TRUE;
}

static int
__kmp_parse_place( const char *var, const char ** scan )
{
    const char *next;

    //
    // valid follow sets are '{' '!' and num
    //
    SKIP_WS(*scan);
    if (**scan == '{') {
        (*scan)++;      // skip '{'
        if (! __kmp_parse_subplace_list(var, scan)) {
            return FALSE;
        }
        if (**scan != '}') {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        (*scan)++;      // skip '}'
    }
    else if (**scan == '!') {
        (*scan)++;      // skip '!'
        return __kmp_parse_place(var, scan); //'!' has lower precedence than ':'
    }
    else if ((**scan >= '0') && (**scan <= '9')) {
        next = *scan;
        SKIP_DIGITS(next);
        int proc = __kmp_str_to_int(*scan, *next);
        KMP_ASSERT(proc >= 0);
        *scan = next;
    }
    else {
        KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
        return FALSE;
    }
    return TRUE;
}

static int
__kmp_parse_place_list( const char *var, const char *env, char **place_list )
{
    const char *scan = env;
    const char *next = scan;

    for (;;) {
        int start, count, stride;

        if (! __kmp_parse_place(var, &scan)) {
            return FALSE;
        }

        //
        // valid follow sets are ',' ':' and EOL
        //
        SKIP_WS(scan);
        if (*scan == '\0') {
            break;
        }
        if (*scan == ',') {
            scan++;     // skip ','
            continue;
        }
        if (*scan != ':') {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        scan++;         // skip ':'

        //
        // Read count parameter
        //
        SKIP_WS(scan);
        if ((*scan < '0') || (*scan > '9')) {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        next = scan;
        SKIP_DIGITS(next);
        count = __kmp_str_to_int(scan, *next);
        KMP_ASSERT(count >= 0);
        scan = next;

        //
        // valid follow sets are ',' ':' and EOL
        //
        SKIP_WS(scan);
        if (*scan == '\0') {
            break;
        }
        if (*scan == ',') {
            scan++;     // skip ','
            continue;
        }
        if (*scan != ':') {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        scan++;         // skip ':'

        //
        // Read stride parameter
        //
        int sign = +1;
        for (;;) {
            SKIP_WS(scan);
            if (*scan == '+') {
                scan++; // skip '+'
                continue;
            }
            if (*scan == '-') {
                sign *= -1;
                scan++; // skip '-'
                continue;
            }
            break;
        }
        SKIP_WS(scan);
        if ((*scan < '0') || (*scan > '9')) {
            KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
            return FALSE;
        }
        next = scan;
        SKIP_DIGITS(next);
        stride = __kmp_str_to_int(scan, *next);
        KMP_ASSERT(stride >= 0);
        scan = next;
        stride *= sign;

        //
        // valid follow sets are ',' and EOL
        //
        SKIP_WS(scan);
        if (*scan == '\0') {
            break;
        }
        if (*scan == ',') {
            scan++;     // skip ','
            continue;
        }

        KMP_WARNING( SyntaxErrorUsing, var, "\"threads\"" );
        return FALSE;
    }

    {
        int len = scan - env;
        char *retlist = (char *)__kmp_allocate((len + 1) * sizeof(char));
        memcpy(retlist, env, len * sizeof(char));
        retlist[len] = '\0';
        *place_list = retlist;
    }
    return TRUE;
}

static void
__kmp_stg_parse_places( char const * name, char const * value, void * data )
{
    int count;
    const char *scan = value;
    const char *next = scan;
    const char *kind = "\"threads\"";
    kmp_setting_t **rivals = (kmp_setting_t **) data;
    int rc;

    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }

    //
    // If OMP_PROC_BIND is not specified but OMP_PLACES is,
    // then let OMP_PROC_BIND default to true.
    //
    if ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_default ) {
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_true;
    }

    //__kmp_affinity_num_places = 0;

    if ( __kmp_match_str( "threads", scan, &next ) ) {
        scan = next;
        __kmp_affinity_type = affinity_compact;
        __kmp_affinity_gran = affinity_gran_thread;
        __kmp_affinity_dups = FALSE;
        kind = "\"threads\"";
    }
    else if ( __kmp_match_str( "cores", scan, &next ) ) {
        scan = next;
        __kmp_affinity_type = affinity_compact;
        __kmp_affinity_gran = affinity_gran_core;
        __kmp_affinity_dups = FALSE;
        kind = "\"cores\"";
    }
    else if ( __kmp_match_str( "sockets", scan, &next ) ) {
        scan = next;
        __kmp_affinity_type = affinity_compact;
        __kmp_affinity_gran = affinity_gran_package;
        __kmp_affinity_dups = FALSE;
        kind = "\"sockets\"";
    }
    else {
        if ( __kmp_affinity_proclist != NULL ) {
            KMP_INTERNAL_FREE( (void *)__kmp_affinity_proclist );
            __kmp_affinity_proclist = NULL;
        }
        if ( __kmp_parse_place_list( name, value, &__kmp_affinity_proclist ) ) {
            __kmp_affinity_type = affinity_explicit;
            __kmp_affinity_gran = affinity_gran_fine;
            __kmp_affinity_dups = FALSE;
            if ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_default ) {
                 __kmp_nested_proc_bind.bind_types[0] = proc_bind_true;
            }
        }
        return;
    }

    if ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_default ) {
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_true;
    }

    SKIP_WS(scan);
    if ( *scan == '\0' ) {
        return;
    }

    //
    // Parse option count parameter in parentheses
    //
    if ( *scan != '(' ) {
        KMP_WARNING( SyntaxErrorUsing, name, kind );
        return;
    }
    scan++;     // skip '('

    SKIP_WS(scan);
    next = scan;
    SKIP_DIGITS(next);
    count = __kmp_str_to_int(scan, *next);
    KMP_ASSERT(count >= 0);
    scan = next;

    SKIP_WS(scan);
    if ( *scan != ')' ) {
        KMP_WARNING( SyntaxErrorUsing, name, kind );
        return;
    }
    scan++;     // skip ')'

    SKIP_WS(scan);
    if ( *scan != '\0' ) {
        KMP_WARNING( ParseExtraCharsWarn, name, scan );
    }
    __kmp_affinity_num_places = count;
}

static void
__kmp_stg_print_places( kmp_str_buf_t * buffer, char const * name,
  void * data )
{
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME;
    } else {
        __kmp_str_buf_print( buffer, "   %s", name );
    }
    if ( ( __kmp_nested_proc_bind.used == 0 )
      || ( __kmp_nested_proc_bind.bind_types == NULL )
      || ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_false ) ) {
        __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
    }
    else if ( __kmp_affinity_type == affinity_explicit ) {
        if ( __kmp_affinity_proclist != NULL ) {
            __kmp_str_buf_print( buffer, "='%s'\n", __kmp_affinity_proclist );
        }
        else {
            __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
        }
    }
    else if ( __kmp_affinity_type == affinity_compact ) {
        int num;
        if ( __kmp_affinity_num_masks > 0 ) {
            num = __kmp_affinity_num_masks;
        }
        else if ( __kmp_affinity_num_places > 0 ) {
            num = __kmp_affinity_num_places;
        }
        else {
            num = 0;
        }
        if ( __kmp_affinity_gran == affinity_gran_thread ) {
            if ( num > 0 ) {
                __kmp_str_buf_print( buffer, "='threads(%d)'\n", num );
            }
            else {
                __kmp_str_buf_print( buffer, "='threads'\n" );
            }
        }
        else if ( __kmp_affinity_gran == affinity_gran_core ) {
            if ( num > 0 ) {
                __kmp_str_buf_print( buffer, "='cores(%d)' \n", num );
            }
            else {
                __kmp_str_buf_print( buffer, "='cores'\n" );
            }
        }
        else if ( __kmp_affinity_gran == affinity_gran_package ) {
            if ( num > 0 ) {
                __kmp_str_buf_print( buffer, "='sockets(%d)'\n", num );
            }
            else {
                __kmp_str_buf_print( buffer, "='sockets'\n" );
            }
        }
        else {
            __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
        }
    }
    else {
        __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
    }
}

# endif /* OMP_40_ENABLED */

# if (! OMP_40_ENABLED)

static void
__kmp_stg_parse_proc_bind( char const * name, char const * value, void * data )
{
    int enabled;
    kmp_setting_t **rivals = (kmp_setting_t **) data;
    int rc;

    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }

    //
    // in OMP 3.1, OMP_PROC_BIND is strictly a boolean
    //
    __kmp_stg_parse_bool( name, value, & enabled );
    if ( enabled ) {
            //
            // OMP_PROC_BIND => granularity=core,scatter
            //
            __kmp_affinity_type = affinity_scatter;
            __kmp_affinity_gran = affinity_gran_core;
    }
    else {
        __kmp_affinity_type = affinity_none;
    }
} // __kmp_parse_proc_bind

# endif /* if (! OMP_40_ENABLED) */


static void
__kmp_stg_parse_topology_method( char const * name, char const * value,
  void * data ) {
    if ( __kmp_str_match( "all", 1, value ) ) {
       __kmp_affinity_top_method = affinity_top_method_all;
    }
# if KMP_ARCH_X86 || KMP_ARCH_X86_64
    else if ( __kmp_str_match( "x2apic id", 9, value )
      || __kmp_str_match( "x2apic_id", 9, value )
      || __kmp_str_match( "x2apic-id", 9, value )
      || __kmp_str_match( "x2apicid", 8, value )
      || __kmp_str_match( "cpuid leaf 11", 13, value )
      || __kmp_str_match( "cpuid_leaf_11", 13, value )
      || __kmp_str_match( "cpuid-leaf-11", 13, value )
      || __kmp_str_match( "cpuid leaf11", 12, value )
      || __kmp_str_match( "cpuid_leaf11", 12, value )
      || __kmp_str_match( "cpuid-leaf11", 12, value )
      || __kmp_str_match( "cpuidleaf 11", 12, value )
      || __kmp_str_match( "cpuidleaf_11", 12, value )
      || __kmp_str_match( "cpuidleaf-11", 12, value )
      || __kmp_str_match( "cpuidleaf11", 11, value )
      || __kmp_str_match( "cpuid 11", 8, value )
      || __kmp_str_match( "cpuid_11", 8, value )
      || __kmp_str_match( "cpuid-11", 8, value )
      || __kmp_str_match( "cpuid11", 7, value )
      || __kmp_str_match( "leaf 11", 7, value )
      || __kmp_str_match( "leaf_11", 7, value )
      || __kmp_str_match( "leaf-11", 7, value )
      || __kmp_str_match( "leaf11", 6, value ) ) {
        __kmp_affinity_top_method = affinity_top_method_x2apicid;
    }
    else if ( __kmp_str_match( "apic id", 7, value )
      || __kmp_str_match( "apic_id", 7, value )
      || __kmp_str_match( "apic-id", 7, value )
      || __kmp_str_match( "apicid", 6, value )
      || __kmp_str_match( "cpuid leaf 4", 12, value )
      || __kmp_str_match( "cpuid_leaf_4", 12, value )
      || __kmp_str_match( "cpuid-leaf-4", 12, value )
      || __kmp_str_match( "cpuid leaf4", 11, value )
      || __kmp_str_match( "cpuid_leaf4", 11, value )
      || __kmp_str_match( "cpuid-leaf4", 11, value )
      || __kmp_str_match( "cpuidleaf 4", 11, value )
      || __kmp_str_match( "cpuidleaf_4", 11, value )
      || __kmp_str_match( "cpuidleaf-4", 11, value )
      || __kmp_str_match( "cpuidleaf4", 10, value )
      || __kmp_str_match( "cpuid 4", 7, value )
      || __kmp_str_match( "cpuid_4", 7, value )
      || __kmp_str_match( "cpuid-4", 7, value )
      || __kmp_str_match( "cpuid4", 6, value )
      || __kmp_str_match( "leaf 4", 6, value )
      || __kmp_str_match( "leaf_4", 6, value )
      || __kmp_str_match( "leaf-4", 6, value )
      || __kmp_str_match( "leaf4", 5, value ) ) {
        __kmp_affinity_top_method = affinity_top_method_apicid;
    }
# endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */
    else if ( __kmp_str_match( "/proc/cpuinfo", 2, value )
      || __kmp_str_match( "cpuinfo", 5, value )) {
        __kmp_affinity_top_method = affinity_top_method_cpuinfo;
    }
# if KMP_OS_WINDOWS && KMP_ARCH_X86_64
    else if ( __kmp_str_match( "group", 1, value ) ) {
        __kmp_affinity_top_method = affinity_top_method_group;
    }
# endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
    else if ( __kmp_str_match( "flat", 1, value ) ) {
        __kmp_affinity_top_method = affinity_top_method_flat;
    }
    else {
        KMP_WARNING( StgInvalidValue, name, value );
    }
} // __kmp_stg_parse_topology_method

static void
__kmp_stg_print_topology_method( kmp_str_buf_t * buffer, char const * name,
  void * data ) {
# if KMP_DEBUG
    char const * value = NULL;

    switch ( __kmp_affinity_top_method ) {
        case affinity_top_method_default:
        value = "default";
        break;

        case affinity_top_method_all:
        value = "all";
        break;

#  if KMP_ARCH_X86 || KMP_ARCH_X86_64
        case affinity_top_method_x2apicid:
        value = "x2APIC id";
        break;

        case affinity_top_method_apicid:
        value = "APIC id";
        break;
#  endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

        case affinity_top_method_cpuinfo:
        value = "cpuinfo";
        break;

#  if KMP_OS_WINDOWS && KMP_ARCH_X86_64
        case affinity_top_method_group:
        value = "group";
        break;
#  endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */

        case affinity_top_method_flat:
        value = "flat";
        break;
    }

    if ( value != NULL ) {
        __kmp_stg_print_str( buffer, name, value );
    }
# endif /* KMP_DEBUG */
} // __kmp_stg_print_topology_method

#endif /* KMP_AFFINITY_SUPPORTED */


#if OMP_40_ENABLED

//
// OMP_PROC_BIND / bind-var is functional on all 4.0 builds, including OS X*
// OMP_PLACES / place-partition-var is not.
//
static void
__kmp_stg_parse_proc_bind( char const * name, char const * value, void * data )
{
    kmp_setting_t **rivals = (kmp_setting_t **) data;
    int rc;

    rc = __kmp_stg_check_rivals( name, value, rivals );
    if ( rc ) {
        return;
    }

    //
    // in OMP 4.0 OMP_PROC_BIND is a vector of proc_bind types.
    //
    KMP_DEBUG_ASSERT( (__kmp_nested_proc_bind.bind_types != NULL)
      && ( __kmp_nested_proc_bind.used > 0 ) );

    const char *buf = value;
    const char *next;
    int num;
    SKIP_WS( buf );
    if ( (*buf >= '0') && (*buf <= '9') ) {
        next = buf;
        SKIP_DIGITS( next );
        num = __kmp_str_to_int( buf, *next );
        KMP_ASSERT( num >= 0 );
        buf = next;
        SKIP_WS( buf );
    }
    else {
        num = -1;
    }

    next = buf;
    if ( __kmp_match_str( "disabled", buf, &next ) ) {
        buf = next;
        SKIP_WS( buf );
# if KMP_AFFINITY_SUPPORTED
        __kmp_affinity_type = affinity_disabled;
# endif /* KMP_AFFINITY_SUPPORTED */
        __kmp_nested_proc_bind.used = 1;
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_disabled;
    }
    else if ( ( num == (int)proc_bind_false )
      || __kmp_match_str( "false", buf, &next ) ) {
        buf = next;
        SKIP_WS( buf );
# if KMP_AFFINITY_SUPPORTED
        __kmp_affinity_type = affinity_none;
# endif /* KMP_AFFINITY_SUPPORTED */
        __kmp_nested_proc_bind.used = 1;
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_false;
    }
    else if ( ( num == (int)proc_bind_true )
      || __kmp_match_str( "true", buf, &next ) ) {
        buf = next;
        SKIP_WS( buf );
        __kmp_nested_proc_bind.used = 1;
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_true;
    }
    else {
        //
        // Count the number of values in the env var string
        //
        const char *scan;
        int nelem = 1;
        for ( scan = buf; *scan != '\0'; scan++ ) {
            if ( *scan == ',' ) {
                nelem++;
            }
        }

        //
        // Create / expand the nested proc_bind array as needed
        //
        if ( __kmp_nested_proc_bind.size < nelem ) {
            __kmp_nested_proc_bind.bind_types = (kmp_proc_bind_t *)
                KMP_INTERNAL_REALLOC( __kmp_nested_proc_bind.bind_types,
                sizeof(kmp_proc_bind_t) * nelem );
            if ( __kmp_nested_proc_bind.bind_types == NULL ) {
                KMP_FATAL( MemoryAllocFailed );
            }
            __kmp_nested_proc_bind.size = nelem;
        }
        __kmp_nested_proc_bind.used = nelem;

        //
        // Save values in the nested proc_bind array
        //
        int i = 0;
        for (;;) {
            enum kmp_proc_bind_t bind;

            if ( ( num == (int)proc_bind_master )
              || __kmp_match_str( "master", buf, &next ) ) {
                buf = next;
                SKIP_WS( buf );
                bind = proc_bind_master;
            }
            else if ( ( num == (int)proc_bind_close )
              || __kmp_match_str( "close", buf, &next ) ) {
                buf = next;
                SKIP_WS( buf );
                bind = proc_bind_close;
            }
            else if ( ( num == (int)proc_bind_spread )
              || __kmp_match_str( "spread", buf, &next ) ) {
                buf = next;
                SKIP_WS( buf );
                bind = proc_bind_spread;
            }
            else {
                KMP_WARNING( StgInvalidValue, name, value );
                __kmp_nested_proc_bind.bind_types[0] = proc_bind_false;
                __kmp_nested_proc_bind.used = 1;
                return;
            }

            __kmp_nested_proc_bind.bind_types[i++] = bind;
            if ( i >= nelem ) {
                break;
            }
            KMP_DEBUG_ASSERT( *buf == ',' );
            buf++;
            SKIP_WS( buf );

            //
            // Read next value if it was specified as an integer
            //
            if ( (*buf >= '0') && (*buf <= '9') ) {
                next = buf;
                SKIP_DIGITS( next );
                num = __kmp_str_to_int( buf, *next );
                KMP_ASSERT( num >= 0 );
                buf = next;
                SKIP_WS( buf );
            }
            else {
                num = -1;
            }
        }
        SKIP_WS( buf );
    }
    if ( *buf != '\0' ) {
        KMP_WARNING( ParseExtraCharsWarn, name, buf );
    }
}


static void
__kmp_stg_print_proc_bind( kmp_str_buf_t * buffer, char const * name,
  void * data )
{
    int nelem = __kmp_nested_proc_bind.used;
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME;
    } else {
        __kmp_str_buf_print( buffer, "   %s", name );
    }
    if ( nelem == 0 ) {
        __kmp_str_buf_print( buffer, ": %s\n", KMP_I18N_STR( NotDefined ) );
    }
    else {
        int i;
        __kmp_str_buf_print( buffer, "='", name );
        for ( i = 0; i < nelem; i++ ) {
            switch ( __kmp_nested_proc_bind.bind_types[i] ) {
                case proc_bind_false:
                __kmp_str_buf_print( buffer, "false" );
                break;

                case proc_bind_true:
                __kmp_str_buf_print( buffer, "true" );
                break;

                case proc_bind_master:
                __kmp_str_buf_print( buffer, "master" );
                break;

                case proc_bind_close:
                __kmp_str_buf_print( buffer, "close" );
                break;

                case proc_bind_spread:
                __kmp_str_buf_print( buffer, "spread" );
                break;

                case proc_bind_disabled:
                __kmp_str_buf_print( buffer, "disabled" );
                break;

                case proc_bind_intel:
                __kmp_str_buf_print( buffer, "intel" );
                break;

                case proc_bind_default:
                __kmp_str_buf_print( buffer, "default" );
                break;
            }
            if ( i < nelem - 1 ) {
                __kmp_str_buf_print( buffer, "," );
            }
        }
        __kmp_str_buf_print( buffer, "'\n" );
    }
}

#endif /* OMP_40_ENABLED */


// -------------------------------------------------------------------------------------------------
// OMP_DYNAMIC
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_omp_dynamic( char const * name, char const * value, void * data )
{
    __kmp_stg_parse_bool( name, value, & (__kmp_global.g.g_dynamic) );
} // __kmp_stg_parse_omp_dynamic

static void
__kmp_stg_print_omp_dynamic( kmp_str_buf_t * buffer, char const * name, void * data )
{
    __kmp_stg_print_bool( buffer, name, __kmp_global.g.g_dynamic );
} // __kmp_stg_print_omp_dynamic

static void
__kmp_stg_parse_kmp_dynamic_mode( char const * name, char const * value, void * data )
{
    if ( TCR_4(__kmp_init_parallel) ) {
        KMP_WARNING( EnvParallelWarn, name );
        __kmp_env_toPrint( name, 0 );
        return;
    }
#ifdef USE_LOAD_BALANCE
    else if ( __kmp_str_match( "load balance", 2, value )
      || __kmp_str_match( "load_balance", 2, value )
      || __kmp_str_match( "load-balance", 2, value )
      || __kmp_str_match( "loadbalance", 2, value )
      || __kmp_str_match( "balance", 1, value ) ) {
        __kmp_global.g.g_dynamic_mode = dynamic_load_balance;
    }
#endif /* USE_LOAD_BALANCE */
    else if ( __kmp_str_match( "thread limit", 1, value )
      || __kmp_str_match( "thread_limit", 1, value )
      || __kmp_str_match( "thread-limit", 1, value )
      || __kmp_str_match( "threadlimit", 1, value )
      || __kmp_str_match( "limit", 2, value ) ) {
        __kmp_global.g.g_dynamic_mode = dynamic_thread_limit;
    }
    else if ( __kmp_str_match( "random", 1, value ) ) {
        __kmp_global.g.g_dynamic_mode = dynamic_random;
    }
    else {
        KMP_WARNING( StgInvalidValue, name, value );
    }
} //__kmp_stg_parse_kmp_dynamic_mode

static void
__kmp_stg_print_kmp_dynamic_mode( kmp_str_buf_t * buffer, char const * name, void * data )
{
#if KMP_DEBUG
    if ( __kmp_global.g.g_dynamic_mode == dynamic_default ) {
        __kmp_str_buf_print( buffer, "   %s: %s \n", name, KMP_I18N_STR( NotDefined ) );
    }
# ifdef USE_LOAD_BALANCE
    else if ( __kmp_global.g.g_dynamic_mode == dynamic_load_balance ) {
        __kmp_stg_print_str( buffer, name, "load balance" );
    }
# endif /* USE_LOAD_BALANCE */
    else if ( __kmp_global.g.g_dynamic_mode == dynamic_thread_limit ) {
        __kmp_stg_print_str( buffer, name, "thread limit" );
    }
    else if ( __kmp_global.g.g_dynamic_mode == dynamic_random ) {
        __kmp_stg_print_str( buffer, name, "random" );
    }
    else {
        KMP_ASSERT(0);
    }
#endif /* KMP_DEBUG */
} // __kmp_stg_print_kmp_dynamic_mode


#ifdef USE_LOAD_BALANCE

// -------------------------------------------------------------------------------------------------
// KMP_LOAD_BALANCE_INTERVAL
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_ld_balance_interval( char const * name, char const * value, void * data )
{
    double interval = __kmp_convert_to_double( value );
    if ( interval >= 0 ) {
        __kmp_load_balance_interval = interval;
    } else {
        KMP_WARNING( StgInvalidValue, name, value );
    }; // if
} // __kmp_stg_parse_load_balance_interval

static void
__kmp_stg_print_ld_balance_interval( kmp_str_buf_t * buffer, char const * name, void * data ) {
#if KMP_DEBUG
    __kmp_str_buf_print( buffer, "   %s=%8.6f\n", name, __kmp_load_balance_interval );
#endif /* KMP_DEBUG */
} // __kmp_stg_print_load_balance_interval

#endif /* USE_LOAD_BALANCE */



// -------------------------------------------------------------------------------------------------
// KMP_INIT_AT_FORK
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_init_at_fork( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_need_register_atfork );
    if ( __kmp_need_register_atfork ) {
        __kmp_need_register_atfork_specified = TRUE;
    };
} // __kmp_stg_parse_init_at_fork

static void
__kmp_stg_print_init_at_fork( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_need_register_atfork_specified );
} // __kmp_stg_print_init_at_fork

// -------------------------------------------------------------------------------------------------
// KMP_SCHEDULE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_schedule( char const * name, char const * value, void * data ) {

    if ( value != NULL ) {
        size_t length = strlen( value );
        if ( length > INT_MAX ) {
            KMP_WARNING( LongValue, name );
        } else {
            char *semicolon;
            if( value[ length - 1 ] == '"' || value[ length -1 ] == '\'' )
                KMP_WARNING( UnbalancedQuotes, name );
            do {
                char sentinel;

                semicolon = (char *) strchr( value, ';' );
                if( *value && semicolon != value ) {
                    char *comma = (char *) strchr( value, ',' );

                    if ( comma ) {
                        ++comma;
                        sentinel = ',';
                    } else
                        sentinel = ';';
                    if ( !__kmp_strcasecmp_with_sentinel( "static", value, sentinel ) ) {
                        if( !__kmp_strcasecmp_with_sentinel( "greedy", comma, ';' ) ) {
                            __kmp_static = kmp_sch_static_greedy;
                            continue;
                        } else if( !__kmp_strcasecmp_with_sentinel( "balanced", comma, ';' ) ) {
                            __kmp_static = kmp_sch_static_balanced;
                            continue;
                        }
                    } else if ( !__kmp_strcasecmp_with_sentinel( "guided", value, sentinel ) ) {
                        if ( !__kmp_strcasecmp_with_sentinel( "iterative", comma, ';' ) ) {
                            __kmp_guided = kmp_sch_guided_iterative_chunked;
                            continue;
                        } else if ( !__kmp_strcasecmp_with_sentinel( "analytical", comma, ';' ) ) {
                            /* analytical not allowed for too many threads */
                            __kmp_guided = kmp_sch_guided_analytical_chunked;
                            continue;
                        }
                    }
                    KMP_WARNING( InvalidClause, name, value );
                } else
                    KMP_WARNING( EmptyClause, name );
            } while ( (value = semicolon ? semicolon + 1 : NULL) );
        }
    }; // if

} // __kmp_stg_parse__schedule

static void
__kmp_stg_print_schedule( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
    } else {
        __kmp_str_buf_print( buffer, "   %s='", name );
    }
    if ( __kmp_static == kmp_sch_static_greedy ) {
        __kmp_str_buf_print( buffer, "%s", "static,greedy");
    } else if ( __kmp_static == kmp_sch_static_balanced ) {
        __kmp_str_buf_print ( buffer, "%s", "static,balanced");
    }
    if ( __kmp_guided == kmp_sch_guided_iterative_chunked ) {
        __kmp_str_buf_print( buffer, ";%s'\n", "guided,iterative");
    } else if ( __kmp_guided == kmp_sch_guided_analytical_chunked ) {
        __kmp_str_buf_print( buffer, ";%s'\n", "guided,analytical");
    }
} // __kmp_stg_print_schedule

// -------------------------------------------------------------------------------------------------
// OMP_SCHEDULE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_omp_schedule( char const * name, char const * value, void * data )
{
    size_t      length;
    if( value ) {
        length = strlen( value );
        if( length ) {
            char *comma = (char *) strchr( value, ',' );
            if( value[ length - 1 ] == '"' || value[ length -1 ] == '\'')
                KMP_WARNING( UnbalancedQuotes, name );
            /* get the specified scheduling style */
            if (!__kmp_strcasecmp_with_sentinel("dynamic", value, ','))          /* DYNAMIC */
                __kmp_sched = kmp_sch_dynamic_chunked;
            else if (!__kmp_strcasecmp_with_sentinel("guided", value, ','))      /* GUIDED */
                __kmp_sched = kmp_sch_guided_chunked;
// AC: TODO: add AUTO schedule, and pprobably remove TRAPEZOIDAL (OMP 3.0 does not allow it)
            else if (!__kmp_strcasecmp_with_sentinel("auto", value, ',')) {       /* AUTO */
                __kmp_sched = kmp_sch_auto;
                if( comma ) {
                    __kmp_msg( kmp_ms_warning, KMP_MSG( IgnoreChunk, name, comma ), __kmp_msg_null );
                    comma = NULL;
                }
            }
            else if (!__kmp_strcasecmp_with_sentinel("trapezoidal", value, ',')) /* TRAPEZOIDAL */
                __kmp_sched = kmp_sch_trapezoidal;
            else if (!__kmp_strcasecmp_with_sentinel("static", value, ','))      /* STATIC */
                __kmp_sched = kmp_sch_static;
#ifdef KMP_STATIC_STEAL_ENABLED
            else if (KMP_ARCH_X86_64 &&
                     !__kmp_strcasecmp_with_sentinel("static_steal", value, ','))
                __kmp_sched = kmp_sch_static_steal;
#endif
            else {
                KMP_WARNING( StgInvalidValue, name, value );
                value = NULL; /* skip processing of comma */
            }
            if( value && comma ) {
                __kmp_env_chunk = TRUE;

                if(__kmp_sched == kmp_sch_static)
                    __kmp_sched = kmp_sch_static_chunked;
                ++comma;
                __kmp_chunk = __kmp_str_to_int( comma, 0 );
                if ( __kmp_chunk < 1 ) {
                    __kmp_chunk = KMP_DEFAULT_CHUNK;
                    __kmp_msg( kmp_ms_warning, KMP_MSG( InvalidChunk, name, comma ), __kmp_msg_null );
                    KMP_INFORM( Using_int_Value, name, __kmp_chunk );
// AC: next block commented out until KMP_DEFAULT_CHUNK != KMP_MIN_CHUNK (to improve code coverage :)
//     The default chunk size is 1 according to standard, thus making KMP_MIN_CHUNK not 1 we would introduce mess:
//     wrong chunk becomes 1, but it will be impossible to explicitely set 1, because it becomes KMP_MIN_CHUNK...
//                } else if ( __kmp_chunk < KMP_MIN_CHUNK ) {
//                    __kmp_chunk = KMP_MIN_CHUNK;
                } else if ( __kmp_chunk > KMP_MAX_CHUNK ) {
                    __kmp_chunk = KMP_MAX_CHUNK;
                    __kmp_msg( kmp_ms_warning, KMP_MSG( LargeChunk, name, comma ), __kmp_msg_null );
                    KMP_INFORM( Using_int_Value, name, __kmp_chunk );
                }
            } else
                __kmp_env_chunk = FALSE;
        } else
            KMP_WARNING( EmptyString, name );
    }
    K_DIAG(1, ("__kmp_static == %d\n", __kmp_static))
    K_DIAG(1, ("__kmp_guided == %d\n", __kmp_guided))
    K_DIAG(1, ("__kmp_sched == %d\n", __kmp_sched))
    K_DIAG(1, ("__kmp_chunk == %d\n", __kmp_chunk))
} // __kmp_stg_parse_omp_schedule

static void
__kmp_stg_print_omp_schedule( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
    } else {
        __kmp_str_buf_print( buffer, "   %s='", name );
    }
    if ( __kmp_chunk ) {
        switch ( __kmp_sched ) {
            case kmp_sch_dynamic_chunked:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "dynamic", __kmp_chunk);
                break;
            case kmp_sch_guided_iterative_chunked:
            case kmp_sch_guided_analytical_chunked:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "guided", __kmp_chunk);
                break;
            case kmp_sch_trapezoidal:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "trapezoidal", __kmp_chunk);
                break;
            case kmp_sch_static:
            case kmp_sch_static_chunked:
            case kmp_sch_static_balanced:
            case kmp_sch_static_greedy:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "static", __kmp_chunk);
                break;
            case kmp_sch_static_steal:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "static_steal", __kmp_chunk);
                break;
            case kmp_sch_auto:
                __kmp_str_buf_print( buffer, "%s,%d'\n", "auto", __kmp_chunk);
                break;
        }
    } else {
        switch ( __kmp_sched ) {
            case kmp_sch_dynamic_chunked:
                __kmp_str_buf_print( buffer, "%s'\n", "dynamic");
                break;
            case kmp_sch_guided_iterative_chunked:
            case kmp_sch_guided_analytical_chunked:
                __kmp_str_buf_print( buffer, "%s'\n", "guided");
                break;
            case kmp_sch_trapezoidal:
                __kmp_str_buf_print( buffer, "%s'\n", "trapezoidal");
                break;
            case kmp_sch_static:
            case kmp_sch_static_chunked:
            case kmp_sch_static_balanced:
            case kmp_sch_static_greedy:
                __kmp_str_buf_print( buffer, "%s'\n", "static");
                break;
            case kmp_sch_static_steal:
                __kmp_str_buf_print( buffer, "%s'\n", "static_steal");
                break;
            case kmp_sch_auto:
                __kmp_str_buf_print( buffer, "%s'\n", "auto");
                break;
        }
    }
} // __kmp_stg_print_omp_schedule

// -------------------------------------------------------------------------------------------------
// KMP_ATOMIC_MODE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_atomic_mode( char const * name, char const * value, void * data ) {
    // Modes: 0 -- do not change default; 1 -- Intel perf mode, 2 -- GOMP compatibility mode.
    int mode = 0;
    int max  = 1;
    #ifdef KMP_GOMP_COMPAT
        max = 2;
    #endif /* KMP_GOMP_COMPAT */
    __kmp_stg_parse_int( name, value, 0, max, & mode );
    // TODO; parse_int is not very suitable for this case. In case of overflow it is better to use
    // 0 rather that max value.
    if ( mode > 0 ) {
        __kmp_atomic_mode = mode;
    }; // if
} // __kmp_stg_parse_atomic_mode

static void
__kmp_stg_print_atomic_mode( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_atomic_mode );
} // __kmp_stg_print_atomic_mode


// -------------------------------------------------------------------------------------------------
// KMP_CONSISTENCY_CHECK
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_consistency_check( char const * name, char const * value, void * data ) {
    if ( ! __kmp_strcasecmp_with_sentinel( "all", value, 0 ) ) {
        // Note, this will not work from kmp_set_defaults because th_cons stack was not allocated
        // for existed thread(s) thus the first __kmp_push_<construct> will break with assertion.
        // TODO: allocate th_cons if called from kmp_set_defaults.
        __kmp_env_consistency_check = TRUE;
    } else if ( ! __kmp_strcasecmp_with_sentinel( "none", value, 0 ) ) {
        __kmp_env_consistency_check = FALSE;
    } else {
        KMP_WARNING( StgInvalidValue, name, value );
    }; // if
} // __kmp_stg_parse_consistency_check

static void
__kmp_stg_print_consistency_check( kmp_str_buf_t * buffer, char const * name, void * data ) {
#if KMP_DEBUG
    const char *value = NULL;

    if ( __kmp_env_consistency_check ) {
        value = "all";
    } else {
        value = "none";
    }

    if ( value != NULL ) {
        __kmp_stg_print_str( buffer, name, value );
    }
#endif /* KMP_DEBUG */
} // __kmp_stg_print_consistency_check


#if USE_ITT_BUILD
// -------------------------------------------------------------------------------------------------
// KMP_ITT_PREPARE_DELAY
// -------------------------------------------------------------------------------------------------

#if USE_ITT_NOTIFY

static void
__kmp_stg_parse_itt_prepare_delay( char const * name, char const * value, void * data )
{
    // Experimental code: KMP_ITT_PREPARE_DELAY specifies numbert of loop iterations.
    int delay = 0;
    __kmp_stg_parse_int( name, value, 0, INT_MAX, & delay );
    __kmp_itt_prepare_delay = delay;
} // __kmp_str_parse_itt_prepare_delay

static void
__kmp_stg_print_itt_prepare_delay( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_uint64( buffer, name, __kmp_itt_prepare_delay );

} // __kmp_str_print_itt_prepare_delay

#endif // USE_ITT_NOTIFY
#endif /* USE_ITT_BUILD */

// -------------------------------------------------------------------------------------------------
// KMP_MALLOC_POOL_INCR
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_malloc_pool_incr( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_size(
            name,
            value,
            KMP_MIN_MALLOC_POOL_INCR,
            KMP_MAX_MALLOC_POOL_INCR,
            NULL,
            & __kmp_malloc_pool_incr,
            1
        );
} // __kmp_stg_parse_malloc_pool_incr

static void
__kmp_stg_print_malloc_pool_incr( kmp_str_buf_t * buffer, char const * name, void * data ) {
       __kmp_stg_print_size( buffer, name, __kmp_malloc_pool_incr );

} // _kmp_stg_print_malloc_pool_incr


#ifdef KMP_DEBUG

// -------------------------------------------------------------------------------------------------
// KMP_PAR_RANGE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_par_range_env( char const * name, char const * value, void * data ) {
        __kmp_stg_parse_par_range(
            name,
            value,
            & __kmp_par_range,
            __kmp_par_range_routine,
            __kmp_par_range_filename,
            & __kmp_par_range_lb,
            & __kmp_par_range_ub
        );
} // __kmp_stg_parse_par_range_env

static void
__kmp_stg_print_par_range_env( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if (__kmp_par_range != 0) {
        __kmp_stg_print_str( buffer, name, par_range_to_print );
    }
} // __kmp_stg_print_par_range_env

// -------------------------------------------------------------------------------------------------
// KMP_YIELD_CYCLE, KMP_YIELD_ON, KMP_YIELD_OFF
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_yield_cycle( char const * name, char const * value, void * data ) {
    int flag = __kmp_yield_cycle;
    __kmp_stg_parse_bool( name, value, & flag );
    __kmp_yield_cycle = flag;
} // __kmp_stg_parse_yield_cycle

static void
__kmp_stg_print_yield_cycle( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_yield_cycle );
} // __kmp_stg_print_yield_cycle

static void
__kmp_stg_parse_yield_on( char const * name, char const * value, void * data ) {
        __kmp_stg_parse_int( name, value, 2, INT_MAX, & __kmp_yield_on_count );
} // __kmp_stg_parse_yield_on

static void
__kmp_stg_print_yield_on( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_yield_on_count );
} // __kmp_stg_print_yield_on

static void
__kmp_stg_parse_yield_off( char const * name, char const * value, void * data ) {
        __kmp_stg_parse_int( name, value, 2, INT_MAX, & __kmp_yield_off_count );
} // __kmp_stg_parse_yield_off

static void
__kmp_stg_print_yield_off( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_yield_off_count );
} // __kmp_stg_print_yield_off

#endif

// -------------------------------------------------------------------------------------------------
// KMP_INIT_WAIT, KMP_NEXT_WAIT
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_init_wait( char const * name, char const * value, void * data ) {
   int wait;
   KMP_ASSERT( ( __kmp_init_wait & 1 ) == 0 );
   wait = __kmp_init_wait / 2;
    __kmp_stg_parse_int( name, value, KMP_MIN_INIT_WAIT, KMP_MAX_INIT_WAIT, & wait );
    __kmp_init_wait = wait * 2;
    KMP_ASSERT( ( __kmp_init_wait & 1 ) == 0 );
    __kmp_yield_init = __kmp_init_wait;
} // __kmp_stg_parse_init_wait

static void
__kmp_stg_print_init_wait( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_init_wait );
} // __kmp_stg_print_init_wait

static void
__kmp_stg_parse_next_wait( char const * name, char const * value, void * data ) {
    int wait;
    KMP_ASSERT( ( __kmp_next_wait & 1 ) == 0 );
    wait = __kmp_next_wait / 2;
    __kmp_stg_parse_int( name, value, KMP_MIN_NEXT_WAIT, KMP_MAX_NEXT_WAIT, & wait );
    __kmp_next_wait = wait * 2;
    KMP_ASSERT( ( __kmp_next_wait & 1 ) == 0 );
    __kmp_yield_next = __kmp_next_wait;
} // __kmp_stg_parse_next_wait

static void
__kmp_stg_print_next_wait( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_next_wait );
} //__kmp_stg_print_next_wait


// -------------------------------------------------------------------------------------------------
// KMP_GTID_MODE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_gtid_mode( char const * name, char const * value, void * data ) {
    //
    // Modes:
    //   0 -- do not change default
    //   1 -- sp search
    //   2 -- use "keyed" TLS var, i.e.
    //        pthread_getspecific(Linux* OS/OS X*) or TlsGetValue(Windows* OS)
    //   3 -- __declspec(thread) TLS var in tdata section
    //
    int mode = 0;
    int max  = 2;
    #ifdef KMP_TDATA_GTID
        max = 3;
    #endif /* KMP_TDATA_GTID */
    __kmp_stg_parse_int( name, value, 0, max, & mode );
    // TODO; parse_int is not very suitable for this case. In case of overflow it is better to use
    // 0 rather that max value.
    if ( mode == 0 ) {
        __kmp_adjust_gtid_mode = TRUE;
    }
    else {
        __kmp_gtid_mode = mode;
        __kmp_adjust_gtid_mode = FALSE;
    }; // if
} // __kmp_str_parse_gtid_mode

static void
__kmp_stg_print_gtid_mode( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if ( __kmp_adjust_gtid_mode ) {
        __kmp_stg_print_int( buffer, name, 0 );
    }
    else {
        __kmp_stg_print_int( buffer, name, __kmp_gtid_mode );
    }
} // __kmp_stg_print_gtid_mode


// -------------------------------------------------------------------------------------------------
// KMP_NUM_LOCKS_IN_BLOCK
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_lock_block( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, 0, KMP_INT_MAX, & __kmp_num_locks_in_block );
} // __kmp_str_parse_lock_block

static void
__kmp_stg_print_lock_block( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_num_locks_in_block );
} // __kmp_stg_print_lock_block

// -------------------------------------------------------------------------------------------------
// KMP_LOCK_KIND
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_lock_kind( char const * name, char const * value, void * data ) {
    if ( __kmp_init_user_locks ) {
        KMP_WARNING( EnvLockWarn, name );
        return;
    }

    if ( __kmp_str_match( "tas", 2, value )
      || __kmp_str_match( "test and set", 2, value )
      || __kmp_str_match( "test_and_set", 2, value )
      || __kmp_str_match( "test-and-set", 2, value )
      || __kmp_str_match( "test andset", 2, value )
      || __kmp_str_match( "test_andset", 2, value )
      || __kmp_str_match( "test-andset", 2, value )
      || __kmp_str_match( "testand set", 2, value )
      || __kmp_str_match( "testand_set", 2, value )
      || __kmp_str_match( "testand-set", 2, value )
      || __kmp_str_match( "testandset", 2, value ) ) {
        __kmp_user_lock_kind = lk_tas;
    }
#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM)
    else if ( __kmp_str_match( "futex", 1, value ) ) {
        if ( __kmp_futex_determine_capable() ) {
            __kmp_user_lock_kind = lk_futex;
        }
        else {
            KMP_WARNING( FutexNotSupported, name, value );
        }
    }
#endif
    else if ( __kmp_str_match( "ticket", 2, value ) ) {
        __kmp_user_lock_kind = lk_ticket;
    }
    else if ( __kmp_str_match( "queuing", 1, value )
      || __kmp_str_match( "queue", 1, value ) ) {
        __kmp_user_lock_kind = lk_queuing;
    }
    else if ( __kmp_str_match( "drdpa ticket", 1, value )
      || __kmp_str_match( "drdpa_ticket", 1, value )
      || __kmp_str_match( "drdpa-ticket", 1, value )
      || __kmp_str_match( "drdpaticket", 1, value )
      || __kmp_str_match( "drdpa", 1, value ) ) {
        __kmp_user_lock_kind = lk_drdpa;
    }
#if KMP_USE_ADAPTIVE_LOCKS
    else if ( __kmp_str_match( "adaptive", 1, value )  ) {
        if( __kmp_cpuinfo.rtm ) { // ??? Is cpuinfo available here?
            __kmp_user_lock_kind = lk_adaptive;
        } else {
            KMP_WARNING( AdaptiveNotSupported, name, value );
            __kmp_user_lock_kind = lk_queuing;
        }
    }
#endif // KMP_USE_ADAPTIVE_LOCKS
    else {
        KMP_WARNING( StgInvalidValue, name, value );
    }
}

static void
__kmp_stg_print_lock_kind( kmp_str_buf_t * buffer, char const * name, void * data ) {
    const char *value = NULL;

    switch ( __kmp_user_lock_kind ) {
        case lk_default:
        value = "default";
        break;

        case lk_tas:
        value = "tas";
        break;

#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)
        case lk_futex:
        value = "futex";
        break;
#endif

        case lk_ticket:
        value = "ticket";
        break;

        case lk_queuing:
        value = "queuing";
        break;

        case lk_drdpa:
        value = "drdpa";
        break;
#if KMP_USE_ADAPTIVE_LOCKS
        case lk_adaptive:
        value = "adaptive";
        break;
#endif
    }

    if ( value != NULL ) {
        __kmp_stg_print_str( buffer, name, value );
    }
}

#if KMP_USE_ADAPTIVE_LOCKS

// -------------------------------------------------------------------------------------------------
// KMP_ADAPTIVE_LOCK_PROPS, KMP_SPECULATIVE_STATSFILE
// -------------------------------------------------------------------------------------------------

// Parse out values for the tunable parameters from a string of the form
// KMP_ADAPTIVE_LOCK_PROPS=max_soft_retries[,max_badness]
static void
__kmp_stg_parse_adaptive_lock_props( const char *name, const char *value, void *data )
{
    int max_retries = 0;
    int max_badness = 0;

    const char *next = value;
    const char *scan = next;

    int total = 0;          // Count elements that were set. It'll be used as an array size
    int prev_comma = FALSE; // For correct processing sequential commas
    int i;

    // Save values in the structure __kmp_speculative_backoff_params
    // Run only 3 iterations because it is enough to read two values or find a syntax error
    for ( i = 0; i < 3 ; i++) {
        SKIP_WS( next );

        if ( *next == '\0' ) {
            break;
        }
        // Next character is not an integer or not a comma OR number of values > 2 => end of list
        if ( ( ( *next < '0' || *next > '9' ) && *next !=',' ) || total > 2 ) {
            KMP_WARNING( EnvSyntaxError, name, value );
            return;
        }
        // The next character is ','
        if ( *next == ',' ) {
            // ',' is the fisrt character
            if ( total == 0 || prev_comma ) {
                total++;
            }
            prev_comma = TRUE;
            next++; //skip ','
            SKIP_WS( next );
        }
        // Next character is a digit
        if ( *next >= '0' && *next <= '9' ) {
            int num;
            const char *buf = next;
            char const * msg  = NULL;
            prev_comma = FALSE;
            SKIP_DIGITS( next );
            total++;

            const char *tmp = next;
            SKIP_WS( tmp );
            if ( ( *next == ' ' || *next == '\t' ) && ( *tmp >= '0' && *tmp <= '9' ) ) {
                KMP_WARNING( EnvSpacesNotAllowed, name, value );
                return;
            }

            num = __kmp_str_to_int( buf, *next );
            if ( num < 1 ) { // The number of retries should be > 0
                msg = KMP_I18N_STR( ValueTooSmall );
                num = 1;
            } else if ( num > KMP_INT_MAX ) {
                msg = KMP_I18N_STR( ValueTooLarge );
                num = KMP_INT_MAX;
            }
            if ( msg != NULL ) {
                // Message is not empty. Print warning.
                KMP_WARNING( ParseSizeIntWarn, name, value, msg );
                KMP_INFORM( Using_int_Value, name, num );
            }
            if( total == 1 ) {
                max_retries = num;
            } else if( total == 2 ) {
                max_badness = num;
            }
        }
    }
    KMP_DEBUG_ASSERT( total > 0 );
    if( total <= 0 ) {
        KMP_WARNING( EnvSyntaxError, name, value );
        return;
    }
    if( max_retries != 0 ) {
        __kmp_adaptive_backoff_params.max_soft_retries = max_retries;
    }
    if( max_badness != 0 ) {
        __kmp_adaptive_backoff_params.max_badness = max_badness;
    }
}


static void
__kmp_stg_print_adaptive_lock_props(kmp_str_buf_t * buffer, char const * name, void * data )
{
    if( __kmp_env_format ) {
        KMP_STR_BUF_PRINT_NAME_EX(name);
    } else {
        __kmp_str_buf_print( buffer, "   %s='", name );
    }
    __kmp_str_buf_print( buffer, "%d,%d'\n", __kmp_adaptive_backoff_params.max_soft_retries,
                         __kmp_adaptive_backoff_params.max_badness );
} // __kmp_stg_print_adaptive_lock_props

#if KMP_DEBUG_ADAPTIVE_LOCKS

static void
__kmp_stg_parse_speculative_statsfile( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_file( name, value, "", & __kmp_speculative_statsfile );
} // __kmp_stg_parse_speculative_statsfile

static void
__kmp_stg_print_speculative_statsfile( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if ( __kmp_str_match( "-", 0, __kmp_speculative_statsfile )  ) {
        __kmp_stg_print_str( buffer, name, "stdout" );
    } else {
        __kmp_stg_print_str( buffer, name, __kmp_speculative_statsfile );
    }

} // __kmp_stg_print_speculative_statsfile

#endif // KMP_DEBUG_ADAPTIVE_LOCKS

#endif // KMP_USE_ADAPTIVE_LOCKS

#if KMP_MIC
// -------------------------------------------------------------------------------------------------
// KMP_PLACE_THREADS
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_place_threads( char const * name, char const * value, void * data ) {
    // Value example: 5Cx2Tx15O
    // Which means "use 5 cores with offset 15, 2 threads per core"

    int         num;
    int         prev_delim = 0;
    const char *next = value;
    const char *prev;

    SKIP_WS( next );
    if ( *next == '\0' ) {
        return;   // leave default values
    }

    // Get num_cores first
    if ( *next >= '0' && *next <= '9' ) {
        prev = next;
        SKIP_DIGITS( next );
        num = __kmp_str_to_int( prev, *next );
        SKIP_WS( next );
        if ( *next == 'C' || *next == 'c' ) {
            __kmp_place_num_cores = num;
            next++;
        } else if ( *next == ',' || *next == 'x' ) {
            __kmp_place_num_cores = num;
            prev_delim = 1;
            next++;
        } else if ( *next == 'T' || *next == 't' ) {
            __kmp_place_num_threads_per_core = num;
            return;   // we ignore offset value in case all cores are used
        } else if ( *next == '\0' ) {
            __kmp_place_num_cores = num;
            return;   // the only value provided
        } else {
            KMP_WARNING( AffThrPlaceInvalid, name, value );
            return;
        }
    } else if ( *next == ',' || *next == 'x' ) {
        // First character is delimiter, skip it, leave num_cores default value
        prev_delim = 2;
        next++;
    } else {
        KMP_WARNING( AffThrPlaceInvalid, name, value );
        return;
    }
    SKIP_WS( next );
    if ( *next == '\0' ) {
        return;   // " n  " - something like this
    }
    if ( ( *next == ',' || *next == 'x' ) && !prev_delim ) {
        prev_delim = 1;
        next++;   // skip delimiter after num_core value
        SKIP_WS( next );
    }

    // Get threads_per_core next
    if ( *next >= '0' && *next <= '9' ) {
        prev_delim = 0;
        prev = next;
        SKIP_DIGITS( next );
        num = __kmp_str_to_int( prev, *next );
        SKIP_WS( next );
        if ( *next == 'T' || *next == 't' ) {
            __kmp_place_num_threads_per_core = num;
            next++;
        } else if ( *next == ',' || *next == 'x' ) {
            __kmp_place_num_threads_per_core = num;
            prev_delim = 1;
            next++;
        } else if ( *next == 'O' || *next == 'o' ) {
            __kmp_place_core_offset = num;
            return;   // threads_per_core remains default
        } else if ( *next == '\0' ) {
            __kmp_place_num_threads_per_core = num;
            return;
        } else {
            KMP_WARNING( AffThrPlaceInvalid, name, value );
            return;
        }
    } else if ( *next == ',' || *next == 'x' ) {
        if ( prev_delim == 2 ) {
            return; // no sense in the only offset value, thus skip the rest
        }
        KMP_DEBUG_ASSERT( prev_delim == 1 );
        next++;     // no value for threads_per_core provided
    } else {
        KMP_WARNING( AffThrPlaceInvalid, name, value );
        return;
    }
    SKIP_WS( next );
    if ( *next == '\0' ) {
        return;   // " nC,mT  " - something like this
    }
    if ( ( *next == ',' || *next == 'x' ) && !prev_delim ) {
        prev_delim = 1;
        next++;   // skip delimiter after threads_per_core value
        SKIP_WS( next );
    }

    // Get core offset last if any,
    // don't bother checking syntax after all data obtained
    if ( *next >= '0' && *next <= '9' ) {
        prev = next;
        SKIP_DIGITS( next );
        num = __kmp_str_to_int( prev, *next );
        __kmp_place_core_offset = num;
    }
}

static void
__kmp_stg_print_place_threads( kmp_str_buf_t * buffer, char const * name, void * data ) {
    if ( __kmp_place_num_cores + __kmp_place_num_threads_per_core ) {
        kmp_str_buf_t buf;
        __kmp_str_buf_init( &buf );
        if( __kmp_env_format ) {
            KMP_STR_BUF_PRINT_NAME_EX(name);
        } else {
            __kmp_str_buf_print( buffer, "   %s='", name );
        }
        __kmp_str_buf_print( &buf, "%dC", __kmp_place_num_cores );
        __kmp_str_buf_print( &buf, "x%dT", __kmp_place_num_threads_per_core );
        if ( __kmp_place_core_offset ) {
            __kmp_str_buf_print( &buf, ",%dO", __kmp_place_core_offset );
        }
        __kmp_str_buf_print(buffer, "%s'\n", buf.str );
        __kmp_str_buf_free(&buf);
/*
    } else {
        __kmp_str_buf_print( buffer, "   %s: %s \n", name, KMP_I18N_STR( NotDefined ) );
*/
    }
}
#endif

#if USE_ITT_BUILD
// -------------------------------------------------------------------------------------------------
// KMP_FORKJOIN_FRAMES
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_forkjoin_frames( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_bool( name, value, & __kmp_forkjoin_frames );
} // __kmp_stg_parse_forkjoin_frames

static void
__kmp_stg_print_forkjoin_frames( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_forkjoin_frames );
} // __kmp_stg_print_forkjoin_frames

// -------------------------------------------------------------------------------------------------
// KMP_FORKJOIN_FRAMES_MODE
// -------------------------------------------------------------------------------------------------

static void
__kmp_stg_parse_forkjoin_frames_mode( char const * name, char const * value, void * data ) {
    __kmp_stg_parse_int( name, value, 0, 3, & __kmp_forkjoin_frames_mode );
} // __kmp_stg_parse_forkjoin_frames

static void
__kmp_stg_print_forkjoin_frames_mode( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_int( buffer, name, __kmp_forkjoin_frames_mode );
} // __kmp_stg_print_forkjoin_frames
#endif /* USE_ITT_BUILD */

// -------------------------------------------------------------------------------------------------
// OMP_DISPLAY_ENV
// -------------------------------------------------------------------------------------------------

#if OMP_40_ENABLED

static void
__kmp_stg_parse_omp_display_env( char const * name, char const * value, void * data )
{
    if ( __kmp_str_match( "VERBOSE", 1, value ) )
    {
        __kmp_display_env_verbose = TRUE;
    } else {
        __kmp_stg_parse_bool( name, value, & __kmp_display_env );
    }

} // __kmp_stg_parse_omp_display_env

static void
__kmp_stg_print_omp_display_env( kmp_str_buf_t * buffer, char const * name, void * data )
{
    if ( __kmp_display_env_verbose )
    {
        __kmp_stg_print_str( buffer, name, "VERBOSE" );
    } else {
        __kmp_stg_print_bool( buffer, name, __kmp_display_env );
    }
} // __kmp_stg_print_omp_display_env

static void
__kmp_stg_parse_omp_cancellation( char const * name, char const * value, void * data ) {
    if ( TCR_4(__kmp_init_parallel) ) {
        KMP_WARNING( EnvParallelWarn, name );
        return;
    }   // read value before first parallel only
    __kmp_stg_parse_bool( name, value, & __kmp_omp_cancellation );
} // __kmp_stg_parse_omp_cancellation

static void
__kmp_stg_print_omp_cancellation( kmp_str_buf_t * buffer, char const * name, void * data ) {
    __kmp_stg_print_bool( buffer, name, __kmp_omp_cancellation );
} // __kmp_stg_print_omp_cancellation

#endif

// -------------------------------------------------------------------------------------------------
// Table.
// -------------------------------------------------------------------------------------------------


static kmp_setting_t __kmp_stg_table[] = {

    { "KMP_ALL_THREADS",                   __kmp_stg_parse_all_threads,        __kmp_stg_print_all_threads,        NULL, 0, 0 },
    { "KMP_BLOCKTIME",                     __kmp_stg_parse_blocktime,          __kmp_stg_print_blocktime,          NULL, 0, 0 },
    { "KMP_DUPLICATE_LIB_OK",              __kmp_stg_parse_duplicate_lib_ok,   __kmp_stg_print_duplicate_lib_ok,   NULL, 0, 0 },
    { "KMP_LIBRARY",                       __kmp_stg_parse_wait_policy,        __kmp_stg_print_wait_policy,        NULL, 0, 0 },
    { "KMP_MAX_THREADS",                   __kmp_stg_parse_all_threads,        NULL,                               NULL, 0, 0 }, // For backward compatibility
    { "KMP_MONITOR_STACKSIZE",             __kmp_stg_parse_monitor_stacksize,  __kmp_stg_print_monitor_stacksize,  NULL, 0, 0 },
    { "KMP_SETTINGS",                      __kmp_stg_parse_settings,           __kmp_stg_print_settings,           NULL, 0, 0 },
    { "KMP_STACKOFFSET",                   __kmp_stg_parse_stackoffset,        __kmp_stg_print_stackoffset,        NULL, 0, 0 },
    { "KMP_STACKSIZE",                     __kmp_stg_parse_stacksize,          __kmp_stg_print_stacksize,          NULL, 0, 0 },
    { "KMP_STACKPAD",                      __kmp_stg_parse_stackpad,           __kmp_stg_print_stackpad,           NULL, 0, 0 },
    { "KMP_VERSION",                       __kmp_stg_parse_version,            __kmp_stg_print_version,            NULL, 0, 0 },
    { "KMP_WARNINGS",                      __kmp_stg_parse_warnings,           __kmp_stg_print_warnings,           NULL, 0, 0 },

    { "OMP_NESTED",                        __kmp_stg_parse_nested,             __kmp_stg_print_nested,             NULL, 0, 0 },
    { "OMP_NUM_THREADS",                   __kmp_stg_parse_num_threads,        __kmp_stg_print_num_threads,        NULL, 0, 0 },
    { "OMP_STACKSIZE",                     __kmp_stg_parse_stacksize,          __kmp_stg_print_stacksize,          NULL, 0, 0 },

    { "KMP_TASKING",                       __kmp_stg_parse_tasking,            __kmp_stg_print_tasking,            NULL, 0, 0 },
    { "KMP_TASK_STEALING_CONSTRAINT",      __kmp_stg_parse_task_stealing,      __kmp_stg_print_task_stealing,      NULL, 0, 0 },
    { "OMP_MAX_ACTIVE_LEVELS",             __kmp_stg_parse_max_active_levels,  __kmp_stg_print_max_active_levels,  NULL, 0, 0 },
    { "OMP_THREAD_LIMIT",                  __kmp_stg_parse_all_threads,        __kmp_stg_print_all_threads,        NULL, 0, 0 },
    { "OMP_WAIT_POLICY",                   __kmp_stg_parse_wait_policy,        __kmp_stg_print_wait_policy,        NULL, 0, 0 },
#if KMP_NESTED_HOT_TEAMS
    { "KMP_HOT_TEAMS_MAX_LEVEL",           __kmp_stg_parse_hot_teams_level,    __kmp_stg_print_hot_teams_level,    NULL, 0, 0 },
    { "KMP_HOT_TEAMS_MODE",                __kmp_stg_parse_hot_teams_mode,     __kmp_stg_print_hot_teams_mode,     NULL, 0, 0 },
#endif // KMP_NESTED_HOT_TEAMS

#if KMP_HANDLE_SIGNALS
    { "KMP_HANDLE_SIGNALS",                __kmp_stg_parse_handle_signals,     __kmp_stg_print_handle_signals,     NULL, 0, 0 },
#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    { "KMP_INHERIT_FP_CONTROL",            __kmp_stg_parse_inherit_fp_control, __kmp_stg_print_inherit_fp_control, NULL, 0, 0 },
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#ifdef KMP_GOMP_COMPAT
    { "GOMP_STACKSIZE",                    __kmp_stg_parse_stacksize,          NULL,                               NULL, 0, 0 },
#endif

#ifdef KMP_DEBUG
    { "KMP_A_DEBUG",                       __kmp_stg_parse_a_debug,            __kmp_stg_print_a_debug,            NULL, 0, 0 },
    { "KMP_B_DEBUG",                       __kmp_stg_parse_b_debug,            __kmp_stg_print_b_debug,            NULL, 0, 0 },
    { "KMP_C_DEBUG",                       __kmp_stg_parse_c_debug,            __kmp_stg_print_c_debug,            NULL, 0, 0 },
    { "KMP_D_DEBUG",                       __kmp_stg_parse_d_debug,            __kmp_stg_print_d_debug,            NULL, 0, 0 },
    { "KMP_E_DEBUG",                       __kmp_stg_parse_e_debug,            __kmp_stg_print_e_debug,            NULL, 0, 0 },
    { "KMP_F_DEBUG",                       __kmp_stg_parse_f_debug,            __kmp_stg_print_f_debug,            NULL, 0, 0 },
    { "KMP_DEBUG",                         __kmp_stg_parse_debug,              NULL, /* no print */                NULL, 0, 0 },
    { "KMP_DEBUG_BUF",                     __kmp_stg_parse_debug_buf,          __kmp_stg_print_debug_buf,          NULL, 0, 0 },
    { "KMP_DEBUG_BUF_ATOMIC",              __kmp_stg_parse_debug_buf_atomic,   __kmp_stg_print_debug_buf_atomic,   NULL, 0, 0 },
    { "KMP_DEBUG_BUF_CHARS",               __kmp_stg_parse_debug_buf_chars,    __kmp_stg_print_debug_buf_chars,    NULL, 0, 0 },
    { "KMP_DEBUG_BUF_LINES",               __kmp_stg_parse_debug_buf_lines,    __kmp_stg_print_debug_buf_lines,    NULL, 0, 0 },
    { "KMP_DIAG",                          __kmp_stg_parse_diag,               __kmp_stg_print_diag,               NULL, 0, 0 },

    { "KMP_PAR_RANGE",                     __kmp_stg_parse_par_range_env,      __kmp_stg_print_par_range_env,      NULL, 0, 0 },
    { "KMP_YIELD_CYCLE",                   __kmp_stg_parse_yield_cycle,        __kmp_stg_print_yield_cycle,        NULL, 0, 0 },
    { "KMP_YIELD_ON",                      __kmp_stg_parse_yield_on,           __kmp_stg_print_yield_on,           NULL, 0, 0 },
    { "KMP_YIELD_OFF",                     __kmp_stg_parse_yield_off,          __kmp_stg_print_yield_off,          NULL, 0, 0 },
#endif // KMP_DEBUG

    { "KMP_ALIGN_ALLOC",                   __kmp_stg_parse_align_alloc,        __kmp_stg_print_align_alloc,        NULL, 0, 0 },

    { "KMP_PLAIN_BARRIER",                 __kmp_stg_parse_barrier_branch_bit, __kmp_stg_print_barrier_branch_bit, NULL, 0, 0 },
    { "KMP_PLAIN_BARRIER_PATTERN",         __kmp_stg_parse_barrier_pattern,    __kmp_stg_print_barrier_pattern,    NULL, 0, 0 },
    { "KMP_FORKJOIN_BARRIER",              __kmp_stg_parse_barrier_branch_bit, __kmp_stg_print_barrier_branch_bit, NULL, 0, 0 },
    { "KMP_FORKJOIN_BARRIER_PATTERN",      __kmp_stg_parse_barrier_pattern,    __kmp_stg_print_barrier_pattern,    NULL, 0, 0 },
#if KMP_FAST_REDUCTION_BARRIER
    { "KMP_REDUCTION_BARRIER",             __kmp_stg_parse_barrier_branch_bit, __kmp_stg_print_barrier_branch_bit, NULL, 0, 0 },
    { "KMP_REDUCTION_BARRIER_PATTERN",     __kmp_stg_parse_barrier_pattern,    __kmp_stg_print_barrier_pattern,    NULL, 0, 0 },
#endif

    { "KMP_ABORT_DELAY",                   __kmp_stg_parse_abort_delay,        __kmp_stg_print_abort_delay,        NULL, 0, 0 },
    { "KMP_CPUINFO_FILE",                  __kmp_stg_parse_cpuinfo_file,       __kmp_stg_print_cpuinfo_file,       NULL, 0, 0 },
    { "KMP_FORCE_REDUCTION",               __kmp_stg_parse_force_reduction,    __kmp_stg_print_force_reduction,    NULL, 0, 0 },
    { "KMP_DETERMINISTIC_REDUCTION",       __kmp_stg_parse_force_reduction,    __kmp_stg_print_force_reduction,    NULL, 0, 0 },
    { "KMP_STORAGE_MAP",                   __kmp_stg_parse_storage_map,        __kmp_stg_print_storage_map,        NULL, 0, 0 },
    { "KMP_ALL_THREADPRIVATE",             __kmp_stg_parse_all_threadprivate,  __kmp_stg_print_all_threadprivate,  NULL, 0, 0 },
    { "KMP_FOREIGN_THREADS_THREADPRIVATE", __kmp_stg_parse_foreign_threads_threadprivate, __kmp_stg_print_foreign_threads_threadprivate,     NULL, 0, 0 },

#if KMP_AFFINITY_SUPPORTED
    { "KMP_AFFINITY",                      __kmp_stg_parse_affinity,           __kmp_stg_print_affinity,           NULL, 0, 0 },
# ifdef KMP_GOMP_COMPAT
    { "GOMP_CPU_AFFINITY",                 __kmp_stg_parse_gomp_cpu_affinity,  NULL, /* no print */                NULL, 0, 0 },
# endif /* KMP_GOMP_COMPAT */
# if OMP_40_ENABLED
    { "OMP_PROC_BIND",                     __kmp_stg_parse_proc_bind,          __kmp_stg_print_proc_bind,          NULL, 0, 0 },
    { "OMP_PLACES",                        __kmp_stg_parse_places,             __kmp_stg_print_places,             NULL, 0, 0 },
# else
    { "OMP_PROC_BIND",                     __kmp_stg_parse_proc_bind,          NULL, /* no print */                NULL, 0, 0 },
# endif /* OMP_40_ENABLED */

    { "KMP_TOPOLOGY_METHOD",               __kmp_stg_parse_topology_method,    __kmp_stg_print_topology_method,    NULL, 0, 0 },

#else

    //
    // KMP_AFFINITY is not supported on OS X*, nor is OMP_PLACES.
    // OMP_PROC_BIND and proc-bind-var are supported, however.
    //
# if OMP_40_ENABLED
    { "OMP_PROC_BIND",                     __kmp_stg_parse_proc_bind,          __kmp_stg_print_proc_bind,          NULL, 0, 0 },
# endif

#endif // KMP_AFFINITY_SUPPORTED

    { "KMP_INIT_AT_FORK",                  __kmp_stg_parse_init_at_fork,       __kmp_stg_print_init_at_fork,       NULL, 0, 0 },
    { "KMP_SCHEDULE",                      __kmp_stg_parse_schedule,           __kmp_stg_print_schedule,           NULL, 0, 0 },
    { "OMP_SCHEDULE",                      __kmp_stg_parse_omp_schedule,       __kmp_stg_print_omp_schedule,       NULL, 0, 0 },
    { "KMP_ATOMIC_MODE",                   __kmp_stg_parse_atomic_mode,        __kmp_stg_print_atomic_mode,        NULL, 0, 0 },
    { "KMP_CONSISTENCY_CHECK",             __kmp_stg_parse_consistency_check,  __kmp_stg_print_consistency_check,  NULL, 0, 0 },

#if USE_ITT_BUILD && USE_ITT_NOTIFY
    { "KMP_ITT_PREPARE_DELAY",             __kmp_stg_parse_itt_prepare_delay,  __kmp_stg_print_itt_prepare_delay,  NULL, 0, 0 },
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
    { "KMP_MALLOC_POOL_INCR",              __kmp_stg_parse_malloc_pool_incr,   __kmp_stg_print_malloc_pool_incr,   NULL, 0, 0 },
    { "KMP_INIT_WAIT",                     __kmp_stg_parse_init_wait,          __kmp_stg_print_init_wait,          NULL, 0, 0 },
    { "KMP_NEXT_WAIT",                     __kmp_stg_parse_next_wait,          __kmp_stg_print_next_wait,          NULL, 0, 0 },
    { "KMP_GTID_MODE",                     __kmp_stg_parse_gtid_mode,          __kmp_stg_print_gtid_mode,          NULL, 0, 0 },
    { "OMP_DYNAMIC",                       __kmp_stg_parse_omp_dynamic,        __kmp_stg_print_omp_dynamic,        NULL, 0, 0 },
    { "KMP_DYNAMIC_MODE",                  __kmp_stg_parse_kmp_dynamic_mode,   __kmp_stg_print_kmp_dynamic_mode,   NULL, 0, 0 },

#ifdef USE_LOAD_BALANCE
    { "KMP_LOAD_BALANCE_INTERVAL",         __kmp_stg_parse_ld_balance_interval,__kmp_stg_print_ld_balance_interval,NULL, 0, 0 },
#endif



    { "KMP_NUM_LOCKS_IN_BLOCK",            __kmp_stg_parse_lock_block,         __kmp_stg_print_lock_block,         NULL, 0, 0 },
    { "KMP_LOCK_KIND",                     __kmp_stg_parse_lock_kind,          __kmp_stg_print_lock_kind,          NULL, 0, 0 },
#if KMP_USE_ADAPTIVE_LOCKS
    { "KMP_ADAPTIVE_LOCK_PROPS",           __kmp_stg_parse_adaptive_lock_props,__kmp_stg_print_adaptive_lock_props,  NULL, 0, 0 },
#if KMP_DEBUG_ADAPTIVE_LOCKS
    { "KMP_SPECULATIVE_STATSFILE",         __kmp_stg_parse_speculative_statsfile,__kmp_stg_print_speculative_statsfile,  NULL, 0, 0 },
#endif
#endif // KMP_USE_ADAPTIVE_LOCKS
#if KMP_MIC
    { "KMP_PLACE_THREADS",                 __kmp_stg_parse_place_threads,      __kmp_stg_print_place_threads,      NULL, 0, 0 },
#endif
#if USE_ITT_BUILD
    { "KMP_FORKJOIN_FRAMES",               __kmp_stg_parse_forkjoin_frames,    __kmp_stg_print_forkjoin_frames,    NULL, 0, 0 },
    { "KMP_FORKJOIN_FRAMES_MODE",          __kmp_stg_parse_forkjoin_frames_mode,__kmp_stg_print_forkjoin_frames_mode,  NULL, 0, 0 },
#endif

# if OMP_40_ENABLED
    { "OMP_DISPLAY_ENV",                   __kmp_stg_parse_omp_display_env,    __kmp_stg_print_omp_display_env,    NULL, 0, 0 },
    { "OMP_CANCELLATION",                  __kmp_stg_parse_omp_cancellation,   __kmp_stg_print_omp_cancellation,   NULL, 0, 0 },
#endif
    { "",                                  NULL,                               NULL,                               NULL, 0, 0 }
}; // settings

static int const __kmp_stg_count = sizeof( __kmp_stg_table ) / sizeof( kmp_setting_t );

static inline
kmp_setting_t *
__kmp_stg_find( char const * name ) {

    int i;
    if ( name != NULL ) {
        for ( i = 0; i < __kmp_stg_count; ++ i ) {
            if ( strcmp( __kmp_stg_table[ i ].name, name ) == 0 ) {
                return & __kmp_stg_table[ i ];
            }; // if
        }; // for
    }; // if
    return NULL;

} // __kmp_stg_find


static int
__kmp_stg_cmp( void const * _a, void const * _b ) {
    kmp_setting_t * a = (kmp_setting_t *) _a;
    kmp_setting_t * b = (kmp_setting_t *) _b;

    //
    // Process KMP_AFFINITY last.
    // It needs to come after OMP_PLACES and GOMP_CPU_AFFINITY.
    //
    if ( strcmp( a->name, "KMP_AFFINITY" ) == 0 ) {
        if ( strcmp( b->name, "KMP_AFFINITY" ) == 0 ) {
            return 0;
        }
        return 1;
    }
    else if ( strcmp( b->name, "KMP_AFFINITY" ) == 0 ) {
        return -1;
    }
    return strcmp( a->name, b->name );
} // __kmp_stg_cmp


static void
__kmp_stg_init( void
) {

    static int initialized = 0;

    if ( ! initialized ) {

        // Sort table.
        qsort( __kmp_stg_table, __kmp_stg_count - 1, sizeof( kmp_setting_t ), __kmp_stg_cmp );

        { // Initialize *_STACKSIZE data.

            kmp_setting_t * kmp_stacksize  = __kmp_stg_find( "KMP_STACKSIZE"  );      // 1st priority.
#ifdef KMP_GOMP_COMPAT
            kmp_setting_t * gomp_stacksize = __kmp_stg_find( "GOMP_STACKSIZE" );      // 2nd priority.
#endif
            kmp_setting_t * omp_stacksize  = __kmp_stg_find( "OMP_STACKSIZE"  );      // 3rd priority.

            // !!! volatile keyword is Intel (R) C Compiler bug CQ49908 workaround.
            // !!! Compiler does not understand rivals is used and optimizes out assignments
            // !!!     rivals[ i ++ ] = ...;
            static kmp_setting_t * volatile rivals[ 4 ];
            static kmp_stg_ss_data_t kmp_data  = {    1, (kmp_setting_t **)rivals };
#ifdef KMP_GOMP_COMPAT
            static kmp_stg_ss_data_t gomp_data = { 1024, (kmp_setting_t **)rivals };
#endif
            static kmp_stg_ss_data_t omp_data  = { 1024, (kmp_setting_t **)rivals };
            int i = 0;

            rivals[ i ++ ] = kmp_stacksize;
#ifdef KMP_GOMP_COMPAT
            if ( gomp_stacksize != NULL ) {
                rivals[ i ++ ] = gomp_stacksize;
            }; // if
#endif
            rivals[ i ++ ] = omp_stacksize;
            rivals[ i ++ ] = NULL;

            kmp_stacksize->data = & kmp_data;
#ifdef KMP_GOMP_COMPAT
            if ( gomp_stacksize != NULL ) {
                gomp_stacksize->data = & gomp_data;
            }; // if
#endif
            omp_stacksize->data = & omp_data;

        }

        { // Initialize KMP_LIBRARY and OMP_WAIT_POLICY data.

            kmp_setting_t * kmp_library     = __kmp_stg_find( "KMP_LIBRARY" );        // 1st priority.
            kmp_setting_t * omp_wait_policy = __kmp_stg_find( "OMP_WAIT_POLICY" );    // 2nd priority.

            // !!! volatile keyword is Intel (R) C Compiler bug CQ49908 workaround.
            static kmp_setting_t * volatile rivals[ 3 ];
            static kmp_stg_wp_data_t kmp_data  = { 0, (kmp_setting_t **)rivals };
            static kmp_stg_wp_data_t omp_data  = { 1, (kmp_setting_t **)rivals };
            int i = 0;

            rivals[ i ++ ] = kmp_library;
            if ( omp_wait_policy != NULL ) {
                rivals[ i ++ ] = omp_wait_policy;
            }; // if
            rivals[ i ++ ] = NULL;

            kmp_library->data  = & kmp_data;
            if ( omp_wait_policy != NULL ) {
                omp_wait_policy->data = & omp_data;
            }; // if

        }

        { // Initialize KMP_ALL_THREADS, KMP_MAX_THREADS, and OMP_THREAD_LIMIT data.

            kmp_setting_t * kmp_all_threads  = __kmp_stg_find( "KMP_ALL_THREADS"  );  // 1st priority.
            kmp_setting_t * kmp_max_threads  = __kmp_stg_find( "KMP_MAX_THREADS"  );  // 2nd priority.
            kmp_setting_t * omp_thread_limit = __kmp_stg_find( "OMP_THREAD_LIMIT" );  // 3rd priority.

            // !!! volatile keyword is Intel (R) C Compiler bug CQ49908 workaround.
            static kmp_setting_t * volatile rivals[ 4 ];
            int i = 0;

            rivals[ i ++ ] = kmp_all_threads;
            rivals[ i ++ ] = kmp_max_threads;
            if ( omp_thread_limit != NULL ) {
                rivals[ i ++ ] = omp_thread_limit;
            }; // if
            rivals[ i ++ ] = NULL;

            kmp_all_threads->data = (void*)& rivals;
            kmp_max_threads->data = (void*)& rivals;
            if ( omp_thread_limit != NULL ) {
                omp_thread_limit->data = (void*)& rivals;
            }; // if

        }

#if KMP_AFFINITY_SUPPORTED
        { // Initialize KMP_AFFINITY, GOMP_CPU_AFFINITY, and OMP_PROC_BIND data.

            kmp_setting_t * kmp_affinity = __kmp_stg_find( "KMP_AFFINITY"  );  // 1st priority.
            KMP_DEBUG_ASSERT( kmp_affinity != NULL );

# ifdef KMP_GOMP_COMPAT
            kmp_setting_t * gomp_cpu_affinity = __kmp_stg_find( "GOMP_CPU_AFFINITY"  );  // 2nd priority.
            KMP_DEBUG_ASSERT( gomp_cpu_affinity != NULL );
# endif

            kmp_setting_t * omp_proc_bind = __kmp_stg_find( "OMP_PROC_BIND" );  // 3rd priority.
            KMP_DEBUG_ASSERT( omp_proc_bind != NULL );

            // !!! volatile keyword is Intel (R) C Compiler bug CQ49908 workaround.
            static kmp_setting_t * volatile rivals[ 4 ];
            int i = 0;

            rivals[ i ++ ] = kmp_affinity;

# ifdef KMP_GOMP_COMPAT
            rivals[ i ++ ] = gomp_cpu_affinity;
            gomp_cpu_affinity->data = (void*)& rivals;
# endif

            rivals[ i ++ ] = omp_proc_bind;
            omp_proc_bind->data = (void*)& rivals;
            rivals[ i ++ ] = NULL;

# if OMP_40_ENABLED
            static kmp_setting_t * volatile places_rivals[ 4 ];
            i = 0;

            kmp_setting_t * omp_places = __kmp_stg_find( "OMP_PLACES" );  // 3rd priority.
            KMP_DEBUG_ASSERT( omp_places != NULL );

            places_rivals[ i ++ ] = kmp_affinity;
#  ifdef KMP_GOMP_COMPAT
            places_rivals[ i ++ ] = gomp_cpu_affinity;
#  endif
            places_rivals[ i ++ ] = omp_places;
            omp_places->data = (void*)& places_rivals;
            places_rivals[ i ++ ] = NULL;
# endif
        }
#else
    // KMP_AFFINITY not supported, so OMP_PROC_BIND has no rivals.
    // OMP_PLACES not supported yet.
#endif // KMP_AFFINITY_SUPPORTED

        { // Initialize KMP_DETERMINISTIC_REDUCTION and KMP_FORCE_REDUCTION data.

            kmp_setting_t * kmp_force_red  = __kmp_stg_find( "KMP_FORCE_REDUCTION" );         // 1st priority.
            kmp_setting_t * kmp_determ_red = __kmp_stg_find( "KMP_DETERMINISTIC_REDUCTION" ); // 2nd priority.

            // !!! volatile keyword is Intel (R) C Compiler bug CQ49908 workaround.
            static kmp_setting_t * volatile rivals[ 3 ];
            static kmp_stg_fr_data_t force_data   = { 1, (kmp_setting_t **)rivals };
            static kmp_stg_fr_data_t determ_data  = { 0, (kmp_setting_t **)rivals };
            int i = 0;

            rivals[ i ++ ] = kmp_force_red;
            if ( kmp_determ_red != NULL ) {
                rivals[ i ++ ] = kmp_determ_red;
            }; // if
            rivals[ i ++ ] = NULL;

            kmp_force_red->data = & force_data;
            if ( kmp_determ_red != NULL ) {
                kmp_determ_red->data  = & determ_data;
            }; // if
        }

        initialized = 1;

    }; // if

    // Reset flags.
    int i;
    for ( i = 0; i < __kmp_stg_count; ++ i ) {
        __kmp_stg_table[ i ].set = 0;
    }; // for

} // __kmp_stg_init


static void
__kmp_stg_parse(
    char const * name,
    char const * value
) {

    // On Windows* OS there are some nameless variables like "C:=C:\" (yeah, really nameless, they are
    // presented in environment block as "=C:=C\\\x00=D:=D:\\\x00...", so let us skip them.
    if ( name[ 0 ] == 0 ) {
        return;
    }; // if

    if ( value != NULL ) {
        kmp_setting_t * setting = __kmp_stg_find( name );
        if ( setting != NULL ) {
            setting->parse( name, value, setting->data );
            setting->defined = 1;
        }; // if
    }; // if

} // __kmp_stg_parse


static int
__kmp_stg_check_rivals(          // 0 -- Ok, 1 -- errors found.
    char const *       name,     // Name of variable.
    char const *       value,    // Value of the variable.
    kmp_setting_t * *  rivals    // List of rival settings (the list must include current one).
) {

    if ( rivals == NULL ) {
        return 0;
    }

    // Loop thru higher priority settings (listed before current).
    int i = 0;
    for ( ; strcmp( rivals[ i ]->name, name ) != 0; i++ ) {
        KMP_DEBUG_ASSERT( rivals[ i ] != NULL );

#if KMP_AFFINITY_SUPPORTED
        if ( rivals[ i ] == __kmp_affinity_notype ) {
            //
            // If KMP_AFFINITY is specified without a type name,
            // it does not rival OMP_PROC_BIND or GOMP_CPU_AFFINITY.
            //
            continue;
        }
#endif

        if ( rivals[ i ]->set ) {
            KMP_WARNING( StgIgnored, name, value, rivals[ i ]->name );
            return 1;
        }; // if
    }; // while

    ++ i; // Skip current setting.
    return 0;

}; // __kmp_stg_check_rivals



static int
__kmp_env_isDefined( char const * name ) {
    int rc = 0;
    kmp_setting_t * setting = __kmp_stg_find( name );
    if ( setting != NULL ) {
        rc = setting->set;
    }; // if
    return rc;
}

static int
__kmp_env_toPrint( char const * name, int flag ) {
    int rc = 0;
    kmp_setting_t * setting = __kmp_stg_find( name );
    if ( setting != NULL ) {
        rc = setting->defined;
        if ( flag >= 0 ) {
            setting->defined = flag;
        }; // if
    }; // if
    return rc;
}


static void
__kmp_aux_env_initialize( kmp_env_blk_t* block ) {

    char const * value;

    /* OMP_NUM_THREADS */
    value = __kmp_env_blk_var( block, "OMP_NUM_THREADS" );
    if ( value ) {
        ompc_set_num_threads( __kmp_dflt_team_nth );
    }

    /* KMP_BLOCKTIME */
    value = __kmp_env_blk_var( block, "KMP_BLOCKTIME" );
    if ( value ) {
        kmpc_set_blocktime( __kmp_dflt_blocktime );
    }

    /* OMP_NESTED */
    value = __kmp_env_blk_var( block, "OMP_NESTED" );
    if ( value ) {
        ompc_set_nested( __kmp_dflt_nested );
    }

    /* OMP_DYNAMIC */
    value = __kmp_env_blk_var( block, "OMP_DYNAMIC" );
    if ( value ) {
        ompc_set_dynamic( __kmp_global.g.g_dynamic );
    }

}

void
__kmp_env_initialize( char const * string ) {

    kmp_env_blk_t block;
    int           i;

    __kmp_stg_init();

    // Hack!!!
    if ( string == NULL ) {
        // __kmp_max_nth = __kmp_sys_max_nth;
        __kmp_threads_capacity = __kmp_initial_threads_capacity( __kmp_dflt_team_nth_ub );
    }; // if
    __kmp_env_blk_init( & block, string );

    //
    // update the set flag on all entries that have an env var
    //
    for ( i = 0; i < block.count; ++ i ) {
        if (( block.vars[ i ].name == NULL )
          || ( *block.vars[ i ].name == '\0')) {
            continue;
        }
        if ( block.vars[ i ].value == NULL ) {
            continue;
        }
        kmp_setting_t * setting = __kmp_stg_find( block.vars[ i ].name );
        if ( setting != NULL ) {
            setting->set = 1;
        }
    }; // for i

    // Special case. If we parse environment, not a string, process KMP_WARNINGS first.
    if ( string == NULL ) {
        char const * name  = "KMP_WARNINGS";
        char const * value = __kmp_env_blk_var( & block, name );
        __kmp_stg_parse( name, value );
    }; // if

#if KMP_AFFINITY_SUPPORTED
    //
    // Special case. KMP_AFFINITY is not a rival to other affinity env vars
    // if no affinity type is specified.  We want to allow
    // KMP_AFFINITY=[no],verbose/[no]warnings/etc.  to be enabled when
    // specifying the affinity type via GOMP_CPU_AFFINITY or the OMP 4.0
    // affinity mechanism.
    //
    __kmp_affinity_notype = NULL;
    char const *aff_str = __kmp_env_blk_var( & block, "KMP_AFFINITY" );
    if ( aff_str != NULL ) {
        //
        // Check if the KMP_AFFINITY type is specified in the string.
        // We just search the string for "compact", "scatter", etc.
        // without really parsing the string.  The syntax of the
        // KMP_AFFINITY env var is such that none of the affinity
        // type names can appear anywhere other that the type
        // specifier, even as substrings.
        //
        // I can't find a case-insensitive version of strstr on Windows* OS.
        // Use the case-sensitive version for now.
        //

# if KMP_OS_WINDOWS
#  define FIND strstr
# else
#  define FIND strcasestr
# endif

        if ( ( FIND( aff_str, "none" ) == NULL )
          && ( FIND( aff_str, "physical" ) == NULL )
          && ( FIND( aff_str, "logical" ) == NULL )
          && ( FIND( aff_str, "compact" ) == NULL )
          && ( FIND( aff_str, "scatter" ) == NULL )
          && ( FIND( aff_str, "explicit" ) == NULL )
# if KMP_MIC
          && ( FIND( aff_str, "balanced" ) == NULL )
# endif
          && ( FIND( aff_str, "disabled" ) == NULL ) ) {
            __kmp_affinity_notype = __kmp_stg_find( "KMP_AFFINITY"  );
        }
        else {
            //
            // A new affinity type is specified.
            // Reset the affinity flags to their default values,
            // in case this is called from kmp_set_defaults().
            //
            __kmp_affinity_type = affinity_default;
            __kmp_affinity_gran = affinity_gran_default;
            __kmp_affinity_top_method = affinity_top_method_default;
            __kmp_affinity_respect_mask = affinity_respect_mask_default;
        }
# undef FIND

#if OMP_40_ENABLED
        //
        // Also reset the affinity flags if OMP_PROC_BIND is specified.
        //
        aff_str = __kmp_env_blk_var( & block, "OMP_PROC_BIND" );
        if ( aff_str != NULL ) {
            __kmp_affinity_type = affinity_default;
            __kmp_affinity_gran = affinity_gran_default;
            __kmp_affinity_top_method = affinity_top_method_default;
            __kmp_affinity_respect_mask = affinity_respect_mask_default;
        }
#endif /* OMP_40_ENABLED */
    }

#endif /* KMP_AFFINITY_SUPPORTED */

#if OMP_40_ENABLED
    //
    // Set up the nested proc bind type vector.
    //
    if ( __kmp_nested_proc_bind.bind_types == NULL ) {
        __kmp_nested_proc_bind.bind_types = (kmp_proc_bind_t *)
          KMP_INTERNAL_MALLOC( sizeof(kmp_proc_bind_t) );
        if ( __kmp_nested_proc_bind.bind_types == NULL ) {
            KMP_FATAL( MemoryAllocFailed );
        }
        __kmp_nested_proc_bind.size = 1;
        __kmp_nested_proc_bind.used = 1;
        __kmp_nested_proc_bind.bind_types[0] = proc_bind_default;
    }
#endif /* OMP_40_ENABLED */

    //
    // Now process all of the settings.
    //
    for ( i = 0; i < block.count; ++ i ) {
        __kmp_stg_parse( block.vars[ i ].name, block.vars[ i ].value );
    }; // for i

    //
    // If user locks have been allocated yet, don't reset the lock vptr table.
    //
    if ( ! __kmp_init_user_locks ) {
        if ( __kmp_user_lock_kind == lk_default ) {
            __kmp_user_lock_kind = lk_queuing;
        }
        __kmp_set_user_lock_vptrs( __kmp_user_lock_kind );
    }
    else {
        KMP_DEBUG_ASSERT( string != NULL); // kmp_set_defaults() was called
        KMP_DEBUG_ASSERT( __kmp_user_lock_kind != lk_default );
        __kmp_set_user_lock_vptrs( __kmp_user_lock_kind );
        // Binds lock functions again to follow the transition between different
        // KMP_CONSISTENCY_CHECK values. Calling this again is harmless as long
        // as we do not allow lock kind changes after making a call to any
        // user lock functions (true).
    }

#if KMP_AFFINITY_SUPPORTED

    if ( ! TCR_4(__kmp_init_middle) ) {
        //
        // Determine if the machine/OS is actually capable of supporting
        // affinity.
        //
        const char *var = "KMP_AFFINITY";
        if ( __kmp_affinity_type == affinity_disabled ) {
            __kmp_affin_mask_size = 0;  // should already be 0
        }
        else if ( ! KMP_AFFINITY_CAPABLE() ) {
            __kmp_affinity_determine_capable( var );
            if ( ! KMP_AFFINITY_CAPABLE() ) {
                if ( __kmp_affinity_verbose || ( __kmp_affinity_warnings
                  && ( __kmp_affinity_type != affinity_default )
                  && ( __kmp_affinity_type != affinity_none )
                  && ( __kmp_affinity_type != affinity_disabled ) ) ) {
                    KMP_WARNING( AffNotSupported, var );
                }
                __kmp_affinity_type = affinity_disabled;
                __kmp_affinity_respect_mask = 0;
                __kmp_affinity_gran = affinity_gran_fine;
            }
        }

# if OMP_40_ENABLED
        if ( __kmp_affinity_type == affinity_disabled )  {
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_disabled;
        }
        else if ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_true ) {
            //
            // OMP_PROC_BIND=true maps to OMP_PROC_BIND=spread.
            //
            __kmp_nested_proc_bind.bind_types[0] = proc_bind_spread;
        }
# endif /* OMP_40_ENABLED */

        if ( KMP_AFFINITY_CAPABLE() ) {

# if KMP_OS_WINDOWS && KMP_ARCH_X86_64

            //
            // Handle the Win 64 group affinity stuff if there are multiple
            // processor groups, or if the user requested it, and OMP 4.0
            // affinity is not in effect.
            //
            if ( ( ( __kmp_num_proc_groups > 1 )
              && ( __kmp_affinity_type == affinity_default )
#  if OMP_40_ENABLED
              && ( __kmp_nested_proc_bind.bind_types[0] == proc_bind_default ) )
#  endif
              || ( __kmp_affinity_top_method == affinity_top_method_group ) ) {
                if ( __kmp_affinity_respect_mask == affinity_respect_mask_default ) {
                    __kmp_affinity_respect_mask = FALSE;
                }
                if ( __kmp_affinity_type == affinity_default ) {
                    __kmp_affinity_type = affinity_compact;
#  if OMP_40_ENABLED
                    __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
#  endif
                }
                if ( __kmp_affinity_top_method == affinity_top_method_default ) {
                    if ( __kmp_affinity_gran == affinity_gran_default ) {
                        __kmp_affinity_top_method = affinity_top_method_group;
                        __kmp_affinity_gran = affinity_gran_group;
                    }
                    else if ( __kmp_affinity_gran == affinity_gran_group ) {
                        __kmp_affinity_top_method = affinity_top_method_group;
                    }
                    else {
                        __kmp_affinity_top_method = affinity_top_method_all;
                    }
                }
                else if ( __kmp_affinity_top_method == affinity_top_method_group ) {
                    if ( __kmp_affinity_gran == affinity_gran_default ) {
                        __kmp_affinity_gran = affinity_gran_group;
                    }
                    else if ( ( __kmp_affinity_gran != affinity_gran_group )
                      && ( __kmp_affinity_gran != affinity_gran_fine )
                      && ( __kmp_affinity_gran != affinity_gran_thread ) ) {
                        char *str = NULL;
                        switch ( __kmp_affinity_gran ) {
                            case affinity_gran_core: str = "core"; break;
                            case affinity_gran_package: str = "package"; break;
                            case affinity_gran_node: str = "node"; break;
                            default: KMP_DEBUG_ASSERT( 0 );
                        }
                        KMP_WARNING( AffGranTopGroup, var, str );
                        __kmp_affinity_gran = affinity_gran_fine;
                    }
                }
                else {
                    if ( __kmp_affinity_gran == affinity_gran_default ) {
                        __kmp_affinity_gran = affinity_gran_core;
                    }
                    else if ( __kmp_affinity_gran == affinity_gran_group ) {
                        char *str = NULL;
                        switch ( __kmp_affinity_type ) {
                            case affinity_physical: str = "physical"; break;
                            case affinity_logical: str = "logical"; break;
                            case affinity_compact: str = "compact"; break;
                            case affinity_scatter: str = "scatter"; break;
                            case affinity_explicit: str = "explicit"; break;
                            // No MIC on windows, so no affinity_balanced case
                            default: KMP_DEBUG_ASSERT( 0 );
                        }
                        KMP_WARNING( AffGranGroupType, var, str );
                        __kmp_affinity_gran = affinity_gran_core;
                    }
                }
            }
            else

# endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */

            {
                if ( __kmp_affinity_respect_mask == affinity_respect_mask_default ) {
# if KMP_OS_WINDOWS && KMP_ARCH_X86_64
                    if ( __kmp_num_proc_groups > 1 ) {
                        __kmp_affinity_respect_mask = FALSE;
                    }
                    else
# endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
                    {
                        __kmp_affinity_respect_mask = TRUE;
                    }
                }
# if OMP_40_ENABLED
                if ( ( __kmp_nested_proc_bind.bind_types[0] != proc_bind_intel )
                  && ( __kmp_nested_proc_bind.bind_types[0] != proc_bind_default ) ) {
                    if ( __kmp_affinity_type == affinity_default ) {
                        __kmp_affinity_type = affinity_compact;
                        __kmp_affinity_dups = FALSE;
                    }
                }
                else
# endif /* OMP_40_ENABLED */
                if ( __kmp_affinity_type == affinity_default ) {
# if KMP_MIC
                    __kmp_affinity_type = affinity_scatter;
#  if OMP_40_ENABLED
                    __kmp_nested_proc_bind.bind_types[0] = proc_bind_intel;
#  endif
# else
                    __kmp_affinity_type = affinity_none;
#  if OMP_40_ENABLED
                    __kmp_nested_proc_bind.bind_types[0] = proc_bind_false;
#  endif
# endif
                }
                if ( ( __kmp_affinity_gran == affinity_gran_default )
                  &&  ( __kmp_affinity_gran_levels < 0 ) ) {
# if KMP_MIC
                    __kmp_affinity_gran = affinity_gran_fine;
# else
                    __kmp_affinity_gran = affinity_gran_core;
# endif
                }
                if ( __kmp_affinity_top_method == affinity_top_method_default ) {
                    __kmp_affinity_top_method = affinity_top_method_all;
                }
            }
        }

        K_DIAG( 1, ( "__kmp_affinity_type         == %d\n", __kmp_affinity_type         ) );
        K_DIAG( 1, ( "__kmp_affinity_compact      == %d\n", __kmp_affinity_compact      ) );
        K_DIAG( 1, ( "__kmp_affinity_offset       == %d\n", __kmp_affinity_offset       ) );
        K_DIAG( 1, ( "__kmp_affinity_verbose      == %d\n", __kmp_affinity_verbose      ) );
        K_DIAG( 1, ( "__kmp_affinity_warnings     == %d\n", __kmp_affinity_warnings     ) );
        K_DIAG( 1, ( "__kmp_affinity_respect_mask == %d\n", __kmp_affinity_respect_mask ) );
        K_DIAG( 1, ( "__kmp_affinity_gran         == %d\n", __kmp_affinity_gran         ) );

        KMP_DEBUG_ASSERT( __kmp_affinity_type != affinity_default);
# if OMP_40_ENABLED
        KMP_DEBUG_ASSERT( __kmp_nested_proc_bind.bind_types[0]
          != proc_bind_default );
# endif
    }

#endif /* KMP_AFFINITY_SUPPORTED */

    if ( __kmp_version ) {
        __kmp_print_version_1();
    }; // if

    // Post-initialization step: some env. vars need their value's further processing
    if ( string != NULL) { // kmp_set_defaults() was called
        __kmp_aux_env_initialize( &block );
    }

    __kmp_env_blk_free( & block );

    KMP_MB();

} // __kmp_env_initialize


void
__kmp_env_print() {

    kmp_env_blk_t block;
    int           i;
    kmp_str_buf_t buffer;

    __kmp_stg_init();
    __kmp_str_buf_init( & buffer );

    __kmp_env_blk_init( & block, NULL );
    __kmp_env_blk_sort( & block );

    // Print real environment values.
    __kmp_str_buf_print( & buffer, "\n%s\n\n", KMP_I18N_STR( UserSettings )  );
    for ( i = 0; i < block.count; ++ i ) {
        char const * name  = block.vars[ i ].name;
        char const * value = block.vars[ i ].value;
        if (
            ( strlen( name ) > 4 && strncmp( name, "KMP_", 4 ) == 0 )
            || strncmp( name, "OMP_", 4 ) == 0
            #ifdef KMP_GOMP_COMPAT
                || strncmp( name, "GOMP_", 5 ) == 0
            #endif // KMP_GOMP_COMPAT
        ) {
            __kmp_str_buf_print( & buffer, "   %s=%s\n", name, value );
        }; // if
    }; // for
    __kmp_str_buf_print( & buffer, "\n" );

    // Print internal (effective) settings.
    __kmp_str_buf_print( & buffer, "%s\n\n", KMP_I18N_STR( EffectiveSettings ) );
    for ( int i = 0; i < __kmp_stg_count; ++ i ) {
        if (  __kmp_stg_table[ i ].print != NULL ) {
            __kmp_stg_table[ i ].print( & buffer, __kmp_stg_table[ i ].name, __kmp_stg_table[ i ].data );
        }; // if
    }; // for

    __kmp_printf( "%s", buffer.str );

    __kmp_env_blk_free( & block );
    __kmp_str_buf_free( & buffer );

    __kmp_printf("\n");

} // __kmp_env_print


#if OMP_40_ENABLED
void
__kmp_env_print_2() {

    kmp_env_blk_t block;
    int           i;
    kmp_str_buf_t buffer;

    __kmp_env_format = 1;

    __kmp_stg_init();
    __kmp_str_buf_init( & buffer );

    __kmp_env_blk_init( & block, NULL );
    __kmp_env_blk_sort( & block );

    __kmp_str_buf_print( & buffer, "\n%s\n", KMP_I18N_STR( DisplayEnvBegin )  );
    __kmp_str_buf_print( & buffer, "   _OPENMP='%d'\n", __kmp_openmp_version );

    for ( int i = 0; i < __kmp_stg_count; ++ i ) {
        if (  __kmp_stg_table[ i ].print != NULL &&
              ( ( __kmp_display_env && strncmp( __kmp_stg_table[ i ].name, "OMP_", 4 ) == 0 ) || __kmp_display_env_verbose ) ) {
            __kmp_stg_table[ i ].print( & buffer, __kmp_stg_table[ i ].name, __kmp_stg_table[ i ].data );
        }; // if
    }; // for

    __kmp_str_buf_print( & buffer, "%s\n", KMP_I18N_STR( DisplayEnvEnd )  );
    __kmp_str_buf_print( & buffer, "\n" );

    __kmp_printf( "%s", buffer.str );

    __kmp_env_blk_free( & block );
    __kmp_str_buf_free( & buffer );

    __kmp_printf("\n");

} // __kmp_env_print_2
#endif // OMP_40_ENABLED



// end of file

