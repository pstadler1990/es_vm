//
// es_vm
//

#include <stdint.h>
#include <stdio.h>
#include "vm_builtins.h"

/* Built-ins */
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen) {
	(void)arglen;
	e_stack_status_ret s1;

	s1 = e_stack_pop(&vm->stack);
	if(s1.status == E_STATUS_OK
	   && s1.val.argtype == E_STRING) {
		printf("%s\n", s1.val.sval.sval);
	}

	return 0;	// 0 args are pushed back onto stack
}

uint32_t e_ext_my_external_func(e_vm* vm, uint32_t arglen) {
	printf("Called my external func in C (passed %d arguments)\n", arglen);

	if(arglen > 0) {
		e_stack_status_ret a1 = e_stack_pop(&vm->stack);
		if(a1.status == E_STATUS_OK && a1.val.argtype == E_NUMBER) {
			// Push a1 value * 2 onto stack
			e_stack_push(&vm->stack, e_create_number(a1.val.val * 2));
			return 1;
		}
	}

	return 0;
}