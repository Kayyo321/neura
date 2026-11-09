# Errors and failure handling

Neura distinguishes expected failures, which a program handles, from runtime failures, which end the run.

**Expected failures are returned values.** A function that can fail returns a value that says so, such as an enum variant or an object describing the failure, and its caller inspects it. There is no exception mechanism and no implicit propagation. A standard result type with construction, inspection, and propagation conventions belongs to the [standard library](StandardLibrary.md), which is outside this release; until it exists, programs declare their own enums and classes for outcomes.

```neura
export let Parse = enum (Ok, Empty, TooLong);

let check = func(string[] words, int limit) -> Parse {
    if (words.length == 0) {
        return Parse.Empty;
    }
    if (words.length > limit) {
        return Parse.TooLong;
    }
    return Parse.Ok;
};
```

**Runtime failures trap.** Integer overflow, a zero divisor, an out-of-range conversion ([Numbers](Numbers.md)), a negative index, a list modified during for-each, a failed console write, and a failed test assertion are program traps with documented reason codes. An index at or past a collection's length, a read of an uninitialized field or global, use of a destroyed object, and exhausted run limits are faults that NVM detects. Both end the run immediately with process status 70 and a diagnostic that names the source location when the image has a source map ([NVM](../runtime/NVM.md)). A trap cannot be caught, and no cleanup runs after it.

For the intended use cases, see [errors.nra](../../examples/guide/errors.nra). Its commented pseudocode illustrates a file-read failure and invalid input; file input belongs to the standard library.
