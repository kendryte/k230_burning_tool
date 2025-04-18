#pragma once

#define callback_register_swap(type, stor, new_cb, new_ctx) \
	if (0)                                                  \
		(void)0;                                            \
	CONCAT(type, _t) old = stor;                            \
	stor.handler = new_cb;                                  \
	stor.context = new_ctx;

#define DEFINE_REGISTER_SWAPPER(name, stor, type)                  \
	CONCAT(type, _t) name(KBMonCTX monitor, type callback, void *ctx) { \
		callback_register_swap(type, stor, callback, ctx);         \
		return old;                                                \
	}
