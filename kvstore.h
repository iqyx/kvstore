/*
 * A key-value store for embedded environments
 *
 * Copyright (c) 2016, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


/**
 * Default configuration parameters.
 */
#ifndef KVSTORE_MAX_KEY_SIZE
#define KVSTORE_MAX_KEY_SIZE 16
#endif

#ifndef KVSTORE_KEY_SIZE_BYTES
#define KVSTORE_KEY_SIZE_BYTES 4
#endif

#ifndef KVSTORE_VALUE_SIZE_BYTES
#define KVSTORE_VALUE_SIZE_BYTES 4
#endif

#ifndef KVSTORE_HEADER_SIZE_BYTES
#define KVSTORE_HEADER_SIZE_BYTES 4
#endif

#ifndef KVSTORE_HEADER_MAGIC
#define KVSTORE_HEADER_MAGIC {0xf8, 0x2a, 0x93, 0x11}
#endif


typedef enum kvstore_result {
	KVSTORE_OK = 0,
	KVSTORE_FAILED = -1,
	KVSTORE_BAD_ARG = -2,
	KVSTORE_NOT_FOUND = -3,
} KvStoreResult;

/**
 * Function callbacks to access the backend storage.
 */
struct kvstore_backend {
	/** @todo Is size_t appropriate here? */
	KvStoreResult (*read)(void *context, uint8_t *buf, size_t pos, size_t size);
	KvStoreResult (*write)(void *context, const uint8_t *buf, size_t pos, size_t size);
	size_t (*get_size)(void *context);
};

typedef struct kvstore_cursor {
	size_t position;
	uint8_t key[KVSTORE_MAX_KEY_SIZE];
	size_t key_size;
} KvStoreCursor;

typedef struct kvstore {
	const struct kvstore_backend *backend;
	void *backend_context;
} KvStore;


/**
 * @brief Initialize the KvStore instance
 *
 * This function must be called before any other API function can be used.
 * Backend callback functions must be properly set and ready to be called
 * (that means that the backend memory must be allocated or a file opened).
 *
 * @param self The KvStore instance to initialize. Cannot be NULL.
 * @param backend Pointer to a structure with function pointers used
 *                to access the backend storage. Cannot be NULL.
 * @param backend_context A void pointer passed to every call to a backend
 *                        storage callback function.
 * @return KVSTORE_OK on success,
 *         KVSTORE_BAD_ARG if a function parameter is invalid,
 *         KVSTORE_FAILED otherwise.
 */
KvStoreResult kvstore_init(KvStore *self, const struct kvstore_backend *backend, void *backend_context);

/**
 * @brief Free the KvStore instance
 *
 * @param self The KvStore instance. Cannot be NULL.
 */
KvStoreResult kvstore_free(KvStore *self);

/**
 * @brief Prepare a new key-value store
 */
KvStoreResult kvstore_prepare(KvStore *self);

/**
 * @brief Save a key-value pair
 *
 * Save a value with size value_size to a key-value store instance. The value can be
 * found later by a key with size key_size.
 *
 * @param self The KvStore instance. Cannot be NULL.
 * @param key The key associated with the key-value pair. Cannot be NULL.
 * @param key_size Size of the @p key. Must be non-zero.
 * @param value The value to save. Cannot be NULL.
 * @param value_size Size of the value to save. Must be non-zero.
 *
 * @return KVSTORE_OK on success,
 *         KVSTORE_BAD_ARG if a function parameter is invalid,
 *         KVSTORE_FAILED otherwise.
 */
KvStoreResult kvstore_put(KvStore *self, const uint8_t *key, size_t key_size, const uint8_t *value, size_t value_size);

/* Initialize the cursor and find the next value with the coresponding key. */
KvStoreResult kvstore_search(KvStore *self, KvStoreCursor *cursor, const uint8_t *key, size_t key_size);

/* Start searching from the actual cursor position and find the next value with the corresponding key. */
KvStoreResult kvstore_search_next(KvStore *self, KvStoreCursor *cursor);

/* Advance to the next non-empty key-value pair without checking the key. */
KvStoreResult kvstore_advance(KvStore *self, KvStoreCursor *cursor);

/* Read the value at the current cursor position. */
KvStoreResult kvstore_get(KvStore *self, KvStoreCursor *cursor, uint8_t *value, size_t *value_size);

/* Read the key-value pair at the cursor position. */
KvStoreResult kvstore_get_kv(KvStore *self, KvStoreCursor *cursor, uint8_t *key, size_t *key_size, uint8_t *value, size_t *value_size);
