// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StreamGuardTypes.h"
#include "StreamGuardThrottle.generated.h"

/**
 * The preload budget, as arithmetic.
 *
 * PlanFrame takes an array of pending requests, a budget, and where the player is and is going, and returns
 * which of them start this frame and which wait one. It has no world, no subsystem, no clock and no engine
 * state - the current time is a field on the budget precisely so that a test can set it to whatever it likes.
 *
 * That shape is a lesson paid for once already: logic that lives inside a UTickableWorldSubsystem is logic
 * that is never tested, because the subsystem cannot be constructed without a world and a world cannot be
 * constructed in an automation test. So the part with the interesting decisions in it lives out here, and the
 * subsystem is left with the boring job of asking the engine what is pending and doing what it is told.
 *
 * Two rules make this a budget rather than a filter, and both are load-bearing:
 *
 *   **The first item always goes through.** If an activation is estimated at 30 ms and the budget is 3 ms,
 *   the budget has nothing to say: refusing it would mean it never runs at all, and a budget that can drop
 *   work permanently is a bug wearing a feature's coat. The budget's job starts with the *second* item.
 *
 *   **Nothing waits forever.** A request older than MaxHoldSeconds is promoted past the budget. Without it, a
 *   player circling one spot could keep a far request pending for the whole session.
 *
 * What it does not do: it does not reorder anything the engine has already started, it does not cancel, and
 * it never invents a request. It can only ever say "not yet".
 */
UCLASS(meta = (DisplayName = "Stream Guard Throttle"))
class STREAMGUARD_API UStreamGuardThrottle : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Decide what starts this frame.
	 *
	 * @param Pending     Everything that wants to start and has not. Order is irrelevant; the result is not.
	 * @param Budget      What may be spent, and what the engine is already spending.
	 * @param PlayerPos   Where the player is, in world space.
	 * @param PlayerVel   How fast and which way. A zero vector turns the heading term off by itself, with no
	 *                    special case: a player who is not moving has no direction to prefer.
	 */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Throttle")
	static FStreamGuardPlan PlanFrame(const TArray<FStreamGuardPending>& Pending,
		const FStreamGuardBudget& Budget, FVector PlayerPos, FVector PlayerVel);

	/**
	 * How far away a source counts as being, once the player's heading is taken into account.
	 *
	 * Straight-line distance, scaled by how well the source lines up with where the player is going: at a
	 * DirectionWeight of 0.5 something dead ahead is treated as half as far as it is, something to the side at
	 * its real distance, and something directly behind as half again as far. That is what makes a cell ahead
	 * beat a cell the same distance to the side, which is the whole reason the planner is given a velocity at
	 * all rather than just a position.
	 *
	 * Lower is better. Exposed and tested on its own because it is the one line in the plugin where a sign
	 * error would be invisible in a screenshot and obvious in a car.
	 */
	UFUNCTION(BlueprintPure, Category = "StreamGuard|Throttle")
	static float ScoreSource(FVector SourceLocation, FVector PlayerPos, FVector PlayerVel, float DirectionWeight);
};
