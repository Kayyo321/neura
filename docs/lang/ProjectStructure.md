# Project structure

The [frontend](Tooling.md) uses this layout to find project-local modules. Give it one or more source roots with `-I`. It also searches beside the importing file and the explicit input files. If multiple files match an import, the frontend reports an ambiguity.

## Project-local modules

Choose a source root for the project. Under that root, each qualified import name maps to a file: namespace components become lowercase, kebab-case directory names, and the final component becomes a lowercase, kebab-case `.nra` filename. Each file declares the module named by the final component. For example:

```text
multi-file/                  # source root
├── main.nra                 # application entry point
└── acme/
    ├── greetings.nra        # Acme.Greetings
    └── greetings/
        └── punctuation.nra  # Acme.Greetings.Punctuation
```

`main.nra` imports `Acme.Greetings`; that module in turn imports `Acme.Greetings.Punctuation`. The directories express the namespace and are required by this mapping, but do not themselves declare modules. A module file and a directory may share a name, so a module can have children. Files do not need to import their parents, and importing a parent does not automatically import its children.

The imported file's named module is the value returned by the import. Only its `export` members are accessible to the importer. Imports are explicit in each file; an alias in one file is not available in another. The example prints `Hello, Neura!` when run with an implementation supporting this convention and `SysIo`.

See [the complete example](../../examples/modules/multi-file/main.nra). `SysIo` is an existing built-in module used by the examples; its resolution is separate from the project-local mapping above.

## Packages and open questions

The `Acme` namespace illustrates how a project can group its own modules. It does not imply a package manifest, dependency installation, or a published package format. Package and dependency management and entry-point discovery remain open. The frontend converts PascalCase components to kebab-case and resolves them in the supplied source roots. See [Modules](Modules.md) for exports, imports, and module inheritance.
