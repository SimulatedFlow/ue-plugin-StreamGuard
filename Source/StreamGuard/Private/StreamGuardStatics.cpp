// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardStatics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/StringBuilder.h"

#include "StreamGuardLog.h"
#include "StreamGuardSubsystem.h"

namespace StreamGuardStaticsPrivate
{
	/**
	 * Quote a CSV field, but only when it needs it.
	 *
	 * Level names contain commas more often than anyone expects - a level called "Block,B" has broken an
	 * export before - and a quote inside a name will break the parse on the far end unless it is doubled.
	 * Quoting everything would be simpler and would make the file unreadable in a text editor, which is half
	 * of what a CSV is for.
	 */
	static FString EscapeCsv(const FString& Field)
	{
		const bool bNeedsQuotes = Field.Contains(TEXT(","))
			|| Field.Contains(TEXT("\""))
			|| Field.Contains(TEXT("\n"))
			|| Field.Contains(TEXT("\r"));

		if (!bNeedsQuotes)
		{
			return Field;
		}

		FString Escaped = Field;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));

		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

//~ The subsystem --------------------------------------------------------------------------------------------

UStreamGuardSubsystem* UStreamGuardStatics::GetStreamGuard(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UStreamGuardSubsystem>() : nullptr;
}

void UStreamGuardStatics::SetShowBoard(const UObject* WorldContextObject, bool bShow)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->SetShowBoard(bShow);
	}
}

bool UStreamGuardStatics::IsShowingBoard(const UObject* WorldContextObject)
{
	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->IsShowingBoard() : false;
}

bool UStreamGuardStatics::ToggleBoard(const UObject* WorldContextObject)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		return false;
	}

	const bool bShow = !Subsystem->IsShowingBoard();
	Subsystem->SetShowBoard(bShow);

	return bShow;
}

void UStreamGuardStatics::FreezeStreamGuard(const UObject* WorldContextObject)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->Freeze();
	}
}

void UStreamGuardStatics::ResumeStreamGuard(const UObject* WorldContextObject)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->Resume();
	}
}

bool UStreamGuardStatics::ToggleFreezeStreamGuard(const UObject* WorldContextObject)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		return false;
	}

	const bool bFreeze = !Subsystem->IsFrozen();

	if (bFreeze)
	{
		Subsystem->Freeze();
	}
	else
	{
		Subsystem->Resume();
	}

	return bFreeze;
}

bool UStreamGuardStatics::IsStreamGuardFrozen(const UObject* WorldContextObject)
{
	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->IsFrozen() : false;
}

void UStreamGuardStatics::ResetStreamGuard(const UObject* WorldContextObject)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->ResetMeasurements();
	}
}

bool UStreamGuardStatics::ExportStreamGuardCsv(const UObject* WorldContextObject, const FString& Path,
	FString& OutPath)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		OutPath.Reset();
		return false;
	}

	return Subsystem->ExportCsv(Path, OutPath);
}

//~ The budget -----------------------------------------------------------------------------------------------

void UStreamGuardStatics::SetThrottleEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->SetThrottleEnabled(bEnabled);
	}
}

bool UStreamGuardStatics::IsThrottleEnabled(const UObject* WorldContextObject)
{
	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->IsThrottleEnabled() : false;
}

bool UStreamGuardStatics::ToggleThrottle(const UObject* WorldContextObject)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		return false;
	}

	const bool bEnable = !Subsystem->IsThrottleEnabled();
	Subsystem->SetThrottleEnabled(bEnable);

	return bEnable;
}

void UStreamGuardStatics::SetMaxConcurrentLoads(const UObject* WorldContextObject, int32 MaxLoads)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->SetMaxConcurrentLoads(MaxLoads);
	}
}

int32 UStreamGuardStatics::GetMaxConcurrentLoads(const UObject* WorldContextObject)
{
	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->GetMaxConcurrentLoads() : 0;
}

