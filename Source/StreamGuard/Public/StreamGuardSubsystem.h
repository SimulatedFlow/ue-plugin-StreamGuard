// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakObjectPtr.h"

#include "StreamGuardTypes.h"
#include "StreamGuardSubsystem.generated.h"

class ULevelStreaming;
class UCanvas;
class UStreamGuardLevelWatcher;

/** Which of the engine's four streaming delegates fired. Plain C++: nothing outside the plugin needs it. */
enum class EStreamGuardLevelEvent : uint8
{
	Loaded,
	Shown,
	Unloaded,
	Hidden
};

/**
 * The measurer. One per world, alive exactly as long as the world whose streaming it is watching.
 *
 * Where the numbers come from, because a diagnostic that will not say is not one:
 *
 *   **The sources are the world's own list.** UWorld::GetStreamingLevels() is walked every frame. It holds
 *   both hand-placed sublevels and World Partition runtime cells, because a runtime cell *is* a
 *   UWorldPartitionLevelStreamingDynamic and that derives from ULevelStreamingDynamic - there is no second
 *   code path for World Partition and there does not need to be. The only World Partition specific call in
 *   the plugin is UWorldPartitionSubsystem::IsAllStreamingCompleted(), a public BlueprintCallable function,
 *   used for one line on the board that says whether the world has settled.
 *
 *   **The durations are timed between two engine states.** ULevelStreaming::GetLevelStreamingState() is the
 *   engine's own state machine, public since 5.2. Loading -> LoadedNotVisible is the load; MakingVisible ->
 *   LoadedVisible is the activation. StreamGuard adds nothing to either number and subtracts nothing from it.
 *
 *   **The frame number comes from the engine's delegates, not from polling.** OnLevelLoaded and OnLevelShown
 *   fire the moment the engine is finished, so GFrameCounter read inside them is the frame it really
 *   happened in. Polling would attribute a completion to whichever tick noticed it, which is off by a frame
 *   about half the time - and being off by a frame is fatal to a tool whose entire claim is "this landed in
 *   that frame". See UStreamGuardLevelWatcher for why that needs an object per level.
 *
 *   **The frame times are the frames.** The timeline is DeltaTime, once per tick, into a fixed ring.
 *
 * And what it costs. StreamGuard keeps one record and one watcher per tracked source, bounded by
 * MaxTrackedSources; one ring of frame samples, bounded by WindowSeconds times AssumedFrameRate; and rebuilds
 * its public arrays at SnapshotsPerSecond rather than every frame. Nothing in here grows with the size of the
 * map. Sources past the ceiling are still counted - that is one integer - and reported as a single folded
 * row, so the ceiling can hide the identity of a cost but never the cost itself.
 */
