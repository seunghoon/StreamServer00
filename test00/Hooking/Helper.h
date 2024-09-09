#define WIN32_LEAN_AND_MEAN

#ifndef _HELPER_H
#define _HELPER_H

extern uintptr_t* GetD3D9DeviceFunctionAddress(short methodIndex);
extern void add_log (const char * fmt, ...);

typedef struct 
{
	enum {
		INJECTOR_NORMAL = 0,
		INJECTOR_STARTER,
	} inject_type;

	unsigned id;
	unsigned width, height;
} inject_payload;

#endif
