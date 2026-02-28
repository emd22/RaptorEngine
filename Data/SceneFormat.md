
# PRX script
PRX is a custom configuration langauge designed for this engine.
A scene is outlined by a PRX script located at the root of the scenes' directory. 
This file specifies:
* The name of the scene and associated metadata
* All objects that are defined in the scene.

## Entries
A file is defined as a collection of entries. Each assignment expression is considered an entry.

Entries use strict typing, and enforce that they can only be read as the type which they are defined.
They can be objects, arrays, or literal values. As well, predefined constants can be added in the configuration file interpreter.

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
Booleans are treated internally as integers and are typed as so. They can be read as any integral type.

Defined in the script are the constants for `TRUE` and `FALSE`, which should be used for clarity.
```
IsBoolean = TRUE
IsCool = FALSE
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

## Including other files
PRX scripts also give the ability to include other PRX scripts directly by substitution.

To include a file, use the `@include` directive. with a path to the script to include. This halts the tokenizer to read this included file, and appends the tokens to the current buffer. Note that since this tokenizes the data before adding it to the token buffer, **other file formats should not be included in the script this way**.

```
@include "[SOME_FILE_PATH].prx"
```
