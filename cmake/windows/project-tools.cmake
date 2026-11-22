# Project tools on Windows (docs/design/Windows030.md, W32; package WP13).
#
# neura-dev controls its hosts through the platform layer on Windows: each
# host runs in a new process group and a kill-on-close job, is stopped with
# Ctrl+Break and then ended with its job, and is stopped before its bundle is
# rebuilt. With that in place, CMakeLists.txt builds the neura-dev tool and
# registers the project-dev test on Windows as it does elsewhere.
set(NEURA_WINDOWS_DEV ON)
