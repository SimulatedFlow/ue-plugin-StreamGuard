// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardTypes.h"

void FStreamGuardFrameRing::Configure(float InWindowSeconds, float InAssumedFrameRate, float InHitchThresholdMs)
{
	WindowSeconds = FMath::Max(1.0f, InWindowSeconds);
	HitchThresholdMs = FMath::Max(1.0f, InHitchThresholdMs);

	// Sized from a frame rate rather than measured from one. A window that resized itself as the frame rate
	// moved would reallocate exactly when the game was already struggling, which is the one moment a
	// diagnostic must not add to the problem. Over-sizing costs a few kilobytes; under-sizing costs the
	// beginning of the hitch you were trying to look at, so the clamp is generous at the top.
	const int32 Wanted = FMath::Clamp(
		FMath::CeilToInt(WindowSeconds * FMath::Max(1.0f, InAssumedFrameRate)), 16, 8192);

	Samples.Reset();
	Samples.SetNum(Wanted);

	Head = 0;
	Count = 0;
}

void FStreamGuardFrameRing::Reset()
{
	for (FStreamGuardFrameSample& Sample : Samples)
	{
		// The names are cleared rather than the array being emptied, so the per-sample TArrays keep the slack
		// they have already paid for and a reset window does not start allocating again on its first frame.
		Sample.CompletedSources.Reset();
		Sample = FStreamGuardFrameSample();
	}

	Head = 0;
	Count = 0;
}

int32 FStreamGuardFrameRing::AddFrame(int64 FrameNumber, float FrameMs, double TimeSeconds)
{
	if (Samples.Num() == 0)
	{
		Configure(WindowSeconds, 60.0f, HitchThresholdMs);
	}

	const int32 Index = Head;

	FStreamGuardFrameSample& Sample = Samples[Index];
	Sample.CompletedSources.Reset();
	Sample.FrameNumber = FrameNumber;
	Sample.FrameMs = FrameMs;
	Sample.TimeSeconds = TimeSeconds;
	Sample.bOverThreshold = FrameMs >= HitchThresholdMs;
	Sample.CompletedCount = 0;

	Head = (Head + 1) % Samples.Num();
	Count = FMath::Min(Count + 1, Samples.Num());

	return Index;
}

void FStreamGuardFrameRing::NoteCompletion(FName SourceName)
{
	if (Count == 0 || Samples.Num() == 0)
	{
		return;
	}

	const int32 Newest = (Head + Samples.Num() - 1) % Samples.Num();
	FStreamGuardFrameSample& Sample = Samples[Newest];

	// The count is uncapped and the name list is not. A frame that landed forty cells still reports forty, so
	// the board can say "and 34 more" rather than quietly claiming six.
	++Sample.CompletedCount;

	if (Sample.CompletedSources.Num() < MaxNamesPerSample)
	{
		Sample.CompletedSources.Add(SourceName);
	}
}

void FStreamGuardFrameRing::GetOrdered(TArray<FStreamGuardFrameSample>& Out) const
{
	Out.Reset(Count);

	if (Count == 0 || Samples.Num() == 0)
	{
		return;
	}

	const int32 Start = (Head + Samples.Num() - Count) % Samples.Num();

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		Out.Add(Samples[(Start + Offset) % Samples.Num()]);
	}
}

const FStreamGuardFrameSample* FStreamGuardFrameRing::GetNewest() const
{
	if (Count == 0 || Samples.Num() == 0)
	{
		return nullptr;
	}

	return &Samples[(Head + Samples.Num() - 1) % Samples.Num()];
}

const FStreamGuardFrameSample* FStreamGuardFrameRing::GetWorst() const
{
	const FStreamGuardFrameSample* Worst = nullptr;

	if (Count == 0 || Samples.Num() == 0)
	{
		return nullptr;
	}

	const int32 Start = (Head + Samples.Num() - Count) % Samples.Num();

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		const FStreamGuardFrameSample& Sample = Samples[(Start + Offset) % Samples.Num()];
		if (!Worst || Sample.FrameMs > Worst->FrameMs)
		{
			Worst = &Sample;
		}
	}

	return Worst;
}

float FStreamGuardFrameRing::GetAverageMs() const
{
	if (Count == 0 || Samples.Num() == 0)
	{
		return 0.0f;
	}

	double Total = 0.0;
	const int32 Start = (Head + Samples.Num() - Count) % Samples.Num();

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		Total += Samples[(Start + Offset) % Samples.Num()].FrameMs;
	}

	return static_cast<float>(Total / Count);
}

int32 FStreamGuardFrameRing::GetHitchCount() const
{
	if (Count == 0 || Samples.Num() == 0)
	{
		return 0;
	}

	int32 Hitches = 0;
	const int32 Start = (Head + Samples.Num() - Count) % Samples.Num();

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		if (Samples[(Start + Offset) % Samples.Num()].bOverThreshold)
		{
			++Hitches;
		}
	}

	return Hitches;
}
