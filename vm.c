//
// es_vm
//

#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "vm_builtins.h"

#define E_DEBUG 0

#if E_DEBUG
char dbg_s[E_MAX_STRLEN];
#endif

#define E_USE_LOCK 0

e_external_mapping e_external_map[E_MAX_EXTIDENTIFIERS];

static uint8_t e_find_value_in_arr(const e_vm* vm, e_value arr, uint32_t index, e_value* vptr);
static uint8_t e_change_value_in_arr(e_vm* vm, uint32_t aptr, uint32_t index, e_value v, uint32_t global_local);
static uint8_t e_array_append(e_vm* vm, uint32_t aptr, uint32_t index, e_value v, uint32_t global_local);

// Stack
static void e_stack_init(e_stack* stack, uint32_t size);
static e_stack_status_ret e_stack_push(e_stack* stack, e_value v);
static e_stack_status_ret e_stack_pop(e_stack* stack);
static e_stack_status_ret e_stack_peek_index(const e_stack* stack, uint32_t index);
static e_stack_status_ret e_stack_insert_at_index(e_stack* stack, e_value v, uint32_t index);
static e_stack_status_ret e_stack_swap_last(e_stack* stack);

// Varstack
static void e_varstack_init(e_value* varstack, uint32_t size);
static e_stack_status_ret e_varstack_peek_index(const e_value* varstack, uint32_t index);
static e_stack_status_ret e_varstack_insert_global_at_index(e_value* varstack, e_value v, uint32_t index);
static e_stack_status_ret e_varstack_insert_local_at_index(e_value* varstack, e_value v, uint32_t index);

// VM
void
e_vm_init(e_vm* vm) {
	if(vm == NULL) return;
	vm->ip = 0;
	e_stack_init(&vm->stack, E_STACK_SIZE);
	e_varstack_init(vm->globals, E_MAX_GLOBALS);
	e_varstack_init(vm->locals, E_MAX_LOCALS);
	vm->pupo_is_data = 0;
	vm->pupo_arr_index = -1;
	vm->ds_offset = 0;
	for(uint32_t i = 0; i < E_MAX_LOCALS; i++) {
		for(uint32_t e = 0; e < E_MAX_ARRAYSIZE; e++) {
			vm->arrays_local[i][e] = (e_array_entry) { .v = {{ 0 }}, .used = 0 };
		}
	}
	for(uint32_t i = 0; i < E_MAX_GLOBALS; i++) {
		for(uint32_t e = 0; e < E_MAX_ARRAYSIZE; e++) {
			vm->arrays_global[i][e] = (e_array_entry) { .v = {{ 0 }}, .used = 0 };
		}
	}
	vm->cfcnt = 0;
	vm->status = E_VM_STATUS_READY;
}

