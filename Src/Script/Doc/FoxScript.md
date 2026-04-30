
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

## Defining variables

To define a new variable, use either the `local` or `global` keyword as well as the type and then identifier for the variable.

For example:
```
// Variable local to the current scope
local int x = 10;

// Variable that is a reference to a globally accessible variable
global int OBJECTID;


```
