
# Fox Script

Fox Script is a scripting language designed for use in the Foxtrot game engine. It is a very simple language designed primarily for:

* Calling native engine functions
* Passing values to the engine and game
* Basic conditional logic that can be easily updated
* Adding code separation engine, replacing baked-in values with configurable scripts and the ability to build game-tailored features at a language level.

This is NOT designed to be a general purpose language and is tailored to the game engine.

For fast execution, each script is compiled to bytecode before being executed by the VM.

## Bytecode 

All operations are defined in `FoxBytecode.hpp`.

Each opcode is 16 bits, or 2 bytes. Each opcode is broken down into two components:

* Base component: The purpose or operation that the instruction performs.
* Specialization component: The specialized function for the base operation.

For example, popping a float off of the stack into a variable would use the base component `BcBase_Pop`, and the specialization component `BcSpecPop_Variable_Float32` to build the final opcode.



## Calling C++ functions from Fox Script

To add a native function with fox script, you will need to add both a definition in a script file and in the C++.

The syntax to define an external procedure is similar to the syntax to define a normal procedure.
Syntax:
```
extproc [identifier] ( [arguments...]  ) [return type] ;
```

For example, to define a native function that adds two numbers:
```
extproc NativeAddition(int x, int y) int;
```

Which can then be defined in C++ by
```
script::FoxVM vm;

// ...

vm.RegisterFunction(
    HashStr32("NativeAddition"),   // Function name hash
    2,                             // Number of arguments
    &NativeAddition_Impl           // Function handle
);

// Implmentation
void NativeAddition_Impl(script::FoxVM* vm, const SizedArray<script::FoxValue>& args)
{
    int32 x = args[0].Get<int32>();
    int32 y = args[1].Get<int32>();
    
    const int32 result = x + y;
    
    // Return the integer value
    vm->PushValue<int32>(result);
}

```


## Calling Fox Script functions from C++

Calling a function in Fox Script is extremely simple:

```
script::FoxVM vm;

// ...

script::VMSymbol* entrypoint = vm.GetSymbol(HashStr32("ScriptEntry"));

SizedArray<FoxValue> args = { FoxValue(20.0f) };
vm.CallFunction(entrypoint, args);

```

The symbol retrieved can be saved if it is used often in a hot path.



# Language Syntax

## Variables

To define a new variable, use either the `local` or `global` keyword as well as the type and then identifier for the variable.


For example:
```
// Variable local to the current scope
local int x;

// Variable that is a reference to a globally accessible variable
global int OBJECTID;

// Both types can also be initialized with a value
local int y = 10;
global int z = 4;
```

Currently, the only builtin types are `int` and `float`.

## Procedures

To separate code, you can define procedures.

```
proc FireWeapon(int gusto)
{
    global float DIRX;
    global float DIRY;
    global float DIRZ;
    
    global int WEAPON_DAMAGE;    
    local int damage = WEAPON_DAMAGE + gusto;

    N_SetDirection(DIRX, DIRY, DIRZ);
    N_FireWeaponInternalCall(damage);
}
```

To return values from procedures, follow the definition with a type:

```
proc ProcThatReturns(int x, int y) int
{
    return x + y;
}
```


## Conditionals

If statements in Fox work in the same way as C.
```
if (x == 10) {
    return 0;
}
else if (x == 5) {
    return 1;
}
else {
    return 2;
}

// Braces are optional, but encouraged

if (x == 5)
    return 2;
else
    return 1;


```

## Scoping and Blocks

Blocks can be anywhere in a function body. Any variables defined inside will be invalidated at the end of the block.

```
proc DoX()
{
    // Valid as x and y are invalidated at the end of this block
    {
        local int x = 1;
        local int y = 2;
    }

    local int x = 5;
    local int y = 6;
}
```

Note as well that the virtual machine prevents clobbering, not the bytecode compiler. Internally on a call to a function, a base offset is added to all variable indices on a function call to prevent writes to the callers variables.

This is to prevent spamming `PUSHV` and `POPV` and avoid needing to compile-time interpret the bytecode for the function that you are calling to retrieve its clobber list.
