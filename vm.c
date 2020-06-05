//
// es_vm
//

#include <stdio.h>
#include <string.h>
#include "vm.h"

static void fail(const char* msg);

#define E_DEBUG 0
#define E_DEBUG_PRINT_TABLES 0

// VM
void
e_vm_init(e_vm* vm) {
	if(vm == NULL) return;
	vm->ip = 0;
	e_stack_init(&vm->stack, E_STACK_SIZE);
	e_stack_init(&vm->globals, E_STACK_SIZE);
	e_stack_init(&vm->locals, E_STACK_SIZE);
	vm->status = E_VM_STATUS_READY;
}

e_vm_status
e_vm_parse_bytes(e_vm* vm, const uint8_t bytes[], uint32_t blen) {
	if(blen == 0) return E_VM_STATUS_EOF;

	// Copy data segment
	//memcpy(&vm->ds, &bytes[E_OUT_SIZE], E_OUT_DS_SIZE);
	memcpy(&vm->ds, bytes, blen);

	do {
#if E_DEBUG
		printf("** IP: %d ** ", vm->ip);
#endif

		e_instr cur_instr;
		uint32_t ip_begin = vm->ip;

		cur_instr.OP = bytes[vm->ip];
		cur_instr.op1 = (uint32_t)((bytes[++vm->ip] << 24u) | (bytes[++vm->ip] << 16u) | (bytes[++vm->ip] << 8u) | bytes[++vm->ip]);
		cur_instr.op2 = (uint32_t)((bytes[++vm->ip] << 24u) | (bytes[++vm->ip] << 16u) | (bytes[++vm->ip] << 8u) | bytes[++vm->ip]);
		vm->ip++;

		uint32_t ip_end = vm->ip;
		if(ip_end - ip_begin != E_INSTR_BYTES) {
			fail("Instruction size / offset error");
			return E_VM_STATUS_ERROR;
		}
#if E_DEBUG
		printf("Fetched instruction -> [0x%02X] (0x%02X, 0x%02X)\n", cur_instr.OP, cur_instr.op1, cur_instr.op2);
#endif

		e_vm_status es = e_vm_evaluate_instr(vm, cur_instr);
		if(es != E_VM_STATUS_OK) {
			fail("Invalid instruction or malformed arguments");
			return E_VM_STATUS_ERROR;
		}

#if E_DEBUG && E_DEBUG_PRINT_TABLES
		printf("-- GLOBAL STACK --\n");
		e_debug_dump_stack(vm, &vm->globals);

		printf("-- LOCAL STACK --\n");
		e_debug_dump_stack(vm, &vm->locals);
#endif

	} while(vm->ip < blen /*&& vm->ip <= E_OUT_SIZE*/);

	return E_VM_STATUS_OK;
}

