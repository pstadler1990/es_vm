//
// es_vm
//

#ifndef ES_VM_VM_BUILTINS_H
#define ES_VM_VM_BUILTINS_H

#include "vm.h"

#define E_API_CALL_RETURN_OK(v)	((uint32_t)1 + v)
#define E_API_CALL_RETURN_ERROR	((uint32_t)0)

// Built-ins
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen);
uint32_t e_builtin_argtype(e_vm* vm, uint32_t arglen);
uint32_t e_builtin_len(e_vm* vm, uint32_t arglen);
uint32_t e_builtin_sort(e_vm* vm, uint32_t arglen);
uint32_t e_builtin_array(e_vm* vm, uint32_t arglen);

// User implemented callbacks
void e_fail(const char* msg);
void e_print(const char* msg);
uint8_t e_check_locked(void);

#endif //ES_VM_VM_BUILTINS_H
