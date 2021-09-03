/* EVMC: Ethereum Client-VM Connector API.
 * Copyright 2018-2019 The EVMC Authors.
 * Licensed under the Apache License, Version 2.0.
 */

/**
 * EVMC Helpers
 *
 * A collection of C helper functions for invoking a VM instance methods.
 * These are convenient for languages where invoking function pointers
 * is "ugly" or impossible (such as Go).
 *
 * It also contains helpers (overloaded operators) for using EVMC types effectively in C++.
 *
 * @defgroup helpers EVMC Helpers
 * @{
 */
#pragma once

#include <evmc/evmc.h>
#include <stdlib.h>
#include <string.h>

#ifndef __POLYJUICE_FAST_MEMSET
#define __POLYJUICE_FAST_MEMSET
// The faster version of memset & memcpy implementations used here are from
// the awesome musl libc project: https://www.musl-libc.org/
void *fast_memset(void *dest, int c, size_t n)
{
	unsigned char *s = (unsigned char *)dest;
	size_t k;

	/* Fill head and tail with minimal branching. Each
	 * conditional ensures that all the subsequently used
	 * offsets are well-defined and in the dest region. */

	if (!n) return dest;
	s[0] = c;
	s[n-1] = c;
	if (n <= 2) return dest;
	s[1] = c;
	s[2] = c;
	s[n-2] = c;
	s[n-3] = c;
	if (n <= 6) return dest;
	s[3] = c;
	s[n-4] = c;
	if (n <= 8) return dest;

	/* Advance pointer to align it at a 4-byte boundary,
	 * and truncate n to a multiple of 4. The previous code
	 * already took care of any head/tail that get cut off
	 * by the alignment. */

	k = -(uintptr_t)s & 3;
	s += k;
	n -= k;
	n &= -4;

#ifdef __GNUC__
	typedef uint32_t __attribute__((__may_alias__)) u32;
	typedef uint64_t __attribute__((__may_alias__)) u64;

	u32 c32 = ((u32)-1)/255 * (unsigned char)c;

	/* In preparation to copy 32 bytes at a time, aligned on
	 * an 8-byte bounary, fill head/tail up to 28 bytes each.
	 * As in the initial byte-based head/tail fill, each
	 * conditional below ensures that the subsequent offsets
	 * are valid (e.g. !(n<=24) implies n>=28). */

	*(u32 *)(s+0) = c32;
	*(u32 *)(s+n-4) = c32;
	if (n <= 8) return dest;
	*(u32 *)(s+4) = c32;
	*(u32 *)(s+8) = c32;
	*(u32 *)(s+n-12) = c32;
	*(u32 *)(s+n-8) = c32;
	if (n <= 24) return dest;
	*(u32 *)(s+12) = c32;
	*(u32 *)(s+16) = c32;
	*(u32 *)(s+20) = c32;
	*(u32 *)(s+24) = c32;
	*(u32 *)(s+n-28) = c32;
	*(u32 *)(s+n-24) = c32;
	*(u32 *)(s+n-20) = c32;
	*(u32 *)(s+n-16) = c32;

	/* Align to a multiple of 8 so we can fill 64 bits at a time,
	 * and avoid writing the same bytes twice as much as is
	 * practical without introducing additional branching. */

	k = 24 + ((uintptr_t)s & 4);
	s += k;
	n -= k;

	/* If this loop is reached, 28 tail bytes have already been
	 * filled, so any remainder when n drops below 32 can be
	 * safely ignored. */

	u64 c64 = c32 | ((u64)c32 << 32);
	for (; n >= 32; n-=32, s+=32) {
		*(u64 *)(s+0) = c64;
		*(u64 *)(s+8) = c64;
		*(u64 *)(s+16) = c64;
		*(u64 *)(s+24) = c64;
	}
#else
	/* Pure C fallback with no aliasing violations. */
	for (; n; n--, s++) *s = c;
#endif

	return dest;
}
#endif

/**
 * Returns true if the VM has a compatible ABI version.
 */
static inline bool evmc_is_abi_compatible(struct evmc_vm* vm)
{
    return vm->abi_version == EVMC_ABI_VERSION;
}

/**
 * Returns the name of the VM.
 */
static inline const char* evmc_vm_name(struct evmc_vm* vm)
{
    return vm->name;
}

/**
 * Returns the version of the VM.
 */
static inline const char* evmc_vm_version(struct evmc_vm* vm)
{
    return vm->version;
}

/**
 * Checks if the VM has the given capability.
 *
 * @see evmc_get_capabilities_fn
 */
