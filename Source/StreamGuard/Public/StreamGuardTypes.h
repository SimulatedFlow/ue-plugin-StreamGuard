// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StreamGuardTypes.generated.h"

/**
 * What kind of thing a streaming source is.
 *
 * Sublevel and PartitionCell are the same C++ object underneath - a World Partition runtime cell is a
 * UWorldPartitionLevelStreamingDynamic, which derives from ULevelStreamingDynamic and sits in the same
 * UWorld::StreamingLevels array as a hand-placed sublevel. StreamGuard tells them apart only so the board can
 * say which world you are looking at, because the two hitch for different reasons and are fixed differently.
 */
UENUM(BlueprintType)
enum class EStreamGuardSourceKind : uint8
{
	/** A sublevel placed in the persistent level, or one added at runtime. */
	Sublevel,

	/** A World Partition runtime cell. */
	PartitionCell,

	/** Not a source at all: the single folded row that stands for everything past the tracking ceiling. */
	Aggregate
};

/**
 * Where a source is in the engine's streaming state machine.
 *
 * One-to-one with ELevelStreamingState, which is a plain C++ enum in the engine and therefore cannot be shown
 * to Blueprint or written into a UPROPERTY. Mirroring it is the price of the board being readable from
 * Blueprint at all; the mapping lives in one function so there is one place for it to be wrong.
 */
UENUM(BlueprintType)
enum class EStreamGuardSourceState : uint8
{
	/** Not in the world's streaming array any more. */
	Removed,

	/** Known, nothing in memory. */
	Unloaded,

	/** The package failed to load. This is a bug in the content, and the board says so rather than hiding it. */
	FailedToLoad,

	/** The package is coming off disk. This is the half that costs milliseconds you do not control. */
	Loading,

	/** In memory, not in the world. The cheap half is done and the expensive half has not started. */
	LoadedNotVisible,

	/** AddToWorld is running. This is the half that costs the frame you are looking at. */
	MakingVisible,

	/** In the world and rendering. */
	Visible,

	/** RemoveFromWorld is running. */
	MakingInvisible
};

/** Which half of the work a queued request is waiting for. */
UENUM(BlueprintType)
enum class EStreamGuardPendingKind : uint8
{
	/** Pull the package off disk. Bounded by MaxConcurrentLoads, because disks and the async loader are. */
	Load,

	/** Add the loaded level to the world. Bounded by MaxActivationMsPerFrame, because this is the hitch. */
	Activate
};

/** How the board is sorted. */
UENUM(BlueprintType)
enum class EStreamGuardSort : uint8
{
	/** Slowest first. What you want when you are looking for the thing that cost you the frame. */
	LoadTime,

	/** Nearest first. What you want when you are looking at what is around the player right now. */
	Distance,

	/** Most recently finished first. What you want while you are driving and watching things arrive. */
	CompletedFrame,

	/** Alphabetical. What you want when you are comparing two runs side by side. */
	Name
};

/**
 * One streaming source, measured.
 *
 * Everything here is either read from the engine or timed by StreamGuard between two engine events. Nothing
 * is modelled or estimated - the one estimate in the plugin (what an activation is going to cost before it
 * has ever run) lives in FStreamGuardPending, where it is clearly labelled as one.
 */
USTRUCT(BlueprintType)
struct FStreamGuardSourceStat
{
	GENERATED_BODY()

