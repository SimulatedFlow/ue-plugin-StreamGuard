// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Level.h"
#include "Engine/LevelBounds.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SceneTypes.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

#include "StreamGuardLevelWatcher.h"
#include "StreamGuardLog.h"
#include "StreamGuardSettings.h"
#include "StreamGuardStatics.h"
#include "StreamGuardThrottle.h"

namespace StreamGuardPrivate
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.70f);
	static const FLinearColor HeadingColor(0.55f, 0.85f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.88f, 0.88f, 0.88f, 1.0f);
	static const FLinearColor DimColor(0.55f, 0.55f, 0.55f, 1.0f);
	static const FLinearColor GoodColor(0.45f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(1.0f, 0.80f, 0.35f, 1.0f);
	static const FLinearColor HotColor(1.0f, 0.42f, 0.38f, 1.0f);
	static const FLinearColor HeldColor(0.62f, 0.72f, 1.0f, 1.0f);
	static const FLinearColor TimelineBackground(0.06f, 0.06f, 0.08f, 0.85f);
	static const FLinearColor TimelineBar(0.40f, 0.70f, 0.95f, 0.85f);
	static const FLinearColor TimelineHitch(1.0f, 0.25f, 0.22f, 1.0f);
	static const FLinearColor TimelineThreshold(1.0f, 0.80f, 0.35f, 0.45f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size,
		const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	/** Pad or clip a name so the columns line up in a font that is nearly, but not quite, monospaced. */
	static FString Fit(const FString& Text, int32 Width)
	{
		if (Text.Len() >= Width)
		{
			return Text.Left(Width);
		}
		return Text + FString::ChrN(Width - Text.Len(), TEXT(' '));
	}

	static UStreamGuardSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UStreamGuardSubsystem>() : nullptr;
	}

	/** The colour a state deserves. Red is not decoration: it is work happening on the game thread. */
	static const FLinearColor& StateColor(EStreamGuardSourceState State)
	{
		switch (State)
		{
		case EStreamGuardSourceState::MakingVisible:
		case EStreamGuardSourceState::MakingInvisible:
			return HotColor;
		case EStreamGuardSourceState::Loading:
			return WarnColor;
		case EStreamGuardSourceState::Visible:
			return GoodColor;
		case EStreamGuardSourceState::FailedToLoad:
			return HotColor;
		default:
			return DimColor;
		}
	}
}

//~ Lifetime -------------------------------------------------------------------------------------------------

UStreamGuardSubsystem::UStreamGuardSubsystem()
{
}

void UStreamGuardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	const UStreamGuardSettings& Settings = UStreamGuardSettings::Get();

	FrameRing.Configure(Settings.WindowSeconds, Settings.AssumedFrameRate, Settings.HitchThresholdMs);

	bShowBoard = Settings.bShowBoardByDefault;
	TimeUntilSnapshot = 0.0;

	RefreshHudDelegate();
}

void UStreamGuardSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	// Anything StreamGuard is holding back is released before it goes away. A plugin that could leave a level
	// permanently unloaded because it happened to be torn down mid-hold would be a plugin nobody ships.
	ReleaseAllHolds();

	for (TPair<FName, TObjectPtr<UStreamGuardLevelWatcher>>& Pair : Watchers)
	{
		if (Pair.Value)
		{
			Pair.Value->Unbind();
		}
	}

	Watchers.Empty();
	Tracked.Empty();
	SourceStats.Empty();
	FrameSamples.Empty();
	PendingScratch.Empty();

	Super::Deinitialize();
}

void UStreamGuardSubsystem::ApplySettings()
{
	const UStreamGuardSettings& Settings = UStreamGuardSettings::Get();

	MaxTrackedSources = Settings.MaxTrackedSources;
	bCountActorsOnShow = Settings.bCountActorsOnShow;
	SnapshotInterval = 1.0 / FMath::Max(1.0f, Settings.SnapshotsPerSecond);

	bThrottleEnabled = Settings.bThrottleEnabled;
	bThrottleEngineRequests = Settings.bThrottleEngineRequests;

	Budget.MaxConcurrentLoads = Settings.MaxConcurrentLoads;
	Budget.MaxActivationMsPerFrame = Settings.MaxActivationMsPerFrame;
	Budget.DirectionWeight = Settings.DirectionWeight;
	Budget.MaxHoldSeconds = Settings.MaxHoldSeconds;

	bAutoDrawBoardOnAnyHUD = Settings.bAutoDrawBoardOnAnyHUD;
	BoardOrigin = Settings.BoardOrigin;
	BoardWidth = Settings.BoardWidth;
	BoardRows = Settings.BoardRows;
	SortBy = Settings.SortBy;
	bShowTimeline = Settings.bShowTimeline;
	TimelineHeight = Settings.TimelineHeight;
	HitchDetailRows = Settings.HitchDetailRows;

	CsvSubdirectory = Settings.CsvSubdirectory;
	bExportFrameRows = Settings.bExportFrameRows;

	Totals.HitchThresholdMs = Settings.HitchThresholdMs;
	Totals.WindowSeconds = Settings.WindowSeconds;
}

TStatId UStreamGuardSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStreamGuardSubsystem, STATGROUP_Tickables);
}

double UStreamGuardSubsystem::GetStreamGuardTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() : 0.0;
}

void UStreamGuardSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double Now = GetStreamGuardTime();
	const int64 Frame = static_cast<int64>(GFrameCounter);

	UpdatePlayerMotion(DeltaTime);

	// The frame sample is written before the scan, so that any completion the scan or a delegate reports
	// lands on the frame that is currently being measured rather than on the previous one.
	if (!bFrozen)
	{
		FrameRing.AddFrame(Frame, DeltaTime * 1000.0f, Now);
	}

	ScanStreamingLevels(Now, Frame);

	if (bThrottleEnabled)
	{
		ApplyThrottle(Now);
	}

	// The scan runs every frame and the snapshot rebuild does not, and that split is the whole performance
	// design. Scanning is a walk over the streaming array reading one enum each; it has to be every frame
	// because a load can start and finish inside one. Rebuilding the public arrays - sorting, folding,
	// formatting - is the expensive half, and nothing reads it faster than a human can look at it.
	TimeUntilSnapshot -= DeltaTime;
	if (TimeUntilSnapshot <= 0.0)
	{
		TimeUntilSnapshot = SnapshotInterval;
		RebuildSnapshot(Now);
	}
}

//~ The player -----------------------------------------------------------------------------------------------

void UStreamGuardSubsystem::UpdatePlayerMotion(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* Controller = World->GetFirstPlayerController();
	const AActor* ViewTarget = Controller ? Controller->GetViewTarget() : nullptr;

	if (!ViewTarget)
	{
		return;
	}

	const FVector NewLocation = ViewTarget->GetActorLocation();

	// The actor's own velocity where it has one - a Character or a movement component keeps it honestly. A
	// finite difference otherwise, because a pawn moved by SetActorLocation reports a zero velocity forever
	// and the direction term would silently switch itself off on exactly the kind of scripted camera drive
	// that a streaming demo is made of.
	FVector NewVelocity = ViewTarget->GetVelocity();

	if (NewVelocity.IsNearlyZero() && DeltaTime > UE_SMALL_NUMBER && !PlayerLocation.IsZero())
	{
		NewVelocity = (NewLocation - PlayerLocation) / DeltaTime;
	}

	// Smoothed, because the planner's ordering should not flip because the player clipped a kerb.
	const float Alpha = FMath::Clamp(DeltaTime * 4.0f, 0.0f, 1.0f);
	PlayerVelocity = FMath::Lerp(PlayerVelocity, NewVelocity, Alpha);
	PlayerLocation = NewLocation;
}

//~ Scanning -------------------------------------------------------------------------------------------------

FName UStreamGuardSubsystem::MakeSourceName(const ULevelStreaming* Level)
{
	if (!Level)
	{
		return NAME_None;
	}

	const FName PackageName = Level->GetWorldAssetPackageFName();
	if (PackageName.IsNone())
	{
		return Level->GetFName();
	}

	// In PIE every streamed package is renamed to UEDPIE_<n>_<Name>. Left in, the board prints a name nobody
	// typed and nobody can look up, and it changes between runs - so the name a source is known by is the one
	// on the asset, in the editor and in a packaged build alike.
	return FName(*FPackageName::GetShortName(UWorld::RemovePIEPrefix(PackageName.ToString())));
}

EStreamGuardSourceState UStreamGuardSubsystem::MapState(const ULevelStreaming* Level)
{
	if (!Level)
	{
		return EStreamGuardSourceState::Removed;
	}

	// One switch, in one place, so there is exactly one line to fix if the engine ever adds a state. The
	// engine's ELevelStreamingState is a plain enum class and cannot be a UPROPERTY, which is the only reason
	// StreamGuard has an enum of its own at all.
	switch (Level->GetLevelStreamingState())
	{
	case ELevelStreamingState::Removed:          return EStreamGuardSourceState::Removed;
	case ELevelStreamingState::Unloaded:         return EStreamGuardSourceState::Unloaded;
	case ELevelStreamingState::FailedToLoad:     return EStreamGuardSourceState::FailedToLoad;
	case ELevelStreamingState::Loading:          return EStreamGuardSourceState::Loading;
	case ELevelStreamingState::LoadedNotVisible: return EStreamGuardSourceState::LoadedNotVisible;
	case ELevelStreamingState::MakingVisible:    return EStreamGuardSourceState::MakingVisible;
	case ELevelStreamingState::LoadedVisible:    return EStreamGuardSourceState::Visible;
	case ELevelStreamingState::MakingInvisible:  return EStreamGuardSourceState::MakingInvisible;
	default:                                     return EStreamGuardSourceState::Unloaded;
	}
}

EStreamGuardSourceKind UStreamGuardSubsystem::MapKind(const ULevelStreaming* Level)
{
	// A World Partition runtime cell answers this without any World Partition header being involved:
	// ULevelStreaming::GetWorldPartitionCell() is virtual, returns null on the base class, and returns the
	// cell on the one class that has one. That is the whole test, and it is why StreamGuard needs no second
	// code path for partitioned worlds.
	if (Level && Level->GetWorldPartitionCell() != nullptr)
	{
		return EStreamGuardSourceKind::PartitionCell;
	}

	return EStreamGuardSourceKind::Sublevel;
}

ULevelStreaming* UStreamGuardSubsystem::FindStreamingLevel(FName PackageName) const
{
	UWorld* World = GetWorld();
	if (!World || PackageName.IsNone())
	{
		return nullptr;
	}

	// The PIE prefix is stripped from both sides. Without this the one call this plugin asks a game to make -
	// "load the level I named" - is the one call that cannot work in the editor, because PIE renamed the
	// package after the name was written down and nothing tells you so.
	const FString Wanted = UWorld::RemovePIEPrefix(PackageName.ToString());
	const FString WantedShort = FPackageName::GetShortName(Wanted);

	for (ULevelStreaming* Level : World->GetStreamingLevels())
	{
		if (!Level)
		{
			continue;
		}

		const FString Package = UWorld::RemovePIEPrefix(Level->GetWorldAssetPackageFName().ToString());

		// Long name or short name, because half the world calls a level by its path and the other half by the
		// name on the tab, and both of them are going to type it into a console command.
		if (Package == Wanted || FPackageName::GetShortName(Package) == WantedShort)
		{
			return Level;
		}
	}

	return nullptr;
}

