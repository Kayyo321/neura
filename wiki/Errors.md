# Errors and failure handling

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)


Neura distinguishes expected failures, which a program handles, from runtime failures, which end the run.

**Expected failures are returned values.** A function that can fail returns a value that says so, such as an enum variant or an object describing the failure, and its caller inspects it. There is no exception mechanism and no implicit propagation. The [standard library](https://github.com/Kayyo321/neura/wiki/StandardLibrary) provides `Std.Core.Result` and `Std.Core.Optional` for this; a program can also declare its own enums and classes for outcomes.

```neura
export let Parse = enum (Ok, Empty, TooLong);

let check = func(words: string[], limit: int) -> Parse {
    if (words.length == 0) {
        return Parse.Empty;
    }
    if (words.length > limit) {
        return Parse.TooLong;
    }
    return Parse.Ok;
};
```

**Runtime failures trap.** Integer overflow, a zero divisor, an out-of-range conversion ([Numbers](https://github.com/Kayyo321/neura/wiki/Numbers)), a negative index, a list modified during for-each, a failed console write, and a failed test assertion are program traps with documented reason codes. An index at or past a collection's length, a read of an uninitialized field or global, use of a destroyed object, and exhausted run limits are faults that NVM detects. Both end the run immediately with a diagnostic that names the source location when the image has a source map ([NVM](https://github.com/Kayyo321/neura/wiki/NVM)). A trap cannot be caught, and no cleanup runs after it.

The process status names the trap's class with the shell convention for a process that a signal ended, 128 plus the Linux signal number, and the matching [`Std.Program.ExitCode`](https://github.com/Kayyo321/neura/wiki/Modules#program-entry) variant:

| Trap | Status | `ExitCode` variant |
| --- | --- | --- |
| Integer overflow, a zero divisor, `MIN / -1`, an out-of-range conversion (reason 1) | 136 | `ArithmeticError` |
| A negative index (reason 2), an index at or past the length, a read of an uninitialized slot, field, or global, an invalid handle, a nil class reference, or the frame depth limit | 139 | `SegmentationFault` |
| An explicit message trap or failed assertion (reason 3), a list modified during for-each (reason 5), or a failed class cast | 134 | `Abort` |
| Invalid dispatch (reason 4), or a malformed graph, text, or host completion | 132 | `IllegalInstruction` |
| The heap or handle limit | 137 | `OutOfMemory` |
| The step limit | 152 | `CpuTimeExceeded` |


For the intended use cases, see [errors.nra](https://github.com/Kayyo321/neura/blob/main/examples/guide/errors.nra). Its commented pseudocode illustrates a file-read failure and invalid input; file input belongs to the standard library.
