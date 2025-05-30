#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "private/lib/basic/array.h"
#include "private/lib/debug/print.h"

dynamic_array_t *_kb_array_create(size_t element_size, uint32_t init_size) {
	dynamic_array_t *array = malloc(sizeof(dynamic_array_t));
	if (array == NULL) {
		return array;
	}

	_kb_array_make(array, element_size, init_size);

	array->free_self = true;
	return array;
}
bool _kb_array_make(dynamic_array_t *array, size_t element_size, uint32_t init_size) {
	memset(array, 0, sizeof(dynamic_array_t));
	array->element_size = element_size;
	return array_resize(array, init_size);
}
void array_delete(dynamic_array_t *array) {
	if (array == NULL) {
		return;
	}

	// debug_print(KBURN_LOG_TRACE, "free: %p", (void *)array->body);
	free(array->body);
	array->body = NULL;

	if (array->free_self) {
		free(array);
	}
}

void p_array_remove(dynamic_array_t *array, size_t index) {
	m_assert(array->element_size == sizeof(void *), "p_array_push must use on pointer array");
	void **ptr = array->body;

	ptr[index] = NULL;
}

void p_array_push(dynamic_array_t *array, void *new_element) {
	m_assert(array->element_size == sizeof(void *), "p_array_push must use on pointer array");
	void **ptr = array->body;

	ssize_t emp = p_array_find(array, NULL);
	if (emp >= 0) {
		ptr[emp] = new_element;
		return;
	}

	if (array->length == array->size) {
		array_grow(array, 5);
	}

	ptr[array->length++] = new_element;
}

ssize_t p_array_find(dynamic_array_t *array, void *needle) {
	m_assert(array->element_size == sizeof(void *), "p_array_push must use on pointer array");

	void **ptr = array->body;
	for (size_t i = 0; i < array->length; i++) {
		if (ptr[i] == needle) {
			return i;
		}
	}
	return -1;
}

void array_move(dynamic_array_t *dst, dynamic_array_t *src) {
	array_resize(dst, 0);

	dst->body = src->body;
	dst->element_size = src->element_size;
	dst->length = src->length;
	dst->size = src->size;

	src->body = NULL;
	src->length = 0;
	src->size = 0;
}

bool array_resize(dynamic_array_t *array, uint32_t new_size) {
	if (new_size == array->size) {
		return true;
	}

	if (new_size == 0) {
		// debug_print(KBURN_LOG_TRACE, "free: %p", (void *)array->body);
		free(array->body);
		array->body = NULL;
		array->size = 0;
		array->length = 0;
		return true;
	}

	void *ret = realloc(array->body, new_size * array->element_size);

	if (ret == NULL) {
		debug_print(KBURN_LOG_ERROR, "realloc failed! = %p", (void *)array->body);
		return false;
	}

	// debug_print(KBURN_LOG_TRACE, "realloc success: %p", (void *)ret);

	array->body = ret;
	array->size = new_size;

	if (array->length < array->size) {
		size_t start = array->length * array->element_size;
		size_t total = array->size * array->element_size;
		memset((char *)array->body + start, 0, total - start);
	}

	return true;
}
bool array_fit(dynamic_array_t *array, uint32_t new_size) {
	if (array->size < new_size) {
		return array_resize(array, new_size);
	} else {
		return true;
	}
}
DECALRE_DISPOSE(array_destroy, dynamic_array_t) {
	array_delete(context);
}
DECALRE_DISPOSE_END()
