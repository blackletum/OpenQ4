# Core runtime refinements — pending release notes

> **Unreleased development notes.** 0.13.2 is the latest available release; 0.14.0 was withdrawn. These changes are not a new download. See [release status](../../user/release-status.md).

## Highlights

- **More reliable AI tactical searches.** Nearby areas are considered in the
  correct order, larger navigation meshes are handled safely, and repeated
  searches reuse more of their working memory.
- **More consistent scripted motion and transitions.** Changes to animation
  endpoints and timing take effect immediately. Instant transitions and samples
  before a motion's start no longer reuse values from an earlier motion.
- **Safer multiplayer message handling.** Truncated data and empty string
  buffers are handled consistently. Larger binary delta fields no longer
  depend on a small temporary buffer.
- Large lists require less repeated copying, and adding an item already in a
  list remains safe when the list grows or shifts its contents.
- Empty script commands no longer produce misleading unknown-command messages
  during startup.

## Upgrade Notes

- Replace the complete package so the engine, renderer modules and both game
  modules remain matched. Original Quake 4 assets are still required.
- These fixes do not introduce a new save format or network protocol version.
- The separate runtime surface-size limitation affecting the large `hub2` map
  remains unresolved.