	/** Short name, the one you would recognise in the Levels window. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	FName SourceName;

	/** Full package name, the one you would type into a console command. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	FName PackageName;

	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	EStreamGuardSourceKind Kind = EStreamGuardSourceKind::Sublevel;

	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	EStreamGuardSourceState State = EStreamGuardSourceState::Unloaded;

	/** Milliseconds between entering Loading and leaving it. Zero until it has happened once. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float LoadMs = 0.0f;

	/**
	 * Milliseconds between entering MakingVisible and becoming Visible.
	 *
	 * This is usually the number that matters. Loading happens on a worker thread and costs you a wait;
	 * activation happens on the game thread and costs you the frame.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float ActivateMs = 0.0f;

	/** LoadMs + ActivateMs, kept as its own field so a sort never has to add two floats in a comparator. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float TotalMs = 0.0f;

	/**
	 * The frame this source finished in.
	 *
	 * Stamped inside the engine's own OnLevelShown / OnLevelLoaded delegate, so it is the frame the engine
	 * finished in and not the frame StreamGuard next got a tick in. That distinction is the whole reason the
	 * timeline can put a name next to a red mark.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int64 CompletedFrame = 0;

	/** The frame the load started in. CompletedFrame minus this is how many frames it was in flight. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int64 StartedFrame = 0;

	/** Seconds (real time) at which it finished, for lining rows up against the timeline. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	double CompletedTimeSeconds = 0.0;

	/** Where the source is, in world space. Before it has ever loaded this is its level transform. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	FVector Location = FVector::ZeroVector;

	/** Centimetres from the player to Location, live. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float DistanceToPlayer = 0.0f;

	/** Centimetres from the player at the moment it finished. The interesting one after the fact. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float DistanceAtCompletion = 0.0f;

	/** How many actors the level brought with it, counted once when it became visible. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 ActorCount = 0;

	/** True if the engine was told to block on this one. A blocking load is a hitch by definition. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bBlocking = false;

	/** True if StreamGuard's budget ever made this source wait. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bThrottled = false;

	/** How many frames in total StreamGuard held it back. The cost side of the budget, made visible. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 HeldFrames = 0;

	/** True for the single folded row that stands for everything past the tracking ceiling. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bIsAggregate = false;

	/** How many real sources that folded row stands for. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 AggregatedSources = 0;
};

/**
 * One frame of the timeline.
 *
 * The timeline is what turns a table of durations into an explanation. A source that took 180 ms to load is
 * not necessarily what cost you anything - the load ran on a worker thread. The frame that took 41 ms is what
 * cost you, and CompletedSources is the list of things that landed inside it.
 */
USTRUCT(BlueprintType)
struct FStreamGuardFrameSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int64 FrameNumber = 0;

	/** How long the frame took, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float FrameMs = 0.0f;

	/** Real time in seconds when the frame was recorded. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	double TimeSeconds = 0.0;

	/** True if FrameMs was over the hitch threshold. Decided once, when the sample is written. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bOverThreshold = false;

	/** How many sources finished in this frame, including any past the per-sample name limit. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 CompletedCount = 0;

	/**
	 * Which sources finished in this frame, capped.
	 *
	 * Capped because this array exists once per frame of the window and a frame in which two hundred cells
	 * landed would otherwise make the diagnostic allocate more than the thing it is diagnosing.
	 * CompletedCount is not capped, so the board can always say "and 12 more".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	TArray<FName> CompletedSources;
};

/**
 * A request that has not started yet.
 *
 * This is the planner's input and it deliberately carries no pointers. PlanFrame is given an array of these,
 * a budget and two vectors, and it has no way to reach a world even if it wanted one - which is what makes it
 * something a test can drive to any state in three lines.
 */
USTRUCT(BlueprintType)
struct FStreamGuardPending
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	FName SourceName;

	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	EStreamGuardPendingKind Kind = EStreamGuardPendingKind::Load;

	/** Where the source is. The planner compares this against the player position and heading. */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	FVector Location = FVector::ZeroVector;

	/**
	 * What the activation is expected to cost, in milliseconds.
	 *
	 * The one estimate in the plugin. For a source that has been activated before it is the measured cost of
	 * the last time, so it stops being an estimate after one pass; for one that never has it is the project
	 * setting. Ignored for Load requests, which are bounded by a count rather than by time.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	float EstimatedMs = 0.0f;

	/** Higher goes first, ahead of any distance consideration. Set by the game; zero unless it says otherwise. */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	int32 Priority = 0;

