//
// es_vm
//

#include <stdio.h>
#include <string.h>
#include "vm.h"

static void fail(const char* msg);

#define E_DEBUG 1
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
	memcpy(&vm->ds, &bytes[E_OUT_SIZE], E_OUT_DS_SIZE);

	do {
#if E_DEBUG
		printf("** IP: %d ** ", vm->ip);
#endif

		e_instr cur_instr;
		uint32_t ip_begin = vm->ip;
		uint32_t instr_nr = vm->ip / E_INSTR_BYTES;

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
		printf("Fetched instruction %d -> [0x%02X] (0x%02X, 0x%02X)\n", instr_nr, cur_instr.OP, cur_instr.op1, cur_instr.op2);
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

	} while(vm->ip < blen && vm->ip <= E_OUT_SIZE);

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
				e_stack_status_ret s = e_stack_insert_at_index(&vm->globals,  s1.val, instr.op1);
				if(s.status == E_STATUS_REQ || s.status == E_STATUS_OK) {
					if(s.status == E_STATUS_REQ && s1.val.argtype == E_STRING) {
						// Drop string
						e_vm_status s_drop = e_ds_drop_string(vm, s.val.val);
					}
				} else goto error;
			} else goto error;
			break;
		case E_OP_POPG:
			// Find value [index] in global stack
			{
				e_stack_status_ret s = e_stack_peek_index(&vm->globals, instr.op1);
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
				printf("Storing value %f to local stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
#endif
				if(instr.op2 == E_ARGT_STRING) {
					s1.val.argtype = E_STRING;
				}
				e_stack_status_ret s = e_stack_insert_at_index(&vm->locals,  s1.val, instr.op1);
				if(s.status == E_STATUS_REQ || s.status == E_STATUS_OK) {
					if(s.status == E_STATUS_REQ && s1.val.argtype == E_STRING) {
						// Drop string
						e_vm_status s_drop = e_ds_drop_string(vm, s.val.val);
					}
				} else goto error;
			} else goto error;
			break;
		case E_OP_POPL:
			// Find value [index] in local stack
			{
				e_stack_status_ret s = e_stack_peek_index(&vm->locals, instr.op1);
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
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s2.val.val * s1.val.val));
			} else goto error;
			break;
		case E_OP_DIV:
			// PUSH (s[-1] / s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_push(&vm->stack, e_create_number(s1.val.val / s2.val.val));
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
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				int str_index;

				if(instr.op2 != E_CONCAT_BOTH) {
					// s1 contains string
					// s2 contains number
					e_vm_status s;
					uint32_t slen = 0;
					char buf[E_MAX_STRLEN];
					char num_buf[E_MAX_STRLEN];

					snprintf(num_buf, E_MAX_STRLEN, "%f", s2.val.val);
					s = e_ds_read_string(vm, s1.val.val, buf, E_MAX_STRLEN);

					if(s == E_VM_STATUS_OK) {
						slen = strlen(buf);
						if(strlen(num_buf) + slen > E_MAX_STRLEN) {
							goto error;
						}
						// Concatenate strings
						if(instr.op2 == E_CONCAT_SECOND) {
							strcat(buf, num_buf);
							str_index = e_ds_store_string(vm, buf);
#if E_DEBUG
							if(str_index != -1) {
								printf("Created dynamic string %s and placed it in ds at index %d\n", buf, str_index);
							}
#endif
						} else {
							strcat(num_buf, buf);
							str_index = e_ds_store_string(vm, num_buf);
#if E_DEBUG
							if (str_index != -1) {
								printf("Created dynamic string %s and placed it in ds at index %d\n", num_buf, str_index);
							}
#endif
						}
					} else goto error;
				} else {
					// s1 contains string
					// s2 contains string
					char buf1[E_MAX_STRLEN];
					char buf2[E_MAX_STRLEN];

					e_vm_status str1 = e_ds_read_string(vm, s2.val.val, buf1, E_MAX_STRLEN);
					e_vm_status str2 = e_ds_read_string(vm, s1.val.val, buf2, E_MAX_STRLEN);
					if(str1 == str2 == E_VM_STATUS_OK) {
						unsigned int slen1 = strlen(buf1);
						unsigned int slen2 = strlen(buf2);
						if(slen1 + slen2 > E_MAX_STRLEN) {
							goto error;
						}
						// Concatenate strings
						strcat(buf1, buf2);
						str_index = e_ds_store_string(vm, buf1);
#if E_DEBUG
						if(str_index != -1) {
							printf("Created dynamic string %s and placed it in ds at index %d\n", buf1, str_index);
						}
#endif
					} else goto error;
				}

				if(str_index != -1) {
					// PUSH new index
					e_stack_push(&vm->stack, e_create_number(str_index));
				} else goto error;
			} else goto error;
			break;
		case E_OP_JZ:
			// POP s[-1]
			// if(s[-1] == 0) then perform_jump()
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				if(s1.val.val == 0) {
#if E_DEBUG
					printf("is zero, perform jump to address [%d]\n", instr.op1 * E_INSTR_BYTES);
#endif
					// Perform jump
					vm->ip = instr.op1 * E_INSTR_BYTES;
				}
			} else goto error;
			break;
		case E_OP_JMP:
			// perform_jump()
