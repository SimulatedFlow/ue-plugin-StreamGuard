# StreamGuard - Level Streaming Diagnostics And Preload Budget

**Which cell or sublevel landed in which frame, what it cost, and how far the player was from it - on
screen, in the build you are playing, with no trace to record and no second window to open. And a preload
budget that spreads the work instead of dropping it all on one frame.**

Unreal Engine 5.8 · C++ code plugin · one runtime module · Win64

---

## The gap this fills

Epic's streaming tooling is excellent and all of it lives somewhere else. Unreal Insights reads a trace in
a second application. `stat levels` gives you a raw state dump with no history. The World Partition editor
view is an editor window. All three are the right tool for a planned investigation.

None of them is any use in the second that actually matters: the second the hitch happens, on one
particular route through the map, in the packaged build, on a machine with a slower disk than yours.

StreamGuard is that second. It draws a board inside the running game:

* every streaming source - sublevel or World Partition runtime cell - with its state, its load time, its
  activation time, **the frame number it finished in**, and the player's distance to it;
* a timeline of the last ten seconds of frame times, with every frame over the hitch threshold marked red;
* and under the timeline, for each red frame, **the names of the sources that finished in exactly that
  frame**.

That last line is the product. It is the thing people otherwise assemble by hand out of two logs.

The second half is the **preload budget**: at most N loads in flight, at most M milliseconds of activation
work started per frame, ordered by where the player is *going* rather than only where the player is.
Whatever does not fit waits a frame. One hitch becomes ten frames nobody sees.

**StreamGuard does not replace World Partition and does not stream anything the engine was not already
going to stream.** It measures, it attributes, and it paces. That sentence is in every part of this
documentation because it is the honest boundary of the tool.

## What is measured and what is modelled

This distinction is the plugin, so it is on the front page rather than buried in an appendix.

| | Where it comes from |
|---|---|
| The list of streaming sources | **Read from the engine.** `UWorld::GetStreamingLevels()`. A World Partition runtime cell is a `UWorldPartitionLevelStreamingDynamic`, which lives in that same array - so there is no second code path for partitioned worlds and none is needed. |
| State (loading, activating, visible…) | **Read from the engine.** `ULevelStreaming::GetLevelStreamingState()`, the engine's own state machine. |
| Load milliseconds | **Measured.** Wall clock between entering `Loading` and the engine's `OnLevelLoaded`. |
| Activation milliseconds | **Measured.** Wall clock between entering `MakingVisible` and the engine's `OnLevelShown`. |
| The frame a source finished in | **Measured, at the moment it happened.** `GFrameCounter` read *inside* `OnLevelLoaded` / `OnLevelShown`, not at the next tick. |
| Frame times on the timeline | **Measured.** `DeltaTime`, once per frame, into a fixed ring buffer. |
| Actor count per level | **Counted**, once, on the frame the level becomes visible. |
| What an activation *will* cost | **Modelled** - and only for a source that has never been activated before. After the first time, the measured cost of the last activation is used instead. |

There is exactly one estimate in the plugin and it stops being one within seconds of the game starting.

### Why the frame number needs a watcher object

`ULevelStreaming::OnLevelLoaded` and `OnLevelShown` are the public, documented, Blueprint-assignable way to
hear about streaming - and they are `DECLARE_DYNAMIC_MULTICAST_DELEGATE` **with no parameters**. They say
"something happened", not "*this* level happened", and a dynamic delegate cannot carry a bound payload the
way a native one can. So StreamGuard binds one small `UStreamGuardLevelWatcher` per tracked source, holding
the one piece of context the delegate does not carry.

The alternative - polling the state every tick - is also done, and is how load *starts* are seen. But
polling cannot tell you the frame: the subsystem's tick may run before or after the world's streaming
update, so a poll attributes a completion to whichever tick noticed it. Being off by a frame is fatal to a
tool whose entire claim is "this landed in that frame".

## The ceiling