e_vm_status
e_vm_evaluate_instr(e_vm* vm, e_instr instr) {
	/* */
	e_stack_status_ret s1;
	e_stack_status_ret s2;

	union {
		uint32_t u[2];
		uint64_t l;
		double d;
	} conv = {
		.u[0] = instr.op2,
		.u[1] = instr.op1
	};
	double d_op = conv.d;

	switch(instr.OP) {
		case E_OP_NOP:
			break;
		case E_OP_PUSHG:
			// Add value of pop([s-1]) to global symbol stack at index u32(op1)
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
#if E_DEBUG
				printf("Storing value %f to global stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
#endif
				if(instr.op2 == E_ARGT_STRING) {
					s1.val.argtype = E_STRING;
				}
				e_stack_status_ret s = e_stack_insert_at_index(&vm->globals,  s1.val, d_op);
				if(s.status != E_STATUS_OK) goto error;
			} else goto error;
			break;
		case E_OP_POPG:
			// Find value [index] in global stack
			{
				e_stack_status_ret s = e_stack_peek_index(&vm->globals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					printf("Loading global from index %d -> %f\n", instr.op1, s.val.val);
#endif
					// Push this value onto the vm->stack
					e_stack_push(&vm->stack, s.val);
				} else goto error;
			}
			break;
		case E_OP_PUSHL:
			// Add value of pop([s-1]) to locals symbol stack at index u32(op1)
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
#if E_DEBUG
				if(instr.op2 == E_ARGT_STRING) {
					printf("Storing value %s to local stack [%d] (type: %d)\n", s1.val.sval.sval, instr.op1, instr.op2);
				} else if(instr.op2 == E_ARGT_NUMBER) {
					printf("Storing value %f to local stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
				}
#endif
				e_stack_status_ret s = e_stack_insert_at_index(&vm->locals,  s1.val, d_op);
				if(s.status != E_STATUS_OK) goto error;
			} else goto error;
			break;
		case E_OP_POPL:
			// Find value [index] in local stack
			{
				e_stack_status_ret s = e_stack_peek_index(&vm->locals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					printf("Loading local from index %d -> %f\n", instr.op1, s.val.val);
#endif
					// Push this value onto the vm->stack
					e_stack_push(&vm->stack, s.val);
				} else goto error;
			}
			break;
		case E_OP_PUSH:
			// Push (u32(operand 1 | operand 2)) onto stack
			e_stack_push(&vm->stack, e_create_number(d_op));
			break;
		case E_OP_PUSHS:
			// Push string onto stack
			// d_op = string length
			{
				char tmp_str[E_MAX_STRLEN];
				if(d_op < E_MAX_STRLEN - 1) {
					memcpy(tmp_str, &vm->ds[vm->ip], (uint32_t)d_op);
					tmp_str[(uint32_t)d_op] = 0;
					e_stack_push(&vm->stack, e_create_string(tmp_str));
					vm->ip = vm->ip + d_op;
				} else goto error;
			}
			break;
		case E_OP_POP:
			break;
		case E_OP_EQ:
			// PUSH (s[-1] == s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val == s1.val.val));
			} else goto error;
			break;
		case E_OP_NOTEQ:
			// PUSH (s[-1] != s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val != s1.val.val));
			} else goto error;
			break;
		case E_OP_LT:
			// PUSH (s[-1] < s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val < s1.val.val));
			} else goto error;
			break;
		case E_OP_GT:
			// PUSH (s[-1] > s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val > s1.val.val));
			} else goto error;
			break;
		case E_OP_LTEQ:
			// PUSH (s[-1] <= s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val <= s1.val.val));
			} else goto error;
			break;
		case E_OP_GTEQ:
			// PUSH (s[-1] >= s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val >= s1.val.val));
			} else goto error;
			break;
		case E_OP_ADD:
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val + s1.val.val));
			} else goto error;
			break;
		case E_OP_SUB:
			// PUSH (s[-1] - s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val - s1.val.val));
			} else goto error;
			break;
		case E_OP_MUL:
			// PUSH (s[-1] * s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val * s1.val.val));
			} else goto error;
			break;
		case E_OP_DIV:
			// PUSH (s[-1] / s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val / s1.val.val));
			}
			break;
		case E_OP_MOD:
			// PUSH (s[-1] % s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number((uint8_t)((uint32_t)s2.val.val % (uint32_t)s1.val.val)));
			}
			break;
		case E_OP_AND:
			// PUSH (s[-1] && s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number((uint8_t)s2.val.val && s1.val.val));
			} else goto error;
			break;
		case E_OP_OR:
			// PUSH (s[-1] || s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number((uint8_t)s2.val.val || s1.val.val));
			} else goto error;
			break;
		case E_OP_NOT:
			// PUSH !s[-1]
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(!s1.val.val));
			} else goto error;
			break;
		case E_OP_CONCAT:
			// Concatenate two strings (cast if number type) s[-1] and s[-2]
			s2 = e_stack_pop(&vm->stack);
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {

				if((s1.val.argtype == E_STRING || s2.val.argtype == E_STRING)
					&& (s1.val.argtype != s2.val.argtype)) {
					// s1 or s2 contains string and
					// s1 or s2 contains number
					e_vm_status s;
					uint32_t slen = 0;
					char buf[E_MAX_STRLEN];
					char num_buf[E_MAX_STRLEN];

					if(s1.val.argtype == E_NUMBER) {
						snprintf(num_buf, E_MAX_STRLEN, "%f", s1.val.val);
						if(s2.val.sval.slen + strlen(num_buf) > E_MAX_STRLEN) goto error;
						memcpy(buf, (char*)s2.val.sval.sval, s2.val.sval.slen);
						buf[s2.val.sval.slen] = 0;
						strcat(buf, num_buf);
					} else {
						snprintf(num_buf, E_MAX_STRLEN, "%f", s2.val.val);
						if(s1.val.sval.slen + strlen(num_buf) > E_MAX_STRLEN) goto error;
						memcpy(buf, (char*)s1.val.sval.sval, s1.val.sval.slen);
						buf[s1.val.sval.slen] = 0;
						strcat(buf, num_buf);
					}

					e_stack_push(&vm->stack, e_create_string(buf));
				} else {
					// s1 contains string
					// s2 contains string
					char buf1[E_MAX_STRLEN];
					char buf2[E_MAX_STRLEN];

					if(s1.val.sval.slen + s2.val.sval.slen > E_MAX_STRLEN) {
						goto error;
					}
					memcpy(buf1, s1.val.sval.sval, s1.val.sval.slen);
					memcpy(buf2, s2.val.sval.sval, s2.val.sval.slen);
					buf1[s1.val.sval.slen] = 0;
					buf2[s2.val.sval.slen] = 0;

					// Concatenate strings
					strcat(buf1, buf2);
					e_stack_push(&vm->stack, e_create_string(buf1));

				}
			} else goto error;
			break;
		case E_OP_JZ:
			// POP s[-1]
			// if(s[-1] == 0) then perform_jump()
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				if(s1.val.val == 0) {
#if E_DEBUG
					printf("is zero, perform jump to address [%f]\n", d_op /** E_INSTR_BYTES*/);
#endif
					// Perform jump
					vm->ip = d_op /** E_INSTR_BYTES*/;
				}
			} else goto error;
			break;
		case E_OP_JMP:
			// perform_jump()