void UStreamGuardSubsystem::ScanStreamingLevels(double Now, int64 Frame)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TPair<FName, FTrackedSource>& Pair : Tracked)
	{
		Pair.Value.bSeenThisScan = false;
	}

	OverflowSources = 0;

	int32 LoadsInFlight = 0;
	int32 ActivationsInFlight = 0;
	int32 VisibleSources = 0;
	int32 SeenSources = 0;

	for (ULevelStreaming* Level : World->GetStreamingLevels())
	{
		if (!Level)
		{
			continue;
		}

		++SeenSources;

		const EStreamGuardSourceState State = MapState(Level);

		switch (State)
		{
		case EStreamGuardSourceState::Loading:       ++LoadsInFlight; break;
		case EStreamGuardSourceState::MakingVisible: ++ActivationsInFlight; break;
		case EStreamGuardSourceState::Visible:       ++VisibleSources; break;
		default: break;
		}

		const FName SourceName = MakeSourceName(Level);
		FTrackedSource* Source = Tracked.Find(SourceName);

		if (!Source)
		{
			// The ceiling. Past it, a source is still counted in every total above - that costs one enum read
			// - but gets no record, no watcher and no history. This is the line that stops a diagnostic from
			// becoming the problem it was bought to find in a world with nine thousand cells.
			if (Tracked.Num() >= MaxTrackedSources)
			{
				++OverflowSources;
				continue;
			}

			Source = &Tracked.Add(SourceName);
			Source->Stat.SourceName = SourceName;
			Source->Stat.PackageName = Level->GetWorldAssetPackageFName();
			Source->Stat.Kind = MapKind(Level);
			Source->Level = Level;
			Source->LastState = EStreamGuardSourceState::Removed;

			UStreamGuardLevelWatcher* Watcher = NewObject<UStreamGuardLevelWatcher>(this);
			Watcher->Bind(Level, this, SourceName);
			Watchers.Add(SourceName, Watcher);
		}
		else if (Source->Level.Get() != Level)
		{
			// Same name, different object. World Partition destroys and recreates a cell's ULevelStreaming as
			// the player leaves and returns, and a watcher bound to the corpse would never fire again.
			Source->Level = Level;

			if (TObjectPtr<UStreamGuardLevelWatcher>* Existing = Watchers.Find(SourceName))
			{
				if (*Existing)
				{
					(*Existing)->Bind(Level, this, SourceName);
				}
			}
		}

		Source->bSeenThisScan = true;

		FStreamGuardSourceStat& Stat = Source->Stat;
		Stat.State = State;
		Stat.bBlocking = Level->bShouldBlockOnLoad != 0;

		// Location: the level transform until the level is in memory, at which point the real bounds are
		// known and are a far better answer for a level whose origin is in a corner. Computed once, on the
		// frame it becomes visible - see NoteLevelEvent.
		if (Stat.Location.IsZero())
		{
			Stat.Location = Level->LevelTransform.GetLocation();
		}

		Stat.DistanceToPlayer = static_cast<float>(FVector::Dist(Stat.Location, PlayerLocation));

		if (State != Source->LastState)
		{
			// Starts are seen by polling, ends by the engine's delegates. A start being a frame late costs
			// nothing - it moves a duration by one frame - whereas an end being a frame late would put the
			// wrong name next to the red mark, which is the one thing this plugin must not do.
			if (State == EStreamGuardSourceState::Loading)
			{
				Source->LoadStartSeconds = Now;
				Stat.StartedFrame = Frame;
			}
			else if (State == EStreamGuardSourceState::MakingVisible)
			{
				Source->ActivateStartSeconds = Now;
			}
			else if (State == EStreamGuardSourceState::Unloaded || State == EStreamGuardSourceState::Removed)
			{
				Source->LoadStartSeconds = 0.0;
				Source->ActivateStartSeconds = 0.0;
			}

			Source->LastState = State;
		}
	}

	// Sources the world no longer has. Their watchers are unbound and their records dropped, because in a
	// World Partition world this is the normal end of a cell's life and keeping them would make the map's
	// history grow without limit - the exact failure the ceiling exists to prevent.
	for (auto It = Tracked.CreateIterator(); It; ++It)
	{
		if (It->Value.bSeenThisScan)
		{
			continue;
		}

		if (TObjectPtr<UStreamGuardLevelWatcher>* Watcher = Watchers.Find(It->Key))
		{
			if (*Watcher)
			{
				(*Watcher)->Unbind();
			}
			Watchers.Remove(It->Key);
		}

		It.RemoveCurrent();
	}

	Totals.SeenSources = SeenSources;
	Totals.TrackedSources = Tracked.Num();
	Totals.LoadsInFlight = LoadsInFlight;
	Totals.ActivationsInFlight = ActivationsInFlight;
	Totals.VisibleSources = VisibleSources;

	Budget.LoadsInFlight = LoadsInFlight;
}