A diagnostic that becomes the problem it was bought to find is worse than no diagnostic. StreamGuard has a
hard ceiling on itself, and every number in it is a visible project setting with its reason written next to
it:

* **256 tracked sources.** One record and one watcher each. Everything past the ceiling is still counted -
  that is one enum read - and reported as a single folded row, so the ceiling can hide *which* source is
  costing you, never *that* something is. This is the number that makes StreamGuard something you can leave
  switched on in a World Partition map with thousands of cells.
* **A fixed timeline ring**, sized once from `WindowSeconds × AssumedFrameRate` and never reallocated.
* **Six source names kept per frame sample**, with the true count uncapped - so a busy frame reads
  "L_Block_A, L_Block_B … and 14 more" rather than quietly claiming six.
* The per-frame scan is one enum read per streaming level. Sorting, folding and formatting run at 10 Hz.

## The budget

Two numbers, because streaming has two halves with two different costs:

* **`MaxConcurrentLoads`** bounds the half that happens off the game thread and is limited by the disk.
* **`MaxActivationMsPerFrame`** bounds the half that happens *on* the game thread - `AddToWorld` - and is
  the part you actually feel.

Ordering is by distance scaled by the player's heading: at the default `DirectionWeight` of 0.5, a source
dead ahead is treated as half as far as it really is, one to the side at its true distance, one directly
behind as half again as far.

Two rules make this a budget and not a filter, and both are unit-tested:

1. **The first item always goes through**, however expensive. Refusing it would mean it never runs at all.
2. **Nothing waits forever.** A request held longer than `MaxHoldSeconds` is promoted past the budget.

Together they are the promise that turning the throttle on can only ever make content arrive *later* -
never not at all.

The throttle is **off by default**. StreamGuard's first job is to show you what is happening; changing what
happens is a separate decision a project should make on purpose.

## Classes

| Class | What it is |
|---|---|
| `UStreamGuardSubsystem` | The measurer. A tickable world subsystem: scans the streaming array, times the transitions, stamps the frames, draws the board, applies the budget. |
| `UStreamGuardThrottle` | The budget, as arithmetic. `PlanFrame` takes an array and a budget and returns what starts now - no world, no subsystem, no clock. |
| `AStreamGuardHUD` | An `AHUD` that draws the board, for projects that prefer being explicit. Most projects do not need it - StreamGuard draws on any HUD through `AHUD::OnHUDPostRender`. |
| `UStreamGuardStatics` | Every call as a Blueprint node, plus the pure, world-free maths: sort, fold, frame-to-source attribution, CSV. |
| `UStreamGuardSettings` | Project Settings → Plugins → StreamGuard. |

## Console commands

| Command | What it does |
|---|---|
| `StreamGuard.Show [0\|1]` | Draw the board. No argument toggles. |
| `StreamGuard.Freeze [0\|1]` | Hold the board still without pausing the game. |
| `StreamGuard.Export [path]` | Write the board to CSV. No argument writes a timestamped file under `Saved/StreamGuard`. |
| `StreamGuard.Budget <n> [ms]` | Loads in flight, and optionally the activation milliseconds per frame. |
| `StreamGuard.Throttle [0\|1]` | Pace streaming work to the budget. **This is the before-and-after switch.** |
| `StreamGuard.Reset` | Throw away every measurement and start from an empty window. |

## What this is not

* Not a level loader. StreamGuard never adds a level to a world and never loads content of its own.
* Not a replacement for World Partition, and not a competitor to it.
* Not a network tool. If you are chasing bytes on the wire rather than bytes off the disk, that is a
  different half of the same hitch and a different plugin.
* No UMG in the product - the board is `UCanvas` from end to end, so it survives a packaged build with no
  widget tree. No editor module. No third-party code.

## Documentation

Full documentation, including the demo map walkthrough, the Blueprint and C++ API, the CSV format and a
section on what the demo can and cannot show about World Partition:

* `Docs/DOCUMENTATION.md`
* <https://wiki.teufel-engineering.com/en/StreamGuard/documentation>

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