	/**
	 * This one cannot wait.
	 *
	 * A blocking load is already a hitch and holding it back would only make it a later hitch. StreamGuard
	 * lets it through and marks the row, which is more useful than pretending it paced it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	bool bBlocking = false;

	/** Real time in seconds at which the request first appeared. Used for ageing, so nothing starves. */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	double RequestTimeSeconds = 0.0;
};

/**
 * What the planner is allowed to spend this frame.
 *
 * Two numbers, because streaming has two halves with two different costs. MaxConcurrentLoads bounds the half
 * that happens off the game thread and is limited by the disk; MaxActivationMsPerFrame bounds the half that
 * happens on the game thread and is limited by your frame budget. A tool that offered one number for both
 * would be lying about one of them.
 */
USTRUCT(BlueprintType)
struct FStreamGuardBudget
{
	GENERATED_BODY()

	/**
	 * How many loads may be in flight at once, counting the ones already running.
	 *
	 * The four knobs below are EditAnywhere as well as BlueprintReadWrite: a budget is something a designer
	 * tunes on an actor in the Details panel, and a struct that can only be built from a Blueprint node is a
	 * struct nobody tunes. The two fields after them are not - they are what the caller measures and passes
	 * in, and offering them as settings would only invite somebody to set a value that is overwritten before
	 * it is read.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxConcurrentLoads = 2;

	/** How many milliseconds of activation work may start in one frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard",
		meta = (ClampMin = "0.0", ForceUnits = "ms"))
	float MaxActivationMsPerFrame = 3.0f;

	/** How many loads the engine already has running. The planner subtracts these from its allowance. */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard", meta = (ClampMin = "0"))
	int32 LoadsInFlight = 0;

	/**
	 * How much the player's heading counts against plain distance, from 0 to 1.
	 *
	 * At 0 the planner is a distance sort and will happily spend the frame on the cell behind you. At 0.5 a
	 * cell straight ahead is treated as half as far as it is and a cell straight behind as half again as far,
	 * which is the ordering you actually want while driving. At 1 a cell behind you is effectively infinitely
	 * far and will never be started while anything ahead is waiting, which is too strong for a game where the
	 * player can turn round.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirectionWeight = 0.5f;

	/**
	 * How long a request may be held before it is promoted past the budget.
	 *
	 * The second half of the no-starvation guarantee (the first is that the first item through always goes).
	 * Without it, a player circling one spot could keep a far-away request waiting forever, and a budget that
	 * can drop work permanently is not a budget, it is a bug.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float MaxHoldSeconds = 5.0f;

	/** Real time in seconds, passed in rather than read, so the planner has no clock of its own. */
	UPROPERTY(BlueprintReadWrite, Category = "StreamGuard")
	double NowSeconds = 0.0;
};

/** What the planner decided. Names only - applying it is the subsystem's job, and a different one. */
USTRUCT(BlueprintType)
struct FStreamGuardPlan
{
	GENERATED_BODY()

	/** Loads that start this frame. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	TArray<FName> StartLoads;

	/** Activations that start this frame. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	TArray<FName> StartActivations;

	/** Everything that waits a frame. Waits - not drops. Nothing ever leaves the queue unstarted. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	TArray<FName> Deferred;

	/** The activation milliseconds this plan spends, by the estimates it was given. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float PlannedActivationMs = 0.0f;

	/** How many of the started items were promoted past the budget because they could not wait any longer. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 PromotedCount = 0;
};

/** The one-line summary at the top of the board, and what a Blueprint reads to build its own. */
USTRUCT(BlueprintType)
struct FStreamGuardTotals
{
	GENERATED_BODY()

