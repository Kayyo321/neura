// Startup blocks (docs/lang/Modules.md, Startup blocks).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. `static scope { ... }` at file level, or among a module's
// members, is an ordinary block that runs once per execution at its place in
// source order among its file's initializers and top-level statements. It
// becomes an IrOp::Block with its own span in the body of its file or module,
// which the converter orders by span with the file's other startup work. It
// is not inside a function, so `return` is E0470, and `scope`, `spawn` and
// `select` are rejected as they are at file level.

IrNode Lowering::startup_block(const Statement& stmt, const Context& context) {
    auto block_context = context;
    block_context.startup_block = true;
    return statement(stmt, block_context);
}

Diagnostic Lowering::return_in_startup_block(const Statement& stmt) {
    const Span keyword{stmt.span.file, stmt.span.line, stmt.span.column, stmt.span.offset, 6};
    return Diagnostic(keyword, "a startup block returns nothing and cannot be left early").with_code("E0470")
        .label("'return' in a startup block")
        .help("handle the result with 'match', or put the work in a function and call it from the block");
}
