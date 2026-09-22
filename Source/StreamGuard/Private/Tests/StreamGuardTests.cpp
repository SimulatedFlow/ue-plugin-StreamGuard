// Copyright 2026 Silvan Teufel. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "StreamGuardStatics.h"
#include "StreamGuardThrottle.h"
#include "StreamGuardTypes.h"

/**
 * Everything under test here is static and world-free, which is exactly why the planner, the fold and the
 * frame-to-source attribution live in a plain UObject and a Blueprint function library rather than inside the
 * subsystem. A UTickableWorldSubsystem needs a world, cannot be constructed in an automation test, and every
 * line of logic that lives in one is a line that never gets a test.
 *
 * These call the same functions the running game calls on every frame. There is no second implementation in
 * here that agrees with the first until somebody edits one of the two.
 */
namespace StreamGuardTest
{
	static constexpr double Now = 1000.0;

	static FStreamGuardBudget MakeBudget(int32 MaxLoads, float MaxActivationMs)
	{
		FStreamGuardBudget Budget;
		Budget.MaxConcurrentLoads = MaxLoads;
		Budget.MaxActivationMsPerFrame = MaxActivationMs;
		Budget.LoadsInFlight = 0;
		Budget.DirectionWeight = 0.5f;
		Budget.MaxHoldSeconds = 5.0f;
		Budget.NowSeconds = Now;

		return Budget;
	}

	static FStreamGuardPending MakeLoad(const TCHAR* Name, FVector Location)
	{
		FStreamGuardPending Pending;
		Pending.SourceName = Name;
		Pending.Kind = EStreamGuardPendingKind::Load;
		Pending.Location = Location;

		// Requested now, so ageing never promotes anything unless a test asks it to.
		Pending.RequestTimeSeconds = Now;

		return Pending;
	}

	static FStreamGuardPending MakeActivate(const TCHAR* Name, float EstimatedMs, FVector Location)
	{
		FStreamGuardPending Pending;
		Pending.SourceName = Name;
		Pending.Kind = EStreamGuardPendingKind::Activate;
		Pending.EstimatedMs = EstimatedMs;
		Pending.Location = Location;
		Pending.RequestTimeSeconds = Now;

		return Pending;
	}

	static FStreamGuardSourceStat MakeSource(const TCHAR* Name, float TotalMs, int64 CompletedFrame)
	{
		FStreamGuardSourceStat Stat;
		Stat.SourceName = Name;
		Stat.PackageName = Name;
		Stat.State = EStreamGuardSourceState::Visible;
		Stat.LoadMs = TotalMs * 0.8f;
		Stat.ActivateMs = TotalMs * 0.2f;
		Stat.TotalMs = TotalMs;
		Stat.CompletedFrame = CompletedFrame;
		Stat.StartedFrame = FMath::Max<int64>(CompletedFrame - 4, 0);

		return Stat;
	}
}

//~ 1. The load ceiling --------------------------------------------------------------------------------------

