// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StreamGuardTypes.h"
#include "StreamGuardSettings.generated.h"

/**
 * Project-wide settings for StreamGuard, under Project Settings -> Plugins -> StreamGuard.
 *
 * Three groups, and the middle one is the important one. Measurement is what the board shows. Budget is what
 * StreamGuard is allowed to do about it, and it is off by default, because a plugin that silently starts
 * pacing your level streaming the moment it is installed would be indistinguishable from a bug. Presentation
 * is where the board sits.
 *
 * Every ceiling in here has its number and its reason written down. A streaming diagnostic that walks every
 * level in a World Partition map and keeps a history per cell will cost more than the hitch it was bought to
 * explain, and then you have two problems and no way to tell them apart.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "StreamGuard"))
class STREAMGUARD_API UStreamGuardSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UStreamGuardSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UStreamGuardSettings& Get();

	//~ Measurement ---------------------------------------------------------------------------------------

	/**
	 * A frame at or above this many milliseconds is a hitch, and is marked red on the timeline.
	 *
	 * 33 ms, which is the frame you would have had at 30 fps. It is deliberately not 16.7: at 60 fps a single
	 * 20 ms frame is a stutter nobody reports, and a timeline where two thirds of the bars are red tells you
	 * nothing. This is the one number worth changing per project - a 120 Hz game should set it far lower.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "1.0", ClampMax = "500.0", ForceUnits = "ms"))
	float HitchThresholdMs = 33.0f;

	/**
	 * How many seconds of frame times the timeline holds.
	 *
	 * Ten, because that is roughly how long it takes to feel a hitch, say "what was that", and look at the
	 * screen. Shorter and the evidence is gone before you have turned your head; longer and one bar on the
	 * timeline is too thin to point at.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "1.0", ClampMax = "60.0", ForceUnits = "s"))
	float WindowSeconds = 10.0f;

	/**
	 * The frame rate the timeline's memory is sized for.
	 *
	 * Not a target and not a measurement - just how many samples to allocate for a window of this many
	 * seconds. Set it above the highest frame rate you expect; if the game runs faster than this the window
	 * simply holds fewer seconds, and nothing else changes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "10.0", ClampMax = "1000.0", ForceUnits = "Hz"))
	float AssumedFrameRate = 120.0f;

	/**
	 * The most streaming sources StreamGuard follows in detail. Everything past it folds into one row.
	 *
	 * 256. This is the number that makes StreamGuard something you can leave switched on in a World Partition
	 * map, where the number of runtime cells is a property of the world's size rather than of anything a
	 * programmer chose. Per source StreamGuard keeps one small record and one delegate binding, so the cost of
	 * the plugin is bounded by this line whatever the map does.
	 *
	 * The folded row keeps the count and the total, so the ceiling can hide the identity of a cost but never
	 * the cost itself.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement", meta = (ClampMin = "16", ClampMax = "4096"))
	int32 MaxTrackedSources = 256;

	/**
	 * Count the actors a level brought with it when it becomes visible.
	 *
	 * On. It is one walk over an array that the engine has just finished building, once per level, and it is
	 * usually the fastest route to the answer - a cell with four thousand actors in it does not need any
	 * further explanation. Turn it off in a map with pathological level sizes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement")
	bool bCountActorsOnShow = true;

	/** How often the board's arrays are rebuilt, in hertz. Not the measuring rate - that is every frame. */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "1.0", ClampMax = "60.0", ForceUnits = "Hz"))
	float SnapshotsPerSecond = 10.0f;

	//~ Budget --------------------------------------------------------------------------------------------

	/**
	 * Pace streaming work instead of letting it all land in one frame.
	 *
	 * Off by default, and that is not timidity. StreamGuard's first job is to show you what is happening;
	 * changing what happens is a second, separate decision that a project should make on purpose. Turn it on
	 * here, or with StreamGuard.Throttle 1 in the game, which is the switch to use when you want to see the
	 * same drive with and without it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget")
	bool bThrottleEnabled = false;

	/**
	 * How many loads may be in flight at once.
	 *
	 * Two. The point is not that two is optimal - it is that the number is small and fixed. Level loading
	 * competes with texture streaming, audio and everything else the async loader is doing, and eight
	 * simultaneous level loads do not finish sooner than eight sequential ones; they finish at the same time
	 * as each other, which is exactly the case where they all activate in the same frame.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxConcurrentLoads = 2;

	/**
	 * How many milliseconds of activation work may start in one frame.
	 *
	 * Three, which is a fifth of a 60 fps frame. Activation - AddToWorld - happens on the game thread and is
	 * the part you actually feel. This is a budget on what StreamGuard *starts*, not a limit the engine
	 * enforces mid-level: once a level has begun being added, the engine finishes it in its own time.
	 * StreamGuard's lever is when the next one begins.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget",
		meta = (ClampMin = "0.1", ClampMax = "100.0", ForceUnits = "ms"))
	float MaxActivationMsPerFrame = 3.0f;

	/**
	 * What an activation is assumed to cost before it has ever been measured.
	 *
	 * Only used for a source StreamGuard has not seen activate yet; after the first time, the measured cost
	 * of the last activation is used instead, so this number stops mattering within seconds of the game
	 * starting. It exists so that the very first frame of a level's life is planned with something rather
	 * than with zero, which would let an unbounded number of first-time activations through at once.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget",
		meta = (ClampMin = "0.0", ClampMax = "500.0", ForceUnits = "ms"))
	float AssumedActivationMs = 4.0f;

	/**
	 * How much the player's heading counts against plain distance, from 0 to 1.
	 *
	 * 0.5: a source dead ahead is treated as half as far as it is, one to the side at its real distance, one
	 * directly behind as half again as far. Raise it for a game on rails, lower it for one where the player
	 * turns on the spot.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirectionWeight = 0.5f;

	/**
	 * How long the budget may hold one request before it goes through regardless.
	 *
	 * Five seconds. This is the guarantee that the budget can only ever delay work and never cancel it, and
	 * it is what lets the documentation promise that turning the throttle on cannot make content fail to
	 * appear - only appear later.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget",
		meta = (ClampMin = "0.0", ClampMax = "60.0", ForceUnits = "s"))
	float MaxHoldSeconds = 5.0f;

	/**
	 * Also pace streaming the engine started by itself, not only what was asked for through StreamGuard.
	 *
	 * On, and it is what makes the throttle useful in a map that streams from volumes or by distance: those
	 * requests never pass through any code of yours, so a plugin that only paced its own queue would pace
	 * nothing in a real level. StreamGuard holds one back by clearing the engine's own should-be-loaded or
	 * should-be-visible flag for a frame and putting it back when the budget allows.
	 *
	 * The honest caveat, and it is in the documentation too: World Partition re-asserts its own decisions on
	 * every streaming update, so a cell held this way is released again by World Partition rather than by
	 * StreamGuard. The measurement half of this plugin is fully accurate on World Partition; the throttle
	 * half is at its best on classic sublevel streaming and on requests routed through RequestLoad.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (EditCondition = "bThrottleEnabled"))
	bool bThrottleEngineRequests = true;

	//~ Presentation --------------------------------------------------------------------------------------

	/**
	 * Show the board as soon as a game world starts.
	 *
	 * Off. StreamGuard is a tool you reach for, not a HUD element, and a board nobody asked for over the top
	 * of a playtest is a board that gets the plugin uninstalled. StreamGuard.Show 1 turns it on at any moment.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowBoardByDefault = false;

	/**
	 * Draw the board through AHUD::OnHUDPostRender, so a project keeps its own HUD class.
	 *
	 * On, because the alternative is asking a shipping project to reparent its HUD to a diagnostic tool's
	 * class in order to look at a number, and nobody is going to do that. AStreamGuardHUD exists for projects
	 * that would rather be explicit; the two never double-draw, because both go through the same function and
	 * it refuses to draw twice in one frame.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawBoardOnAnyHUD = true;

	/** Where the board sits, in pixels from the top left of the viewport. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	FVector2D BoardOrigin = FVector2D(28.0f, 60.0f);

	/** How wide the board is drawn, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation",
		meta = (ClampMin = "320.0", ClampMax = "1920.0"))
	float BoardWidth = 720.0f;

	/**
	 * How many source rows the board prints.
	 *
	 * Fourteen fits above the middle of a 1080p viewport and is far more than the number of levels that are
	 * ever actually the problem. Everything past it folds into one row that keeps the count.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "1", ClampMax = "60"))
	int32 BoardRows = 14;

	/** What the board is sorted by when it opens. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	EStreamGuardSort SortBy = EStreamGuardSort::CompletedFrame;

	/** Draw the frame-time timeline under the table. Off leaves the table and drops the strip. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowTimeline = true;

	/** How tall the timeline strip is drawn, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation",
		meta = (ClampMin = "16.0", ClampMax = "200.0"))
	float TimelineHeight = 54.0f;

	/**
	 * How many hitches are written out under the timeline with what landed in them.
	 *
	 * Three. This is the payload of the whole plugin - the line that says "frame 184312, 47.1 ms, and here is
	 * what finished in it" - and three of them is enough to see whether it is the same cell every time
	 * without turning the board into a log.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0", ClampMax = "10"))
	int32 HitchDetailRows = 3;

	//~ Export --------------------------------------------------------------------------------------------

	/**
	 * Folder the CSV is written to, relative to the project's Saved directory.
	 *
	 * Relative on purpose. An absolute path baked into a project setting is a path that exists on exactly one
	 * machine, and the first thing that happens to it is that somebody commits it. StreamGuard.Export with no
	 * argument writes here under a timestamped name; with a path it writes exactly where you say.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	FString CsvSubdirectory = TEXT("StreamGuard");

	/** Include one row per frame of the timeline, not just one per source. Bigger file, real analysis. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bExportFrameRows = true;
};
