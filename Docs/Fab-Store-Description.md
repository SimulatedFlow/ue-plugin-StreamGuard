# StreamGuard - Fab Store Description

## Title

StreamGuard - Level Streaming Diagnostics And Preload Budget

## Short description (one line)

Shows why your open world hitches: which cell or sublevel loaded in which frame, what it cost and how far
the player was - plus a preload budget that spreads the work instead of dropping it on one frame.

## Long description

**Epic's streaming tools are other programs. StreamGuard is a board inside your game.**

Unreal Insights reads a trace in a second application. `stat levels` is a state dump with no history. The
World Partition editor view is an editor window. All three are the right tool for a planned investigation,
and none of them is any use in the second that actually matters: the second the hitch happens, on one
particular route through the map, in the packaged build, on a machine with a slower disk than yours.

StreamGuard is that second. It draws a board inside the running game listing every streaming source -
sublevel or World Partition runtime cell - with its state, its load milliseconds, its activation
milliseconds, **the frame number it finished in**, and the player's live distance to it. Under the board is
a timeline of the last ten seconds of frame times with every frame over the hitch threshold marked red -
and **next to the red mark stands the list of sources that finished in exactly that frame**.

That pairing is the product. It is the thing people otherwise assemble by hand out of two logs, a text
editor and a guess.

---

### The frame number is real

The completion frame is not inferred from a polling loop. StreamGuard binds the engine's own
`OnLevelLoaded` and `OnLevelShown` delegates through a small watcher object per level, so `GFrameCounter`
is read at the instant the engine finished - not at whichever tick happened to notice afterwards. Being off
by a frame is fatal to a tool whose entire claim is "this landed in that frame", so it is not off by a
frame.

### The preload budget

The second half. At most N loads in flight, and at most M milliseconds of activation work started per
frame - because streaming has two halves with two different costs: the disk half that runs off the game
thread, and the `AddToWorld` half that runs on it and is what you actually feel.

Ordering is by distance scaled by the player's **heading**, not only by position: at the default setting a
source dead ahead is treated as half as far as it is, one to the side at its real distance, one behind as
half again as far. Whatever does not fit waits a frame. One hitch becomes ten frames nobody sees.

Two rules make it a budget rather than a filter, and both are unit-tested:

* **The first item always goes through**, however expensive - a budget that can refuse work forever is a
  bug wearing a feature's coat.
* **Nothing waits forever** - a request held past the limit is promoted.

Together they are the guarantee that turning the throttle on can only make content arrive *later*, never
not at all.

### Every number comes from something

| | |
|---|---|
| The list of sources | Read from `UWorld::GetStreamingLevels()`. A World Partition runtime cell is a `ULevelStreamingDynamic` in that same array - there is no second code path and none is needed. |
| State | `ULevelStreaming::GetLevelStreamingState()`, the engine's own state machine. |
| Load and activation milliseconds | Measured between two engine events. |
| The completion frame | `GFrameCounter`, read inside the engine's own delegate. |
| Frame times | `DeltaTime`, once per frame, into a fixed ring. |
| Actor count | Counted once, on the frame the level becomes visible. |
| What an activation *will* cost | The only estimate in the plugin - and only until that source has been activated once, after which the measured cost is used. |

### The ceiling

A diagnostic that becomes the problem it was bought to find is worse than no diagnostic. StreamGuard is
bounded: at most **256 tracked sources**, with everything past that folded into a single row that keeps the
count; a timeline ring allocated once and never resized; six source names per frame sample with the true
count uncapped; and the per-frame work is one enum read per level, with all sorting and formatting at
10 Hz. Every limit is a project setting with its reason written next to it.

### What this is not

**StreamGuard does not replace World Partition and does not stream anything the engine was not already
going to stream.** It measures, it attributes, and it paces.

The measurement half is equally accurate on World Partition and on classic sublevels. The throttle half is
at its strongest on sublevel streaming and on requests you route through StreamGuard; World Partition
re-asserts its own decisions each update, so a held cell is released by World Partition rather than by
StreamGuard. That is written plainly in the documentation rather than left for you to discover.

If you are chasing bytes on the *wire* rather than bytes off the *disk*, that is the other half of a hitch
and a different tool.

---

### Included

* One runtime module. No editor module - everything ships, including into a packaged build.
* No UMG in the product: the board is `UCanvas` drawn from `AHUD`, so it survives a build with no widget
  tree. Both draw routes are safe together.
* Full Blueprint API, plus a static, world-free planner, sort, fold, frame-to-source attribution and CSV
  that you can call from anywhere.
* Console commands: `StreamGuard.Show`, `StreamGuard.Freeze`, `StreamGuard.Export`, `StreamGuard.Budget`,
  `StreamGuard.Throttle`, `StreamGuard.Reset`.
* CSV export: thirteen columns, one row per source and one per frame, header included, quoting that
  survives a level called `L_Block,B`.
* A demo map with four sublevels and a fixed route, so the same hitch happens at the same place every time
  - and a button that shows you the same drive with the budget off and on.
* Six automation tests under `StreamGuard.*` covering the budget, the ordering, the attribution, the
  ceiling and the export.
* Documented settings, every limit with its reason next to it.

### Technical details

| | |
|---|---|
| Engine | Unreal Engine 5.8 |
| Platform | Win64 |
| Modules | 1 runtime (`PreDefault`) |
| Dependencies | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `RenderCore` |
| Editor module | None |
| Third-party code | None |
| Network replicated | No |
| Supported build configurations | Development, Test, Shipping |

### Documentation

<https://wiki.teufel-engineering.com/en/StreamGuard/documentation>

### Support

<mailto:teufelsilvan@gmail.com>

---

## Tags

level streaming, world partition, open world, hitch, stutter, profiling, diagnostics, performance,
optimization, preload, streaming budget, frame time, debug hud, csv

## Category

Code Plugins → Engine Tools
