#ifndef TRAY_WATCHER_H
#define TRAY_WATCHER_H

#include <gio/gio.h>

/*
 * Singleton representing a StatusNotifierWatceher instance.
 */

#define SNW_PATH  "/StatusNotifierWatcher"
#define SNW_NAME  "org.kde.StatusNotifierWatcher"
#define SNW_IFACE "org.kde.StatusNotifierWatcher"

typedef struct _Watcher Watcher;

/*!
 * Initializes and launches the watcher, if needed.
 *
 * Returns a reference to the instance.
 * Once there are no more references to the instance,
 * the Watcher is automatically destroyed - see watcher_unref().
 */
extern Watcher *watcher_launch (void);

/*!
 * Returns a new reference to the Watcher's instance if it exists,
 * or NULL otherwise.
 */
extern Watcher *watcher_instance (void);

extern void watcher_unref (Watcher *watcher);

#endif
