//
// es_vm
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "vm_builtins.h"

static int cmpfunc(const void* a, const void* b);

/* Built-ins */
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK
		   && s1.val.argtype == E_STRING) {
			e_print((char*)s1.val.sval.sval);
		}
	}
	return E_API_CALL_RETURN_OK(0);
}

uint32_t e_builtin_argtype(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK) {
			e_stack_status_ret s_push = e_api_stack_push(&vm->stack, e_create_number(s1.val.argtype));
			if(s_push.status == E_STATUS_OK) {
				return E_API_CALL_RETURN_OK(1);
			}
		}
	}
	return E_API_CALL_RETURN_ERROR;
}

uint32_t e_builtin_len(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);

		e_stack_status_ret s_push;
		if(s1.status == E_STATUS_OK) {
			switch(s1.val.argtype) {
				default:
				case E_NUMBER:
					s_push = e_api_stack_push(&vm->stack, e_create_number(0));
					break;
				case E_STRING:
					s_push = e_api_stack_push(&vm->stack, e_create_number(s1.val.sval.slen));
					break;
				case E_ARRAY:
					s_push = e_api_stack_push(&vm->stack, e_create_number(s1.val.aval.alen));
					break;
			}
			if(s_push.status == E_STATUS_OK) {
				return E_API_CALL_RETURN_OK(1);
			}
		}
	}
	return E_API_CALL_RETURN_ERROR;
}

int cmpfunc(const void* a, const void* b) {
	e_value* v1 = (e_value*)a;
	e_value* v2 = (e_value*)b;

	if(v1->argtype == E_NUMBER && v2->argtype == E_NUMBER) {
		return (int)(v1->val - v2->val);
	} else if(v1->argtype == E_STRING && v2->argtype == E_STRING) {
		return (int)(v1->sval.slen - v2->sval.slen);
	}
	return 0;
}

uint32_t e_builtin_sort(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK && s1.val.argtype == E_ARRAY) {

			e_value tmp_arr[E_MAX_ARRAYSIZE];

			e_array_entry (*arr_ptr)[E_MAX_ARRAYSIZE] = NULL;
			if(s1.val.aval.global_local == E_ARRAY_GLOBAL) {
				arr_ptr = vm->arrays_global;
			} else {
				arr_ptr = vm->arrays_local;
			}
			
			for(uint32_t i = 0; i < s1.val.aval.alen; i++) {
				tmp_arr[i] = arr_ptr[s1.val.aval.aptr][i].v;
			}

			qsort(&tmp_arr, s1.val.aval.alen, sizeof(e_value), cmpfunc);

			for(uint32_t i = 0; i < s1.val.aval.alen; i++) {
				e_stack_status_ret s_push = e_api_stack_push(&vm->stack, tmp_arr[i]);
				if(s_push.status != E_STATUS_OK) {
					return E_API_CALL_RETURN_ERROR;
				}
			}
			vm->pupo_is_data = s1.val.aval.alen;
			return E_API_CALL_RETURN_OK(s1.val.aval.alen);
		}
	}
	return E_API_CALL_RETURN_ERROR;
}

uint32_t e_builtin_array(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK && s1.val.argtype == E_NUMBER) {
			for(uint32_t i = 0; i < s1.val.val; i++) {
				e_stack_status_ret s_push = e_api_stack_push(&vm->stack, e_create_number(0));
				if(s_push.status != E_STATUS_OK) {
					return E_API_CALL_RETURN_ERROR;
				}
			}
			vm->pupo_is_data = s1.val.val;
			return E_API_CALL_RETURN_OK(0);
		}
	}
	return 0;
}

#if 0
uint8_t e_read_byte(uint32_t offset) {
	// TODO: Return byte at >offset<
	return 0;
}

void e_print(const char* msg) {
	printf("%s\n", msg);
}

void e_fail(const char* msg) {
	// TODO: Implement your custom error printing function here
	printf("ERROR happend: %s\n", msg);
}

uint8_t e_check_locked(void) {
	// TODO: You can use this function to return a locked status of the VM
	return 0;
}
#endif