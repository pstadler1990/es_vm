//
// es_vm
//

#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "vm_builtins.h"

static void fail(const char* msg);
static uint8_t e_find_value_in_arr(const e_vm* vm, uint32_t aptr, uint32_t index, e_value* vptr);
static uint8_t e_change_value_in_arr(e_vm* vm, uint32_t aptr, uint32_t index, e_value v);
static uint8_t e_array_append(e_vm* vm, uint32_t aptr, e_value v);

// Stack
void e_stack_init(e_stack* stack, uint32_t size);
static e_stack_status_ret e_stack_push(e_stack* stack, e_value v);
static e_stack_status_ret e_stack_pop(e_stack* stack);
static e_stack_status_ret e_stack_peek(const e_stack* stack);
static e_stack_status_ret e_stack_peek_index(const e_stack* stack, uint32_t index);
static e_stack_status_ret e_stack_insert_at_index(e_stack* stack, e_value v, uint32_t index);
static e_stack_status_ret e_stack_swap_last(e_stack* stack);
static void e_dump_stack(const e_stack* stack);

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
	vm->pupo_is_data = 0;
	vm->pupo_arr_index = -1;
	vm->dscnt = 0;
	for(uint32_t i = 0; i < E_MAX_ARRAYS; i++) {
		for(uint32_t e = 0; e < E_MAX_ARRAYSIZE; e++) {
			vm->arrays[i][e] = (e_array_entry) { .v = { 0 }, .used = 0 };
		}
	}
	vm->acnt = 0;
	vm->cfcnt = 0;
	vm->status = E_VM_STATUS_READY;
}

