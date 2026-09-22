# StreamGuard — Documentation

**Level streaming diagnostics and a preload budget, inside the running game.**

Unreal Engine 5.8 · C++ code plugin · one runtime module · Win64

Copyright 2026 Silvan Teufel. All Rights Reserved.

---

## Contents

1. [What StreamGuard is, and is not](#1-what-streamguard-is-and-is-not)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Quick start](#4-quick-start)
5. [The demo map](#5-the-demo-map)
6. [World Partition vs. Sublevels: what the demo shows and what it does not](#6-world-partition-vs-sublevels-what-the-demo-shows-and-what-it-does-not)
7. [Reading the board](#7-reading-the-board)
8. [The preload budget](#8-the-preload-budget)
9. [Class overview](#9-class-overview)
10. [Blueprint API](#10-blueprint-api)
11. [C++ examples](#11-c-examples)
12. [Console commands](#12-console-commands)
13. [Where every number comes from](#13-where-every-number-comes-from)
14. [The ceiling StreamGuard puts on itself](#14-the-ceiling-streamguard-puts-on-itself)
15. [CSV export format](#15-csv-export-format)
16. [Project settings reference](#16-project-settings-reference)
17. [Tests](#17-tests)
18. [Troubleshooting](#18-troubleshooting)
19. [Support](#19-support)
20. [Version history](#20-version-history)

---

## 1. What StreamGuard is, and is not

Open worlds hitch when something arrives from disk. Unreal Insights will tell you afterwards, in another
program, from a trace you had to remember to record. `stat levels` gives you a state dump with no history.
The World Partition editor view is an editor window. All three are the right tool for a planned
investigation, and none of them is any use in the moment the hitch happens - which is usually on one
particular route through the map, in the packaged build, on a machine slower than yours.

StreamGuard is that moment. Two halves that belong together:

**Seeing.** A board in the running game listing every streaming source with its state, load time,
activation time, the frame it finished in and the player's distance to it - and under it a timeline of the
last ten seconds of frame times with every hitch marked red and **the names of the sources that finished in
exactly that frame written next to the mark**.

**Pacing.** A preload budget: at most N loads in flight and at most M milliseconds of activation work
started per frame, ordered by where the player is going. Whatever does not fit waits a frame. One hitch
becomes ten frames nobody sees.

### What it is not

> **StreamGuard does not replace World Partition and does not stream anything the engine was not already
> going to stream.** It measures, it attributes, and it paces.

* It never adds a level to a world. `RequestLoad` only asks for a level the world already has.
* It never cancels a request. The budget can only ever say "not yet", and `MaxHoldSeconds` puts a ceiling
  on how long "not yet" lasts.
* It is not a network diagnostic. Bytes over the wire are the other half of a hitch and a different tool.

---

## 2. Supported engine and platforms

| | |
|---|---|
| Engine | Unreal Engine 5.8 |
| Platform | Win64 (`PlatformAllowList`) |
| Modules | One: `StreamGuard`, Runtime, `PreDefault` |
| Dependencies | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `RenderCore` |
| Editor module | None. Everything in this plugin ships. |
| UMG | None. The board is `UCanvas` drawn from `AHUD`. |
| Third-party code | None. |

`LoadingPhase` is `PreDefault` so the console commands are registered and the subsystem exists before any
game module starts asking for levels.

The plugin has no editor module on purpose: the build where you most need to see these numbers is the
packaged one, and an editor-only diagnostic would be missing from exactly that build.

---

## 3. Installation

### From the Fab library

Add StreamGuard to the project from the Epic Games Launcher, then enable it in
**Edit → Plugins → Engine Tools → StreamGuard** and restart the editor.

### From a folder

1. Copy the `StreamGuard` folder into `<YourProject>/Plugins/`.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build the project.

### Using StreamGuard from your own C++ module

Add it to your module's dependencies:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "StreamGuard" });
```

Then include what you need:

```cpp
#include "StreamGuardStatics.h"   // every call, plus the pure maths
#include "StreamGuardSubsystem.h" // the subsystem itself
#include "StreamGuardThrottle.h"  // the planner, on its own
#include "StreamGuardTypes.h"     // the structs
```

---

## 4. Quick start

**Nothing needs placing in the level.** The subsystem is created with the world.

1. Play the game.
2. Open the console and type `StreamGuard.Show 1`.
3. Drive around. Watch the board fill up and the timeline scroll.
4. When a frame goes red, read the line under the timeline: it names what landed in it.
5. Type `StreamGuard.Throttle 1` and drive the same route again.
6. Type `StreamGuard.Export` to write the whole thing to a CSV under `Saved/StreamGuard`.

To have the board appear on its own, set **Show Board By Default** in
**Project Settings → Plugins → StreamGuard**, or set the map's HUD class to `AStreamGuardHUD`.

### In a packaged build

Everything above works unchanged in a Development or Test package. In a Shipping build the console is
usually compiled out; bind `UStreamGuardStatics::ToggleBoard` to a key or a debug menu instead, and the
board draws exactly the same.

---

## 5. The demo map

`Content/StreamGuard/Maps/L_StreamGuardDemo`

The demo is a persistent level with **four sublevels**, each holding a few hundred simple meshes built from
engine primitives - no third-party assets and nothing from another plugin. A pawn drives a **fixed route**
rather than being free-flown, because the whole value of the demo is that the same hitch happens at the
same place every time.

| Asset | What it does |
|---|---|
| `Maps/L_StreamGuardDemo` | The persistent level. Four sublevels, a fixed route, and the board on screen from the first frame. |
| `Maps/Districts/L_SG_North` … `_East`, `_South`, `_West` | The four heavy sublevels, ~270-360 engine primitives each. Registered as streaming levels in the persistent level and **initially unloaded**, so the demo starts with nothing streamed in and the thing it exists to show has not already happened before the first frame. |
| `Blueprints/BP_StreamGuardDemoPawn` | Drives the fixed route. No free flying - the demo has to be reproducible. |
| `Blueprints/BP_StreamGuardDemoDirector` | Deliberately asks for all four heavy sublevels at once. This is the thing that hitches. Its Details panel is where the demo's budget is tuned. |
| `Blueprints/BP_StreamGuardDemoHUD` | A Blueprint child of `AStreamGuardHUD`: the board with no configuration, plus the button strip. |
| `Blueprints/BP_StreamGuardDemoGameMode` | Wires the pawn, the controller and the HUD together. |
| `Blueprints/BP_StreamGuardDemoController` | Mouse cursor and UI input mode for the button strip. |
| `UI/WBP_StreamGuardDemoHUD` | The button strip. |
| `Materials/M_SG_*` | Eight flat materials - ground, road and one per district. No textures, nothing imported. |

### What the demo is arranged to prove

The demo exists to show one before-and-after, and it is arranged so that both halves are the *same drive*:

* **Throttle off.** The director asks for four sublevels in one frame. They load together, activate
  together, and the timeline shows a single red spike with all four names next to it.
* **Throttle on.** Same route, same request, same content. The budget starts them two at a time and lets
  one activation begin per frame, and the timeline is flat. The board's *HELD* column shows the cost: a few
  frames of waiting, spread over levels that were not on screen yet anyway.

Nothing about the content changes between the two runs. That is the point.

### The button strip

Every button is **one call**:

| Button | Call |
|---|---|
| Load all districts | `Request Level Load`, once per name in the director's `Districts` array |
| Unload all | `Request Level Unload`, the same way |
| Throttle on / off | `Set Throttle Enabled` |
| Budget 1 / 1.0 ms, Budget 4 / 8.0 ms | `Set Max Concurrent Loads` + `Set Max Activation Ms Per Frame` |
| Freeze / resume | `Toggle Freeze Stream Guard` |
| Export CSV | `Export Stream Guard Csv` |
| Reset window | `Reset Stream Guard` |
| Board on / off | `Toggle Board` |

The names in `Districts` are the plain asset names - `L_SG_North`, not the PIE package name. StreamGuard strips
the editor's `UEDPIE_<n>_` prefix on both sides of the comparison, so the one name you wrote down is the name
that works in the editor and in a packaged build alike.

The widget is demo content and is not required by the plugin. The board itself is `UCanvas` and needs no
widget at all.

---

## 6. World Partition vs. Sublevels: what the demo shows and what it does not

This section exists because the honest answer is more useful than the flattering one.

### The measurement half: identical for both

A World Partition runtime cell is a `UWorldPartitionLevelStreamingDynamic`. That class derives from
`ULevelStreamingDynamic`, which derives from `ULevelStreaming`, and it sits in the same
`UWorld::StreamingLevels` array as any hand-placed sublevel. It runs through the same state machine, fires
the same `OnLevelLoaded` and `OnLevelShown` delegates, and is timed by StreamGuard in exactly the same way.

**Everything the board shows is as accurate on World Partition as it is on sublevels**: state, load
milliseconds, activation milliseconds, the completion frame, distance, actor count, the timeline and the
frame-to-source attribution. The board says "World Partition" in its header when it is looking at a
partitioned world, and adds one line - from `UWorldPartitionSubsystem::IsAllStreamingCompleted()` - saying
whether the world has settled.

The plugin needs no World Partition specific code path to do any of this, and it does not have one.

### The throttle half: strong on sublevels, limited on World Partition

StreamGuard holds a request back by clearing the engine's own should-be-loaded or should-be-visible flag
for a frame and putting it back when the budget allows.

* For **classic sublevel streaming** - level streaming volumes, distance streaming, `ULevelStreamingDynamic`,
  and anything asked for through `RequestLoad` - that flag *is* the decision, and the hold works.
* For **World Partition**, the streaming policy re-asserts its own decision on every streaming update. A
  cell StreamGuard held is released again by World Partition rather than by StreamGuard, usually within a
  frame or two. The hold is therefore weak: it will not corrupt anything and it will not reliably pace
  anything either.

If you are pacing a World Partition world, the levers that work are World Partition's own loading range,
your cell size, and routing whatever *you* ask for through `RequestLoad`. StreamGuard's job there is to
show you which cell cost you which frame, which is most of the work.

### Why the demo is sublevels

Because a demo has to show the same thing on every machine. Sublevels are told to load and unload
explicitly and deterministically. World Partition decides for itself, based on the loading range, the cell
grid, the streaming sources and how fast the machine is - so two people running the same demo would see two
different timelines, and a screenshot of it would not be evidence of anything.

**What cannot be reproduced does not belong in a screenshot.** The World Partition side belongs in this
documentation, where it can be described precisely, and that is where it is.

---

## 7. Reading the board

```
StreamGuard  |  Sublevels  |  6 sources  |  1 loading, 0 activating, 3 visible
frames  worst   47.1 ms   avg  16.8 ms   3 hitches over 33 ms in the last 10 s
budget  ON   2 loads at once, 3.0 ms of activation per frame   |   holding 2

SOURCE                       STATE              LOAD     ACT      FRAME     DIST  HELD
L_Block_A                    visible           182.4     9.1     184312    142m      0
L_Block_B                    activating         96.7       -     184310    268m      3
L_Block_C                    loading               -       -          0    901m      1
L_Block_D                    unloaded              -       -          0   1.4km      0

frame 184312    47.1 ms  <-  L_Block_A, L_Block_B
frame 184180    39.8 ms  <-  L_Block_C
frame 183940    35.2 ms  <-  nothing finished streaming in this frame

[ timeline strip: one column per frame, red above the threshold, amber threshold line ]
```

### The header

* **Sublevels / World Partition** — which kind of world this is.
* **N sources** — how many streaming levels the world has, including any past the tracking ceiling.
* **loading / activating / visible** — live state counts.
* **worst / avg / hitches** — over the window, against the hitch threshold.
* **budget** — on or off, what it is pacing to, and how many requests it is holding right now.

### The table

| Column | Meaning |
|---|---|
| `SOURCE` | Short level name. A folded row for everything past the ceiling. |
| `STATE` | `unloaded`, `loading`, `loaded`, `activating`, `visible`, `hiding`, `FAILED`. |
| `LOAD` | Milliseconds spent in `Loading`. A dash means it has not happened. |
| `ACT` | Milliseconds spent in `MakingVisible`. **Usually the number that matters** - this is game-thread work. |
| `FRAME` | The frame it last finished in. This is the number that lines up with the timeline. |
| `DIST` | Live distance from the player. |
| `HELD` | How many frames the budget made it wait. The cost side of the throttle, made visible. |

Rows are coloured by state: red while activating (game-thread work happening now), amber while loading,
green once visible, grey otherwise.

### The hitch lines

Under the table, the worst frames in the window - each with its frame number, its cost, and what finished
inside it. **This is the line the plugin exists for.**

A hitch with `nothing finished streaming in this frame` is a real and useful answer: the frame was lost to
something that is not streaming, and knowing that is worth as much as a name would have been.

### The timeline

One column per frame, oldest on the left, height proportional to frame time, red above the threshold. The
amber line is the threshold itself.

The strip is scaled to **twice the threshold**, not to the worst frame in the window. Scaling to the worst
would make every window look the same - the spike always touching the top - and the point of the strip is
that you can see at a glance whether this run was worse than the last one.

---

## 8. The preload budget

### The two numbers

| Setting | Bounds |
|---|---|
| `MaxConcurrentLoads` | How many loads may be in flight. The half that runs off the game thread and is limited by the disk. |
| `MaxActivationMsPerFrame` | How many milliseconds of activation work may *start* in one frame. The half that runs on the game thread and is what you feel. |

`MaxActivationMsPerFrame` bounds what StreamGuard *starts*, not what the engine finishes: once `AddToWorld`
has begun for a level, the engine completes it in its own time. StreamGuard's lever is when the next one
begins.

### The ordering

```
EffectiveDistance = Distance × (1 − DirectionWeight × Alignment)
```

`Alignment` is +1 dead ahead, 0 to the side, −1 directly behind. At the default `DirectionWeight` of 0.5, a
source ahead of the player is treated as half as far as it really is, and one behind as half again as far.

A player standing still has no heading, the alignment term falls out at zero, and the ordering becomes a
plain distance sort - with no special case in the code.

Above geometry sits `Priority`, which a game sets through `RequestLoad` when it knows something the geometry
does not - a scripted teleport, a cutscene, a level the player is about to be dropped into.

### The two rules that make it a budget

1. **The first item always goes through**, however expensive. An activation estimated at 30 ms with a 3 ms
   budget still runs: refusing it would mean it never runs at all, and a budget that can drop work
   permanently is a bug wearing a feature's coat. The budget's job starts with the *second* item.
2. **Nothing waits forever.** A request older than `MaxHoldSeconds` (default 5 s) is promoted past the
   budget. Without it, a player circling one spot could keep a distant request pending for the session.

Both are unit-tested. Together they are the guarantee that turning the throttle on can only make content
arrive later, never not at all.

### How a hold is applied

One cleared engine flag - `SetShouldBeLoaded(false)` or `SetShouldBeVisible(false)` - and nothing else. The
flag goes back the moment the budget has room. StreamGuard remembers the intent while the flag is cleared,
so the request is never forgotten, and releases every hold it owns when the throttle is switched off or the
world goes away.

See [section 6](#6-world-partition-vs-sublevels-what-the-demo-shows-and-what-it-does-not) for where this
works well and where it does not.

---

## 9. Class overview

| Class | Header | What it is |
|---|---|---|
| `UStreamGuardSubsystem` | `StreamGuardSubsystem.h` | The measurer. One per world. |
| `UStreamGuardThrottle` | `StreamGuardThrottle.h` | The planner. Static, pure, world-free. |
| `AStreamGuardHUD` | `StreamGuardHUD.h` | An `AHUD` that draws the board. |
| `UStreamGuardStatics` | `StreamGuardStatics.h` | Blueprint library, plus the pure maths. |
| `UStreamGuardSettings` | `StreamGuardSettings.h` | Project settings. |
| `FStreamGuardFrameRing` | `StreamGuardTypes.h` | The fixed-size timeline. |

### `UStreamGuardSubsystem` — the C++ surface

```cpp
void  SetShowBoard(bool bShow);
bool  IsShowingBoard() const;

void  Freeze();
void  Resume();
bool  IsFrozen() const;
void  ResetMeasurements();

bool  ExportCsv(const FString& Path, FString& OutPath);

void  SetThrottleEnabled(bool bEnabled);
bool  IsThrottleEnabled() const;
void  SetMaxConcurrentLoads(int32 InMax);
void  SetMaxActivationMsPerFrame(float InMs);

bool  RequestLoad(FName PackageName, bool bMakeVisible, int32 Priority);
bool  RequestUnload(FName PackageName, bool bAlsoUnloadPackage);

const TArray<FStreamGuardSourceStat>&   GetSourceStats() const;
const TArray<FStreamGuardFrameSample>&  GetFrameSamples() const;
const FStreamGuardTotals&               GetTotals() const;
bool  GetWorstFrame(FStreamGuardFrameSample& OutSample) const;

void  DrawBoard(UCanvas* Canvas, const FVector2D& Origin, float Width) const;
```

### `AStreamGuardHUD`

Place it as the map's HUD class and the board appears with no console command. Most projects should not
need it: StreamGuard already draws on whatever HUD the project has, through `AHUD::OnHUDPostRender`. Using
both at once is safe - the draw refuses to run twice in one frame.

| Property | Default | |
|---|---|---|
| `bDrawStreamGuardBoard` | `true` | Whether this HUD is one of the draw routes. |
| `bShowBoardOnBeginPlay` | `true` | Turn the board on when this HUD starts. |
| `BoardOrigin` | `(-1, -1)` | Negative means "use the project setting". |
| `BoardWidth` | `0` | Zero or less means "use the project setting". |

---

## 10. Blueprint API

All nodes are under **StreamGuard**. Nothing needs a reference to anything placed in the level.

### Control

| Node | |
|---|---|
| `Get Stream Guard` | The subsystem, or null outside a game world. |
| `Set Show Board` / `Is Showing Board` / `Toggle Board` | The board. |
| `Freeze Stream Guard` / `Resume Stream Guard` / `Toggle Freeze Stream Guard` / `Is Stream Guard Frozen` | Hold the numbers still without pausing the game. |
| `Reset Stream Guard` | Empty window, start again. |
| `Export Stream Guard Csv` | Writes the file, returns where it went. |

### The budget

| Node | |
|---|---|
| `Set Throttle Enabled` / `Is Throttle Enabled` / `Toggle Throttle` | The before-and-after switch. |
| `Set Max Concurrent Loads` / `Get Max Concurrent Loads` | Loads in flight. |
| `Set Max Activation Ms Per Frame` / `Get Max Activation Ms Per Frame` | Game-thread budget. |

### Requests

| Node | |
|---|---|
| `Request Level Load` | Package name, make visible, priority. Returns false if the world has no such level. |
| `Request Level Unload` | Package name, also unload the package. |

### Reading

| Node | |
|---|---|
| `Get Source Stats` | The board's rows, sorted and folded. |
| `Get Frame Samples` | The timeline, oldest first. |
| `Get Totals` | The one-line summary as a struct. |
| `Get Worst Frame` | The worst frame in the window. |

### Maths — static, world-free, unit-tested

| Node | |
|---|---|
| `Plan Frame` | The whole budget as one pure call. |
| `Score Source` | The distance-and-heading score on its own. |
| `Sort Sources` / `Fold Sources` | The table's ordering and its ceiling. |
| `Find Sources Completed In Frame` | The attribution, on your own data. |
| `Find Hitches` / `Describe Completions` | The hitch lines. |
| `Build Csv` / `Get Csv Header` / `Split Csv Line` | The export, both directions. |
| `Format Ms` / `Format Distance` / `Get State Name` | The formatters the board uses. |

---

## 11. C++ examples

### Bind the board to a debug key

```cpp
#include "StreamGuardStatics.h"

void AMyPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AMyPlayerController::ToggleStreamGuard);
}

void AMyPlayerController::ToggleStreamGuard()
{
    UStreamGuardStatics::ToggleBoard(this);
}
```

### Find out what cost you the last hitch

```cpp
#include "StreamGuardStatics.h"
#include "StreamGuardTypes.h"

void AMyActor::ReportWorstHitch()
{
    FStreamGuardFrameSample Worst;
    if (!UStreamGuardStatics::GetWorstFrame(this, Worst))
    {
        return;
    }

    TArray<FStreamGuardSourceStat> Sources;
    UStreamGuardStatics::GetSourceStats(this, Sources);

    TArray<FName> Landed;
    UStreamGuardStatics::FindSourcesCompletedInFrame(Sources, Worst.FrameNumber, Landed);

    UE_LOG(LogTemp, Display, TEXT("Worst frame %lld cost %.1f ms; %d source(s) landed in it: %s"),
        Worst.FrameNumber, Worst.FrameMs, Landed.Num(),
        *UStreamGuardStatics::DescribeCompletions(Worst));
}
```

### Ask for a level through the budget

```cpp
#include "StreamGuardStatics.h"

void AMyDirector::PreloadTheArena()
{
    // Priority 10: this is where the player is about to be teleported, and the geometry does not know that.
    UStreamGuardStatics::RequestLevelLoad(this, TEXT("L_Arena"), /*bMakeVisible*/ true, /*Priority*/ 10);
}
```

With the throttle off this behaves exactly like `LoadStreamLevel`. With it on, the request is paced.

### Turn the budget on for a cutscene and off again afterwards

```cpp
#include "StreamGuardStatics.h"

void AMyCutscene::Start()
{
    // Nothing may cost the game thread more than a millisecond while the camera is moving.
    UStreamGuardStatics::SetThrottleEnabled(this, true);
    UStreamGuardStatics::SetMaxActivationMsPerFrame(this, 1.0f);
    UStreamGuardStatics::SetMaxConcurrentLoads(this, 1);
}

void AMyCutscene::Finish()
{
    UStreamGuardStatics::SetThrottleEnabled(this, false);
}
```

Switching the throttle off releases every hold immediately, so nothing is left waiting.

### Plan a frame yourself, with no world at all

```cpp
#include "StreamGuardThrottle.h"

TArray<FStreamGuardPending> Pending;

FStreamGuardPending Request;
Request.SourceName          = TEXT("L_Block_A");
Request.Kind                = EStreamGuardPendingKind::Activate;
Request.Location            = FVector(12000.0, 0.0, 0.0);
Request.EstimatedMs         = 4.0f;
Request.RequestTimeSeconds  = 0.0;
Pending.Add(Request);

FStreamGuardBudget Budget;
Budget.MaxConcurrentLoads      = 2;
Budget.MaxActivationMsPerFrame = 3.0f;
Budget.NowSeconds              = 0.0;

const FStreamGuardPlan Plan = UStreamGuardThrottle::PlanFrame(
    Pending, Budget, /*PlayerPos*/ FVector::ZeroVector, /*PlayerVel*/ FVector(1200.0, 0.0, 0.0));

// Plan.StartActivations, Plan.StartLoads, Plan.Deferred, Plan.PlannedActivationMs, Plan.PromotedCount
```

No world, no subsystem, no clock. This is the same function the running game calls every frame, which is
why it is the one under test.

### Export a CSV and read it back

```cpp
#include "StreamGuardStatics.h"
#include "Misc/FileHelper.h"

FString Path;
if (UStreamGuardStatics::ExportStreamGuardCsv(this, TEXT(""), Path))
{
    FString Contents;
    FFileHelper::LoadFileToString(Contents, *Path);

    TArray<FString> Lines;
    Contents.ParseIntoArrayLines(Lines);

    for (int32 Index = 1; Index < Lines.Num(); ++Index)
    {
        TArray<FString> Fields;
        UStreamGuardStatics::SplitCsvLine(Lines[Index], Fields);

        if (Fields.Num() == 13 && Fields[0] == TEXT("Source"))
        {
            // Fields[1] name, Fields[5] total ms, Fields[7] completion frame
        }
    }
}
```

### Draw the board somewhere of your own

```cpp
#include "StreamGuardSubsystem.h"

void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (UStreamGuardSubsystem* Guard = GetWorld()->GetSubsystem<UStreamGuardSubsystem>())
    {
        Guard->DrawBoard(Canvas, FVector2D(40.0f, 200.0f), 800.0f);
    }
}
```

---

## 12. Console commands

| Command | |
|---|---|
| `StreamGuard.Show [0\|1]` | Draw the board. No argument toggles. |
| `StreamGuard.Freeze [0\|1]` | Hold the board still without pausing the game. No argument toggles. |
| `StreamGuard.Export [path]` | Write the board to CSV. No argument writes a timestamped file under `Saved/StreamGuard`. A relative path is relative to the same place; an absolute path is used as given. |
| `StreamGuard.Budget <n> [ms]` | Loads in flight, and optionally the activation milliseconds per frame. With no argument, prints the current budget. |
| `StreamGuard.Throttle [0\|1]` | Pace streaming work to the budget. No argument toggles. |
| `StreamGuard.Reset` | Throw away every measurement and start from an empty window. |

Every command reports what it did to the `LogStreamGuard` category, including every reason it could not do
it. A diagnostic that fails quietly is worse than one that is not installed.

---

## 13. Where every number comes from

### Measured

| Number | Source |
|---|---|
| The list of sources | `UWorld::GetStreamingLevels()`, walked every frame. |
| State | `ULevelStreaming::GetLevelStreamingState()` - the engine's own state machine, public since 5.2. |
| `LOAD` | Wall clock from the poll that saw `Loading` begin to the engine's `OnLevelLoaded`. |
| `ACT` | Wall clock from the poll that saw `MakingVisible` begin to the engine's `OnLevelShown`. |
| `FRAME` | `GFrameCounter`, read **inside** `OnLevelLoaded` / `OnLevelShown`. |
| Timeline frame times | `DeltaTime` per tick, into a fixed ring. |
| `DIST` | Distance from the player's view target to the level's location. |
| Actor count | `ULevel::Actors.Num()`, once, on the frame the level becomes visible. |
| Level location | `ULevelStreaming::LevelTransform` before it loads; `ALevelBounds::CalculateLevelBounds` centre once it is in the world. |
| World Partition settled | `UWorldPartitionSubsystem::IsAllStreamingCompleted()`. |

### Modelled

| Number | How |
|---|---|
| `EstimatedMs` for an activation | The measured cost of the *last* activation of that source. For a source that has never been activated, the `AssumedActivationMs` project setting - which stops mattering after the first time. |

That is the entire list. Everything else on the board is read or timed.

### Known limits, stated plainly

* **A load start can be one frame late.** Starts are seen by polling, and the subsystem's tick may run
  before or after the world's streaming update. This moves a duration by one frame; it does not move a
  completion frame, which comes from the delegate.
* **`ACT` is not the whole frame cost.** `AddToWorld` is the largest part of making a level visible, but the
  frame it lands in also pays for whatever else that content triggered - construction scripts, physics
  registration, render state. The timeline shows the true frame cost; `ACT` shows the streaming part of it.
* **StreamGuard cannot see inside the async loader.** A load's milliseconds are the wall clock the request
  took, not CPU time spent on it. On a busy loader, a source's `LOAD` includes time it spent queued.
* **The throttle is weak on World Partition.** See [section 6](#6-world-partition-vs-sublevels-what-the-demo-shows-and-what-it-does-not).
* **`bShouldBlockOnLoad` is reported, not prevented.** A blocking load is a hitch by definition and
  StreamGuard lets it through, marking the row rather than pretending it paced it.

---

## 14. The ceiling StreamGuard puts on itself

| Limit | Default | Why |
|---|---|---|
| Tracked sources | 256 | One record and one watcher each. A World Partition map's cell count is a property of the world's size, not of anything a programmer chose - without a ceiling, the diagnostic would scale with the map. |
| Timeline samples | `WindowSeconds × AssumedFrameRate`, clamped to 8192 | Allocated once at startup and never resized, so the window never allocates on the frame the game is already struggling. |
| Names per frame sample | 6 | With the true count uncapped, so a busy frame reads "… and 14 more" rather than quietly claiming six. |
| Snapshot rebuilds | 10 Hz | The scan is every frame; the sorting, folding and formatting are not. Nothing reads the board faster than a human can look at it. |

Sources past the ceiling are **still counted** - that is one enum read per level - and reported as a single
folded row that keeps the number. The ceiling can hide *which* source is costing you; it can never hide
*that* something is.

The fold survives being folded again: the subsystem's overflow row already stands for hundreds of cells,
and when the board's row limit folds it a second time the count comes with it.

---

## 15. CSV export format

Thirteen columns, one header line, one line per entry. Every row has the same shape, so a single parse
reads the whole file.

```
Kind,Name,State,LoadMs,ActivateMs,TotalMs,StartedFrame,CompletedFrame,TimeSeconds,DistanceMeters,Actors,HeldFrames,Note
```

| `Kind` | What the row is |
|---|---|
| `Summary` | One row. World type, throttle state, average and worst frame, window length, source count, held count, hitch count. |
| `Source` | One per row on the board. |
| `Frame` | One per frame of the timeline, when `bExportFrameRows` is on. `Name` holds what finished in that frame, `TotalMs` the frame time, `Actors` the completion count, `State` is `Hitch` or `Ok`. |

Fields containing a comma, a quote or a newline are quoted, and quotes inside them are doubled - so a level
called `L_Block,B` survives the round trip. `SplitCsvLine` undoes it.

---

## 16. Project settings reference

**Project Settings → Plugins → StreamGuard**

### Measurement

| Setting | Default | |
|---|---|---|
| `HitchThresholdMs` | 33 | A frame at or above this is a hitch. Not 16.7: at 60 fps a single 20 ms frame is a stutter nobody reports, and a timeline two-thirds red tells you nothing. Lower it for a 120 Hz game. |
| `WindowSeconds` | 10 | Roughly how long it takes to feel a hitch, say "what was that" and look at the screen. |
| `AssumedFrameRate` | 120 | Only sizes the ring. If the game runs faster, the window simply holds fewer seconds. |
| `MaxTrackedSources` | 256 | The ceiling. See section 14. |
| `bCountActorsOnShow` | on | One walk over an array the engine has just built, once per level. |
| `SnapshotsPerSecond` | 10 | How often the board's arrays are rebuilt. Not the measuring rate - that is every frame. |

### Budget

| Setting | Default | |
|---|---|---|
| `bThrottleEnabled` | **off** | Showing you what is happening is one decision; changing what happens is another, and a project should make it on purpose. |
| `MaxConcurrentLoads` | 2 | Small and fixed. Eight simultaneous level loads do not finish sooner than eight sequential ones - they finish at the same time *as each other*, which is exactly the case where they all activate in one frame. |
| `MaxActivationMsPerFrame` | 3 | A fifth of a 60 fps frame. |
| `AssumedActivationMs` | 4 | Only for a source that has never been activated. |
| `DirectionWeight` | 0.5 | How much the player's heading counts against plain distance. |
| `MaxHoldSeconds` | 5 | The no-starvation guarantee. |
| `bThrottleEngineRequests` | on | Also pace streaming the engine started by itself. See section 6 for where this works. |

### Presentation

| Setting | Default | |
|---|---|---|
| `bShowBoardByDefault` | off | A board nobody asked for over a playtest is a board that gets the plugin uninstalled. |
| `bAutoDrawBoardOnAnyHUD` | on | Draws through `AHUD::OnHUDPostRender` so a project keeps its own HUD class. |
| `BoardOrigin` | (28, 60) | Pixels from the top left. |
| `BoardWidth` | 720 | Pixels. |
| `BoardRows` | 14 | Everything past it folds into one row. |
| `SortBy` | `CompletedFrame` | Most recently finished first - what you want while driving. |
| `bShowTimeline` | on | The strip under the table. |
| `TimelineHeight` | 54 | Pixels. |
| `HitchDetailRows` | 3 | How many hitches are written out with what landed in them. |

### Export

| Setting | Default | |
|---|---|---|
| `CsvSubdirectory` | `StreamGuard` | Relative to `Saved`. Relative on purpose: an absolute path in a project setting exists on exactly one machine, and the first thing that happens to it is that somebody commits it. |
| `bExportFrameRows` | on | One row per frame of the timeline as well as one per source. |

---

## 17. Tests

Automation tests under `StreamGuard.*`, runnable from **Tools → Session Frontend → Automation** or with
`Automation RunTests StreamGuard`. They exercise the same functions the running game calls - there is no
second implementation in the tests that agrees with the first until somebody edits one of the two.

| Test | What it proves |
|---|---|
| `StreamGuard.Plan.RespectsMaxConcurrentLoads` | The planner never starts more loads than the budget allows, counts what the engine already has in flight, drops nothing, and lets a blocking load through while reporting it as a promotion. |
| `StreamGuard.Plan.PrefersTheDirectionOfTravel` | A source ahead of the player beats one the same distance to the side, which beats one behind; a standing player scores by distance alone; priority outranks geometry. |
| `StreamGuard.Plan.MillisecondBudgetDefersRatherThanDrops` | Driven frame by frame, the queue drains completely and nothing starts twice. An activation more expensive than the whole budget still runs. A request held too long is promoted past a spent budget. |
| `StreamGuard.Attribution.FindsTheSourcesForAFrame` | The frame-to-source lookup finds both sources in a marked frame and nothing in a quiet one; frame zero never matches; the ring records completions against the right frame and keeps the true count when it caps the names. |
| `StreamGuard.Ceiling.OverflowFoldsIntoOneRow` | Past 256 sources the table is exactly 257 rows with exactly one aggregate; nothing is lost through the fold; a table exactly at the ceiling gains no pointless row; a fold of a fold keeps its count. |
| `StreamGuard.Csv.HeaderPlusOneLinePerEntry` | Header plus one line per entry, thirteen columns on every line, and names containing commas and quotes survive the round trip. |

---

## 18. Troubleshooting

**The board says "this world has no streaming levels".**
The map has no sublevels and is not a World Partition world. There is nothing to measure and nothing is
wrong with the plugin.

**Every `LOAD` column is a dash.**
Nothing has loaded since the board was opened or reset. Durations are measured, not recovered - a level
that was already in memory when you started watching has no measured load time, and StreamGuard prints a
dash rather than inventing a zero.

**The throttle is on but nothing is being held.**
Either nothing is waiting (the budget only ever holds requests that have not started), or this is a World
Partition world - see [section 6](#6-world-partition-vs-sublevels-what-the-demo-shows-and-what-it-does-not).

**`RequestLoad` returns false.**
This world has no streaming level with that name; the reason is logged with the name you passed. Check it
against the Levels window. Both the long package name and the short name are accepted.

**The board is drawn twice / overlaps something.**
It cannot be drawn twice - the draw refuses to run more than once per frame - but it can sit under your own
HUD. Move it with `BoardOrigin`, or turn `bAutoDrawBoardOnAnyHUD` off and use `AStreamGuardHUD`.

**The timeline is empty but the game is running.**
The measurement is frozen. `StreamGuard.Freeze 0`.

**Nothing appears in a Shipping build.**
The console is usually compiled out of Shipping. Call `UStreamGuardStatics::ToggleBoard` from a key or a
debug menu instead; the board itself works unchanged.

---

## 19. Support

* Documentation: <https://wiki.teufel-engineering.com/en/StreamGuard/documentation>
* Support: <mailto:teufelsilvan@gmail.com>

When reporting a streaming problem, the fastest thing you can send is a CSV. Freeze the board at the hitch
(`StreamGuard.Freeze`), export it (`StreamGuard.Export`) and attach the file: it carries the frame numbers,
the durations and the frame-to-source attribution, which is everything the answer depends on.

---

## 20. Version history

### 1.0.0 — 2026-09-03

First release. Built and verified against **Unreal Engine 5.8**, Win64, with `RunUAT BuildPlugin` for the
editor and game targets in Development and Shipping — zero warnings.

Contents of this release:

| | |
|---|---|
| Measurement | Per-source load and activation timing, completion frame stamped inside the engine's own delegates, live and at-completion distance, actor count, blocking and held flags. |
| Timeline | A ten-second frame-time ring with hitch marking and the frame-to-source attribution under it. |
| Preload budget | `MaxConcurrentLoads` + `MaxActivationMsPerFrame`, ordered by distance scaled by the player's heading, with the first-item-always-goes and nothing-waits-forever guarantees. |
| Board | `UCanvas`, drawn from `AHUD::OnHUDPostRender` or from `AStreamGuardHUD`. No UMG. |
| API | Full Blueprint surface plus a static, world-free planner, sort, fold, attribution, CSV build and CSV parse. |
| Console | `StreamGuard.Show`, `.Freeze`, `.Export`, `.Budget`, `.Throttle`, `.Reset`. |
| Settings | 24 documented project settings, every ceiling with its reason next to it. |
| Tests | 6 automation tests under `StreamGuard.*`. |
| Content | Demo map with four sublevels, a fixed route, five Blueprints, a button strip and eight flat materials — engine primitives only, nothing imported. |

Known limits in this release, stated rather than discovered:

* The **throttle** half is at its strongest on classic sublevel streaming and on requests routed through
  `RequestLoad`. On World Partition it is weak, because World Partition re-asserts its own decisions on
  every streaming update — see [section 6](#6-world-partition-vs-sublevels-what-the-demo-shows-and-what-it-does-not).
  The **measurement** half is fully accurate on both.
* Win64 only. There is nothing platform-specific in the code, but nothing else has been built and tested,
  and a platform this plugin has not run on is not a platform it supports.

---

Copyright 2026 Silvan Teufel. All Rights Reserved.
