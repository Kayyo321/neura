# Project structure

This page proposes a convention for organizing multi-file Neura projects. The repository currently contains language examples and documentation, not a compiler or package manager; module discovery, package distribution, and the commands to build or run a project are not yet specified by an implementation.

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

The `Acme` namespace illustrates how a project can group its own modules. It does not imply a package manifest, dependency installation, or a published package format. Source-root selection, external dependencies, name collisions, capitalization conversion, circular imports, and entry-point discovery still need specification before interoperable tooling can be built. See [Modules](Modules.md) for exports, imports, and module inheritance.
