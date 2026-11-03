#pragma once

namespace Slate {

// Installs a process-wide handler that logs the exception code, faulting
// address and a symbolized stack trace before terminating. Windows-only;
// no-op on other platforms.
void InstallCrashHandler();

} // namespace Slate