void UStreamGuardSubsystem::NoteLevelEvent(FName SourceName, ULevelStreaming* Level, EStreamGuardLevelEvent Event)
{
	if (bFrozen)
	{
		return;
	}

	FTrackedSource* Source = Tracked.Find(SourceName);
	if (!Source)
	{
		return;
	}

	const double Now = GetStreamGuardTime();
	const int64 Frame = static_cast<int64>(GFrameCounter);

	FStreamGuardSourceStat& Stat = Source->Stat;

	switch (Event)
	{
	case EStreamGuardLevelEvent::Loaded:
	{
		if (Source->LoadStartSeconds > 0.0)
		{
			Stat.LoadMs = static_cast<float>((Now - Source->LoadStartSeconds) * 1000.0);
			Source->LoadStartSeconds = 0.0;
		}

		Stat.CompletedFrame = Frame;
		Stat.CompletedTimeSeconds = Now;
		Stat.DistanceAtCompletion = Stat.DistanceToPlayer;
		Stat.TotalMs = Stat.LoadMs + Stat.ActivateMs;

		// A load landing is a real event in a real frame - the async loader finishes its work on the game
		// thread - so it goes on the timeline whether or not the level is also going to be made visible.
		FrameRing.NoteCompletion(SourceName);
		break;
	}

	case EStreamGuardLevelEvent::Shown:
	{
		if (Source->ActivateStartSeconds > 0.0)
		{
			Stat.ActivateMs = static_cast<float>((Now - Source->ActivateStartSeconds) * 1000.0);
			Source->ActivateStartSeconds = 0.0;
		}

		// The measured cost becomes the estimate for the next time this source is activated, so the budget
		// stops guessing about this level after it has seen it once.
		Source->LastActivateMs = Stat.ActivateMs;

		Stat.CompletedFrame = Frame;
		Stat.CompletedTimeSeconds = Now;
		Stat.DistanceAtCompletion = Stat.DistanceToPlayer;
		Stat.TotalMs = Stat.LoadMs + Stat.ActivateMs;

		if (Level)
		{
			if (const ULevel* Loaded = Level->GetLoadedLevel())
			{
				if (bCountActorsOnShow)
				{
					Stat.ActorCount = Loaded->Actors.Num();
				}

				// Now that the actors are in the world their bounds are known, and the centre of the level is
				// a much better "where is this" than a transform that is usually the world origin.
				const FBox Bounds = ALevelBounds::CalculateLevelBounds(Loaded);
				if (Bounds.IsValid)
				{
					Stat.Location = Bounds.GetCenter();
				}
			}
		}

		FrameRing.NoteCompletion(SourceName);
		break;
	}

	case EStreamGuardLevelEvent::Unloaded:
	case EStreamGuardLevelEvent::Hidden:
	{
		// Deliberately not put on the timeline. Removal costs a frame too, but it is the engine's own
		// RemoveFromWorld and there is nothing StreamGuard's budget can do about it, so listing it next to
		// every red mark would be noise on the one line that has to stay readable.
		Source->LoadStartSeconds = 0.0;
		Source->ActivateStartSeconds = 0.0;
		break;
	}
	}
}

//~ The budget -----------------------------------------------------------------------------------------------

void UStreamGuardSubsystem::ApplyThrottle(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	PendingScratch.Reset();

	const float AssumedActivationMs = UStreamGuardSettings::Get().AssumedActivationMs;

	for (TPair<FName, FTrackedSource>& Pair : Tracked)
	{
		FTrackedSource& Source = Pair.Value;

		ULevelStreaming* Level = Source.Level.Get();
		if (!Level || !Source.bSeenThisScan)
		{
			continue;
		}

		// What the game wants. While StreamGuard is holding a request the engine's own flag has been cleared,
		// so the flag is no longer the truth and the remembered intent is - which is exactly why the intent
		// is remembered rather than re-read.
		const bool bWantsLoaded = Source.bHeldLoad || Level->ShouldBeLoaded();
		const bool bWantsVisible = Source.bHeldVisible || Level->GetShouldBeVisibleFlag();

		Source.bWantsLoaded = bWantsLoaded;
		Source.bWantsVisible = bWantsVisible;

		if (!bThrottleEngineRequests && !Source.bHeldLoad && !Source.bHeldVisible && Source.Priority == 0
			&& Source.RequestSeconds <= 0.0)
		{
			// Only requests that came through RequestLoad are paced in this mode, and this one did not.
			continue;
		}

		const EStreamGuardSourceState State = Source.Stat.State;

		FStreamGuardPending Pending;
		Pending.SourceName = Source.Stat.SourceName;
		Pending.Location = Source.Stat.Location;
		Pending.Priority = Source.Priority;
		Pending.bBlocking = Source.Stat.bBlocking;
		Pending.RequestTimeSeconds = Source.RequestSeconds > 0.0 ? Source.RequestSeconds : Now;

		if (bWantsLoaded && (State == EStreamGuardSourceState::Unloaded
			|| State == EStreamGuardSourceState::Removed))
		{
			Pending.Kind = EStreamGuardPendingKind::Load;
			PendingScratch.Add(Pending);
		}
		else if (bWantsVisible && State == EStreamGuardSourceState::LoadedNotVisible)
		{
			Pending.Kind = EStreamGuardPendingKind::Activate;
			Pending.EstimatedMs = Source.LastActivateMs > 0.0f ? Source.LastActivateMs : AssumedActivationMs;
			PendingScratch.Add(Pending);
		}
		else if (Source.bHeldLoad || Source.bHeldVisible)
		{
			// Held, but no longer waiting for anything the hold covers - the engine got past it another way.
			// Put the flags back and forget about it rather than leaving a level held by a stale decision.
			if (Source.bHeldLoad)
			{
				Level->SetShouldBeLoaded(true);
				Source.bHeldLoad = false;
			}
			if (Source.bHeldVisible)
			{
				Level->SetShouldBeVisible(true);
				Source.bHeldVisible = false;
			}
		}
	}

	Budget.NowSeconds = Now;

	const FStreamGuardPlan Plan =
		UStreamGuardThrottle::PlanFrame(PendingScratch, Budget, PlayerLocation, PlayerVelocity);

	int32 HeldRequests = 0;

	for (const FStreamGuardPending& Pending : PendingScratch)
	{
		FTrackedSource* Source = Tracked.Find(Pending.SourceName);
		if (!Source)
		{
			continue;
		}

		ULevelStreaming* Level = Source->Level.Get();
		if (!Level)
		{
			continue;
		}

		const bool bStarting = Pending.Kind == EStreamGuardPendingKind::Load
			? Plan.StartLoads.Contains(Pending.SourceName)
			: Plan.StartActivations.Contains(Pending.SourceName);

		if (bStarting)
		{
			if (Source->bHeldLoad)
			{
				Level->SetShouldBeLoaded(true);
				Source->bHeldLoad = false;
			}
			if (Source->bHeldVisible && Pending.Kind == EStreamGuardPendingKind::Activate)
			{
				Level->SetShouldBeVisible(true);
				Source->bHeldVisible = false;
			}

			continue;
		}

		++HeldRequests;
		Source->Stat.bThrottled = true;
		++Source->Stat.HeldFrames;

		// A hold is one cleared engine flag, and nothing else. The flag goes back the moment the budget has
		// room, which is why the plugin can promise that the throttle delays content and never loses it.
		if (Pending.Kind == EStreamGuardPendingKind::Load && !Source->bHeldLoad)
		{
			Level->SetShouldBeLoaded(false);
			Source->bHeldLoad = true;
		}
		else if (Pending.Kind == EStreamGuardPendingKind::Activate && !Source->bHeldVisible)
		{
			Level->SetShouldBeVisible(false);
			Source->bHeldVisible = true;
		}
	}

	Totals.HeldRequests = HeldRequests;
}

