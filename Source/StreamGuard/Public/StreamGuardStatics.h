// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StreamGuardTypes.h"
#include "StreamGuardStatics.generated.h"

class UStreamGuardSubsystem;

/**
 * Everything StreamGuard can do, reachable from a Blueprint node with nothing placed in the level.
 *
 * Two halves, and the split is not cosmetic.
 *
 * The first half forwards to the world's subsystem: show, freeze, export, throttle, ask for a level, read the
 * board. A debug menu, a demo button strip or a bug-report button calls these and nothing else.
 *
 * The second half is the arithmetic - the sort, the fold, the frame-to-source attribution, the CSV and the
 * formatters - and it is **static, pure and world-free**. That is the lesson this line of plugins has paid
 * for once already: a UTickableWorldSubsystem cannot be constructed in an automation test because it needs a
 * world, so any logic that lives inside one is logic that is never tested. Here the maths takes an array and
 * an integer, and the tests exercise the same code the board runs rather than a second copy of it that agrees
 * with the first right up until somebody edits one of the two.
 */
UCLASS(meta = (DisplayName = "Stream Guard"))
class STREAMGUARD_API UStreamGuardStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The subsystem -----------------------------------------------------------------------------------

	/** The StreamGuard subsystem for the context object's world, or null outside a game world. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static UStreamGuardSubsystem* GetStreamGuard(const UObject* WorldContextObject);

	/** Show or hide the board. Same thing StreamGuard.Show does. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void SetShowBoard(const UObject* WorldContextObject, bool bShow);

	/** Whether the board is being drawn. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool IsShowingBoard(const UObject* WorldContextObject);

	/** Toggle the board. What a single button on a debug HUD wants. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool ToggleBoard(const UObject* WorldContextObject);

	/**
	 * Stop measuring and hold the board where it is.
	 *
	 * The reason this exists rather than "pause the game" is that pausing stops the streaming too, and what
	 * you wanted was to read the numbers from the moment of the hitch - which are out of the window half a
	 * second later. Freezing keeps them and lets the game carry on.
	 */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void FreezeStreamGuard(const UObject* WorldContextObject);

	/** Start measuring again. The window keeps what is in it; it does not restart empty. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void ResumeStreamGuard(const UObject* WorldContextObject);

	/** Toggle between frozen and running. Returns the new state. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool ToggleFreezeStreamGuard(const UObject* WorldContextObject);

	/** Whether measuring is frozen. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool IsStreamGuardFrozen(const UObject* WorldContextObject);

	/** Throw away every measurement and start from an empty window. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void ResetStreamGuard(const UObject* WorldContextObject);

	/**
	 * Write the board to a CSV and say where it went.
	 *
	 * Empty path - a timestamped file under Saved. Relative path - relative to the same place. Absolute path
	 * - exactly there.
	 */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool ExportStreamGuardCsv(const UObject* WorldContextObject, const FString& Path, FString& OutPath);

	//~ The budget --------------------------------------------------------------------------------------

	/** Turn the preload budget on or off. The before-and-after switch. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static void SetThrottleEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Whether the preload budget is on. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static bool IsThrottleEnabled(const UObject* WorldContextObject);

	/** Toggle the preload budget. Returns the new state. One button, one call. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static bool ToggleThrottle(const UObject* WorldContextObject);

	/** How many loads may be in flight at once. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static void SetMaxConcurrentLoads(const UObject* WorldContextObject, int32 MaxLoads);

	/** How many loads may be in flight at once. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static int32 GetMaxConcurrentLoads(const UObject* WorldContextObject);

	/** How many milliseconds of activation work may start in one frame. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static void SetMaxActivationMsPerFrame(const UObject* WorldContextObject, float MaxMs);

	/** How many milliseconds of activation work may start in one frame. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Throttle", meta = (WorldContext = "WorldContextObject"))
	static float GetMaxActivationMsPerFrame(const UObject* WorldContextObject);

	//~ Requests ----------------------------------------------------------------------------------------

	/**
	 * Ask for a streaming level, through the budget.
	 *
	 * Not a second loader: it sets the same flags on the same ULevelStreaming that LoadStreamLevel sets. The
	 * difference is that StreamGuard knows the request exists, can attach a priority to it, and - with the
	 * throttle on - can hold it for a frame instead of letting four of them land together.
	 */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool RequestLevelLoad(const UObject* WorldContextObject, FName PackageName, bool bMakeVisible = true,
		int32 Priority = 0);

	/** Ask for a streaming level to go away. Unloading is not budgeted - removal is cheap. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool RequestLevelUnload(const UObject* WorldContextObject, FName PackageName,
		bool bAlsoUnloadPackage = true);

	//~ Reading -----------------------------------------------------------------------------------------

	/** The board's rows, already sorted and folded. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void GetSourceStats(const UObject* WorldContextObject, TArray<FStreamGuardSourceStat>& OutStats);

	/** The frame timeline, oldest first. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static void GetFrameSamples(const UObject* WorldContextObject, TArray<FStreamGuardFrameSample>& OutSamples);

	/** The one-line summary. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static FStreamGuardTotals GetTotals(const UObject* WorldContextObject);

	/** The worst frame in the window. False on an empty window. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard", meta = (WorldContext = "WorldContextObject"))
	static bool GetWorstFrame(const UObject* WorldContextObject, FStreamGuardFrameSample& OutSample);

	//~ Pure arithmetic ---------------------------------------------------------------------------------

	/** Sort a set of source rows. Safe to pass the same array as input and output. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Pure")
	static void SortSources(const TArray<FStreamGuardSourceStat>& In, EStreamGuardSort SortBy,
		TArray<FStreamGuardSourceStat>& Out);

	/**
	 * Keep the first MaxRows rows and fold the rest into exactly one aggregate row.
	 *
	 * One row, whatever the excess - that is the promise, and it is the promise that stops the board from
	 * growing with the map. The fold keeps the count, and an aggregate row that is folded again keeps the
	 * count of what it already stood for, so nesting cannot quietly collapse four hundred sources into
	 * "and 1 more". A MaxRows of zero or less means no folding at all.
	 *
	 * Safe to pass the same array as input and output.
	 */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Pure")
	static void FoldSources(const TArray<FStreamGuardSourceStat>& In, int32 MaxRows,
		TArray<FStreamGuardSourceStat>& Out);

	/**
	 * Which sources finished in a given frame.
	 *
	 * The attribution the whole plugin sells, as a function you can call on your own data: point at a frame
	 * number from the timeline and get back the names of what landed in it.
	 */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Pure")
	static void FindSourcesCompletedInFrame(const TArray<FStreamGuardSourceStat>& Sources, int64 FrameNumber,
		TArray<FName>& OutNames);

	/** The worst frames over the threshold, worst first, at most MaxCount of them. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Pure")
	static void FindHitches(const TArray<FStreamGuardFrameSample>& Samples, int32 MaxCount,
		TArray<FStreamGuardFrameSample>& OutHitches);

	/** "L_Block_A, L_Block_B and 3 more", or a plain sentence when nothing landed in the frame. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString DescribeCompletions(const FStreamGuardFrameSample& Sample);

	/** The whole board as a CSV: a summary row, one row per source and optionally one row per frame. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString BuildCsv(const TArray<FStreamGuardSourceStat>& Sources,
		const TArray<FStreamGuardFrameSample>& Samples, const FStreamGuardTotals& Totals, bool bIncludeFrames);

	/** The CSV's first line. Every row has this many columns, so one parse reads the whole file. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString GetCsvHeader();

	/** Split one CSV line back into fields, undoing the quoting. The other half of an export worth having. */
	UFUNCTION(BlueprintCallable, Category = "StreamGuard|Pure")
	static void SplitCsvLine(const FString& Line, TArray<FString>& OutFields);

	/** "182.4" or "-" for a duration that has not happened. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString FormatMs(float Milliseconds);

	/** Centimetres in, something a person reads out. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString FormatDistance(float Centimetres);

	/** The short name of a state, for the board and the CSV. */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Pure")
	static FString GetStateName(EStreamGuardSourceState State);
};