#if E_DEBUG
			printf("perform jump to address [%d]\n", instr.op1 * E_INSTR_BYTES);
#endif
			vm->ip = instr.op1 * E_INSTR_BYTES;
			break;
		case E_OP_PRINT:
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				if(instr.op2 == E_NUMBER) {
					printf("%f\n", s1.val.val);
				} else if(instr.op2 == E_STRING) {
					char buf[E_MAX_STRLEN];
					e_vm_status str = e_ds_read_string(vm, s1.val.val, buf, E_MAX_STRLEN);
#if E_DEBUG
					printf("Read string for print at index: %d\n", (uint32_t)s1.val.val);
#endif
					if(str == E_VM_STATUS_OK) {
						printf("%s\n", buf);
					} else goto error;
				} else {
					fail("Unsupported expression");
					goto error;
				}
			} else goto error;
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

	if(stack->entries[index].argtype == E_STRING) {
		// If previous node was of type string, drop this string from the DS
#if E_DEBUG
		printf("Previous node was of type string. Dropping string at index: %d\n", (uint32_t)stack->entries[index].val);
#endif
		uint32_t prev = (uint32_t)stack->entries[index].val;
		stack->entries[index] = v;
		return (e_stack_status_ret) { .status = E_STATUS_REQ, .val = e_create_number(prev) };
	}

	stack->entries[index] = v;
	return (e_stack_status_ret) { .status = E_STATUS_OK };
}

e_value
e_create_number(double n) {
	return (e_value){ .val = n, .argtype = E_NUMBER };
}

e_vm_status
e_ds_read_string(const e_vm* vm, uint32_t addr, char* buf, uint32_t slen) {
	uint32_t offset = addr - E_OUT_SIZE;

	uint16_t size = (vm->ds[offset] << 8) | (vm->ds[offset+1]);
	if(size > slen) {
		return E_VM_STATUS_ERROR;
	}
	uint16_t i = 0;	// first two bytes are length information
	while(i < slen && i < size) {
		buf[i] = vm->ds[offset + 2 + i];
		i++;
	}
	buf[i] = 0;
	if(strlen(buf) == size) return E_VM_STATUS_OK;
	return E_VM_STATUS_ERROR;
}

int
e_ds_get_size(e_vm* vm) {
	uint32_t i = 1;
	if(i + 1 < E_OUT_SIZE) {
		uint16_t size = (vm->ds[i] << 8) | vm->ds[i+1];
		while(size != 0) {
			i = i + size + 2;
			size = (vm->ds[i] << 8) | vm->ds[i+1];
		}
	}
	return i;
}

int
e_ds_store_string(e_vm* vm, const char* str) {
	// Stores a string in the required format [LENGTH, 2 Bytes][<str data>]
	uint32_t r_len = strlen(str);
	int ds_size = e_ds_get_size(vm);

	if(r_len > UINT16_MAX || ds_size + r_len > E_OUT_TOTAL_SIZE) {
		return -1;
	} else {
		uint32_t start_index = ds_size;
		uint16_t len = (uint16_t)r_len;

		vm->ds[ds_size++] = (uint8_t)((len >> 8) & 0xFF);
		vm->ds[ds_size++] = (uint8_t)(len & 0xFF);
		for(uint16_t i = 0; i < len; i++) {
			vm->ds[ds_size++] = (uint8_t)str[i];
		}
		return E_OUT_SIZE + start_index;
	}
}

e_vm_status
e_ds_drop_string(e_vm* vm, uint32_t addr) {
	uint32_t offset = addr - E_OUT_SIZE;
#if E_DEBUG
	printf("Dropping string from %d\n", addr);
#endif

	uint16_t size = (vm->ds[offset] << 8) | (vm->ds[offset + 1]);
	if(size > E_OUT_DS_SIZE || size == 0) {
		return E_VM_STATUS_ERROR;
	}
	memset(&vm->ds[offset], 0, size + 2);

	return E_VM_STATUS_OK;
}

void
e_debug_dump_stack(const e_vm* vm, const e_stack* tab) {
#define E_DEBUG_OUT	10
	for(unsigned int i = 0; i < tab->size && i < E_DEBUG_OUT; i++) {
		const e_value cur = tab->entries[i];

		printf("[%d]\t", i);

		if(cur.argtype == E_STRING) {
			char buf[E_MAX_STRLEN];
			e_ds_read_string(vm, cur.val, buf, E_MAX_STRLEN);
			printf("STRING\tIndex: %d\t%s\n", (uint32_t)cur.val-E_OUT_SIZE, buf);
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