void UStreamGuardSubsystem::ReleaseAllHolds()
{
	for (TPair<FName, FTrackedSource>& Pair : Tracked)
	{
		FTrackedSource& Source = Pair.Value;

		ULevelStreaming* Level = Source.Level.Get();
		if (!Level)
		{
			Source.bHeldLoad = false;
			Source.bHeldVisible = false;
			continue;
		}

		if (Source.bHeldLoad)
		{
			Level->SetShouldBeLoaded(true);
			Source.bHeldLoad = false;
		}

		if (Source.bHeldVisible)
		{
			Level->SetShouldBeVisible(true);
			Source.bHeldVisible = false;
		}
	}

	Totals.HeldRequests = 0;
}

//~ Control --------------------------------------------------------------------------------------------------

void UStreamGuardSubsystem::SetShowBoard(bool bShow)
{
	bShowBoard = bShow;
}

void UStreamGuardSubsystem::Freeze()
{
	bFrozen = true;
	Totals.bFrozen = true;
}

void UStreamGuardSubsystem::Resume()
{
	bFrozen = false;
	Totals.bFrozen = false;
}

void UStreamGuardSubsystem::ResetMeasurements()
{
	FrameRing.Reset();

	for (TPair<FName, FTrackedSource>& Pair : Tracked)
	{
		FTrackedSource& Source = Pair.Value;

		// The identity survives a reset and the history does not. Re-adding every source would drop the
		// watchers and lose the next completion, which is the one thing worth keeping across a reset.
		Source.Stat.LoadMs = 0.0f;
		Source.Stat.ActivateMs = 0.0f;
		Source.Stat.TotalMs = 0.0f;
		Source.Stat.CompletedFrame = 0;
		Source.Stat.StartedFrame = 0;
		Source.Stat.CompletedTimeSeconds = 0.0;
		Source.Stat.HeldFrames = 0;
		Source.Stat.bThrottled = false;
	}

	SourceStats.Reset();
	FrameSamples.Reset();

	Resume();
}

void UStreamGuardSubsystem::SetThrottleEnabled(bool bEnabled)
{
	if (bThrottleEnabled == bEnabled)
	{
		return;
	}

	bThrottleEnabled = bEnabled;

	if (!bThrottleEnabled)
	{
		ReleaseAllHolds();
	}

	Totals.bThrottleEnabled = bThrottleEnabled;
}

void UStreamGuardSubsystem::SetMaxConcurrentLoads(int32 InMax)
{
	Budget.MaxConcurrentLoads = FMath::Clamp(InMax, 1, 64);
}

void UStreamGuardSubsystem::SetMaxActivationMsPerFrame(float InMs)
{
	Budget.MaxActivationMsPerFrame = FMath::Clamp(InMs, 0.1f, 100.0f);
}

void UStreamGuardSubsystem::SetBoardRows(int32 InRows)
{
	BoardRows = FMath::Clamp(InRows, 1, 60);
}

void UStreamGuardSubsystem::SetSortBy(EStreamGuardSort InSortBy)
{
	SortBy = InSortBy;
}

//~ Requests -------------------------------------------------------------------------------------------------

bool UStreamGuardSubsystem::RequestLoad(FName PackageName, bool bMakeVisible, int32 Priority)
{
	ULevelStreaming* Level = FindStreamingLevel(PackageName);
	if (!Level)
	{
		UE_LOG(LogStreamGuard, Warning,
			TEXT("RequestLoad: this world has no streaming level called '%s'. Check the name against the "
				"Levels window - StreamGuard never adds a level to a world, it only asks for one the world "
				"already has."), *PackageName.ToString());
		return false;
	}

	const FName SourceName = MakeSourceName(Level);

	if (FTrackedSource* Source = Tracked.Find(SourceName))
	{
		Source->Priority = Priority;
		Source->RequestSeconds = GetStreamGuardTime();
		Source->bWantsLoaded = true;
		Source->bWantsVisible = bMakeVisible;
	}

	// The flags are set immediately and unconditionally. With the throttle off this is exactly what
	// LoadStreamLevel does; with it on, the next tick may clear them again for a frame. Setting them here
	// rather than queueing means a project that turns StreamGuard off entirely still behaves identically.
	Level->SetShouldBeLoaded(true);
	Level->SetShouldBeVisible(bMakeVisible);

	return true;
}

