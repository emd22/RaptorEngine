# Fox Script

Fox Script is a scripting language designed for use in the Foxtrot game engine. It is a very simple language designed primarily for:

- Calling native engine functions
- Passing values to the engine and game
- Basic conditional logic that can be easily updated
- Adding code separation engine, replacing baked-in values with configurable scripts and the ability to build game-tailored features at a language level.

This is NOT designed to be a general purpose language and is tailored to the game engine.

For fast execution, each script is compiled to bytecode before being executed by the VM.

## Bytecode

All operations are defined in `FoxBytecode.hpp`.

Each opcode is 16 bits, or 2 bytes. Each opcode is broken down into two components:

- Base component: The purpose or operation that the instruction performs.
- Specialization component: The specialized function for the base operation.

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

```cpp
script::FoxScript script("Some/File/Path.fox");

// ...

vm.RegisterProc(
    HashStr32("NativeAddition"),         // Function name hash
    eFoxProcFlags::ReturnsValue,         // Flags
    { eFoxType::INT, eFoxType::STRING }, // Argument types
    &NativeAddition_Impl                 // Function handle
);

// Implementation
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
script::FoxScript script("Some/Script.fox");

// ...

script::FoxSymbol* sym = script.GetSymbol("ScriptEntry");

SizedArray<FoxValue> args = { FoxValue(20.0f) };
vm.CallProc(sym, args);

```

The symbol retrieved can be saved if it is used often in a hot path.

# Language Syntax

## Variables

To define a new variable, use either the `local` or `global` keyword as well as the type and then identifier for the variable.

For example:

```
{
    // Variable local to the current scope
    local int x;
}
// Variable x is deleted

// Global variables can be defined or  brought into a scope by using the `global` keyword.
{
    // Variable that is a reference to a globally accessible variable
    global int SOME_GLOBAL = 2;
}
{
    global int SOME_GLOBAL; // Value is 2
}

// Globals are automatically hoisted if they are not defined in the current scope.
{
    local int using_globals = SOME_GLOBAL;

    // Is the exact same as:

    global int SOME_GLOBAL;
    local int using_globals2 = SOME_GLOBAL;
}

{
    // Strings can also be defined, but they are immutable
    local str hello = "Hello, World";
}

{
    // Floats can be defined with or without `f`
    local float x = 1.0;
    local float y = 1.0f;

    // Floats can be defined without a decimal by using the `f` postfix.
    local float z = 1f;
}

```

As you can see from the above example, global variables need to be brought into a local scope to be used.
They can either be brought in implicitly, or defined explicitly. If there is no local `global [type] [name]` definition in the current scope but the value is requested, the bytecode compiler automatically outputs a `VGLOBAL` instruction to define it in local scope.

Note that `global [type] [name]` acts as a _reference_ to a global variable `[name]`, and not a copy of the value.

For example,

```
init()
{
    global int SC_GlobalValue = 10;
}

ModifyGlobal()
{
    // Explicit hoisting
    global int SC_GlobalValue;
    SC_GlobalValue = 5;
}

A() int
{
    global int SC_GlobalValue;
    ModifyGlobal();

    // Returns 5, not 10.
    return SC_GlobalValue;
}
```

Currently, the only builtin types are `int`, `float` and `str`.

## Variable References

Previously, the only way to modify variables across functions was to use globals. This has been recently updated with the addition of references.

```
reftest() int
{
    local int some_value = 10;

    // Take the address of `some_value`
    local &int some_ref = some_value;
    some_ref = 5;

    // Returns 5.
    return some_value;
}
```

This is super helpful when you want to return a value in a deeply nested call stack, or want a function to result in more than one value.

```
// Without references, the value would need to be passed up 3 layers of functions, and returned by each one.
// This also limits these functions to only return one result.

A(int v) int { return v + 10; }
B(int v) int { return A(v); }
C(int v) int { return B(v); }

EntryPoint() int
{
    local int value = 10;

    value = C(value);
}

// With references, only the index to the original variable is passed up, and therefore can modify the source.

A(&int v) { v = v + 10; }
B(&int v) { A(v); }
C(&int v) { B(v); }

EntryPoint() int
{
    local int value = 10;
    C(value);
}
```

### Casting Variables

To cast variables, you can use the builtin functions `castint()` and `castfloat()`.

```
hello() float
{
    local int x = 10 * 2;
    return castfloat(x);
}
```

## Procedures

To separate code, you can define procedures.
A procedure is defined in the following format:
`[identifier] ( [argument]... ) [return type]?`