UCLASS()
class STREAMGUARD_API UStreamGuardSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UStreamGuardSubsystem();

	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	//~ Control ------------------------------------------------------------------------------------------

	/** Show or hide the board. */
	void SetShowBoard(bool bShow);

	/** Whether the board is being drawn. */
	bool IsShowingBoard() const { return bShowBoard; }

	/**
	 * Stop measuring. The board holds whatever it had when you called this.
	 *
	 * Freezing does not pause the game and does not stop the engine streaming. It stops StreamGuard writing
	 * into its ring and its records, which is the only thing you actually wanted stopped - the numbers from
	 * the moment of the hitch are gone half a second later otherwise.
	 */
	void Freeze();

	/** Start measuring again, keeping what is already in the window. */
	void Resume();

	/** Whether measuring is frozen. */
	bool IsFrozen() const { return bFrozen; }

	/** Throw everything away and start from an empty window. Also unfreezes. */
	void ResetMeasurements();

	/**
	 * Write the current board to a CSV.
	 *
	 * Empty path - a timestamped file under Saved/<CsvSubdirectory>. Relative path - relative to the same
	 * place. Absolute path - exactly there. OutPath is always filled in with where it went.
	 */
	bool ExportCsv(const FString& Path, FString& OutPath);

	/** Turn the preload budget on or off. The before-and-after switch. */
	void SetThrottleEnabled(bool bEnabled);

	/** Whether the preload budget is on. */
	bool IsThrottleEnabled() const { return bThrottleEnabled; }

	/** How many loads may be in flight at once. */
	void SetMaxConcurrentLoads(int32 InMax);

	/** How many loads may be in flight at once. */
	int32 GetMaxConcurrentLoads() const { return Budget.MaxConcurrentLoads; }

	/** How many milliseconds of activation work may start in one frame. */
	void SetMaxActivationMsPerFrame(float InMs);

	/** How many milliseconds of activation work may start in one frame. */
	float GetMaxActivationMsPerFrame() const { return Budget.MaxActivationMsPerFrame; }

	/** How many rows the board prints. */
	void SetBoardRows(int32 InRows);

	/** What the board is sorted by. */
	void SetSortBy(EStreamGuardSort InSortBy);

	//~ Requests -----------------------------------------------------------------------------------------

	/**
	 * Ask for a level, through the budget.
	 *
	 * This is not a second loader. It sets the same flags on the same ULevelStreaming that
	 * UGameplayStatics::LoadStreamLevel sets; the only difference is that StreamGuard knows the request
	 * exists, can attach a priority to it, and - when the throttle is on - can hold it for a frame. With the
	 * throttle off this is exactly LoadStreamLevel with a name attached, which is the point: the same demo
	 * runs both ways and only one of them hitches.
	 *
	 * @param PackageName   The level's package name, or its short name.
	 * @param bMakeVisible  Also add it to the world once it is loaded.
	 * @param Priority      Higher goes first, ahead of any distance consideration.
	 * @return false if this world has no streaming level with that name, which is logged.
	 */
	bool RequestLoad(FName PackageName, bool bMakeVisible, int32 Priority);

	/** Ask for a level to go away. Unloading is not budgeted - removal is cheap and waiting for it is not. */
	bool RequestUnload(FName PackageName, bool bAlsoUnloadPackage);

	//~ Reading ------------------------------------------------------------------------------------------

	/** The sources, already sorted and folded to the board's row count. */
	const TArray<FStreamGuardSourceStat>& GetSourceStats() const { return SourceStats; }

	/** The frame timeline, oldest first. */
	const TArray<FStreamGuardFrameSample>& GetFrameSamples() const { return FrameSamples; }

	/** The one-line summary. */
	const FStreamGuardTotals& GetTotals() const { return Totals; }

	/** The budget currently in force. */
	const FStreamGuardBudget& GetBudget() const { return Budget; }

	/** The worst frame in the window. False on an empty window. */
	bool GetWorstFrame(FStreamGuardFrameSample& OutSample) const;

	//~ Drawing ------------------------------------------------------------------------------------------

	/** Draw the board. Refuses to draw twice in one frame, so both draw routes are safe together. */
	void DrawBoard(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** Whether the board has already been drawn this frame. */
	bool HasDrawnBoardThisFrame() const;

	/** How many lines the board printed last time it drew. Useful for laying something out under it. */
	int32 GetBoardLineCount() const { return LastBoardLineCount; }

	//~ Internal, called by the per-level watchers -------------------------------------------------------

	/** One of the engine's streaming delegates fired for a tracked source. */
	void NoteLevelEvent(FName SourceName, ULevelStreaming* Level, EStreamGuardLevelEvent Event);

private:
	/** Everything StreamGuard remembers about one streaming source. */
	struct FTrackedSource
	{
		TWeakObjectPtr<ULevelStreaming> Level;

		FStreamGuardSourceStat Stat;

		EStreamGuardSourceState LastState = EStreamGuardSourceState::Removed;

		double LoadStartSeconds = 0.0;
		double ActivateStartSeconds = 0.0;

		/** What the game wants, remembered across a hold - the engine's own flag is cleared while held. */
		bool bWantsLoaded = false;
		bool bWantsVisible = false;

		bool bHeldLoad = false;
		bool bHeldVisible = false;

		double RequestSeconds = 0.0;
		int32 Priority = 0;

		/** Measured cost of the last activation, used as the estimate for the next one. */
		float LastActivateMs = 0.0f;

		bool bSeenThisScan = false;
	};

	void ApplySettings();
	void ScanStreamingLevels(double Now, int64 Frame);
	void ApplyThrottle(double Now);
	void ReleaseAllHolds();
	void RebuildSnapshot(double Now);
	void UpdatePlayerMotion(float DeltaTime);
	void RefreshHudDelegate();
	void OnAnyHUDPostRender(class AHUD* HUD, UCanvas* Canvas);

	/** Real time in seconds. Real rather than game time, so a paused or dilated game still measures frames. */
	double GetStreamGuardTime() const;

	ULevelStreaming* FindStreamingLevel(FName PackageName) const;

	/** The short name a source is listed under. Stable across load and unload, unlike the loaded level. */
	static FName MakeSourceName(const ULevelStreaming* Level);

	static EStreamGuardSourceState MapState(const ULevelStreaming* Level);
	static EStreamGuardSourceKind MapKind(const ULevelStreaming* Level);

	//~ State

	UPROPERTY()
	TMap<FName, TObjectPtr<UStreamGuardLevelWatcher>> Watchers;

	TMap<FName, FTrackedSource> Tracked;

	FStreamGuardFrameRing FrameRing;

	/** Rebuilt at SnapshotsPerSecond, read by the board, Blueprint and the CSV. */
	TArray<FStreamGuardSourceStat> SourceStats;
	TArray<FStreamGuardFrameSample> FrameSamples;
	FStreamGuardTotals Totals;

	FStreamGuardBudget Budget;

	/** Reused every frame so the planner's input costs no allocation once the game is warm. */
	TArray<FStreamGuardPending> PendingScratch;

	FVector PlayerLocation = FVector::ZeroVector;
	FVector PlayerVelocity = FVector::ZeroVector;

	/**
	 * How many sources the world had that did not fit under the ceiling.
	 *
	 * Recounted on every scan rather than accumulated, because a World Partition world's cell objects come
	 * and go and a running total would drift upwards forever. Their identity is folded away; their number
	 * never is - it is printed on the board next to the ceiling that caused it.
	 */
	int32 OverflowSources = 0;

	double TimeUntilSnapshot = 0.0;
	double SnapshotInterval = 0.1;

	bool bShowBoard = false;
	bool bFrozen = false;
	bool bThrottleEnabled = false;
	bool bThrottleEngineRequests = true;
	bool bAutoDrawBoardOnAnyHUD = true;
	bool bCountActorsOnShow = true;
	bool bShowTimeline = true;
	bool bExportFrameRows = true;

	int32 MaxTrackedSources = 256;
	int32 BoardRows = 14;
	int32 HitchDetailRows = 3;

	float BoardWidth = 720.0f;
	float TimelineHeight = 54.0f;
	FVector2D BoardOrigin = FVector2D(28.0f, 60.0f);

	EStreamGuardSort SortBy = EStreamGuardSort::CompletedFrame;

	FString CsvSubdirectory;

	FDelegateHandle HudPostRenderHandle;

	/** Which frame the board was last drawn in, so two routes cannot double-draw. Mutable: drawing is const. */
	mutable uint64 LastBoardDrawFrame = 0;
	mutable int32 LastBoardLineCount = 0;
};