bool UStreamGuardSubsystem::RequestUnload(FName PackageName, bool bAlsoUnloadPackage)
{
	ULevelStreaming* Level = FindStreamingLevel(PackageName);
	if (!Level)
	{
		UE_LOG(LogStreamGuard, Warning,
			TEXT("RequestUnload: this world has no streaming level called '%s'."), *PackageName.ToString());
		return false;
	}

	const FName SourceName = MakeSourceName(Level);

	if (FTrackedSource* Source = Tracked.Find(SourceName))
	{
		Source->bWantsLoaded = !bAlsoUnloadPackage;
		Source->bWantsVisible = false;
		Source->Priority = 0;
		Source->RequestSeconds = 0.0;

		// A held level being asked to go away has to have its flags handed back first, or the hold would
		// fight the unload for MaxHoldSeconds.
		if (Source->bHeldLoad)
		{
			Level->SetShouldBeLoaded(true);
			Source->bHeldLoad = false;
		}
		if (Source->bHeldVisible)
		{
			Level->SetShouldBeVisible(true);
			Source->bHeldVisible = false;
		}
	}

	Level->SetShouldBeVisible(false);
	Level->SetShouldBeLoaded(!bAlsoUnloadPackage);

	return true;
}

//~ Snapshot -------------------------------------------------------------------------------------------------

void UStreamGuardSubsystem::RebuildSnapshot(double Now)
{
	TArray<FStreamGuardSourceStat> Raw;
	Raw.Reserve(Tracked.Num() + 1);

	for (const TPair<FName, FTrackedSource>& Pair : Tracked)
	{
		Raw.Add(Pair.Value.Stat);
	}

	// The sources the ceiling refused are folded into one row here, before sorting, so the row takes its place
	// in the table by the same rules as everything else rather than being stapled to the bottom.
	if (OverflowSources > 0)
	{
		FStreamGuardSourceStat Aggregate;
		Aggregate.SourceName = FName(*FString::Printf(TEXT("(%d sources past the ceiling)"), OverflowSources));
		Aggregate.Kind = EStreamGuardSourceKind::Aggregate;
		Aggregate.bIsAggregate = true;
		Aggregate.AggregatedSources = OverflowSources;
		Raw.Add(Aggregate);
	}

	UStreamGuardStatics::SortSources(Raw, SortBy, SourceStats);
	UStreamGuardStatics::FoldSources(SourceStats, BoardRows, SourceStats);

	FrameRing.GetOrdered(FrameSamples);

	Totals.WorstFrameMs = 0.0f;
	if (const FStreamGuardFrameSample* Worst = FrameRing.GetWorst())
	{
		Totals.WorstFrameMs = Worst->FrameMs;
	}

	Totals.AverageFrameMs = FrameRing.GetAverageMs();
	Totals.HitchCount = FrameRing.GetHitchCount();
	Totals.HitchThresholdMs = FrameRing.GetHitchThresholdMs();
	Totals.WindowSeconds = FrameRing.GetWindowSeconds();
	Totals.bThrottleEnabled = bThrottleEnabled;
	Totals.bFrozen = bFrozen;
	Totals.MaxConcurrentLoads = Budget.MaxConcurrentLoads;
	Totals.MaxActivationMsPerFrame = Budget.MaxActivationMsPerFrame;
	Totals.PlayerLocation = PlayerLocation;
	Totals.PlayerVelocity = PlayerVelocity;

	UWorld* World = GetWorld();
	Totals.bWorldPartition = World ? World->IsPartitionedWorld() : false;
	Totals.bAllStreamingCompleted = true;

	if (Totals.bWorldPartition && World)
	{
		// The one World Partition specific call in the plugin, and a public BlueprintCallable one. It is used
		// for a single line that says whether the world has settled - not to enumerate cells, which is done
		// through the world's ordinary streaming array like everything else.
		if (UWorldPartitionSubsystem* Partition = World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			Totals.bAllStreamingCompleted = Partition->IsAllStreamingCompleted();
		}
	}
}

bool UStreamGuardSubsystem::GetWorstFrame(FStreamGuardFrameSample& OutSample) const
{
	if (const FStreamGuardFrameSample* Worst = FrameRing.GetWorst())
	{
		OutSample = *Worst;
		return true;
	}

	return false;
}

//~ Export ---------------------------------------------------------------------------------------------------

