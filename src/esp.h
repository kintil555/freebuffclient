#pragma once
// ESP projection: reads the game camera position + rotation (Camera), builds a
// view-projection matrix, projects g_espEntries into g_espScreen each frame.

namespace esp {
    // Called after modules tick, before drawing. Needs the JVM on this thread.
    void project();
}