static inline bool evmc_vm_has_capability(struct evmc_vm* vm, enum evmc_capabilities capability)
{
    return (vm->get_capabilities(vm) & (evmc_capabilities_flagset)capability) != 0;
}

/**
 * Destroys the VM instance.
 *
 * @see evmc_destroy_fn
 */
static inline void evmc_destroy(struct evmc_vm* vm)
{
    vm->destroy(vm);
}

/**
 * Sets the option for the VM, if the feature is supported by the VM.
 *
 * @see evmc_set_option_fn
 */
static inline enum evmc_set_option_result evmc_set_option(struct evmc_vm* vm,
                                                          char const* name,
                                                          char const* value)
{
    if (vm->set_option)
        return vm->set_option(vm, name, value);
    return EVMC_SET_OPTION_INVALID_NAME;
}

/**
 * Executes code in the VM instance.
 *
 * @see evmc_execute_fn.
 */
static inline struct evmc_result evmc_execute(struct evmc_vm* vm,
                                              const struct evmc_host_interface* host,
                                              struct evmc_host_context* context,
                                              enum evmc_revision rev,
                                              const struct evmc_message* msg,
                                              uint8_t const* code,
                                              size_t code_size)
{
    return vm->execute(vm, host, context, rev, msg, code, code_size);
}

/// The evmc_result release function using free() for releasing the memory.
///
/// This function is used in the evmc_make_result(),
/// but may be also used in other case if convenient.
///
/// @param result The result object.
static void evmc_free_result_memory(const struct evmc_result* result)
{
    free((uint8_t*)result->output_data);
}

/// Creates the result from the provided arguments.
///
/// The provided output is copied to memory allocated with malloc()
/// and the evmc_result::release function is set to one invoking free().
///
/// In case of memory allocation failure, the result has all fields zeroed
/// and only evmc_result::status_code is set to ::EVMC_OUT_OF_MEMORY internal error.
///
/// @param status_code  The status code.
/// @param gas_left     The amount of gas left.
/// @param output_data  The pointer to the output.
/// @param output_size  The output size.
static inline struct evmc_result evmc_make_result(enum evmc_status_code status_code,
                                                  int64_t gas_left,
                                                  const uint8_t* output_data,
                                                  size_t output_size)
{
    struct evmc_result result;
    memset(&result, 0, sizeof(result));

    if (output_size != 0)
    {
        uint8_t* buffer = (uint8_t*)malloc(output_size);

        if (!buffer)
        {
            result.status_code = EVMC_OUT_OF_MEMORY;
            return result;
        }

        memcpy(buffer, output_data, output_size);
        result.output_data = buffer;
        result.output_size = output_size;
        result.release = evmc_free_result_memory;
    }

    result.status_code = status_code;
    result.gas_left = gas_left;
    return result;
}

/**
 * Releases the resources allocated to the execution result.
 *
 * @param result  The result object to be released. MUST NOT be NULL.
 *
 * @see evmc_result::release() evmc_release_result_fn
 */
static inline void evmc_release_result(struct evmc_result* result)
{
    if (result->release)
        result->release(result);
}


/**
 * Helpers for optional storage of evmc_result.
 *
 * In some contexts (i.e. evmc_result::create_address is unused) objects of
 * type evmc_result contains a memory storage that MAY be used by the object
 * owner. This group defines helper types and functions for accessing
 * the optional storage.
 *
 * @defgroup result_optional_storage Result Optional Storage
 * @{
 */

/**
 * The union representing evmc_result "optional storage".
 *
 * The evmc_result struct contains 24 bytes of optional storage that can be
 * reused by the object creator if the object does not contain
 * evmc_result::create_address.
 *
 * A VM implementation MAY use this memory to keep additional data
 * when returning result from evmc_execute_fn().
 * The host application MAY use this memory to keep additional data
 * when returning result of performed calls from evmc_call_fn().
 *
 * @see evmc_get_optional_storage(), evmc_get_const_optional_storage().
 */
union evmc_result_optional_storage
{
    uint8_t bytes[24]; /**< 24 bytes of optional storage. */
    void* pointer;     /**< Optional pointer. */
};

/** Provides read-write access to evmc_result "optional storage". */
static inline union evmc_result_optional_storage* evmc_get_optional_storage(
    struct evmc_result* result)
{
    return (union evmc_result_optional_storage*)&result->create_address;
}

/** Provides read-only access to evmc_result "optional storage". */
static inline const union evmc_result_optional_storage* evmc_get_const_optional_storage(
    const struct evmc_result* result)
{
    return (const union evmc_result_optional_storage*)&result->create_address;
}

/** @} */

/** @} */