bool UStreamGuardSubsystem::ExportCsv(const FString& Path, FString& OutPath)
{
	const FString Csv = UStreamGuardStatics::BuildCsv(SourceStats, FrameSamples, Totals, bExportFrameRows);

	FString Target = Path;

	if (Target.IsEmpty())
	{
		const FString FileName = FString::Printf(TEXT("StreamGuard_%s.csv"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		Target = FPaths::Combine(FPaths::ProjectSavedDir(), CsvSubdirectory, FileName);
	}
	else if (FPaths::IsRelative(Target))
	{
		Target = FPaths::Combine(FPaths::ProjectSavedDir(), CsvSubdirectory, Target);
	}

	OutPath = FPaths::ConvertRelativePathToFull(Target);

	if (!FFileHelper::SaveStringToFile(Csv, *OutPath))
	{
		UE_LOG(LogStreamGuard, Warning, TEXT("Export: could not write '%s'."), *OutPath);
		return false;
	}

	UE_LOG(LogStreamGuard, Display, TEXT("Export: wrote %d sources and %d frames to '%s'."),
		SourceStats.Num(), bExportFrameRows ? FrameSamples.Num() : 0, *OutPath);

	return true;
}

//~ Drawing --------------------------------------------------------------------------------------------------

void UStreamGuardSubsystem::RefreshHudDelegate()
{
	if (bAutoDrawBoardOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UStreamGuardSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawBoardOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void UStreamGuardSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bShowBoard || !HUD || !Canvas)
	{
		return;
	}

	// The delegate is global; the subsystem is per world. Without this, a PIE session with two windows draws
	// one world's streaming on the other's screen and nobody can tell which is which.
	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	DrawBoard(Canvas, BoardOrigin, BoardWidth);
}

bool UStreamGuardSubsystem::HasDrawnBoardThisFrame() const
{
	return LastBoardDrawFrame == GFrameCounter;
}

void UStreamGuardSubsystem::DrawBoard(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace StreamGuardPrivate;

	if (!Canvas)
	{
		return;
	}

	// Both draw routes call this. Whichever gets there first this frame wins and the other does nothing,
	// which is what makes it safe for a project to use AStreamGuardHUD *and* leave the any-HUD setting on.
	if (HasDrawnBoardThisFrame())
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastBoardDrawFrame = GFrameCounter;

	struct FBoardLine
	{
		FString Text;
		FLinearColor Color;
	};

	TArray<FBoardLine> Lines;
	Lines.Reserve(32);

	auto AddLine = [&Lines](FString&& Text, const FLinearColor& Color)
	{
		FBoardLine& Line = Lines.AddDefaulted_GetRef();
		Line.Text = MoveTemp(Text);
		Line.Color = Color;
	};

	//~ Header

	AddLine(FString::Printf(TEXT("StreamGuard  |  %s  |  %d source%s  |  %d loading, %d activating, %d visible%s"),
		Totals.bWorldPartition ? TEXT("World Partition") : TEXT("Sublevels"),
		Totals.SeenSources, Totals.SeenSources == 1 ? TEXT("") : TEXT("s"),
		Totals.LoadsInFlight, Totals.ActivationsInFlight, Totals.VisibleSources,
		Totals.bFrozen ? TEXT("  |  [FROZEN]") : TEXT("")),
		Totals.bFrozen ? WarnColor : HeadingColor);

	AddLine(FString::Printf(
		TEXT("frames  worst %6.1f ms   avg %5.1f ms   %d hitch%s over %.0f ms in the last %.0f s"),
		Totals.WorstFrameMs, Totals.AverageFrameMs, Totals.HitchCount,
		Totals.HitchCount == 1 ? TEXT("") : TEXT("es"),
		Totals.HitchThresholdMs, Totals.WindowSeconds),
		Totals.HitchCount > 0 ? HotColor : GoodColor);

	if (Totals.bThrottleEnabled)
	{
		AddLine(FString::Printf(
			TEXT("budget  ON   %d load%s at once, %.1f ms of activation per frame   |   holding %d"),
			Totals.MaxConcurrentLoads, Totals.MaxConcurrentLoads == 1 ? TEXT("") : TEXT("s"),
			Totals.MaxActivationMsPerFrame, Totals.HeldRequests),
			HeldColor);
	}
	else
	{
		AddLine(TEXT("budget  OFF  (StreamGuard.Throttle 1 - everything arrives when the engine says so)"),
			DimColor);
	}

	if (Totals.bWorldPartition && !Totals.bAllStreamingCompleted)
	{
		AddLine(TEXT("        World Partition has not settled - cells are still being considered"), DimColor);
	}

	//~ Table

	AddLine(TEXT(""), BodyColor);
	AddLine(FString::Printf(TEXT("%s %-16s %7s %7s %10s %8s %5s"),
		*Fit(TEXT("SOURCE"), 28), TEXT("STATE"), TEXT("LOAD"), TEXT("ACT"), TEXT("FRAME"), TEXT("DIST"),
		TEXT("HELD")),
		HeadingColor);

	if (SourceStats.Num() == 0)
	{
		// The most common reason for an empty board, said out loud rather than left to be discovered. A map
		// with no sublevels and no World Partition streams nothing, and there is nothing wrong with the tool.
		AddLine(TEXT("    this world has no streaming levels - nothing to measure"), WarnColor);
	}

	for (const FStreamGuardSourceStat& Stat : SourceStats)
	{
		if (Stat.bIsAggregate)
		{
			AddLine(FString::Printf(TEXT("%s %-16s %7s %7s %10s %8s %5s"),
				*Fit(Stat.SourceName.ToString(), 28), TEXT("(folded)"), TEXT("-"), TEXT("-"), TEXT("-"),
				TEXT("-"), TEXT("-")),
				DimColor);
			continue;
		}

		AddLine(FString::Printf(TEXT("%s %-16s %7s %7s %10lld %8s %5d"),
			*Fit(Stat.SourceName.ToString(), 28),
			*UStreamGuardStatics::GetStateName(Stat.State),
			*UStreamGuardStatics::FormatMs(Stat.LoadMs),
			*UStreamGuardStatics::FormatMs(Stat.ActivateMs),
			Stat.CompletedFrame,
			*UStreamGuardStatics::FormatDistance(Stat.DistanceToPlayer),
			Stat.HeldFrames),
			StateColor(Stat.State));
	}

	//~ The hitches, with what landed in them

	if (HitchDetailRows > 0)
	{
		TArray<FStreamGuardFrameSample> Hitches;
		UStreamGuardStatics::FindHitches(FrameSamples, HitchDetailRows, Hitches);

		AddLine(TEXT(""), BodyColor);

		if (Hitches.Num() == 0)
		{
			AddLine(FString::Printf(TEXT("no frame over %.0f ms in the window"), Totals.HitchThresholdMs),
				GoodColor);
		}

		for (const FStreamGuardFrameSample& Hitch : Hitches)
		{
			// This is the line the plugin exists for: a frame that cost you, and the names of the things that
			// finished inside it. Everything else on the board is context for this sentence.
			FString What = UStreamGuardStatics::DescribeCompletions(Hitch);

			AddLine(FString::Printf(TEXT("frame %lld  %6.1f ms  <-  %s"),
				Hitch.FrameNumber, Hitch.FrameMs, *What),
				HotColor);
		}
	}

	//~ Draw

	LastBoardLineCount = Lines.Num();

	const float TimelineBlock = bShowTimeline ? (TimelineHeight + BoxPadding) : 0.0f;
	const float BoxHeight = Lines.Num() * LineHeight + BoxPadding * 2.0f + TimelineBlock;

	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);

	for (const FBoardLine& Line : Lines)
	{
		if (!Line.Text.IsEmpty())
		{
			FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), FStringView(Line.Text), Font, Line.Color);
			Canvas->DrawItem(Item);
		}

		LineY += LineHeight;
	}

	if (!bShowTimeline)
	{
		return;
	}

	//~ The timeline

	const float StripX = static_cast<float>(Origin.X);
	const float StripY = LineY + BoxPadding * 0.5f;
	const float StripWidth = Width - BoxPadding * 2.0f;

	DrawFilledRect(Canvas, FVector2D(StripX, StripY), FVector2D(StripWidth, TimelineHeight),
		TimelineBackground);

	if (FrameSamples.Num() > 0)
	{
		// The strip is scaled to twice the threshold, not to the worst frame in the window. Scaling to the
		// worst would make every window look the same - the spike always touching the top - and the whole
		// point of the strip is that you can see at a glance whether this run was worse than the last one.
		const float FullScaleMs = FMath::Max(Totals.HitchThresholdMs * 2.0f, 1.0f);

		// A column is never thinner than a pixel, so a window holding more samples than the strip is wide
		// cannot show all of them. Drop the oldest rather than letting the bars run off the panel: the
		// window is sized for AssumedFrameRate, which is deliberately set above the frame rate anyone
		// expects, so on a fast machine there are routinely twice as many samples as there are pixels.
		const int32 MaxColumns = FMath::Max(FMath::FloorToInt(StripWidth), 1);
		const int32 FirstShown = FMath::Max(0, FrameSamples.Num() - MaxColumns);
		const int32 ShownCount = FMath::Max(FrameSamples.Num() - FirstShown, 1);

		const float ColumnWidth = FMath::Max(StripWidth / ShownCount, 1.0f);

		for (int32 Index = FirstShown; Index < FrameSamples.Num(); ++Index)
		{
			const FStreamGuardFrameSample& Sample = FrameSamples[Index];

			const float Height = FMath::Clamp(Sample.FrameMs / FullScaleMs, 0.0f, 1.0f) * TimelineHeight;
			const float X = StripX + (Index - FirstShown) * ColumnWidth;

			DrawFilledRect(Canvas,
				FVector2D(X, StripY + TimelineHeight - Height),
				FVector2D(FMath::Max(ColumnWidth - 0.5f, 1.0f), Height),
				Sample.bOverThreshold ? TimelineHitch : TimelineBar);
		}

		const float ThresholdY = StripY + TimelineHeight
			- FMath::Clamp(Totals.HitchThresholdMs / FullScaleMs, 0.0f, 1.0f) * TimelineHeight;

		DrawFilledRect(Canvas, FVector2D(StripX, ThresholdY), FVector2D(StripWidth, 1.0f),
			TimelineThreshold);
	}
}

