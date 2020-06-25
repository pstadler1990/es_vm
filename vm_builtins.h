//
// es_vm
//

#ifndef ES_VM_VM_BUILTINS_H
#define ES_VM_VM_BUILTINS_H

#include "vm.h"

// Built-ins
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen);
uint32_t e_builtin_argtype(e_vm* vm, uint32_t arglen);
#endif //ES_VM_VM_BUILTINS_H