```
FireWeapon(int gusto)
{
    global float DIRX;
    global float DIRY;
    global float DIRZ;

    global int WEAPON_DAMAGE;
    local int damage = WEAPON_DAMAGE + gusto;

    N_SetDirection(DIRX, DIRY, DIRZ);
    N_FireWeaponInternalCall(damage);
}

// Optionally, procedures can be defined using the `proc` prefix, similarly to `extproc`.
proc DoSomething()
{
    // ...
}
```

To return values from procedures, follow the definition with a type:

```
ProcThatReturns(int x, int y) int
{
    return x + y;
}
```

## Waiting / Execution Handoff

A key feature to game scripting languages is the ability to time events based on wall-time. This could be for timing enemies spawning, opening doors, or for animating objects.

To do this in Fox, you can use the `pause(time_in_100ms)` function. The time is by a 10th of a second.

Example:

```
#include "Scripts/Time"

DoSomething()
{
    Object_DoSomething(OBJECT_ID);

    // Handoff control back to the engine for 1 second.
    pause(10);

    Object_Remove(OBJECT_ID);
}

// You can also use the simple time helpers in `Scripts/time`.
DoPause()
{
    Object_DoSomething(OBJECT_ID);

    // Handoff control back to the engine for 0.5 seconds.
    pause(seconds(0.5));

    Object_Remove(OBJECT_ID);
}
```

The C++ code for this would look like:

```cpp
FoxScript script("Script.fox");

// Call the entrypoint
FoxValue return_value = script.CallProc(script.GetSymbol("start"), {});

// Resume from the paused state. This might be in the game loop or the object update function.
return_value = script.Resume();
```

## Including Scripts

To reuse functions, utilities and global variables, you can include other scripts into your source file. Note that there are currently no systems in place to prevent multiple inclusion.

Include directives can either include the file extension, or infer it from the language. This is the same functionality as in PRX files or config scripts.

```
#include "Scripts/Animation"

// Include directives can optionally have the file extension
#include "Scripts/Time.fox"

init() int
{
    InitUtil();


    // The global variable in `Scripts/Utility` is automatically hoisted
    return seconds(G_AnimationDuration);
}
```

## Modules

Fox modules are to reduce code duplication, add a basic "object" system, and be able to act on already compiled code. It allows calling containerized functions from other scripts.

```
// This tells the compiler and VM to load in previously generated bytecode,
// or compile the script if it requires it.

// Modules can be refered to by a user-defined alias:

loadmod ModuleAlias => "Scripts/TestModule";

init()
{
    // Calls the `init()` function from the `TestModule` script.
    ModuleAlias:init();

    // You can also retrieve values.
    local int return_value = ModuleAlias:GetSomeValue();
}
```

And `TestModule.fox` might be defined as:

```
init()
{
    // This is inaccessible to any functions that load this as a module,
    // but is accessible anywhere in this local script.
    global int G_SomeValue = 10;
}

GetSomeValue() int
{
    return G_SomeValue;
}
```

Currently, local and global variables defined in a script are local to the script itself. To transfer data between scripts, use functions and function return values. This behaviour may be changed in the future.

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
DoX()
{
    {
        local int x = 1;
        local int y = 2;
    }

    // Valid as x and y are invalidated at the end of the block

    local int x = 5;
    local int y = 6;
}
```

Note as well that the virtual machine prevents clobbering, not the bytecode compiler. Internally on a call to a function, a base offset is added to all variable indices on a function call to prevent writes to the callers variables.

This is to prevent spamming `PUSHV` and `POPV` and avoid needing to compile-time interpret the bytecode for the function that you are calling to retrieve its clobber list.

## Stack Manipulation

There are builtin functions within the language to manipulate the stack. These can be used to either circumvent the type system or implement custom logic.

Example:

```
// Helper function to return the value instead of pass by reference
vpopi() int
{
    local int temp;
    vpop(temp);
    return temp;
}

Init()
{
    vpush(1.0f);

    // Pass in an output variable. Internally, there is no way for a called
    // (builtin) function to know what the type of the variable it will be
    // assigned to is. Therefore, the output variable will need to be passed in.

    local float value;
    vpop(value);

    // Using the helper function
    vpush(20);
    local int other_value = vpopi();
}

// Example for "objects"

saveobject(str name)
{
    local float health;
    local int ammo;
    local int mags;

    vpop(mags);
    vpop(ammo);
    vpop(health);

    N_UpdateWeaponStats(ammo, mags);
    N_UpdatePlayerState(name, health);
}

caller()
{
    vpush(10.0f); // Health
    vpush(32); // Ammo
    vpush(10); // Mags

    saveobject("ploober");
}
```
