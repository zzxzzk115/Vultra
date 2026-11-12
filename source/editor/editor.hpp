#pragma once

#include <vultra/host/workspace.hpp>

namespace vultra::editor
{
    // The exit code of an editor that ended to be built again: the source of a native package changed.
    // `vultra edit` builds the editor with the project's packages, and starts it again, when it sees this code.
    inline constexpr int kRestart = 75;
    // Set by `vultra edit` when it will do that.
    inline constexpr const char* kSupervisedVariable = "VULTRA_SUPERVISED";

    // The whole `vultra-editor` command line over what `registry` holds. An editor executable registers the
    // packages it was built with and calls this from main(); the result is the process exit code.
    int commandLine(const host::Registry& registry, int argc, char** argv);
} // namespace vultra::editor