void UStreamGuardStatics::SetMaxActivationMsPerFrame(const UObject* WorldContextObject, float MaxMs)
{
	if (UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		Subsystem->SetMaxActivationMsPerFrame(MaxMs);
	}
}

float UStreamGuardStatics::GetMaxActivationMsPerFrame(const UObject* WorldContextObject)
{
	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->GetMaxActivationMsPerFrame() : 0.0f;
}

//~ Requests -------------------------------------------------------------------------------------------------

bool UStreamGuardStatics::RequestLevelLoad(const UObject* WorldContextObject, FName PackageName,
	bool bMakeVisible, int32 Priority)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogStreamGuard, Warning,
			TEXT("RequestLevelLoad: no StreamGuard in this context's world."));
		return false;
	}

	return Subsystem->RequestLoad(PackageName, bMakeVisible, Priority);
}

bool UStreamGuardStatics::RequestLevelUnload(const UObject* WorldContextObject, FName PackageName,
	bool bAlsoUnloadPackage)
{
	UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogStreamGuard, Warning,
			TEXT("RequestLevelUnload: no StreamGuard in this context's world."));
		return false;
	}

	return Subsystem->RequestUnload(PackageName, bAlsoUnloadPackage);
}

//~ Reading --------------------------------------------------------------------------------------------------

void UStreamGuardStatics::GetSourceStats(const UObject* WorldContextObject,
	TArray<FStreamGuardSourceStat>& OutStats)
{
	OutStats.Reset();

	if (const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		OutStats = Subsystem->GetSourceStats();
	}
}

void UStreamGuardStatics::GetFrameSamples(const UObject* WorldContextObject,
	TArray<FStreamGuardFrameSample>& OutSamples)
{
	OutSamples.Reset();

	if (const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		OutSamples = Subsystem->GetFrameSamples();
	}
}

FStreamGuardTotals UStreamGuardStatics::GetTotals(const UObject* WorldContextObject)
{
	if (const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject))
	{
		return Subsystem->GetTotals();
	}

	return FStreamGuardTotals();
}

bool UStreamGuardStatics::GetWorstFrame(const UObject* WorldContextObject, FStreamGuardFrameSample& OutSample)
{
	OutSample = FStreamGuardFrameSample();

	const UStreamGuardSubsystem* Subsystem = GetStreamGuard(WorldContextObject);
	return Subsystem ? Subsystem->GetWorstFrame(OutSample) : false;
}

//~ Pure arithmetic ------------------------------------------------------------------------------------------

void UStreamGuardStatics::SortSources(const TArray<FStreamGuardSourceStat>& In, EStreamGuardSort SortBy,
	TArray<FStreamGuardSourceStat>& Out)
{
	// Copied rather than sorted in place, so that passing the same array as input and output - which the
	// subsystem does, every snapshot - is defined behaviour rather than a crash nobody sees until the day the
	// implementation stops being a copy.
	TArray<FStreamGuardSourceStat> Working = In;

	Working.Sort([SortBy](const FStreamGuardSourceStat& A, const FStreamGuardSourceStat& B)
	{
		// An aggregate row is never allowed to outrank a real one: it is a footnote, and a footnote at the
		// top of a table is a table nobody trusts.
		if (A.bIsAggregate != B.bIsAggregate)
		{
			return B.bIsAggregate;
		}

		switch (SortBy)
		{
		case EStreamGuardSort::LoadTime:
			if (!FMath::IsNearlyEqual(A.TotalMs, B.TotalMs))
			{
				return A.TotalMs > B.TotalMs;
			}
			break;

		case EStreamGuardSort::Distance:
			if (!FMath::IsNearlyEqual(A.DistanceToPlayer, B.DistanceToPlayer))
			{
				return A.DistanceToPlayer < B.DistanceToPlayer;
			}
			break;

		case EStreamGuardSort::CompletedFrame:
			if (A.CompletedFrame != B.CompletedFrame)
			{
				return A.CompletedFrame > B.CompletedFrame;
			}
			break;

		case EStreamGuardSort::Name:
		default:
			break;
		}

		// Always the same tie-break, so the board does not shuffle between two snapshots that measured the
		// same thing. A table that reorders itself while you read it is a table you stop reading.
		return A.SourceName.LexicalLess(B.SourceName);
	});

	Out = MoveTemp(Working);
}

