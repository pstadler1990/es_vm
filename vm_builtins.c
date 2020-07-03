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
	return 0;
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
				return 1;
			}
		}
	}
	return 0;
}

int cmpfunc(const void* a, const void* b) {
	e_value* v1 = (e_value*)a;
	e_value* v2 = (e_value*)b;

	if(v1->argtype == E_NUMBER && v2->argtype == E_NUMBER) {
		return (int)(v1->val - v2->val);
	}
	return 0;
}

uint32_t e_builtin_sort(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK && s1.val.argtype == E_ARRAY) {

			e_value tmp_arr[E_MAX_ARRAYSIZE];
			for(uint32_t i = 0; i < s1.val.aval.alen; i++) {
				tmp_arr[i] = vm->arrays[s1.val.aval.aptr][i].v;
			}

			qsort(&tmp_arr, s1.val.aval.alen, sizeof(e_value), cmpfunc);

			e_stack_status_ret s_push = e_api_stack_push(&vm->stack, e_create_array(vm, tmp_arr, s1.val.aval.alen));
			if(s_push.status == E_STATUS_OK) {
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

uint32_t e_builtin_array(e_vm* vm, uint32_t arglen) {
	if(arglen == 1) {
		e_stack_status_ret s1 = e_api_stack_pop(&vm->stack);
		if(s1.status == E_STATUS_OK && s1.val.argtype == E_NUMBER) {

			e_value tmp_array[E_MAX_ARRAYSIZE];
			for(uint32_t i = 0; i < s1.val.val; i++) {
				tmp_array[i] = (e_value) { .argtype = E_NUMBER, .val = 0.0f };
			}
			e_stack_status_ret s_push = e_api_stack_push(&vm->stack, e_create_array(vm, tmp_array, s1.val.val));
			if(s_push.status == E_STATUS_OK) {
				return 1;
			}
			return 0;
		}
	}
	return 0;
}