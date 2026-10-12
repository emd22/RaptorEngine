# Project format (.prx)

PRX is a custom configuration language designed for this engine.
A scene is outlined by a PRX script located at the root of the scenes' directory.
This file specifies:

- The name of the scene and associated metadata
- All objects that are defined in the scene.

## Entries

A file is defined as a collection of entries. Each assignment expression is considered an entry.

Entries use strict typing, and enforce that they can only be read as the type which they are defined.
They can be objects, arrays, or literal values. As well, predefined constants can be added in the configuration file interpreter.

Entries can either contain the value of a primitive (int, float, string, etc.), an object, or an array.

The types currently supported include:

### Strings

```
Name = "String Thing"
```

### Numbers

```
SomeFloat = 10.05
SomeInt = 5
```

### Booleans

Booleans are treated internally as integers and can be retrieved as any integral type.

`true` and `false` are built into the language and can be written directly. The older `$true` and `$false` reference form is still accepted.

```
IsBoolean = true
IsCool = false
```

### Arrays

Arrays store a collection of unnamed values inside of an object. They can also be used to represent vector and quaternion values.

```
Multi = [10, 15, 20, 25]
```

### Objects

Objects allow an entry to store a number of other named members inside of it.

```
SomeObject = { X = 10 }
```

Members are separated by whitespace. A semicolon can also be used, which helps when several members share a line.

```
SomeObject = { X = 10; Y = 20 }
```

### Arrays of objects

An array can hold objects instead of values. Its elements are stored as members named by their index (`0`, `1`, ...).

```
Events = [
	{ Time = 0.1; Name = "start" },
	{ Time = 0.6; Name = "end" },
]
```

## Referencing other entries

To reference other values in a config file, prefix an identifier with a dollar sign. Nested objects can be referenced by using the dot operator.

Example:

```
// Referencing a root entry
X = $DefinedValue

// Referencing a nested entry
Y = $DefinedObject.NestedValue
```

An element of an array can be referenced with an index.

```
Z = $DefinedObject.Position[1]
```

When a reference is the whole value of an entry it can refer to anything, including an object or an array, and the entry becomes a deep copy of it. These references are resolved once the whole file has been read, so they can point at entries defined later in the file. A reference that cannot be found, or that is part of a cycle, is an error.

References used inside an array or an expression (`$A | $B`) are resolved as they are read, and can only refer to single values that are already defined.

## Constants

`true` and `false` are built into the parser. All other builtin constant values are located at `/Config/Internal/Constants.conf`, which is automatically imported into all config files.

## Including other files

PRX scripts also give the ability to include other PRX scripts directly by substitution.

To include a file, use the `#include` directive. with a path to the script to include. This halts the tokenizer to read this included file, and appends the tokens to the current buffer. Note that since this tokenizes the data before adding it to the token buffer, **other file formats should not be included in the script this way**.

```
#include "[SOME_FILE_PATH].prx"
```

## Counting syntax

When having a collection of data that is not specifically configuration related but still needs identifiable entries, you can use `$` in place of a name for an entry.

This sets the name to the current index in the members list.

For example:

```
parent = {
	$ = {    // becomes 0 = {
		...
	}
	$ = {	 // becomes 1 = {
		...
	}

}
```
