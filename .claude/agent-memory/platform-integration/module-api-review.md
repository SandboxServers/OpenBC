# Module API ABI Review Notes

## Variadic Function Pointers in Vtables (Win32 i686)

The `obc_engine_api_t` vtable contains four variadic function pointers:
```c
void (*log_info)(const char *fmt, ...);
void (*log_warn)(const char *fmt, ...);
void (*log_debug)(const char *fmt, ...);
void (*log_error)(const char *fmt, ...);
```

### The Problem
On i686 Windows, variadic functions **must** use `__cdecl`. If a module DLL is compiled with `/Gz` (default `__stdcall`), calling through these pointers corrupts the stack because `__stdcall` expects the callee to clean up arguments, but variadic functions require the caller to do it.

On x86-64, there is only one calling convention, so this is not an issue.
On Linux/macOS, there is only one C calling convention, so this is not an issue.

### Fix Options
1. Annotate with `__cdecl` on Win32:
   ```c
   #ifdef _WIN32
   void (__cdecl *log_info)(const char *fmt, ...);
   #else
   void (*log_info)(const char *fmt, ...);
   #endif
   ```
2. Change to non-variadic: `void (*log_info)(const char *msg);`
3. Use `va_list` variant: `void (*log_infov)(const char *fmt, va_list ap);`

### Current Status
Not a blocker while all modules are compiled with the project Makefile (uses __cdecl by default). Should be fixed before declaring the module API stable for third-party modules.

## config_float Type Mismatch
- `module_api.h`: `float (*config_float)(..., float def)` -- 32-bit
- `config.h`: `double obc_config_mod_float(..., double default_val)` -- 64-bit
- Engine shim will need to narrow. Intentional (game floats are 32-bit) but undocumented.

## Missing Test Coverage
- `test_module_api.c` does not include stubs for the four variadic log pointers.
- All other vtable slots are covered by stub assignment + dispatch.