/**
 * PlanFrame never starts more loads than the budget allows, and counts what the engine is already doing.
 *
 * This is the promise on the store page in its smallest form. The second half - that in-flight loads count
 * against the allowance - is the half that is easy to get wrong and impossible to see: a planner that ignored
 * them would allow N new loads on top of N running ones, every frame, and the "budget" would be a suggestion.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardPlanRespectsLoadBudget,
	"StreamGuard.Plan.RespectsMaxConcurrentLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardPlanRespectsLoadBudget::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	TArray<FStreamGuardPending> Pending;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Pending.Add(MakeLoad(*FString::Printf(TEXT("Cell_%02d"), Index),
			FVector(1000.0 * (Index + 1), 0.0, 0.0)));
	}

	FStreamGuardBudget Budget = MakeBudget(3, 100.0f);

	FStreamGuardPlan Plan = UStreamGuardThrottle::PlanFrame(Pending, Budget, FVector::ZeroVector,
		FVector::ZeroVector);

	TestEqual(TEXT("exactly the allowance is started"), Plan.StartLoads.Num(), 3);
	TestEqual(TEXT("everything else waits"), Plan.Deferred.Num(), 7);
	TestEqual(TEXT("nothing is dropped"), Plan.StartLoads.Num() + Plan.Deferred.Num(), Pending.Num());
	TestEqual(TEXT("nothing had to be promoted"), Plan.PromotedCount, 0);

	// Nearest first, because the player is standing still and the heading term has nothing to say.
	TestEqual(TEXT("the nearest is started first"), Plan.StartLoads[0], FName(TEXT("Cell_00")));

	// Two of the three are already running: only one new one may start.
	Budget.LoadsInFlight = 2;
	Plan = UStreamGuardThrottle::PlanFrame(Pending, Budget, FVector::ZeroVector, FVector::ZeroVector);

	TestEqual(TEXT("in-flight loads count against the allowance"), Plan.StartLoads.Num(), 1);
	TestEqual(TEXT("and the rest still wait"), Plan.Deferred.Num(), 9);

	// The allowance is already spent. Nothing new starts, and nothing is lost.
	Budget.LoadsInFlight = 3;
	Plan = UStreamGuardThrottle::PlanFrame(Pending, Budget, FVector::ZeroVector, FVector::ZeroVector);

	TestEqual(TEXT("a full pipe starts nothing"), Plan.StartLoads.Num(), 0);
	TestEqual(TEXT("and everything is still in the queue"), Plan.Deferred.Num(), 10);

	// A blocking load is a hitch already; holding it back would only move the hitch. It goes, and it is
	// reported as a promotion so the board can say the budget was overridden rather than pretending it wasn't.
	TArray<FStreamGuardPending> WithBlocking = Pending;
	WithBlocking[7].bBlocking = true;

	Plan = UStreamGuardThrottle::PlanFrame(WithBlocking, Budget, FVector::ZeroVector, FVector::ZeroVector);

	TestEqual(TEXT("a blocking load goes through a full pipe"), Plan.StartLoads.Num(), 1);
	TestEqual(TEXT("and it is the blocking one"), Plan.StartLoads[0], FName(TEXT("Cell_07")));
	TestEqual(TEXT("and it is reported as a promotion"), Plan.PromotedCount, 1);

	return true;
}

//~ 2. Where the player is going -----------------------------------------------------------------------------

/**
 * A source in the direction of travel beats one the same distance to the side.
 *
 * The one line in the plugin where a sign error would be invisible in a screenshot and obvious in a car:
 * with the sign the wrong way round, StreamGuard would carefully preload everything the player has just
 * driven past. Both the ordering and the scoring function underneath it are checked, because the ordering
 * would still pass by luck on a two-element array with a broken comparator.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardPlanPrefersTravelDirection,
	"StreamGuard.Plan.PrefersTheDirectionOfTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardPlanPrefersTravelDirection::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	const FVector PlayerPos = FVector::ZeroVector;
	const FVector PlayerVel(1200.0, 0.0, 0.0);

	// Same distance from the player. One is where the player is going, one is at ninety degrees to it, one is
	// behind. Nothing but the direction separates them.
	const FVector Ahead(10000.0, 0.0, 0.0);
	const FVector Sideways(0.0, 10000.0, 0.0);
	const FVector Behind(-10000.0, 0.0, 0.0);

	const float ScoreAhead = UStreamGuardThrottle::ScoreSource(Ahead, PlayerPos, PlayerVel, 0.5f);
	const float ScoreSideways = UStreamGuardThrottle::ScoreSource(Sideways, PlayerPos, PlayerVel, 0.5f);
	const float ScoreBehind = UStreamGuardThrottle::ScoreSource(Behind, PlayerPos, PlayerVel, 0.5f);

	TestTrue(TEXT("ahead scores better than sideways"), ScoreAhead < ScoreSideways);
	TestTrue(TEXT("sideways scores better than behind"), ScoreSideways < ScoreBehind);
	TestEqual(TEXT("sideways is scored at its true distance"), ScoreSideways, 10000.0f, 1.0f);

	// A standing player has no direction to prefer, so all three are equally far. Without this, a paused game
	// would order its streaming by whatever the last velocity happened to be.
	TestEqual(TEXT("a standing player scores by distance alone"),
		UStreamGuardThrottle::ScoreSource(Behind, PlayerPos, FVector::ZeroVector, 0.5f), 10000.0f, 1.0f);

	// And now the ordering the score exists for. One slot in the budget, three candidates.
	TArray<FStreamGuardPending> Pending;
	Pending.Add(MakeLoad(TEXT("Sideways"), Sideways));
	Pending.Add(MakeLoad(TEXT("Behind"), Behind));
	Pending.Add(MakeLoad(TEXT("Ahead"), Ahead));

	const FStreamGuardBudget Budget = MakeBudget(1, 100.0f);

	const FStreamGuardPlan Plan = UStreamGuardThrottle::PlanFrame(Pending, Budget, PlayerPos, PlayerVel);

	TestEqual(TEXT("one slot, one start"), Plan.StartLoads.Num(), 1);
	TestEqual(TEXT("the one ahead of the player is started"), Plan.StartLoads[0], FName(TEXT("Ahead")));

	// Priority is a louder instruction than geometry. A game that knows the player is about to be teleported
	// somewhere has to be able to say so.
	Pending[1].Priority = 10;

	const FStreamGuardPlan Prioritised = UStreamGuardThrottle::PlanFrame(Pending, Budget, PlayerPos, PlayerVel);

	TestEqual(TEXT("priority outranks direction"), Prioritised.StartLoads[0], FName(TEXT("Behind")));

	return true;
}

//~ 3. The millisecond budget --------------------------------------------------------------------------------

/**
 * The activation budget moves work to a later frame instead of throwing it away.
 *
 * Driven the way the subsystem drives it: plan, remove what started, plan again with what is left. After a
 * handful of frames everything must have started - that is the difference between a budget and a filter, and
 * it is the sentence the documentation makes a promise out of.
 *
 * The no-starvation rule is checked in its own right at the end, because it is the one case where the budget
 * is deliberately exceeded: a single activation that costs more than the whole frame budget still has to run,
 * or it never runs at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardBudgetDefersRatherThanDrops,
	"StreamGuard.Plan.MillisecondBudgetDefersRatherThanDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardBudgetDefersRatherThanDrops::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	// Eight activations at 1.4 ms each, and a 3 ms frame budget: two fit (2.8 ms), a third does not (4.2 ms),
	// so the queue drains at two a frame and takes four frames.
	TArray<FStreamGuardPending> Queue;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Queue.Add(MakeActivate(*FString::Printf(TEXT("Cell_%02d"), Index), 1.4f,
			FVector(1000.0 * (Index + 1), 0.0, 0.0)));
	}

	const FStreamGuardBudget Budget = MakeBudget(4, 3.0f);

	TSet<FName> Started;
	int32 Frames = 0;

	while (Queue.Num() > 0 && Frames < 64)
	{
		++Frames;

		const FStreamGuardPlan Plan = UStreamGuardThrottle::PlanFrame(Queue, Budget, FVector::ZeroVector,
			FVector::ZeroVector);

		TestTrue(TEXT("a frame always makes progress"), Plan.StartActivations.Num() > 0);
		TestTrue(TEXT("nothing is lost between the two lists"),
			Plan.StartActivations.Num() + Plan.Deferred.Num() == Queue.Num());

		if (Plan.PromotedCount == 0)
		{
			TestTrue(TEXT("an unpromoted frame stays inside the budget"),
				Plan.PlannedActivationMs <= Budget.MaxActivationMsPerFrame + UE_KINDA_SMALL_NUMBER);
		}

		for (const FName& Name : Plan.StartActivations)
		{
			bool bAlreadyInSet = false;
			Started.Add(Name, &bAlreadyInSet);
			TestFalse(TEXT("nothing is started twice"), bAlreadyInSet);
		}

		Queue.RemoveAll([&Plan](const FStreamGuardPending& Item)
		{
			return Plan.StartActivations.Contains(Item.SourceName);
		});
	}

	TestEqual(TEXT("everything started in the end"), Started.Num(), 8);
	TestEqual(TEXT("two per frame over four frames"), Frames, 4);

	// One activation, far more expensive than the entire frame budget. It has to go anyway: a budget that can
	// refuse work forever is not a budget.
	TArray<FStreamGuardPending> Heavy;
	Heavy.Add(MakeActivate(TEXT("Enormous"), 250.0f, FVector(500.0, 0.0, 0.0)));

	const FStreamGuardPlan HeavyPlan = UStreamGuardThrottle::PlanFrame(Heavy, Budget, FVector::ZeroVector,
		FVector::ZeroVector);

	TestEqual(TEXT("an over-budget activation still runs"), HeavyPlan.StartActivations.Num(), 1);
	TestEqual(TEXT("and nothing was deferred behind it"), HeavyPlan.Deferred.Num(), 0);

	// Ageing. Two requests that have waited a minute, and one that arrived this frame and is nearer than
	// either of them. Both aged ones go - the second of them past a budget that is already spent - and the
	// fresh one waits, however close it is. That ordering is what stops a player circling one spot from
	// keeping a distant request pending for the rest of the session.
	TArray<FStreamGuardPending> Old;
	Old.Add(MakeActivate(TEXT("Ancient_Near"), 2.5f, FVector(1000.0, 0.0, 0.0)));
	Old.Add(MakeActivate(TEXT("Ancient_Far"), 2.5f, FVector(90000.0, 0.0, 0.0)));
	Old.Add(MakeActivate(TEXT("Fresh"), 2.5f, FVector(100.0, 0.0, 0.0)));
	Old[0].RequestTimeSeconds = Now - 60.0;
	Old[1].RequestTimeSeconds = Now - 60.0;

	const FStreamGuardPlan Aged = UStreamGuardThrottle::PlanFrame(Old, Budget, FVector::ZeroVector,
		FVector::ZeroVector);

	TestEqual(TEXT("both long-held requests go"), Aged.StartActivations.Num(), 2);
	TestEqual(TEXT("the nearer aged one first"), Aged.StartActivations[0], FName(TEXT("Ancient_Near")));
	TestEqual(TEXT("the second was promoted past a spent budget"), Aged.StartActivations[1],
		FName(TEXT("Ancient_Far")));
	TestEqual(TEXT("and the promotion is reported"), Aged.PromotedCount, 1);
	TestEqual(TEXT("the fresh request waits, however near it is"), Aged.Deferred.Num(), 1);
	TestEqual(TEXT("and it is the fresh one"), Aged.Deferred[0], FName(TEXT("Fresh")));

	return true;
}

//~ 4. Frame to source ---------------------------------------------------------------------------------------

/**
 * The attribution finds the right sources for a marked frame.
 *
 * This is the sentence the whole plugin is sold on - "frame 184312 cost you 47 ms, and here is what landed in
 * it" - so it is checked from both ends: the frame-to-source lookup over the measured sources, and the
 * per-frame name list the timeline carries, including the "and N more" that keeps a busy frame honest.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardFrameAttribution,
	"StreamGuard.Attribution.FindsTheSourcesForAFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardFrameAttribution::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	TArray<FStreamGuardSourceStat> Sources;
	Sources.Add(MakeSource(TEXT("L_Early"), 40.0f, 1000));
	Sources.Add(MakeSource(TEXT("L_Hitch_A"), 180.0f, 1234));
	Sources.Add(MakeSource(TEXT("L_Hitch_B"), 90.0f, 1234));
	Sources.Add(MakeSource(TEXT("L_Late"), 12.0f, 1300));

	// A source that has never finished. Its CompletedFrame is zero, and zero must never match anything.
	FStreamGuardSourceStat Pending = MakeSource(TEXT("L_NeverLoaded"), 0.0f, 0);
	Pending.State = EStreamGuardSourceState::Unloaded;
	Sources.Add(Pending);

	TArray<FName> Names;
	UStreamGuardStatics::FindSourcesCompletedInFrame(Sources, 1234, Names);

	TestEqual(TEXT("both sources in the marked frame are found"), Names.Num(), 2);
	TestTrue(TEXT("the first one is there"), Names.Contains(FName(TEXT("L_Hitch_A"))));
	TestTrue(TEXT("the second one is there"), Names.Contains(FName(TEXT("L_Hitch_B"))));

	UStreamGuardStatics::FindSourcesCompletedInFrame(Sources, 1235, Names);
	TestEqual(TEXT("a quiet frame attributes nothing"), Names.Num(), 0);

	UStreamGuardStatics::FindSourcesCompletedInFrame(Sources, 0, Names);
	TestEqual(TEXT("frame zero is never a match"), Names.Num(), 0);

	// The other end: the ring's own per-frame record, driven exactly as the subsystem drives it.
	FStreamGuardFrameRing Ring;
	Ring.Configure(10.0f, 60.0f, 33.0f);

	TestTrue(TEXT("a fresh ring is empty"), Ring.IsEmpty());

	Ring.AddFrame(1233, 16.6f, 100.0);
	Ring.AddFrame(1234, 47.1f, 100.017);
	Ring.NoteCompletion(TEXT("L_Hitch_A"));
	Ring.NoteCompletion(TEXT("L_Hitch_B"));
	Ring.AddFrame(1235, 16.4f, 100.064);

	TArray<FStreamGuardFrameSample> Samples;
	Ring.GetOrdered(Samples);

	TestEqual(TEXT("three frames in the window"), Samples.Num(), 3);
	TestEqual(TEXT("oldest first"), Samples[0].FrameNumber, (int64)1233);
	TestFalse(TEXT("a normal frame is not a hitch"), Samples[0].bOverThreshold);
	TestTrue(TEXT("the long frame is a hitch"), Samples[1].bOverThreshold);
	TestEqual(TEXT("the completions landed in the long frame"), Samples[1].CompletedCount, 2);
	TestEqual(TEXT("and not in the next one"), Samples[2].CompletedCount, 0);

	TArray<FStreamGuardFrameSample> Hitches;
	UStreamGuardStatics::FindHitches(Samples, 3, Hitches);

	TestEqual(TEXT("one hitch in the window"), Hitches.Num(), 1);
	TestEqual(TEXT("and it is the right frame"), Hitches[0].FrameNumber, (int64)1234);
	TestEqual(TEXT("described with both names"),
		UStreamGuardStatics::DescribeCompletions(Hitches[0]), FString(TEXT("L_Hitch_A, L_Hitch_B")));

	TestEqual(TEXT("the worst frame is the hitch"), Ring.GetWorst()->FrameNumber, (int64)1234);
	TestEqual(TEXT("one frame over the threshold"), Ring.GetHitchCount(), 1);

	// A frame that landed more sources than the sample keeps names for still reports the true count, so the
	// board can say "and N more" rather than quietly claiming six.
	Ring.AddFrame(1236, 80.0f, 100.08);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Ring.NoteCompletion(*FString::Printf(TEXT("Cell_%02d"), Index));
	}

	const FStreamGuardFrameSample* Busy = Ring.GetNewest();
	if (TestNotNull(TEXT("the busy frame is there"), Busy))
	{
		TestEqual(TEXT("the count is not capped"), Busy->CompletedCount, 20);
		TestEqual(TEXT("the names are"), Busy->CompletedSources.Num(),
			FStreamGuardFrameRing::MaxNamesPerSample);
		TestTrue(TEXT("and the description says how many were left out"),
			UStreamGuardStatics::DescribeCompletions(*Busy).Contains(TEXT("and 14 more")));
	}

	return true;
}

//~ 5. The ceiling -------------------------------------------------------------------------------------------

/**
 * Past 256 sources the table folds into exactly one row, and the fold keeps the count.
 *
 * This is the promise that StreamGuard does not become the hitch, tested at the one place where it could
 * quietly stop being true. Two things have to hold: the output is bounded, and nothing that fell off the end
 * is forgotten about - a fold that dropped its count would let the board report a tidy world while four
 * hundred cells churn behind it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardCeilingFoldsIntoOneRow,
	"StreamGuard.Ceiling.OverflowFoldsIntoOneRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardCeilingFoldsIntoOneRow::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	constexpr int32 Ceiling = 256;
	constexpr int32 SourceCount = 300;

	TArray<FStreamGuardSourceStat> Input;
	Input.Reserve(SourceCount);

	float ExpectedTotalMs = 0.0f;

	for (int32 Index = 0; Index < SourceCount; ++Index)
	{
		// Descending, so the fold takes the cheapest 44 and the first 256 are kept.
		const float TotalMs = static_cast<float>(SourceCount - Index);
		Input.Add(MakeSource(*FString::Printf(TEXT("Cell_%03d"), Index), TotalMs, 1000 + Index));
		ExpectedTotalMs += TotalMs;
	}

	TArray<FStreamGuardSourceStat> Folded;
	UStreamGuardStatics::FoldSources(Input, Ceiling, Folded);

	TestEqual(TEXT("the table is bounded at the ceiling plus one fold row"), Folded.Num(), Ceiling + 1);

	int32 AggregateRows = 0;
	for (const FStreamGuardSourceStat& Stat : Folded)
	{
		if (Stat.bIsAggregate)
		{
			++AggregateRows;
		}
	}

	TestEqual(TEXT("exactly one folded row"), AggregateRows, 1);

	const FStreamGuardSourceStat& Aggregate = Folded.Last();
	TestTrue(TEXT("and it is the last row"), Aggregate.bIsAggregate);
	TestEqual(TEXT("it stands for everything past the ceiling"), Aggregate.AggregatedSources,
		SourceCount - Ceiling);

	float TotalMs = 0.0f;
	for (const FStreamGuardSourceStat& Stat : Folded)
	{
		TotalMs += Stat.TotalMs;
	}

	TestEqual(TEXT("nothing was lost on the way through the ceiling"), TotalMs, ExpectedTotalMs, 1.0f);

	// Exactly at the ceiling nothing is folded at all - the off-by-one that would put a pointless
	// "(and 0 more)" row under every full board.
	TArray<FStreamGuardSourceStat> Exact = Input;
	Exact.SetNum(Ceiling);
	UStreamGuardStatics::FoldSources(Exact, Ceiling, Folded);

	TestEqual(TEXT("a table exactly at the ceiling is not folded"), Folded.Num(), Ceiling);
	TestFalse(TEXT("and gains no aggregate row"), Folded.Last().bIsAggregate);

	// A fold of a fold keeps the count it already stood for. This is the subsystem's own overflow row - which
	// already speaks for the cells it never tracked - being folded again by the board's row limit.
	TArray<FStreamGuardSourceStat> Nested;
	Nested.Add(MakeSource(TEXT("Big"), 1000.0f, 5000));
	Nested.Add(MakeSource(TEXT("Small"), 10.0f, 5001));

	FStreamGuardSourceStat Overflow = MakeSource(TEXT("(44 sources past the ceiling)"), 0.0f, 0);
	Overflow.bIsAggregate = true;
	Overflow.AggregatedSources = 44;
	Nested.Add(Overflow);

	UStreamGuardStatics::FoldSources(Nested, 1, Folded);

	TestEqual(TEXT("one kept row and one fold"), Folded.Num(), 2);
	TestEqual(TEXT("the nested aggregate kept its count"), Folded[1].AggregatedSources, 45);

	// And the sort keeps the footnote at the bottom whatever it is sorted by, because an aggregate row at the
	// top of a table is a table nobody trusts.
	TArray<FStreamGuardSourceStat> Sorted;
	UStreamGuardStatics::SortSources(Nested, EStreamGuardSort::LoadTime, Sorted);

	TestEqual(TEXT("the most expensive source is first"), Sorted[0].SourceName, FName(TEXT("Big")));
	TestTrue(TEXT("the aggregate is last"), Sorted.Last().bIsAggregate);

	return true;
}

//~ 6. The export --------------------------------------------------------------------------------------------

/**
 * The CSV is one header and one line per entry, and it reads back into the same fields.
 *
 * Not one of the five the specification asked for, and here anyway: "and it is readable again" is half of
 * what an export is worth, and the awkward names are in here on purpose - a level called "L_Block,B" and one
 * with a quote in its name have both broken a CSV before now.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamGuardCsvRoundTrip,
	"StreamGuard.Csv.HeaderPlusOneLinePerEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStreamGuardCsvRoundTrip::RunTest(const FString& /*Parameters*/)
{
	using namespace StreamGuardTest;

	TArray<FStreamGuardSourceStat> Sources;
	Sources.Add(MakeSource(TEXT("L_Block,B"), 182.4f, 1234));
	Sources.Add(MakeSource(TEXT("L_Quote\"Name"), 12.0f, 1240));

	TArray<FStreamGuardFrameSample> Samples;

	FStreamGuardFrameSample Sample;
	Sample.FrameNumber = 1234;
	Sample.FrameMs = 47.1f;
	Sample.TimeSeconds = 100.0;
	Sample.bOverThreshold = true;
	Sample.CompletedCount = 1;
	Sample.CompletedSources.Add(TEXT("L_Block,B"));
	Samples.Add(Sample);

	FStreamGuardTotals Totals;
	Totals.SeenSources = 2;
	Totals.HitchCount = 1;
	Totals.WorstFrameMs = 47.1f;

	const FString Csv = UStreamGuardStatics::BuildCsv(Sources, Samples, Totals, true);

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines);

	// Header, one summary row, two sources, one frame.
	const int32 Expected = 1 + 1 + Sources.Num() + Samples.Num();
	TestEqual(TEXT("header plus one line per entry"), Lines.Num(), Expected);
	TestEqual(TEXT("the first line is the header"), Lines[0], UStreamGuardStatics::GetCsvHeader());

	TArray<FString> HeaderFields;
	UStreamGuardStatics::SplitCsvLine(Lines[0], HeaderFields);
	TestEqual(TEXT("thirteen columns"), HeaderFields.Num(), 13);

	// Every data line has to have the same shape as the header, or a single parse on the far end is a lie.
	bool bFoundComma = false;
	bool bFoundQuote = false;

	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		TArray<FString> Fields;
		UStreamGuardStatics::SplitCsvLine(Lines[Index], Fields);

		TestEqual(*FString::Printf(TEXT("line %d has thirteen columns"), Index), Fields.Num(), 13);

		if (Fields.Num() > 1 && Fields[1] == TEXT("L_Block,B"))
		{
			bFoundComma = true;
			TestEqual(TEXT("the duration survived the round trip"), FCString::Atod(*Fields[5]), 182.4, 0.01);
		}
		if (Fields.Num() > 1 && Fields[1] == TEXT("L_Quote\"Name"))
		{
			bFoundQuote = true;
		}
	}

	TestTrue(TEXT("a name with a comma in it survived"), bFoundComma);
	TestTrue(TEXT("a name with a quote in it survived"), bFoundQuote);

	// Without frame rows the file is exactly one line shorter, which is the whole meaning of the flag.
	const FString Lean = UStreamGuardStatics::BuildCsv(Sources, Samples, Totals, false);
	TArray<FString> LeanLines;
	Lean.ParseIntoArrayLines(LeanLines);
	TestEqual(TEXT("dropping frames drops exactly the frame rows"), LeanLines.Num(), Expected - 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