e_vm_status
e_vm_parse_bytes(e_vm* vm, const uint8_t bytes[], uint32_t blen) {
	if(blen == 0) return E_VM_STATUS_EOF;

	// Copy data segment
	if(blen > E_OUT_DS_SIZE) {
		fail("Not enough space");
		return E_VM_STATUS_ERROR;
	}
	memcpy(&vm->ds, bytes, blen);
	vm->dscnt = blen;

	do {
#if E_DEBUG
		printf("** IP: %d ** ", vm->ip);
#endif

		e_instr cur_instr;
		uint32_t ip_begin = vm->ip;

		cur_instr.OP = bytes[vm->ip];
		if(!sb_ops[cur_instr.OP]) {
			cur_instr.op1 = (uint32_t)((bytes[++vm->ip] << 24u) | (bytes[++vm->ip] << 16u) | (bytes[++vm->ip] << 8u) | bytes[++vm->ip]);
			cur_instr.op2 = (uint32_t)((bytes[++vm->ip] << 24u) | (bytes[++vm->ip] << 16u) | (bytes[++vm->ip] << 8u) | bytes[++vm->ip]);
			vm->ip++;

			uint32_t ip_end = vm->ip;
			if(ip_end - ip_begin != E_INSTR_BYTES) {
				fail("Instruction size / offset error");
				return E_VM_STATUS_ERROR;
			}
		} else {
			vm->ip++;

			uint32_t ip_end = vm->ip;
			if(ip_end - ip_begin != E_INSTR_SINGLE_BYTES) {
				fail("Instruction size / offset error");
				return E_VM_STATUS_ERROR;
			}
		}

#if E_DEBUG
		printf("Fetched instruction -> [0x%02X] (0x%02X, 0x%02X)\n", cur_instr.OP, cur_instr.op1, cur_instr.op2);
#endif

		e_vm_status es = e_vm_evaluate_instr(vm, cur_instr);
		if(es != E_VM_STATUS_OK) {
			fail("Invalid instruction or malformed arguments - STOPPED EXECUTION");
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
			if(vm->pupo_is_data) {
				e_value tmp_arr[E_MAX_ARRAYSIZE];
				uint32_t arr_len = vm->pupo_is_data;
				uint32_t e = arr_len - 1;
				do {
					s1 = e_stack_pop(&vm->stack);
					if(s1.status == E_STATUS_OK) {
						tmp_arr[e] = s1.val;
					} else goto error;
					e--;
				} while((vm->pupo_is_data--) - 1);

				e_value arr = e_create_array(vm, tmp_arr, arr_len);
				if(arr.aval.alen == arr_len) {
					e_stack_status_ret s = e_stack_insert_at_index(&vm->globals, arr, d_op);
					vm->pupo_is_data = 0;
					vm->pupo_is_data = 0;

					if (s.status != E_STATUS_OK) goto error;
				} else goto error;
			} else {
					e_stack_status_ret s_peek = e_stack_peek_index(&vm->globals, d_op);
					if(s_peek.val.argtype == E_ARRAY) {
						/* Array access based on index */
						//e_stack_status_ret s_index = e_stack_pop(&vm->stack);
						if(vm->pupo_arr_index >= 0) {
							e_stack_status_ret s_value = e_stack_pop(&vm->stack);
							if(s_value.status == E_STATUS_OK) {
								e_change_value_in_arr(vm, s_peek.val.aval.aptr, vm->pupo_arr_index, s_value.val);
							} else {
								fail("Array out of bounds");
								goto error;
							}
							vm->pupo_arr_index = -1;
						} else goto error;
					} else {
						s1 = e_stack_pop(&vm->stack);
						if(s1.status == E_STATUS_OK) {
#if E_DEBUG
							printf("Storing value %f to global stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
#endif
						if (instr.op2 == E_ARGT_STRING) {
							s1.val.argtype = E_STRING;
						}
						e_stack_status_ret s = e_stack_insert_at_index(&vm->globals, s1.val, d_op);
						if (s.status != E_STATUS_OK) goto error;
					} else goto error;
				}
			}
			break;
		case E_OP_POPG:
			// Find value [index] in global stack
			{
				e_stack_status_ret s = e_stack_peek_index(&vm->globals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					printf("Loading global from index %d -> %f\n", instr.op1, s.val.val);
#endif
					if(s.val.argtype == E_ARRAY) {
						/* Array access based on index */
						if(vm->pupo_arr_index >= 0) {
							e_value v;
							if(e_find_value_in_arr(vm, s.val.aval.aptr, vm->pupo_arr_index, &v)) {
								e_stack_status_ret s_push = e_stack_push(&vm->stack, v);
								if(s_push.status == E_STATUS_NESIZE) {
									fail("Stack overflow");
									goto error;
								}
							} else {
								/* Out of bounds */
								fail("Array out of bounds");
								goto error;
							}
							vm->pupo_arr_index = -1;
						} else {
							/* Array pass-by-value? */
							e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
							if(s_push.status == E_STATUS_NESIZE) {
								fail("Stack overflow");
								goto error;
							}
						}
					} else {
						// Push this value onto the vm->stack
						e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
						if(s_push.status == E_STATUS_NESIZE) {
							fail("Stack overflow");
							goto error;
						}
					}
				} else goto error;
			}
			break;
		case E_OP_PUSHL:
			// Add value of pop([s-1]) to locals symbol stack at index u32(op1)
			if(vm->pupo_is_data) {
				e_value tmp_arr[E_MAX_ARRAYSIZE];
				uint32_t arr_len = vm->pupo_is_data;
				uint32_t e = arr_len - 1;
				do {
					s1 = e_stack_pop(&vm->stack);
					if(s1.status == E_STATUS_OK) {
						tmp_arr[e] = s1.val;
					} else goto error;
					e--;
				} while((vm->pupo_is_data--) - 1);

				e_value arr = e_create_array(vm, tmp_arr, arr_len);
				e_stack_status_ret s;

				if(vm->cfcnt > 0) {
					s = e_stack_insert_at_index(&vm->callframes[vm->cfcnt - 1].locals, arr, d_op);
				} else {
					s = e_stack_insert_at_index(&vm->locals, arr, d_op);
				}
				vm->pupo_is_data = 0;

				if (s.status != E_STATUS_OK) goto error;
			} else {
				e_stack_status_ret s_peek;

				if(vm->cfcnt > 0) {
					s_peek = e_stack_peek_index(&vm->callframes[vm->cfcnt - 1].locals, d_op);
				} else {
					s_peek = e_stack_peek_index(&vm->locals, d_op);
				}
				if(s_peek.val.argtype == E_ARRAY) {
					/* Array access based on index */
					if(vm->pupo_arr_index >= 0) {
						e_stack_status_ret s_value = e_stack_pop(&vm->stack);
						if(s_value.status == E_STATUS_OK) {
							e_change_value_in_arr(vm, s_peek.val.aval.aptr, vm->pupo_arr_index, s_value.val);
						} else {
							fail("Array out of bounds");
							goto error;
						}
						vm->pupo_arr_index = -1;
					} else goto error;
				} else {
					s1 = e_stack_pop(&vm->stack);
					if (s1.status == E_STATUS_OK) {
#if E_DEBUG
						if(instr.op2 == E_ARGT_STRING) {
							printf("Storing value %s to local stack [%d] (type: %d)\n", s1.val.sval.sval, instr.op1, instr.op2);
						} else if(instr.op2 == E_ARGT_NUMBER) {
							printf("Storing value %f to local stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
						}
#endif
						e_stack_status_ret s;
						if(vm->cfcnt > 0) {
							s = e_stack_insert_at_index(&vm->callframes[vm->cfcnt - 1].locals, s1.val, d_op);
						} else {
							s = e_stack_insert_at_index(&vm->locals, s1.val, d_op);
						}
						if (s.status != E_STATUS_OK) goto error;
					} else goto error;
				}
			}
			break;
		case E_OP_POPL:
			// Find value [index] in local stack
			{
				e_stack_status_ret s;

				if(vm->cfcnt > 0) {
					s = e_stack_peek_index(&vm->callframes[vm->cfcnt - 1].locals, d_op);
				} else {
					s = e_stack_peek_index(&vm->locals, d_op);
				}

				//e_stack_status_ret s = e_stack_peek_index(&vm->locals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					printf("Loading local from index %d -> %f\n", instr.op1, s.val.val);
#endif
					if(s.val.argtype == E_ARRAY) {
						/* Array access based on index */
						if(vm->pupo_arr_index >= 0) {
							e_value v;
							if(e_find_value_in_arr(vm, s.val.aval.aptr, vm->pupo_arr_index, &v)) {
								e_stack_status_ret s_push = e_stack_push(&vm->stack, v);
								if(s_push.status == E_STATUS_NESIZE) {
									fail("Stack overflow");
									goto error;
								}
							} else {
								/* Out of bounds */
								fail("Array out of bounds");
							}
							vm->pupo_arr_index = -1;
						} else {
							/* Array pass-by-value? */
							e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
							if(s_push.status == E_STATUS_NESIZE) {
								fail("Stack overflow");
								goto error;
							}
						}
					} else {
						// Push this value onto the vm->stack
						e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
						if(s_push.status == E_STATUS_NESIZE) {
							fail("Stack overflow");
							goto error;
						}
					}
				} else goto error;
			}
			break;
		case E_OP_PUSHA:
			vm->pupo_arr_index = d_op;
			break;
		case E_OP_PUSHAS:
			{
				s1 = e_stack_pop(&vm->stack);
				if(s1.status == E_STATUS_OK) {
					vm->pupo_arr_index = s1.val.val;
				} else goto error;
			}
			break;
		case E_OP_PUSH:
			// Push (u32(operand 1 | operand 2)) onto stack
			{
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(d_op));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			}
			break;
		case E_OP_PUSHS:
			// Push string onto stack
			{
				char tmp_str[E_MAX_STRLEN];
				if(d_op < E_MAX_STRLEN - 1) {
					memcpy(tmp_str, &vm->ds[vm->ip], (uint32_t)d_op);
					tmp_str[(uint32_t)d_op] = 0;
					e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_string(tmp_str));
					if(s_push.status == E_STATUS_NESIZE) {
						fail("Stack overflow");
						goto error;
					}
					vm->ip = vm->ip + d_op;
				} else goto error;
			}
			break;
		case E_OP_DATA:
			vm->pupo_is_data = d_op;
			break;
		case E_OP_EQ:
			// PUSH (s[-1] == s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val == s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_NOTEQ:
			// PUSH (s[-1] != s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val != s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_LT:
			// PUSH (s[-1] < s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val < s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_GT:
			// PUSH (s[-1] > s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val > s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_LTEQ:
			// PUSH (s[-1] <= s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val <= s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_GTEQ:
			// PUSH (s[-1] >= s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val >= s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_ADD:
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val + s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_NEG:
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(-s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_SUB:
			// PUSH (s[-1] - s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val - s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_MUL:
			// PUSH (s[-1] * s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val * s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_DIV:
			// PUSH (s[-1] / s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(s2.val.val / s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			}
			break;
		case E_OP_MOD:
			// PUSH (s[-1] % s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number((uint8_t)((uint32_t)s2.val.val % (uint32_t)s1.val.val)));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			}
			break;
		case E_OP_AND:
			// PUSH (s[-1] && s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number((uint8_t)s2.val.val && s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_OR:
			// PUSH (s[-1] || s[-2])
			// PUSH (s[-1] + s[-2])
			s1 = e_stack_pop(&vm->stack);
			s2 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number((uint8_t)s2.val.val || s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_NOT:
			// PUSH !s[-1]
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(!s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					fail("Stack overflow");
					goto error;
				}
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
					char buf[E_MAX_STRLEN];
					char num_buf[E_MAX_STRLEN];
					memset(buf, 0, E_MAX_STRLEN);
					memset(num_buf, 0, E_MAX_STRLEN);

					if(s1.val.argtype == E_NUMBER) {
						uint32_t l1 = snprintf(num_buf, E_MAX_STRLEN, "%f", s1.val.val);
						uint32_t len = s2.val.sval.slen + l1;
						if(len > E_MAX_STRLEN) goto error;
						strcat(buf, num_buf);
						strcat(buf, (char*)s2.val.sval.sval);
						buf[len] = 0;
					} else if(s2.val.argtype == E_NUMBER) {
						uint32_t l1 = snprintf(num_buf, E_MAX_STRLEN, "%f", s2.val.val);
						uint32_t len = s1.val.sval.slen + l1;
						if(len > E_MAX_STRLEN) goto error;
						memcpy(buf, (char*)s1.val.sval.sval, s1.val.sval.slen);
						buf[s1.val.sval.slen] = 0;
						strcat(buf, num_buf);
					} else {
						// Array?
						if(s1.val.argtype == E_ARRAY) {
							uint32_t l1 = snprintf(num_buf, E_MAX_STRLEN, "Array<%d> with length %d", s1.val.aval.aptr, s1.val.aval.alen);
							uint32_t len = s2.val.sval.slen + l1;
							if(len + strlen(num_buf) > E_MAX_STRLEN) goto error;
							strcat(buf, num_buf);
							strcat(buf, (char*)s2.val.sval.sval);
							buf[len] = 0;
						} else if(s2.val.argtype == E_ARRAY) {
							uint32_t l1 = snprintf(num_buf, E_MAX_STRLEN, "Array<%d> with length %d", s2.val.aval.aptr, s2.val.aval.alen);
							uint32_t len = s1.val.sval.slen + l1;
							if(len + strlen(num_buf) > E_MAX_STRLEN) goto error;
							memcpy(buf, (char*)s1.val.sval.sval, s1.val.sval.slen);
							buf[s1.val.sval.slen] = 0;
							strcat(buf, num_buf);
						} else {
							fail("Unsupported argtype");
							goto error;
						}
					}

					e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_string(buf));
					if(s_push.status == E_STATUS_NESIZE) {
						fail("Stack overflow");
						goto error;
					}
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
					e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_string(buf1));
					if(s_push.status == E_STATUS_NESIZE) {
						fail("Stack overflow");
						goto error;
					}
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
					vm->ip = d_op;
				}
			} else goto error;
			break;
		case E_OP_JMP:
			// perform_jump()
