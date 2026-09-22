// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardThrottle.h"

float UStreamGuardThrottle::ScoreSource(FVector SourceLocation, FVector PlayerPos, FVector PlayerVel,
	float DirectionWeight)
{
	const FVector Delta = SourceLocation - PlayerPos;
	const double Distance = Delta.Size();

	// On top of the source, or the player is standing still: there is no heading to prefer and the score is
	// the distance. No special case needed for a zero velocity - GetSafeNormal returns zero, the dot product
	// is zero, and the scale falls out at exactly 1.
	if (Distance <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const FVector Heading = PlayerVel.GetSafeNormal();
	const FVector ToSource = Delta / Distance;

	// -1 directly behind, 0 to the side, +1 dead ahead.
	const double Alignment = FVector::DotProduct(Heading, ToSource);

	// Clamped below 1 rather than at it: a weight of exactly 1 would make everything dead ahead score zero
	// and the tie-break between two cells in front of the player would collapse into whichever the array
	// happened to hold first.
	const double Weight = FMath::Clamp(static_cast<double>(DirectionWeight), 0.0, 0.99);

	return static_cast<float>(Distance * (1.0 - Weight * Alignment));
}

FStreamGuardPlan UStreamGuardThrottle::PlanFrame(const TArray<FStreamGuardPending>& Pending,
	const FStreamGuardBudget& Budget, FVector PlayerPos, FVector PlayerVel)
{
	FStreamGuardPlan Plan;

	if (Pending.Num() == 0)
	{
		return Plan;
	}

	struct FScored
	{
		int32 Index = 0;
		float Score = 0.0f;
		bool bMustGo = false;
	};

	TArray<FScored> Order;
	Order.Reserve(Pending.Num());

	const double MaxHold = FMath::Max(0.0, static_cast<double>(Budget.MaxHoldSeconds));

	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		const FStreamGuardPending& Request = Pending[Index];

		FScored Scored;
		Scored.Index = Index;
		Scored.Score = ScoreSource(Request.Location, PlayerPos, PlayerVel, Budget.DirectionWeight);

		// Ageing. A request that has waited longer than the budget's patience stops being negotiable. Note
		// that MaxHoldSeconds of zero would promote everything on its first frame, which is the correct
		// reading of "hold nothing", so it is not defended against.
		const double Waited = Budget.NowSeconds - Request.RequestTimeSeconds;
		Scored.bMustGo = Request.bBlocking || (Waited >= MaxHold);

		Order.Add(Scored);
	}

	// Stable, and with a total order down to the name, so two runs of the same frame produce the same plan.
	// A planner that shuffled equal candidates would make the before-and-after screenshot impossible to
	// reproduce, and reproducibility is most of what this plugin sells.
	Order.Sort([&Pending](const FScored& A, const FScored& B)
	{
		if (A.bMustGo != B.bMustGo)
		{
			return A.bMustGo;
		}

		const FStreamGuardPending& RequestA = Pending[A.Index];
		const FStreamGuardPending& RequestB = Pending[B.Index];

		if (RequestA.Priority != RequestB.Priority)
		{
			return RequestA.Priority > RequestB.Priority;
		}

		if (!FMath::IsNearlyEqual(A.Score, B.Score))
		{
			return A.Score < B.Score;
		}

		if (RequestA.RequestTimeSeconds != RequestB.RequestTimeSeconds)
		{
			return RequestA.RequestTimeSeconds < RequestB.RequestTimeSeconds;
		}

		return RequestA.SourceName.LexicalLess(RequestB.SourceName);
	});

	const int32 LoadAllowance = FMath::Max(1, Budget.MaxConcurrentLoads) - FMath::Max(0, Budget.LoadsInFlight);

	int32 LoadsStarted = 0;
	float ActivationMs = 0.0f;
	bool bActivatedAnything = false;

	for (const FScored& Scored : Order)
	{
		const FStreamGuardPending& Request = Pending[Scored.Index];

		if (Request.Kind == EStreamGuardPendingKind::Load)
		{
			const bool bFits = LoadsStarted < LoadAllowance;

			if (bFits || Scored.bMustGo)
			{
				Plan.StartLoads.Add(Request.SourceName);
				++LoadsStarted;

				if (!bFits)
				{
					++Plan.PromotedCount;
				}
			}
			else
			{
				Plan.Deferred.Add(Request.SourceName);
			}

			continue;
		}

		// Activation. The estimate is only ever consulted here; a load is bounded by a count because the time
		// it takes is the disk's business and not something a frame budget can bound.
		const float Cost = FMath::Max(0.0f, Request.EstimatedMs);
		const bool bFits = !bActivatedAnything || (ActivationMs + Cost) <= Budget.MaxActivationMsPerFrame;

		if (bFits || Scored.bMustGo)
		{
			Plan.StartActivations.Add(Request.SourceName);
			ActivationMs += Cost;
			bActivatedAnything = true;

			if (!bFits)
			{
				++Plan.PromotedCount;
			}
		}
		else
		{
			Plan.Deferred.Add(Request.SourceName);
		}
	}

	Plan.PlannedActivationMs = ActivationMs;

	return Plan;
}