#if E_DEBUG
			printf("perform jump to address [%f]\n", d_op /** E_INSTR_BYTES*/);
#endif
			vm->ip = d_op /** E_INSTR_BYTES*/;
			break;
		case E_OP_PRINT:
			// TODO: Call __print() builtin
			e_builtin_print(vm, 1);
			break;
		default:
			return E_VM_STATUS_ERROR;
	}

	return E_VM_STATUS_OK;
	error:
		return E_VM_STATUS_ERROR;
}


// Stack
void
e_stack_init(e_stack* stack, uint32_t size) {
	if(stack == NULL) {
		return;
	}
	memset(stack->entries, 0, (sizeof(e_value) * size));
	stack->size = size;
	stack->top = 0;
}

e_stack_status_ret
e_stack_push(e_stack* stack, e_value v) {
	if(stack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}

	if(stack->top + 1 >= stack->size) {
		/* Not enough free space */
		return (e_stack_status_ret) { .status = E_STATUS_NESIZE };
	}

	stack->entries[stack->top++] = v;
	return (e_stack_status_ret) { .status = E_STATUS_OK };
}

e_stack_status_ret
e_stack_pop(e_stack* stack) {
	if(stack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}

	if(stack->top == 0) {
		/* Stack underflow */
		return (e_stack_status_ret) { .status = E_STATUS_UNDERFLOW };
	}
	return (e_stack_status_ret) { .status = E_STATUS_OK, .val = stack->entries[--stack->top] };
}

e_stack_status_ret
e_stack_peek(e_stack* stack) {
	return e_stack_peek_index(stack, stack->top);
}

e_stack_status_ret
e_stack_peek_index(e_stack* stack, uint32_t index) {
	if(stack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}
	return (e_stack_status_ret) { .status = E_STATUS_OK, .val = stack->entries[index] };
}

e_stack_status_ret
e_stack_insert_at_index(e_stack* stack, e_value v, uint32_t index) {
	if(stack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}

	stack->entries[index] = v;
	return (e_stack_status_ret) { .status = E_STATUS_OK };
}

e_value
e_create_number(double n) {
	return (e_value){ .val = n, .argtype = E_NUMBER };
}

e_value
e_create_string(const char* str) {
	e_str_type new_str;

	new_str.sval = (uint8_t*)strdup(str);
	new_str.slen = strlen(str);

	return (e_value) { .sval = new_str, .argtype = E_ARGT_STRING };
}

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


void
e_debug_dump_stack(const e_vm* vm, const e_stack* tab) {
#define E_DEBUG_OUT	10
	for(unsigned int i = 0; i < tab->size && i < E_DEBUG_OUT; i++) {
		const e_value cur = tab->entries[i];

		printf("[%d]\t", i);

		if(cur.argtype == E_STRING) {
			//char buf[E_MAX_STRLEN];
			//memcpy(buf, cur.val, E_MAX_STRLEN);
			//printf("STRING\tIndex: %d\t%s\n", (uint32_t)cur.val-E_OUT_SIZE, buf);
			printf("STRING\t");
		} else if(cur.argtype == E_NUMBER) {
			printf("NUMBER\t%f\n", cur.val);
		}
	}
#undef E_DEBUG_OUT
}

void
fail(const char* msg) {
	printf("Stopped execution! %s\n", msg);
}