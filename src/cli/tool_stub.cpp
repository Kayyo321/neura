// A tool executable on Windows (docs/design/Windows030.md, decision W04).
// The tool's code is in one product DLL, neura-core-<version>.dll or
// neura-sdk-<version>.dll, which the loader finds beside this executable;
// the build names its entry with NEURA_TOOL_ENTRY (neura_tool_<id>, see
// cmake/tool_entry.cpp.in). Only C types cross the boundary, so this module
// and the DLL may each carry their own static C runtime.

#ifndef NEURA_TOOL_ENTRY
#error "NEURA_TOOL_ENTRY names the tool's entry in its DLL"
#endif

extern "C" __declspec(dllimport) int NEURA_TOOL_ENTRY(int argc, char** argv);

int main(int argc, char** argv) {
    return NEURA_TOOL_ENTRY(argc, argv);
}
