//
// es_vm
//

#include <stdint.h>
#include <stdio.h>
#include "vm_builtins.h"

/* Built-ins */
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK
		   && s1.val.argtype == E_STRING) {
			printf("%s\n", s1.val.sval.sval);
		}
	}
	return 0;	// 0 args are pushed back onto stack
}

uint32_t e_builtin_argtype(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK) {
			e_stack_status_ret s_push = e_api_stack_push(&vm->stack, e_create_number(s1.val.argtype));
			if(s_push.status == E_STATUS_OK) {
				return 1;
			}
		}
	}
	return 0;	// 0 args are pushed back onto stack
}