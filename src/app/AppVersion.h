#pragma once

// Keep the executable resource, tray tooltip and startup log in sync.
#define TRAYGROUPS_VERSION_NUMBERS 1, 7, 1, 0
#define TRAYGROUPS_VERSION_TEXT "1.7.1"
#define TRAYGROUPS_WIDEN_INNER(value) L##value
#define TRAYGROUPS_WIDEN(value) TRAYGROUPS_WIDEN_INNER(value)
#define TRAYGROUPS_VERSION_W TRAYGROUPS_WIDEN(TRAYGROUPS_VERSION_TEXT)
