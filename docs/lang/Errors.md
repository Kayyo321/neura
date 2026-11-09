# Errors and failure handling

Expected failures should be represented explicitly in function results; invalid operations and runtime failures are a separate category. The syntax and standard result type are not yet specified, so APIs in this repository must not pretend that error propagation or exception handling exists. The initial compiler needs a decision on a result type, construction and inspection of variants, and propagation before fallible library APIs can be implemented consistently.

Proposed behavior for checked bounds violations is already described in [Collections](Collections.md): fail before an invalid access. Unhandled runtime failures terminate the operation; the process-level behavior, stack traces, allocation failures, cleanup during failure, and whether failures can be caught remain open.

For the intended use cases, see [errors.nra](../../examples/guide/errors.nra). Its commented pseudocode illustrates a file-read failure and invalid input without inventing a finalized API. Programs should distinguish invalid user input from infrastructure failures and report useful context without assuming either is recoverable.
