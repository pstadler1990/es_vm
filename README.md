# esvm
Virtual machine for the `evoscript` language.

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
        e_stack_status_ret a1 = e_stack_pop(&vm->stack);
        if(a1.status == E_STATUS_OK && a1.val.argtype == E_NUMBER) {
            // Push a1 value * 2 onto stack
            e_stack_push(&vm->stack, e_create_number(a1.val.val * 2));
            return 1;
        }
    }

    return 0;
}
```
