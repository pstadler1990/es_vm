//
// es_vm
//

#ifndef ES_VM_H
#define ES_VM_H

#include <stdint.h>

#define	E_STACK_SIZE ((uint32_t)1024)
#define	E_INSTR_BYTES ((uint32_t)9)
#define E_OUT_DS_SIZE       ((int)500)	// FIXME
#define E_OUT_SIZE          ((int)0/*2000*/)	// FIXME
#define E_OUT_TOTAL_SIZE    ((int)E_OUT_DS_SIZE + E_OUT_SIZE)

#define E_MAX_STRLEN    ((int)1024)
#define E_MAX_ARRAYSIZE ((int)512)
typedef enum {
	E_ARGT_NULL = 2,
	E_ARGT_NUMBER = 0,
	E_ARGT_STRING = 1
} e_arg_type;

typedef enum {
	E_CONCAT_FIRST,
	E_CONCAT_SECOND,
	E_CONCAT_BOTH
} e_concat_type;

typedef enum {
	E_STATUS_NESTING = -7,
	E_STATUS_UNDERFLOW = -6,
	E_STATUS_DATATMIS = -5,
	E_STATUS_NOTFOUND = -4,
	E_STATUS_ALRDYDEF = -3,
	E_STATUS_NESIZE = -2,
	E_STATUS_NOINIT = -1,
	E_STATUS_UNDEF = 0,
	E_STATUS_OK = 1,
	E_STATUS_REQ = 2,
} e_statusc;

typedef enum {
	E_VM_STATUS_ERROR = -1,
	E_VM_STATUS_READY = 0,
	E_VM_STATUS_OK = 1,
	E_VM_STATUS_EOF = 2
} e_vm_status;

// ES Types
typedef struct {
	uint8_t* sval;
	uint32_t slen;
} e_str_type;

typedef struct {
	uint8_t** aptr;
	uint32_t alen;
} e_array_type;

typedef struct {
	union {
		double val;
		e_str_type sval;
		e_array_type aval;
	};
	enum {E_NUMBER, E_STRING, E_ARRAY } argtype;
} e_value;

// Stack
typedef struct {
	e_value entries[E_STACK_SIZE];
	uint32_t size;
	uint32_t top;
} e_stack;

typedef struct {
	e_statusc status;
	e_value val;
} e_stack_status_ret;

// VM
typedef struct {
	uint32_t ip;
	e_stack stack;
	e_stack globals;
	e_stack locals;
	e_vm_status status;

	uint8_t ds[E_OUT_DS_SIZE];
	uint32_t dscnt;

	uint8_t pupo_is_data;
} e_vm;

// OPCODES
typedef enum {
	E_OP_NOP = 0x00,	   /* NOP																		    */
	E_OP_PUSHG = 0x10,     /* Push global variable,                    PUSHG [index],      s[-1]           	*/
	E_OP_POPG = 0x11,      /* Pop global variable,                     POPG [index]        [s-1]         	*/
	E_OP_PUSHL = 0x12,     /* Push local variable,                     PUSHL [index]                       	*/
	E_OP_POPL = 0x13,      /* Pop local variable,                      POPG [index]        [s-1]         	*/
	E_OP_PUSH = 0x14,      /* Push variable onto top of stack,         PUSH 3                              	*/
	E_OP_POP = 0x15,       /* Pop variable from top of stack,          POP,                s[-1]           	*/
	E_OP_PUSHS = 0x16,	   /* Push string 							   PUSHS [ascii byte(s)] 				*/
	E_OP_DATA = 0x17,	   /* Size of following data segment,		   DATA [entries]	   s[-entries]		*/

	E_OP_EQ = 0x20,        /* Equal check,                             EQ,                 s[-1]==s[-2]    	*/
	E_OP_LT = 0x21,        /* Less than,                               LT,                 s[-1]<s[-2]     	*/
	E_OP_GT = 0x22,        /* Greater than,                            GT,                 s[-1]<s[-2]    	*/
	E_OP_LTEQ = 0x23,      /* Less than or equal,                      LTEQ,               s[-1]<=s[-2]    	*/
	E_OP_GTEQ = 0x24,      /* Greater than or equal,                   GTEQ,               s[-1]>=s[-2]    	*/
	E_OP_NOTEQ = 0x25,	   /* Not equal check,						   NOTEQ,			   s[-1]!=[s-2]		*/

	E_OP_ADD = 0x30,
	E_OP_SUB = 0x32,
	E_OP_MUL = 0x33,
	E_OP_DIV = 0x34,
	E_OP_AND = 0x35,
	E_OP_OR = 0x36,
	E_OP_NOT = 0x37,
	E_OP_CONCAT = 0x38,    /* Concatenate strings                       CONCAT              s[s-1].[s-2]   */
	E_OP_MOD = 0x39,       /* Modulo                                    MOD                 s[-1] % s[-2]  */

	E_OP_JZ = 0x40,        /* Jump if zero,                            JZ [addr]                           */
	E_OP_JMP = 0x41,       /* unconditional jump,                      JMP [addr]                          */

	E_OP_PRINT  = 0x50,    /* Print statement (debug)                  PRINT(expr)                         */
} e_opcode;

typedef struct {
	e_opcode OP;
	uint32_t op1;
	uint32_t op2;
} e_instr;

// Built-ins
uint32_t e_builtin_print(e_vm* vm, uint32_t arglen);

// VM
void e_vm_init(e_vm* vm);
e_vm_status e_vm_parse_bytes(e_vm* vm, const uint8_t bytes[], uint32_t blen);
e_vm_status e_vm_evaluate_instr(e_vm* vm, e_instr instr);
e_value e_create_number(double n);
e_value e_create_string(const char* str);
e_value e_create_array(e_vm* vm, e_value* arr, uint32_t arrlen);

// Stack
void e_stack_init(e_stack* stack, uint32_t size);
e_stack_status_ret e_stack_push(e_stack* stack, e_value v);
e_stack_status_ret e_stack_pop(e_stack* stack);
e_stack_status_ret e_stack_peek(e_stack* stack);
e_stack_status_ret e_stack_peek_index(e_stack* stack, uint32_t index);
e_stack_status_ret e_stack_insert_at_index(e_stack* stack, e_value v, uint32_t index);

// Debug
void e_debug_dump_stack(const e_vm* vm, const e_stack* tab);

#endif //ES_VM_H
