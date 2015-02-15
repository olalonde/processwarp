#pragma once

extern void* __intrinsic_va_arg(va_list*, size_t);

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type) (*((type*)(__intrinsic_va_arg(&(ap), sizeof(type)))))

#if __STDC_VERSION__ >= 199900L || __cplusplus >= 201103L || !defined(__STRICT_ANSI__)
#define va_copy(dest, src)  __builtin_va_copy(dest, src)
#endif