	/** How many sources StreamGuard is following in detail. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 TrackedSources = 0;

	/** How many the world has. Larger than TrackedSources means the ceiling folded some. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 SeenSources = 0;

	/** How many are loading right now. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 LoadsInFlight = 0;

	/** How many are being added to the world right now. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 ActivationsInFlight = 0;

	/** How many are in the world and rendering. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 VisibleSources = 0;

	/** How many requests the budget is currently holding back. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 HeldRequests = 0;

	/** Worst frame inside the window, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float WorstFrameMs = 0.0f;

	/** Mean frame time inside the window, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float AverageFrameMs = 0.0f;

	/** How many frames in the window went over the threshold. The number people quote at each other. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 HitchCount = 0;

	/** The threshold those hitches were counted against. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float HitchThresholdMs = 33.0f;

	/** How many seconds of frames the window holds. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float WindowSeconds = 10.0f;

	/** Whether the budget is on. The before-and-after switch. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bThrottleEnabled = false;

	/** Whether measuring is frozen. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bFrozen = false;

	/** Whether this world is a World Partition world. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bWorldPartition = false;

	/** For a World Partition world, whether it has settled. Always true elsewhere. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	bool bAllStreamingCompleted = true;

	/** The budget in force, so the board can print what it is pacing to. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	int32 MaxConcurrentLoads = 2;

	/** The budget in force, so the board can print what it is pacing to. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	float MaxActivationMsPerFrame = 3.0f;

	/** Where the player was when the snapshot was taken. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	FVector PlayerLocation = FVector::ZeroVector;

	/** How fast, and which way. This is what the planner sorts by. */
	UPROPERTY(BlueprintReadOnly, Category = "StreamGuard")
	FVector PlayerVelocity = FVector::ZeroVector;
};

/**
 * The last few seconds of frame times, in a fixed amount of memory.
 *
 * A plain circular buffer rather than an array that is trimmed from the front, because this is written to
 * every single frame and it is the one piece of StreamGuard that has no excuse for allocating. Configure once
 * and it never allocates again.
 *
 * Not a UCLASS and not owned by the subsystem's tick alone: it is a struct with no world behind it so the
 * tests can drive it directly to the states that are hard to reach in a running game - an empty window, a
 * window that has wrapped, a window with a hitch exactly on the boundary.
 */
USTRUCT()
struct STREAMGUARD_API FStreamGuardFrameRing
{
	GENERATED_BODY()

	/** Size the ring for a window of seconds at an assumed frame rate, and clear it. */
	void Configure(float InWindowSeconds, float InAssumedFrameRate, float InHitchThresholdMs);

	/** Record one frame. Returns the index of the sample so a completion can be attributed to it. */
	int32 AddFrame(int64 FrameNumber, float FrameMs, double TimeSeconds);

	/** Note that a source finished in the frame most recently added. */
	void NoteCompletion(FName SourceName);

	/** Throw everything away. The window keeps its size. */
	void Reset();

	/** How many samples the window is holding. */
	int32 Num() const { return Count; }

	bool IsEmpty() const { return Count == 0; }

	/** The samples, oldest first. */
	void GetOrdered(TArray<FStreamGuardFrameSample>& Out) const;

	/** The most recent sample, or null on an empty window. */
	const FStreamGuardFrameSample* GetNewest() const;

	/** The worst frame in the window, or null on an empty window. */
	const FStreamGuardFrameSample* GetWorst() const;

	/** Mean frame time in the window, in milliseconds. Zero on an empty window. */
	float GetAverageMs() const;

	/** How many frames in the window went over the threshold. */
	int32 GetHitchCount() const;

	/** The threshold this ring was configured with. */
	float GetHitchThresholdMs() const { return HitchThresholdMs; }

	/** How many seconds of frames the ring can hold at the frame rate it was configured for. */
	float GetWindowSeconds() const { return WindowSeconds; }

	/**
	 * How many names one sample will keep.
	 *
	 * Six, which is more than the number of things that land in one frame in any situation you would still
	 * call a game, and small enough that six hundred samples of them is a rounding error.
	 */
	static constexpr int32 MaxNamesPerSample = 6;

private:
	UPROPERTY()
	TArray<FStreamGuardFrameSample> Samples;

	/** Where the next sample goes. */
	int32 Head = 0;

	/** How many of the slots are real. Stops rising at Samples.Num(). */
	int32 Count = 0;

	float WindowSeconds = 10.0f;
	float HitchThresholdMs = 33.0f;
};
