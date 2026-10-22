// Shared stateless helpers for QML views. Imported with a namespace, e.g.
//   import "../Utils.js" as Utils
//   Utils.clamp(value, lo, hi)
.pragma library

// Returns value confined to the range [lo, hi].
function clamp(value, lo, hi) {
    return Math.min(Math.max(value, lo), hi);
}

// Returns the index of the signal hue a family of nodes is drawn with.
//
// Node types are published under a namespace, and that namespace is the family
// a node belongs to. The same family always lands on the same hue, so a graph
// reads as a set of colour coded groups rather than as a field of unrelated
// cards. The hue is picked by hash so it stays put between runs.
function signalIndex(family) {
    const text = String(family || "");
    let hash = 2166136261;
    for (let i = 0; i < text.length; i++) {
        hash ^= text.charCodeAt(i);
        hash = Math.imul(hash, 16777619);
    }
    return Math.abs(hash) % 6;
}
