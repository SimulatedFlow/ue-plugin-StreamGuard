// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "StreamGuardLevelWatcher.generated.h"

class ULevelStreaming;
class UStreamGuardSubsystem;

/**
 * One of these per tracked streaming source, and it exists for exactly one reason: to know the frame.
 *
 * **Which route was taken, so the next reader does not have to go and find out.** ULevelStreaming publishes
 * four delegates - OnLevelLoaded, OnLevelShown, OnLevelUnloaded, OnLevelHidden - and they are the public,
 * documented, Blueprint-assignable way to hear about streaming. They are also
 * DECLARE_DYNAMIC_MULTICAST_DELEGATE with **no parameters**: they say "something happened", not "this level
 * happened". A dynamic delegate cannot carry a payload the way CreateUObject with bound arguments can, so
 * there is no way to bind one handler for every level and still know which level called it.
 *
 * Hence a watcher object per level, holding the one piece of context the delegate does not carry. It is small
 * (a weak pointer, a weak pointer and a name), there is at most one per tracked source, and the number of
 * tracked sources is capped in the project settings - so this scales with the ceiling and not with the map.
 *
 * The alternative - polling GetLevelStreamingState() every tick - is also done, and is how StreamGuard sees
 * load *starts* and how it covers World Partition cells that appear and disappear between ticks. But polling
 * cannot tell you the frame a level finished in: the subsystem's tick may run before or after the world's
 * streaming update, so a poll attributes a completion to the frame it noticed rather than the frame it
 * happened. These delegates fire at the moment the engine finished, so GFrameCounter read inside them is the
 * real answer - and "which frame" is the entire product.
 */
UCLASS()
class UStreamGuardLevelWatcher : public UObject
{
	GENERATED_BODY()

public:
	/** Start listening. Safe to call twice; the second call unbinds the first. */
	void Bind(ULevelStreaming* InLevel, UStreamGuardSubsystem* InOwner, FName InSourceName);

	/** Stop listening. Safe on a level that has already been garbage collected. */
	void Unbind();

	/** The level this watcher speaks for, or null once it has gone. */
	ULevelStreaming* GetLevel() const { return Level.Get(); }

private:
	UFUNCTION()
	void HandleLevelLoaded();

	UFUNCTION()
	void HandleLevelShown();

	UFUNCTION()
	void HandleLevelUnloaded();

	UFUNCTION()
	void HandleLevelHidden();

	/**
	 * Weak on purpose.
	 *
	 * A World Partition cell's ULevelStreaming is created and destroyed by the streaming policy as the player
	 * moves, and a diagnostic that kept them alive with a hard reference would keep the map's memory high and
	 * then be blamed for it.
	 */
	UPROPERTY()
	TWeakObjectPtr<ULevelStreaming> Level;

	UPROPERTY()
	TWeakObjectPtr<UStreamGuardSubsystem> Owner;

	FName SourceName;
};
