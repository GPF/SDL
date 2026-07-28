/*
 * Bring a running Flycast window to the current KDE desktop and keep it above
 * other windows long enough for an automated full-screen capture.
 *
 * This supports both the Plasma 5/X11 clientList API and the newer windowList
 * API. Closing Flycast restores the affected state by destroying the window.
 */

const windows = (typeof workspace.windowList === "function")
    ? workspace.windowList()
    : workspace.clientList();

for (const window of windows) {
    const caption = String(window.caption || "").toLowerCase();
    const resource = String(window.resourceClass || "").toLowerCase();

    if (caption.includes("flycast") || resource.includes("flycast")) {
        window.minimized = false;

        if (typeof workspace.currentDesktop !== "undefined" &&
            typeof window.desktop !== "undefined") {
            window.desktop = workspace.currentDesktop;
        }

        window.keepAbove = true;
        if (typeof workspace.windowList === "function") {
            workspace.activeWindow = window;
        } else {
            workspace.activeClient = window;
        }
        break;
    }
}