e_vm_status
e_vm_parse_bytes(e_vm* vm, uint32_t script_offset, uint32_t blen) {
	if(blen == 0) return E_VM_STATUS_EOF;

	vm->ds_offset = script_offset;

	do {
#if E_DEBUG
		snprintf(dbg_s, E_MAX_STRLEN, "** IP: %d ** ", vm->ip);
		e_print(dbg_s);
#endif


#if E_USE_LOCK
		if (!e_check_locked()) {
#endif
			e_instr cur_instr;
			uint32_t ip_begin = vm->ip;
			uint8_t next_bytes[E_INSTR_BYTES - 1] = { 0 };

			cur_instr.OP = e_read_byte(vm->ds_offset + vm->ip);
			if (!sb_ops[cur_instr.OP]) {
				next_bytes[0] = e_read_byte(vm->ds_offset + vm->ip + 1);
				next_bytes[1] = e_read_byte(vm->ds_offset + vm->ip + 2);
				next_bytes[2] = e_read_byte(vm->ds_offset + vm->ip + 3);
				next_bytes[3] = e_read_byte(vm->ds_offset + vm->ip + 4);
				next_bytes[4] = e_read_byte(vm->ds_offset + vm->ip + 5);
				next_bytes[5] = e_read_byte(vm->ds_offset + vm->ip + 6);
				next_bytes[6] = e_read_byte(vm->ds_offset + vm->ip + 7);
				next_bytes[7] = e_read_byte(vm->ds_offset + vm->ip + 8);

				cur_instr.op1 = (uint32_t) ((next_bytes[0] << 24u) | (next_bytes[1] << 16u) |
											(next_bytes[2] << 8u) | next_bytes[3]);
				cur_instr.op2 = (uint32_t) ((next_bytes[4] << 24u) | (next_bytes[5] << 16u) |
											(next_bytes[6] << 8u) | next_bytes[7]);
				vm->ip += 9;

				uint32_t ip_end = vm->ip;
				if (ip_end - ip_begin != E_INSTR_BYTES) {
					e_fail("Instruction size / offset error");
					return E_VM_STATUS_ERROR;
				}
			} else {
				vm->ip++;

				uint32_t ip_end = vm->ip;
				if (ip_end - ip_begin != E_INSTR_SINGLE_BYTES) {
					e_fail("Instruction size / offset error");
					return E_VM_STATUS_ERROR;
				}
			}

#if E_DEBUG
			snprintf(dbg_s, E_MAX_STRLEN, "Fetched instruction -> [0x%02X] (0x%02X, 0x%02X)\n", cur_instr.OP, cur_instr.op1, cur_instr.op2);
			e_print(dbg_s);
#endif

			e_vm_status es = e_vm_evaluate_instr(vm, cur_instr);
			if (es != E_VM_STATUS_OK) {
				e_fail("Invalid instruction or malformed arguments - STOPPED EXECUTION");
				return E_VM_STATUS_ERROR;
			}

		}

#if E_USE_LOCK
	}
#endif
	while (vm->ip < blen);

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

				e_value arr = e_create_array(vm, tmp_arr, arr_len, d_op, E_ARRAY_GLOBAL);
				if(arr.aval.alen == arr_len) {
					e_stack_status_ret s = e_varstack_insert_global_at_index(vm->globals, arr, d_op);
					vm->pupo_is_data = 0;

					if (s.status != E_STATUS_OK) goto error;
				} else goto error;
			} else {
					e_stack_status_ret s_peek = e_varstack_peek_index(vm->globals, d_op);
					if(s_peek.val.argtype == E_ARRAY) {
						/* Array access based on index */
						if(vm->pupo_arr_index >= 0) {
							e_stack_status_ret s_value = e_stack_pop(&vm->stack);
							if(s_value.status == E_STATUS_OK) {
								e_change_value_in_arr(vm, s_peek.val.aval.aptr, vm->pupo_arr_index, s_value.val, E_ARRAY_GLOBAL);
							} else {
								e_fail("Array out of bounds");
								goto error;
							}
							vm->pupo_arr_index = -1;
						}
					} else {
						s1 = e_stack_pop(&vm->stack);
						if(s1.status == E_STATUS_OK) {
#if E_DEBUG
						snprintf(dbg_s, E_MAX_STRLEN, "Storing value %f to global stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
						e_print(dbg_s);
#endif
						if (instr.op2 == E_ARGT_STRING) {
							s1.val.argtype = E_STRING;
						}
						e_stack_status_ret s = e_varstack_insert_global_at_index(vm->globals, s1.val, d_op);
						if (s.status != E_STATUS_OK) goto error;
					} else goto error;
				}
			}
			break;
		case E_OP_POPG:
			// Find value [index] in global stack
			{
				e_stack_status_ret s = e_varstack_peek_index(vm->globals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					snprintf(dbg_s, E_MAX_STRLEN, "Loading global from index %d -> %f\n", instr.op1, s.val.val);
					e_print(dbg_s);
#endif
					if(s.val.argtype == E_ARRAY) {
						/* Array access based on index */
						if(vm->pupo_arr_index >= 0) {
							e_value v;
							if(e_find_value_in_arr(vm, s.val, vm->pupo_arr_index, &v)) {
								e_stack_status_ret s_push = e_stack_push(&vm->stack, v);
								if(s_push.status == E_STATUS_NESIZE) {
									e_fail("Stack overflow");
									goto error;
								}
							} else {
								/* Out of bounds */
								e_fail("Array out of bounds");
								goto error;
							}
							vm->pupo_arr_index = -1;
						} else {
							/* Array pass-by-value? */
							e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
							if(s_push.status == E_STATUS_NESIZE) {
								e_fail("Stack overflow");
								goto error;
							}
						}
					} else {
						// Push this value onto the vm->stack
						e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
						if(s_push.status == E_STATUS_NESIZE) {
							e_fail("Stack overflow");
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

				e_value arr = e_create_array(vm, tmp_arr, arr_len, d_op, E_ARRAY_LOCAL);
				e_stack_status_ret s;

				if(vm->cfcnt > 0) {
					s = e_stack_insert_at_index(&vm->callframes[vm->cfcnt - 1].locals, arr, d_op);
				} else {
					s = e_varstack_insert_local_at_index(vm->locals, arr, d_op);
				}
				vm->pupo_is_data = 0;

				if (s.status != E_STATUS_OK) goto error;
			} else {
				e_stack_status_ret s_peek;

				if(vm->cfcnt > 0) {
					s_peek = e_stack_peek_index(&vm->callframes[vm->cfcnt - 1].locals, d_op);
				} else {
					s_peek = e_varstack_peek_index(vm->locals, d_op);
				}
				if(s_peek.val.argtype == E_ARRAY) {
					/* Array access based on index */
					if(vm->pupo_arr_index >= 0) {
						e_stack_status_ret s_value = e_stack_pop(&vm->stack);
						if(s_value.status == E_STATUS_OK) {
							e_change_value_in_arr(vm, s_peek.val.aval.aptr, vm->pupo_arr_index, s_value.val, E_ARRAY_LOCAL);
						} else {
							e_fail("Array out of bounds");
							goto error;
						}
						vm->pupo_arr_index = -1;
					}
				} else {
					s1 = e_stack_pop(&vm->stack);
					if (s1.status == E_STATUS_OK) {
#if E_DEBUG
						if(instr.op2 == E_ARGT_STRING) {
							snprintf(dbg_s, E_MAX_STRLEN, "Storing value %s to local stack [%d] (type: %d)\n", s1.val.sval.sval, instr.op1, instr.op2);
							e_print(dbg_s);
						} else if(instr.op2 == E_ARGT_NUMBER) {
							snprintf(dbg_s, E_MAX_STRLEN, "Storing value %f to local stack [%d] (type: %d)\n", s1.val.val, instr.op1, instr.op2);
							e_print(dbg_s);
						}
#endif
						e_stack_status_ret s;
						if(vm->cfcnt > 0) {
							s = e_stack_insert_at_index(&vm->callframes[vm->cfcnt - 1].locals, s1.val, d_op);
						} else {
							s = e_varstack_insert_local_at_index(vm->locals, s1.val, d_op);
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
					s = e_varstack_peek_index(vm->locals, d_op);
				}

				//e_stack_status_ret s = e_stack_peek_index(&vm->locals, d_op);
				if(s.status == E_STATUS_OK) {
#if E_DEBUG
					snprintf(dbg_s, E_MAX_STRLEN, "Loading local from index %d -> %f\n", instr.op1, s.val.val);
					e_print(dbg_s);
#endif
					if(s.val.argtype == E_ARRAY) {
						/* Array access based on index */
						if(vm->pupo_arr_index >= 0) {
							e_value v;
							if(e_find_value_in_arr(vm, s.val, vm->pupo_arr_index, &v)) {
								e_stack_status_ret s_push = e_stack_push(&vm->stack, v);
								if(s_push.status == E_STATUS_NESIZE) {
									e_fail("Stack overflow");
									goto error;
								}
							} else {
								/* Out of bounds */
								e_fail("Array out of bounds");
							}
							vm->pupo_arr_index = -1;
						} else {
							/* Array pass-by-value? */
							e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
							if(s_push.status == E_STATUS_NESIZE) {
								e_fail("Stack overflow");
								goto error;
							}
						}
					} else {
						// Push this value onto the vm->stack
						e_stack_status_ret s_push = e_stack_push(&vm->stack, s.val);
						if(s_push.status == E_STATUS_NESIZE) {
							e_fail("Stack overflow");
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
					e_fail("Stack overflow");
					goto error;
				}
			}
			break;
		case E_OP_PUSHS:
			// Push string onto stack
			{
				char tmp_str[E_MAX_STRLEN];
				if(d_op < E_MAX_STRLEN - 1) {
					for(uint32_t i = 0; i < d_op && i < E_MAX_STRLEN; i++) {
						tmp_str[i] = e_read_byte(vm->ds_offset + vm->ip + i);
					}
					tmp_str[(uint32_t)d_op] = 0;
					e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_string(tmp_str));
					if(s_push.status == E_STATUS_NESIZE) {
						e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
					goto error;
				}
			} else goto error;
			break;
		case E_OP_NEG:
			s1 = e_stack_pop(&vm->stack);
			if(s1.status == E_STATUS_OK) {
				e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_number(-s1.val.val));
				if(s_push.status == E_STATUS_NESIZE) {
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
					e_fail("Stack overflow");
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
							e_fail("Unsupported argtype");
							goto error;
						}
					}

					e_stack_status_ret s_push = e_stack_push(&vm->stack, e_create_string(buf));
					if(s_push.status == E_STATUS_NESIZE) {
						e_fail("Stack overflow");
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
						e_fail("Stack overflow");
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
					snprintf(dbg_s, E_MAX_STRLEN, "is zero, perform jump to address [%f]\n", d_op);
					e_print(dbg_s);
#endif
					// Perform jump
					vm->ip = d_op;
				}
			} else goto error;
			break;
		case E_OP_JMP:
			// perform_jump()
#if E_DEBUG
			snprintf(dbg_s, E_MAX_STRLEN, "perform jump to address [%f]\n", d_op);
			e_print(dbg_s);
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
				e_callframe callframe = { 0 };
				s1 = e_stack_pop(&vm->stack);
				if(s1.status == E_STATUS_OK) {
					callframe.retAddr = s1.val.val;

					if(vm->cfcnt == 0) {
						memcpy(callframe.locals.entries, vm->locals, E_MAX_LOCALS);
					} else {
						callframe.locals = vm->callframes[vm->cfcnt - 1].locals;
					}
					vm->callframes[vm->cfcnt] = callframe;

					if(vm->cfcnt + 1 < E_MAX_CALLFRAMES) {
						vm->cfcnt++;
					} else {
						e_fail("Cannot create another call frame");
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
				int32_t tmp_stat = e_api_call_sub(vm, (const char*)s1.val.sval.sval, d_op);
				if(tmp_stat == -1) {
					char tmp[E_MAX_STRLEN + 30];
					snprintf(tmp, E_MAX_STRLEN + 30, "Unknown function / subroutine %s", s1.val.sval.sval);
					e_fail(tmp);
					goto error;
				} else if(tmp_stat == 0) {
					char tmp[E_MAX_STRLEN + 30];
					snprintf(tmp, E_MAX_STRLEN + 30, "Error in external function %s", s1.val.sval.sval);
					e_fail(tmp);
					goto error;
				} else {
					uint32_t ret_values = (uint32_t)tmp_stat;
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
					uint32_t arr_len = ret_values - 1;
					if(arr_len > 1) {
						vm->pupo_is_data = arr_len;
					}
				}
			}
			break;
		case E_OP_PRINT:
			e_builtin_print(vm, 1);
			break;
		case E_OP_ARGTYPE:
			{
				uint32_t r = e_builtin_argtype(vm, 1);
				if(r == 0) {
					e_fail("Builtin function ARGTYPE failed");
					goto error;
				}
			}
			break;
		case E_OP_LEN:
			{
				uint32_t r = e_builtin_len(vm, 1);
				if(r == 0) {
					e_fail("Builtin function LEN failed");
					goto error;
				}
			}
			break;
		case E_OP_ARRAY:
			{
				uint32_t r = e_builtin_array(vm, 1);
				if(r == 0) {
					e_fail("Builtin function ARRAY failed");
					goto error;
				}
			}
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

// Globals / Locals
void
e_varstack_init(e_value* varstack, uint32_t size) {
	if(varstack == NULL) {
		return;
	}
	memset(varstack, 0, (sizeof(e_value) * size));
}

e_stack_status_ret
e_varstack_peek_index(const e_value* varstack, uint32_t index) {
	if(varstack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}
	return (e_stack_status_ret) { .status = E_STATUS_OK, .val = varstack[index] };
}

e_stack_status_ret
e_varstack_insert_global_at_index(e_value* varstack, e_value v, uint32_t index) {
	if(varstack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}

	if(index >= E_MAX_GLOBALS) {
		return (e_stack_status_ret) { .status = E_STATUS_NESIZE };
	}

	varstack[index] = v;
	return (e_stack_status_ret) { .status = E_STATUS_OK };
}

e_stack_status_ret
e_varstack_insert_local_at_index(e_value* varstack, e_value v, uint32_t index) {
	if(varstack == NULL) {
		return (e_stack_status_ret) { .status = E_STATUS_NOINIT };
	}

	if(index >= E_MAX_LOCALS) {
		return (e_stack_status_ret) { .status = E_STATUS_NESIZE };
	}

	varstack[index] = v;
	return (e_stack_status_ret) { .status = E_STATUS_OK };
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
e_create_array(e_vm* vm, e_value* arr, uint32_t arrlen, uint32_t index, uint32_t global_local) {
	for(uint32_t i = 0; i < arrlen && i < E_MAX_ARRAYSIZE; i++) {
		uint8_t s = e_array_append(vm, index, i, arr[i], global_local);
		if(!s) {
			return (e_value) { 0 };
		}
	}

	return (e_value) { .argtype = E_ARRAY, .aval.aptr = index, .aval.alen = arrlen, .aval.global_local = global_local };
}

uint8_t
e_array_append(e_vm* vm, uint32_t aptr, uint32_t index, e_value v, uint32_t global_local) {
	if(global_local == E_ARRAY_GLOBAL) {
		if(aptr >= E_MAX_GLOBALS) return 0;

		vm->arrays_global[aptr][index].v = v;
		vm->arrays_global[aptr][index].used = 1;
		return 1;
	} else {
		if(aptr >= E_MAX_LOCALS) return 0;

		vm->arrays_local[aptr][index].v = v;
		vm->arrays_local[aptr][index].used = 1;
		return 1;
	}
}

uint8_t
e_find_value_in_arr(const e_vm* vm, e_value arr, uint32_t index, e_value* vptr) {
	if(arr.aval.global_local == E_ARRAY_GLOBAL) {
		if(index >= E_MAX_ARRAYSIZE) return 0;

		e_array_entry e = vm->arrays_global[arr.aval.aptr][index];
		*vptr = e.v;
		return e.used;
	} else {
		if(index >= E_MAX_ARRAYSIZE) return 0;

		e_array_entry e = vm->arrays_local[arr.aval.aptr][index];
		*vptr = e.v;
		return e.used;
	}
}

uint8_t
e_change_value_in_arr(e_vm* vm, uint32_t aptr, uint32_t index, e_value v, uint32_t global_local) {
	if(global_local == E_ARRAY_GLOBAL) {
		if(aptr >= E_MAX_GLOBALS) return 0;
		if(index >= E_MAX_ARRAYSIZE) return 0;

		if(vm->arrays_global[aptr][index].used) {
			vm->arrays_global[aptr][index].v = v;
			return 1;
		}
	} else {
		if(aptr >= E_MAX_LOCALS) return 0;
		if(index >= E_MAX_ARRAYSIZE) return 0;

		if(vm->arrays_local[aptr][index].used) {
			vm->arrays_local[aptr][index].v = v;
			return 1;
		}
	}
	return 0;
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

	e_fail("Cannot register another subroutine");
}

int32_t
e_api_call_sub(e_vm* vm, const char* identifier, uint32_t arglen) {
	uint32_t slen = strlen(identifier);
	if(slen > E_MAX_EXTIDENTIFIERS_STRLEN) return 0;

	for(uint32_t i = 0; i < E_MAX_EXTIDENTIFIERS; i++) {
		if(strncmp(identifier, e_external_map[i].identifier, slen) == 0) {
			// Call external function through fp
			return e_external_map[i].fptr(vm, arglen);
		}
	}

	return -1;
}