#if E_DEBUG
			printf("perform jump to address [%f]\n", d_op /** E_INSTR_BYTES*/);
#endif
			vm->ip = d_op;
			break;
		case E_OP_JFS:
			{
				// Get callframe
				if(vm->cfcnt == 0) goto error;
				e_callframe callframe = vm->callframes[vm->cfcnt - 1];

				// Close callframe
				if(vm->cfcnt - 1 >= 0) {
					vm->cfcnt -= 1;
				} else goto error;

				// Return addr
				vm->ip = callframe.retAddr;
			}
			break;
		case E_OP_JMPFUN:
			// Create CallFrame
			{
				e_callframe callframe;
				s1 = e_stack_pop(&vm->stack);
				if(s1.status == E_STATUS_OK) {
					callframe.retAddr = s1.val.val;

					if(vm->cfcnt == 0) {
						callframe.locals = vm->locals;
					} else {
						callframe.locals = vm->callframes[vm->cfcnt - 1].locals;
					}
					vm->callframes[vm->cfcnt] = callframe;

					if(vm->cfcnt + 1 < E_MAX_CALLFRAMES) {
						vm->cfcnt++;
					} else {
						fail("Cannot create another call frame");
						goto error;
					}
				} else goto error;

				vm->ip = d_op;
			}
			break;
		case E_OP_CALL:
			// External function / subroutine call
			s1 = e_stack_pop(&vm->stack);

			if(s1.status == E_STATUS_OK && s1.val.argtype == E_STRING) {
				uint32_t argsbefore = vm->stack.top;

				uint32_t ret_values = e_api_call_sub(vm, (const char*)s1.val.sval.sval, d_op);
				if(ret_values == 0) {
					fail("Unknown function / subroutine");
					goto error;
				} else {
					// Discard all remaining stack values that are unwanted after the function call
					uint32_t A = argsbefore - d_op;	// allowed remaining
					uint32_t argsafter = vm->stack.top;
					uint32_t I = argsafter - (ret_values - 1);

					if(A != I) {
						for (uint32_t i = 0; i < (I - A); i++) {
							if (vm->stack.top > 1) {
								e_stack_swap_last(&vm->stack);
							}
							e_stack_pop(&vm->stack);
						}
					}

					// Return array?
					e_value tmp_arr[E_MAX_ARRAYSIZE];
					uint32_t arr_len = ret_values - 1;
					uint32_t e = 0;
					if(arr_len > 0) {
						e = arr_len - 1;
						while((arr_len--) - 1) {
							s1 = e_stack_pop(&vm->stack);
							if(s1.status == E_STATUS_OK) {
								tmp_arr[e] = s1.val;
							} else goto error;
							e--;
						};

						e_value arr = e_create_array(vm, tmp_arr, ret_values - 1);
						if(arr.aval.alen == (ret_values - 1)) {
							e_stack_status_ret s = e_stack_push(&vm->stack, arr);
							if (s.status != E_STATUS_OK) goto error;
						} else goto error;
					}
				}
			}
			break;
		case E_OP_PRINT:
			e_builtin_print(vm, 1);
			break;
		case E_OP_ARGTYPE:
			e_builtin_argtype(vm, 1);
			break;
		case E_OP_LEN:
			e_builtin_len(vm, 1);
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
e_stack_peek(const e_stack* stack) {
	return e_stack_peek_index(stack, stack->top);
}

