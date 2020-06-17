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