void UStreamGuardStatics::FoldSources(const TArray<FStreamGuardSourceStat>& In, int32 MaxRows,
	TArray<FStreamGuardSourceStat>& Out)
{
	if (MaxRows <= 0 || In.Num() <= MaxRows)
	{
		TArray<FStreamGuardSourceStat> Passthrough = In;
		Out = MoveTemp(Passthrough);
		return;
	}

	TArray<FStreamGuardSourceStat> Working;
	Working.Reserve(MaxRows + 1);

	for (int32 Index = 0; Index < MaxRows; ++Index)
	{
		Working.Add(In[Index]);
	}

	FStreamGuardSourceStat Aggregate;
	Aggregate.Kind = EStreamGuardSourceKind::Aggregate;
	Aggregate.bIsAggregate = true;

	int32 Folded = 0;
	float TotalMs = 0.0f;

	for (int32 Index = MaxRows; Index < In.Num(); ++Index)
	{
		const FStreamGuardSourceStat& Stat = In[Index];

		// An aggregate that is folded again brings its count with it. Without this, the subsystem's own
		// overflow row - which already stands for four hundred cells - would be counted as one more source
		// and the board would claim the ceiling folded a single thing.
		Folded += Stat.bIsAggregate ? FMath::Max(Stat.AggregatedSources, 1) : 1;
		TotalMs += Stat.TotalMs;
	}

	Aggregate.AggregatedSources = Folded;
	Aggregate.TotalMs = TotalMs;
	Aggregate.SourceName = FName(*FString::Printf(TEXT("(and %d more)"), Folded));

	Working.Add(Aggregate);

	Out = MoveTemp(Working);
}

void UStreamGuardStatics::FindSourcesCompletedInFrame(const TArray<FStreamGuardSourceStat>& Sources,
	int64 FrameNumber, TArray<FName>& OutNames)
{
	OutNames.Reset();

	// Frame zero means "has never finished anything", so it can never match. Without this every source that
	// has not loaded yet would be reported as having landed in frame zero, and the one query this plugin
	// exists to answer would answer it wrongly on an empty window.
	if (FrameNumber <= 0)
	{
		return;
	}

	for (const FStreamGuardSourceStat& Stat : Sources)
	{
		if (!Stat.bIsAggregate && Stat.CompletedFrame == FrameNumber)
		{
			OutNames.Add(Stat.SourceName);
		}
	}
}

void UStreamGuardStatics::FindHitches(const TArray<FStreamGuardFrameSample>& Samples, int32 MaxCount,
	TArray<FStreamGuardFrameSample>& OutHitches)
{
	OutHitches.Reset();

	if (MaxCount <= 0)
	{
		return;
	}

	for (const FStreamGuardFrameSample& Sample : Samples)
	{
		if (Sample.bOverThreshold)
		{
			OutHitches.Add(Sample);
		}
	}

	OutHitches.Sort([](const FStreamGuardFrameSample& A, const FStreamGuardFrameSample& B)
	{
		if (!FMath::IsNearlyEqual(A.FrameMs, B.FrameMs))
		{
			return A.FrameMs > B.FrameMs;
		}

		// Most recent first among equals: if the same hitch happens on every lap, the one you just felt is
		// the one you are looking for.
		return A.FrameNumber > B.FrameNumber;
	});

	if (OutHitches.Num() > MaxCount)
	{
		OutHitches.SetNum(MaxCount);
	}
}