//~ Console commands -----------------------------------------------------------------------------------------

namespace StreamGuardPrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdShow(
		TEXT("StreamGuard.Show"),
		TEXT("StreamGuard.Show [0|1] - draw the streaming board. No argument toggles it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Show: no StreamGuard in this world."));
				return;
			}

			const bool bShow = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsShowingBoard();
			Subsystem->SetShowBoard(bShow);

			UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Show: %s"), bShow ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdFreeze(
		TEXT("StreamGuard.Freeze"),
		TEXT("StreamGuard.Freeze [0|1] - hold the board still without pausing the game. No argument toggles."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Freeze: no StreamGuard in this world."));
				return;
			}

			const bool bFreeze = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsFrozen();

			if (bFreeze)
			{
				Subsystem->Freeze();
			}
			else
			{
				Subsystem->Resume();
			}

			UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Freeze: %s"),
				bFreeze ? TEXT("frozen") : TEXT("running"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdExport(
		TEXT("StreamGuard.Export"),
		TEXT("StreamGuard.Export [path] - write the board to CSV. No argument writes a timestamped file "
			"under Saved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Export: no StreamGuard in this world."));
				return;
			}

			FString OutPath;
			if (Subsystem->ExportCsv(Args.Num() > 0 ? Args[0] : FString(), OutPath))
			{
				UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Export: %s"), *OutPath);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBudget(
		TEXT("StreamGuard.Budget"),
		TEXT("StreamGuard.Budget <n> [ms] - how many loads at once, and optionally how many milliseconds of "
			"activation per frame."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Budget: no StreamGuard in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Budget: %d loads, %.1f ms per frame."),
					Subsystem->GetMaxConcurrentLoads(), Subsystem->GetMaxActivationMsPerFrame());
				return;
			}

			Subsystem->SetMaxConcurrentLoads(FCString::Atoi(*Args[0]));

			if (Args.Num() > 1)
			{
				Subsystem->SetMaxActivationMsPerFrame(FCString::Atof(*Args[1]));
			}

			UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Budget: %d loads, %.1f ms per frame."),
				Subsystem->GetMaxConcurrentLoads(), Subsystem->GetMaxActivationMsPerFrame());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdThrottle(
		TEXT("StreamGuard.Throttle"),
		TEXT("StreamGuard.Throttle [0|1] - pace streaming work to the budget. No argument toggles it. This "
			"is the before-and-after switch."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Throttle: no StreamGuard in this world."));
				return;
			}

			const bool bEnable = Args.Num() > 0
				? (FCString::Atoi(*Args[0]) != 0)
				: !Subsystem->IsThrottleEnabled();

			Subsystem->SetThrottleEnabled(bEnable);

			UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Throttle: %s"),
				bEnable ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdReset(
		TEXT("StreamGuard.Reset"),
		TEXT("StreamGuard.Reset - throw away every measurement and start from an empty window."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			UStreamGuardSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogStreamGuard, Warning, TEXT("StreamGuard.Reset: no StreamGuard in this world."));
				return;
			}

			Subsystem->ResetMeasurements();
			UE_LOG(LogStreamGuard, Display, TEXT("StreamGuard.Reset: cleared."));
		}));
}
