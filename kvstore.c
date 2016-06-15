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

#include <string.h>
#include "kvstore.h"

/**
 * @todo
 *   - delete the key-value pair at the cursor position (kvstore_remove)
 *   - merge adjacent free slots (kvstore_clean)
 *   - API documentation
 *   - tests
 *   - locking
 *   - defragment (merge all free slots)
 *   - truncate (remove all free slots from the end of the backend storage)
 */


static size_t calculate_slot_size(size_t key_size, size_t value_size) {
	return KVSTORE_HEADER_SIZE_BYTES + KVSTORE_KEY_SIZE_BYTES + key_size + KVSTORE_VALUE_SIZE_BYTES + value_size;
}


static size_t calculate_value_size(size_t slot_size, size_t key_size) {
	return slot_size - KVSTORE_HEADER_SIZE_BYTES - KVSTORE_KEY_SIZE_BYTES - key_size - KVSTORE_VALUE_SIZE_BYTES;
}


static KvStoreResult write_slot(KvStore *self, size_t position, const uint8_t *key, size_t key_size, const uint8_t *value, size_t value_size) {
	if (self == NULL ||
	    value_size == 0 ||
	    self->backend->get_size == NULL ||
	    self->backend->write == NULL) {
		return KVSTORE_BAD_ARG;
	}

	size_t slot_size = calculate_slot_size(key_size, value_size);
	if ((position + slot_size) > self->backend->get_size(self->backend_context)) {
		return KVSTORE_FAILED;
	}

	if (self->backend->write(self->backend_context, (uint8_t[KVSTORE_HEADER_SIZE_BYTES])KVSTORE_HEADER_MAGIC, position, KVSTORE_HEADER_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_HEADER_SIZE_BYTES;

	#if KVSTORE_KEY_SIZE_BYTES == 1
		uint8_t tmp = key_size;
	#elif KVSTORE_KEY_SIZE_BYTES == 2
		uint16_t tmp = key_size;
	#elif KVSTORE_KEY_SIZE_BYTES == 4
		uint32_t tmp = key_size;
	#else
		#error "unsupported key size"
	#endif
	if (self->backend->write(self->backend_context, (uint8_t *)&tmp, position, KVSTORE_KEY_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_KEY_SIZE_BYTES;

	if (key != NULL) {
		if (self->backend->write(self->backend_context, key, position, key_size) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
	}
	position += key_size;

	#if KVSTORE_VALUE_SIZE_BYTES == 1
		uint8_t tmp2 = value_size;
	#elif KVSTORE_VALUE_SIZE_BYTES == 2
		uint16_t tmp2 = value_size;
	#elif KVSTORE_VALUE_SIZE_BYTES == 4
		uint32_t tmp2 = value_size;
	#else
		#error "unsupported value size"
	#endif

	if (self->backend->write(self->backend_context, (uint8_t *)&tmp2, position, KVSTORE_VALUE_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_VALUE_SIZE_BYTES;

	if (value != NULL) {
		if (self->backend->write(self->backend_context, value, position, value_size) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
	}
	position += value_size;

	return KVSTORE_OK;
}


/* key_size can be 0 to mark an empty slot */
static KvStoreResult read_slot(KvStore *self, size_t position, uint8_t *key, size_t *key_size, uint8_t *value, size_t *value_size) {
	if (self == NULL ||
	    self->backend->get_size == NULL ||
	    self->backend->read == NULL ||
	    key_size == NULL ||
	    value_size == NULL) {
		return KVSTORE_BAD_ARG;
	}

	size_t size = self->backend->get_size(self->backend_context);

	/* Expecting KVSTORE_HEADER_SIZE_BYTES bytes. */
	if ((position + KVSTORE_HEADER_SIZE_BYTES) > size) {
		return KVSTORE_FAILED;
	}

	uint8_t header[KVSTORE_HEADER_SIZE_BYTES];
	if (self->backend->read(self->backend_context, header, position, KVSTORE_HEADER_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_HEADER_SIZE_BYTES;

	/* Compare header magic number. */
	if (memcmp(header, (uint8_t[KVSTORE_HEADER_SIZE_BYTES])KVSTORE_HEADER_MAGIC, KVSTORE_HEADER_SIZE_BYTES) != 0) {
		return KVSTORE_FAILED;
	}

	#if KVSTORE_KEY_SIZE_BYTES == 1
		uint8_t tmp = 0;
	#elif KVSTORE_KEY_SIZE_BYTES == 2
		uint16_t tmp = 0;
	#elif KVSTORE_KEY_SIZE_BYTES == 4
		uint32_t tmp = 0;
	#else
		#error "unsupported key size"
	#endif

	/* Expecting KVSTORE_KEY_SIZE_BYTES bytes. */
	if ((position + KVSTORE_KEY_SIZE_BYTES) > size) {
		return KVSTORE_FAILED;
	}
	if (self->backend->read(self->backend_context, (uint8_t *)&tmp, position, KVSTORE_KEY_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_KEY_SIZE_BYTES;
	if (tmp > *key_size && key != NULL) {
		return KVSTORE_FAILED;
	}
	*key_size = tmp;
	if ((position + tmp) > size) {
		return KVSTORE_FAILED;
	}
	if (key != NULL) {
		if (self->backend->read(self->backend_context, key, position, tmp) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
	}
	position += tmp;

	#if KVSTORE_VALUE_SIZE_BYTES == 1
		uint8_t tmp2 = 0;
	#elif KVSTORE_VALUE_SIZE_BYTES == 2
		uint16_t tmp2 = 0;
	#elif KVSTORE_VALUE_SIZE_BYTES == 4
		uint32_t tmp2 = 0;
	#else
		#error "unsupported value size"
	#endif

	if ((position + KVSTORE_VALUE_SIZE_BYTES) > size) {
		return KVSTORE_FAILED;
	}
	if (self->backend->read(self->backend_context, (uint8_t *)&tmp2, position, KVSTORE_VALUE_SIZE_BYTES) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}
	position += KVSTORE_VALUE_SIZE_BYTES;
	if (tmp2 > *value_size && value != NULL) {
		return KVSTORE_FAILED;
	}
	*value_size = tmp2;
	if ((position + tmp2) > size) {
		return KVSTORE_FAILED;
	}
	if (value != NULL) {
		if (self->backend->read(self->backend_context, value, position, tmp2) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
	}
	position += tmp2;

	return KVSTORE_OK;
}


static KvStoreResult advance_slot(KvStore *self, size_t *position) {
	if (self == NULL ||
	    position == NULL) {
		return KVSTORE_BAD_ARG;
	}

	return KVSTORE_OK;
}


/**********************************************************************************************************************/

KvStoreResult kvstore_init(KvStore *self, const struct kvstore_backend *backend, void *backend_context) {
	if (self == NULL ||
	    backend == NULL) {
		return KVSTORE_BAD_ARG;
	}

	memset(self, 0, sizeof(KvStore));
	self->backend = backend;
	self->backend_context = backend_context;

	return KVSTORE_OK;
}


KvStoreResult kvstore_free(KvStore *self) {
	if (self == NULL) {
		return KVSTORE_BAD_ARG;
	}

	return KVSTORE_OK;
}


KvStoreResult kvstore_prepare(KvStore *self) {
	if (self == NULL ||
	    self->backend->get_size == NULL) {
		return KVSTORE_BAD_ARG;
	}

	size_t value_size = calculate_value_size(self->backend->get_size(self->backend_context), 0);

	/* Write an empty slot at position 0, with an empty key with size 0, with an empty value
	 * of size value_size. The whole slot size will be exactly get_size bytes. */
	write_slot(self, 0, NULL, 0, NULL, value_size);

	return KVSTORE_OK;
}


KvStoreResult kvstore_put(KvStore *self, const uint8_t *key, size_t key_size, const uint8_t *value, size_t value_size) {
	if (self == NULL ||
	    key == NULL ||
	    key_size == 0 ||
	    value == NULL ||
	    value_size == 0 ||
	    self->backend->get_size == NULL ||
	    key_size > KVSTORE_MAX_KEY_SIZE) {
		return KVSTORE_BAD_ARG;
	}

	size_t found_key_size = 0;
	size_t found_value_size = 0;
	size_t position = 0;
	size_t size = self->backend->get_size(self->backend_context);

	while (1) {
		if (position >= size) {
			break;
		}
		if (read_slot(self, position, NULL, &found_key_size, NULL, &found_value_size) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
		/* If the slot is not free, continue. */
		if (found_key_size > 0) {
			position += calculate_slot_size(found_key_size, found_value_size);
			continue;
		}
		/* If the slot is free, but not big enough to be split into two, continue. */
		if (calculate_slot_size(found_key_size, found_value_size) <
		    (calculate_slot_size(key_size, value_size) + calculate_slot_size(0, 0))) {
			position += calculate_slot_size(found_key_size, found_value_size);
			continue;
		}
		/* If everything looks ok, split the slot. */
		size_t remaining_space = calculate_slot_size(found_key_size, found_value_size) - calculate_slot_size(key_size, value_size);
		size_t slot2_value_size = calculate_value_size(remaining_space, 0);

		write_slot(self, position, key, key_size, value, value_size);
		position += calculate_slot_size(key_size, value_size);
		write_slot(self, position, NULL, 0, NULL, slot2_value_size);
		return KVSTORE_OK;
	}

	/* No free slot was found, try to write past the end of the backend storage. */
	if (write_slot(self, position, key, key_size, value, value_size) != KVSTORE_OK) {
		return KVSTORE_NOT_FOUND;
	}

	return KVSTORE_OK;
}


KvStoreResult kvstore_search(KvStore *self, KvStoreCursor *cursor, const uint8_t *key, size_t key_size) {
	if (self == NULL ||
	    cursor == NULL ||
	    key == NULL ||
	    key_size == 0 ||
	    key_size > KVSTORE_MAX_KEY_SIZE) {
		return KVSTORE_BAD_ARG;
	}

	/* Initialize the cursor. */
	cursor->position = 0;
	cursor->key_size = key_size;
	memcpy(cursor->key, key, key_size);

	/* And perform a regular search (find next). */
	return kvstore_search_next(self, cursor);
}


KvStoreResult kvstore_search_next(KvStore *self, KvStoreCursor *cursor) {
	if (self == NULL ||
	    cursor == NULL) {
		return KVSTORE_BAD_ARG;
	}

	while (1) {
		uint8_t key[KVSTORE_MAX_KEY_SIZE];
		size_t key_size = sizeof(key);
		size_t value_size = 0;

		if (read_slot(self, cursor->position, key, &key_size, NULL, &value_size) != KVSTORE_OK) {
			return KVSTORE_FAILED;
		}
		if (key_size != cursor->key_size) {
			kvstore_advance(self, cursor);
			continue;
		}
		if (memcmp(key, cursor->key, key_size) != 0) {
			kvstore_advance(self, cursor);
			continue;
		}
		/* The slot is found. */
		return KVSTORE_OK;
	}
	return KVSTORE_NOT_FOUND;
}


KvStoreResult kvstore_advance(KvStore *self, KvStoreCursor *cursor) {
	if (self == NULL ||
	    cursor == NULL) {
		return KVSTORE_BAD_ARG;
	}

	size_t key_size = 0;
	size_t value_size = 0;
	if (read_slot(self, cursor->position, NULL, &key_size, NULL, &value_size) != KVSTORE_OK) {
		return KVSTORE_FAILED;
	}

	cursor->position += calculate_slot_size(key_size, value_size);

	return KVSTORE_OK;
}


KvStoreResult kvstore_get(KvStore *self, KvStoreCursor *cursor, uint8_t *value, size_t *value_size) {
	if (self == NULL ||
	    cursor == NULL ||
	    value == NULL ||
	    value_size == NULL ||
	    *value_size == 0) {
		return KVSTORE_BAD_ARG;
	}

	size_t key_size = 0;
	return read_slot(self, cursor->position, NULL, &key_size, value, value_size);
}


KvStoreResult kvstore_get_kv(KvStore *self, KvStoreCursor *cursor, uint8_t *key, size_t *key_size, uint8_t *value, size_t *value_size) {
	if (self == NULL ||
	    cursor == NULL ||
	    key_size == NULL ||
	    value_size == NULL) {
		return KVSTORE_BAD_ARG;
	}

	return read_slot(self, cursor->position, key, key_size, value, value_size);
}