e_stack_status_ret
e_stack_peek_index(const e_stack* stack, uint32_t index) {
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

e_stack_status_ret
e_stack_swap_last(e_stack* stack) {
	if(stack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}
	if(stack->top < 2) {
		return (e_stack_status_ret) { .status = E_STATUS_UNDERFLOW };
	}

	e_stack_status_ret tmp1 = e_stack_peek_index(stack, stack->top - 1);
	e_stack_status_ret tmp2 = e_stack_peek_index(stack, stack->top - 2);
	if(tmp1.status == E_STATUS_OK && tmp2.status == E_STATUS_OK) {
		e_stack_status_ret s2 = e_stack_pop(stack); // s1
		e_stack_status_ret s1 = e_stack_pop(stack);	// s2
		if(s1.status == E_STATUS_OK && s2.status == E_STATUS_OK) {
			e_stack_status_ret s_push1 = e_stack_push(stack, s2.val);
			e_stack_status_ret s_push2 = e_stack_push(stack, s1.val);
			if(s_push1.status == E_STATUS_OK && s_push2.status == E_STATUS_OK) {
				return (e_stack_status_ret) { .status = E_STATUS_OK };
			}
		}
	}
	return (e_stack_status_ret) { .status = E_STATUS_UNDERFLOW };
}

e_value
e_create_number(double n) {
	return (e_value){ .val = n, .argtype = E_NUMBER };
}

e_value
e_create_string(const char* str) {
	e_str_type new_str;

	uint32_t slen = strlen(str);
	memcpy(new_str.sval, str, slen);
	new_str.sval[slen] = 0;
	if(strlen((const char*)new_str.sval) != slen) {
		return (e_value) { 0 };
	}
	new_str.slen = slen;

	return (e_value) { .sval = new_str, .argtype = E_STRING };
}

e_value
e_create_array(e_vm* vm, e_value* arr, uint32_t arrlen) {
	if(vm->acnt + 1 >= E_MAX_ARRAYS) {
		fail("Cannot initialize another array");
		return (e_value) {0};
	}

	for(uint32_t i = 0; i < arrlen && i < E_MAX_ARRAYSIZE; i++) {
		uint8_t s = e_array_append(vm, vm->acnt, arr[i]);
		if(!s) {
			return (e_value) { 0 };
		}
	}

	return (e_value) { .argtype = E_ARRAY, .aval.aptr = vm->acnt++, .aval.alen = arrlen };
}

uint8_t
e_array_append(e_vm* vm, uint32_t aptr, e_value v) {
	if(aptr >= E_MAX_ARRAYS) return 0;

	uint32_t e = 0;
	while(vm->arrays[aptr][e].used) {
		if(++e >= E_MAX_ARRAYSIZE) {
			return 0;
		}
	}
	vm->arrays[aptr][e].v = v;
	vm->arrays[aptr][e].used = 1;
	return 1;
}

uint8_t
e_find_value_in_arr(const e_vm* vm, uint32_t aptr, uint32_t index, e_value* vptr) {
	if(aptr >= E_MAX_ARRAYS || index >= E_MAX_ARRAYSIZE) return 0;

	e_array_entry e = vm->arrays[aptr][index];
	*vptr = e.v;
	return e.used;
}

uint8_t
e_change_value_in_arr(e_vm* vm, uint32_t aptr, uint32_t index, e_value v) {
	if(aptr >= E_MAX_ARRAYS || index >= E_MAX_ARRAYSIZE) return 0;

	if(vm->arrays[aptr][index].used) {
		vm->arrays[aptr][index].v = v;
	}
	return 0;
}

void
e_dump_stack(const e_stack* stack) {
	if(stack == NULL) return;

	printf("| ENTRIES IN GIVEN STACK: %d (%d total)\n", stack->top, stack->size);

	for(uint32_t i = 0; i < stack->size; i++) {
		e_value e = stack->entries[i];
		if(e.argtype == E_NUMBER || e.argtype == E_STRING ||e.argtype == E_ARRAY) {
			switch(e.argtype) {
				case E_NUMBER:
					printf("| [%d] - NUMBER - %f\n", i, e.val);
					break;
				case E_STRING:
					printf("| [%d] - STRING (%d bytes) - %s\n", i, e.sval.slen, e.sval.sval);
					break;
				case E_ARRAY:
					printf("| [%d] - ARRAY (%d entries) beginning at addr %d\n", i, e.aval.alen, e.aval.aptr);
					break;
				default:
					printf("| INVALID TYPE\n");
			}
		}
	}
	printf("| ---------------------------------------|\n\n");
}

void
fail(const char* msg) {
	printf("%s\n", msg);
}

// C-API
e_stack_status_ret
e_api_stack_push(e_stack* stack, e_value v) {
	return e_stack_push(stack, v);
}


e_stack_status_ret
e_api_stack_pop(e_stack* stack) {
	return e_stack_pop(stack);
}

void
e_api_register_sub(const char* identifier, uint32_t (*fptr)(e_vm*, uint32_t)) {
	static uint32_t m_index = 0;
	if(m_index + 1 < E_MAX_EXTIDENTIFIERS) {
		if(strlen(identifier) < E_MAX_EXTIDENTIFIERS_STRLEN) {
			strcpy(e_external_map[m_index].identifier, identifier);
			if(strlen(identifier) == strlen(e_external_map[m_index].identifier)) {
				e_external_map[m_index].fptr = fptr;
				m_index++;
				return;
			}
		}
	}

	fail("Cannot register another subroutine");
}

uint8_t
e_api_call_sub(e_vm* vm, const char* identifier, uint32_t arglen) {
	uint32_t slen = strlen(identifier);
	if(slen > E_MAX_EXTIDENTIFIERS_STRLEN) return 0;

	for(uint32_t i = 0; i < E_MAX_EXTIDENTIFIERS; i++) {
		if(strncmp(identifier, e_external_map[i].identifier, slen) == 0) {
			// Call external function through fp
			return 1 + e_external_map[i].fptr(vm, arglen);
		}
	}

	return 0;
}