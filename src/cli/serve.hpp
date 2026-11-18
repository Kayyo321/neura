#pragma once

namespace neura::cli {

// `neura serve <file.nra>... | <file.nvr> [neura-server-host options]`:
// builds the program when given source and replaces this process with
// neura-server-host serving the image. Returns only on failure.
int run_serve(int argc, char** argv);

} // namespace neura::cli