FString UStreamGuardStatics::DescribeCompletions(const FStreamGuardFrameSample& Sample)
{
	if (Sample.CompletedCount <= 0)
	{
		// Worth saying rather than leaving blank. A hitch with nothing streaming in it is a real answer: it
		// means the frame was lost to something that is not this plugin's business, and knowing that is worth
		// as much as a name would have been.
		return TEXT("nothing finished streaming in this frame");
	}

	FString Result;

	for (int32 Index = 0; Index < Sample.CompletedSources.Num(); ++Index)
	{
		if (Index > 0)
		{
			Result += TEXT(", ");
		}

		Result += Sample.CompletedSources[Index].ToString();
	}

	const int32 Unnamed = Sample.CompletedCount - Sample.CompletedSources.Num();
	if (Unnamed > 0)
	{
		Result += FString::Printf(TEXT("%sand %d more"), Result.IsEmpty() ? TEXT("") : TEXT(" "), Unnamed);
	}

	return Result;
}

FString UStreamGuardStatics::GetCsvHeader()
{
	return TEXT("Kind,Name,State,LoadMs,ActivateMs,TotalMs,StartedFrame,CompletedFrame,TimeSeconds,")
		TEXT("DistanceMeters,Actors,HeldFrames,Note");
}

FString UStreamGuardStatics::BuildCsv(const TArray<FStreamGuardSourceStat>& Sources,
	const TArray<FStreamGuardFrameSample>& Samples, const FStreamGuardTotals& Totals, bool bIncludeFrames)
{
	using namespace StreamGuardStaticsPrivate;

	TStringBuilder<4096> Builder;

	Builder << GetCsvHeader() << LINE_TERMINATOR;

	// One summary row, in the same thirteen columns as everything else. A file whose first data row has a
	// different shape from the rest is a file that needs two parsers, and nobody writes the second one.
	Builder << TEXT("Summary,")
		<< EscapeCsv(Totals.bWorldPartition ? TEXT("World Partition") : TEXT("Sublevels")) << TEXT(",")
		<< (Totals.bThrottleEnabled ? TEXT("ThrottleOn") : TEXT("ThrottleOff")) << TEXT(",")
		<< FString::Printf(TEXT("%.3f"), Totals.AverageFrameMs) << TEXT(",")
		<< FString::Printf(TEXT("%.3f"), Totals.MaxActivationMsPerFrame) << TEXT(",")
		<< FString::Printf(TEXT("%.3f"), Totals.WorstFrameMs) << TEXT(",")
		<< TEXT("0,0,")
		<< FString::Printf(TEXT("%.3f"), Totals.WindowSeconds) << TEXT(",")
		<< TEXT("0,")
		<< Totals.SeenSources << TEXT(",")
		<< Totals.HeldRequests << TEXT(",")
		<< EscapeCsv(FString::Printf(TEXT("%d hitches over %.1f ms"), Totals.HitchCount,
			Totals.HitchThresholdMs))
		<< LINE_TERMINATOR;

	for (const FStreamGuardSourceStat& Stat : Sources)
	{
		Builder << TEXT("Source,")
			<< EscapeCsv(Stat.SourceName.ToString()) << TEXT(",")
			<< EscapeCsv(GetStateName(Stat.State)) << TEXT(",")
			<< FString::Printf(TEXT("%.3f"), Stat.LoadMs) << TEXT(",")
			<< FString::Printf(TEXT("%.3f"), Stat.ActivateMs) << TEXT(",")
			<< FString::Printf(TEXT("%.3f"), Stat.TotalMs) << TEXT(",")
			<< Stat.StartedFrame << TEXT(",")
			<< Stat.CompletedFrame << TEXT(",")
			<< FString::Printf(TEXT("%.3f"), Stat.CompletedTimeSeconds) << TEXT(",")
			<< FString::Printf(TEXT("%.1f"), Stat.DistanceToPlayer / 100.0f) << TEXT(",")
			<< Stat.ActorCount << TEXT(",")
			<< Stat.HeldFrames << TEXT(",")
			<< EscapeCsv(Stat.bIsAggregate
				? FString::Printf(TEXT("aggregate of %d"), Stat.AggregatedSources)
				: (Stat.bBlocking ? TEXT("blocking") : TEXT("")))
			<< LINE_TERMINATOR;
	}

	if (bIncludeFrames)
	{
		for (const FStreamGuardFrameSample& Sample : Samples)
		{
			Builder << TEXT("Frame,")
				<< EscapeCsv(DescribeCompletions(Sample)) << TEXT(",")
				<< (Sample.bOverThreshold ? TEXT("Hitch") : TEXT("Ok")) << TEXT(",")
				<< TEXT("0.000,0.000,")
				<< FString::Printf(TEXT("%.3f"), Sample.FrameMs) << TEXT(",")
				<< Sample.FrameNumber << TEXT(",")
				<< Sample.FrameNumber << TEXT(",")
				<< FString::Printf(TEXT("%.3f"), Sample.TimeSeconds) << TEXT(",")
				<< TEXT("0.0,")
				<< Sample.CompletedCount << TEXT(",")
				<< TEXT("0,")
				<< LINE_TERMINATOR;
		}
	}

	return Builder.ToString();
}

