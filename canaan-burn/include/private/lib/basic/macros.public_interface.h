#pragma once

#define _CREATE_GETTER_SETTER(Subsystem, Field, config_field)                                     \
	extern KBMonCTX monitor;                                                                           \
	void kburnSet##Subsystem##Field(KBMonCTX monitor, typeof(subsystem_settings.config_field) Field) { \
		debug_trace_function("%" PRId64, (int64_t)Field);                                         \
		subsystem_settings.config_field = Field;                                                  \
	}                                                                                             \
	typeof(subsystem_settings.config_field) kburnGet##Subsystem##Field(KBMonCTX monitor) {             \
		return subsystem_settings.config_field;                                                   \
	}
