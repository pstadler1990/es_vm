# esvm
Virtual machine for the `evoscript` language.

## Initialization
Use `e_vm_init(..)` to initialize a new vm context:

```c
e_vm context;
...

e_vm_init(&context);
```

To start the byte interpreter, use the `e_vm_parse_bytes(..)` function:

```c
uint8_t bytes_in[BUF_SIZE];
...
bytes_in = {..};
...

e_vm_parse_bytes(&context, bytes_in, BUF_SIZE);
```

## Function / Subroutine binding
To call `C` functions / routines from within the `evoscript` scripting environment, 
you need to register the `C` functions first:

```
// Register API functions
e_api_register_sub("my_external_func", &e_ext_my_external_func);
```

The `e_api_register_sub` function takes a struct of type `e_external_mapping` which is defined as:

```
char identifier[E_MAX_EXTIDENTIFIERS_STRLEN];
uint32_t (*fptr)(e_vm* vm, uint32_t arglen);
```

Pass your desired `C` function to the `fptr` (`vm` is a pointer to the current vm context, `arglen` contains the number
of passed arguments from the `evoscript` scripting environment).

**Important** Ensure to register all required functions before initializing the `e_vm` context!

### Using C functions
Inside a `C` API function you have full access to the current `e_vm` context, including all it's variables and it's stack(s).

A simple example that shows most of the C APIs functionality is shown below:
```c
uint32_t e_ext_my_external_func(e_vm* vm, uint32_t arglen) {
    printf("<- Called my external func in C (passed %d arguments)\n", arglen);

    
    if(arglen > 0) {
        // Get one value from the stack
        e_stack_status_ret a1 = e_api_stack_pop(&vm->stack);
        if(a1.status == E_STATUS_OK && a1.val.argtype == E_NUMBER) {
            // Push a1 value * 2 onto stack
            e_api_stack_push(&vm->stack, e_create_number(a1.val.val * 2));
            return E_API_CALL_RETURN_OK(1); // 1 value is returned sucessfully
        }
    }

    return E_API_CALL_RETURN_ERROR; // the function failed
}
```

#### Return values from C functions / subs
Use the following macros to return from a `C` API function:

```c
E_API_CALL_RETURN_OK(n)     // successfully returned n values
E_API_CALL_RETURN_ERROR     // function failed
```

See the example above on how to use the macros.


#### Stack popping and pushing
To pop values from the stack, use the `e_api_stack_pop()` respectively `e_api_stack_push()` functions.
These functions will always return a `status` code and the value in case of `push`ing.

**Important** When `pop`ping from the stack, ensure that there are sufficient values on the stack! Use the `arglen` argument to check for the number of given arguments. As the compiler knows nothing about the external functions, it cannot ensure the correct number of arguments in the external function calls.

##### Type checking
Use the `argtype` field to check the type of the values:
```c
e_stack_status_ret a1 = e_api_stack_pop(&vm->stack);
if(a1.status == E_STATUS_OK && a1.val.argtype == E_NUMBER) {
    // a1 is valid and contains value of type E_NUMBER
}
```

When `push`ing, make sure the `return` the number of pushed values from the function, i.e. when pushing 4 values onto the stack using the `e_api_stack_push()` functions,
`return 4`. It is important to use the right `return` value, otherwise the virtual machine will fail after the call operation.

`push`ing more than a single value will result in an automatically `array` conversion after the `call` statement!

##### Create values for pushing
To push anything onto the stack you need to create a suitable `e_value` type. You can use the utility functions for each supported type:

```c
// These functions return a new e_value type
e_create_number(double n);
e_create_string(const char* s);

// Arrays are a bit different as they require the vm context
// arr is an array of e_values, arrlen is the new array's length
e_create_array(e_vm* vm, e_value* arr, uint32_t arrlen);
```

## Implementing required functions
The `evoscript` VM requires you to implement some functions within your target application:

| Function name | Arguments | Returned values | Description |
| ------------- | --------- | --------------- | ----------- |
| `e_print()` | `const char* msg` | `void` | Standard message printing function |
| `e_fail()` | `const char* msg` | `void` | Standard error printing function |
| `e_check_locked()` | `void` | `uint8` | Function to return whether the vm is currently locked |

You can find dummies for these functions in `vm_builtins.c`.