void UStreamGuardStatics::SplitCsvLine(const FString& Line, TArray<FString>& OutFields)
{
	OutFields.Reset();

	FString Current;
	bool bInQuotes = false;

	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		const TCHAR Char = Line[Index];

		if (bInQuotes)
		{
			if (Char == TEXT('"'))
			{
				// A doubled quote inside a quoted field is one literal quote, which is how it went in.
				if (Index + 1 < Line.Len() && Line[Index + 1] == TEXT('"'))
				{
					Current.AppendChar(TEXT('"'));
					++Index;
				}
				else
				{
					bInQuotes = false;
				}
			}
			else
			{
				Current.AppendChar(Char);
			}

			continue;
		}

		if (Char == TEXT('"'))
		{
			bInQuotes = true;
		}
		else if (Char == TEXT(','))
		{
			OutFields.Add(Current);
			Current.Reset();
		}
		else
		{
			Current.AppendChar(Char);
		}
	}

	OutFields.Add(Current);
}

FString UStreamGuardStatics::FormatMs(float Milliseconds)
{
	if (Milliseconds <= 0.0f)
	{
		// A dash, not "0.0". Zero would claim the load was instant; what actually happened is that it has not
		// happened yet, and a table full of zeroes reads as a broken tool.
		return TEXT("-");
	}

	if (Milliseconds < 10.0f)
	{
		return FString::Printf(TEXT("%.2f"), Milliseconds);
	}

	return FString::Printf(TEXT("%.1f"), Milliseconds);
}

FString UStreamGuardStatics::FormatDistance(float Centimetres)
{
	const float Metres = Centimetres / 100.0f;

	if (Metres >= 1000.0f)
	{
		return FString::Printf(TEXT("%.1fkm"), Metres / 1000.0f);
	}

	return FString::Printf(TEXT("%.0fm"), Metres);
}

FString UStreamGuardStatics::GetStateName(EStreamGuardSourceState State)
{
	switch (State)
	{
	case EStreamGuardSourceState::Removed:          return TEXT("removed");
	case EStreamGuardSourceState::Unloaded:         return TEXT("unloaded");
	case EStreamGuardSourceState::FailedToLoad:     return TEXT("FAILED");
	case EStreamGuardSourceState::Loading:          return TEXT("loading");
	case EStreamGuardSourceState::LoadedNotVisible: return TEXT("loaded");
	case EStreamGuardSourceState::MakingVisible:    return TEXT("activating");
	case EStreamGuardSourceState::Visible:          return TEXT("visible");
	case EStreamGuardSourceState::MakingInvisible:  return TEXT("hiding");
	default:                                        return TEXT("?");
	}
}
