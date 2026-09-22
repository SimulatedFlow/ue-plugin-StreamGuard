// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardLevelWatcher.h"

#include "Engine/LevelStreaming.h"
#include "StreamGuardSubsystem.h"

void UStreamGuardLevelWatcher::Bind(ULevelStreaming* InLevel, UStreamGuardSubsystem* InOwner, FName InSourceName)
{
	Unbind();

	Level = InLevel;
	Owner = InOwner;
	SourceName = InSourceName;

	if (!InLevel)
	{
		return;
	}

	InLevel->OnLevelLoaded.AddDynamic(this, &UStreamGuardLevelWatcher::HandleLevelLoaded);
	InLevel->OnLevelShown.AddDynamic(this, &UStreamGuardLevelWatcher::HandleLevelShown);
	InLevel->OnLevelUnloaded.AddDynamic(this, &UStreamGuardLevelWatcher::HandleLevelUnloaded);
	InLevel->OnLevelHidden.AddDynamic(this, &UStreamGuardLevelWatcher::HandleLevelHidden);
}

void UStreamGuardLevelWatcher::Unbind()
{
	if (ULevelStreaming* Bound = Level.Get())
	{
		Bound->OnLevelLoaded.RemoveDynamic(this, &UStreamGuardLevelWatcher::HandleLevelLoaded);
		Bound->OnLevelShown.RemoveDynamic(this, &UStreamGuardLevelWatcher::HandleLevelShown);
		Bound->OnLevelUnloaded.RemoveDynamic(this, &UStreamGuardLevelWatcher::HandleLevelUnloaded);
		Bound->OnLevelHidden.RemoveDynamic(this, &UStreamGuardLevelWatcher::HandleLevelHidden);
	}

	Level.Reset();
	Owner.Reset();
}

void UStreamGuardLevelWatcher::HandleLevelLoaded()
{
	if (UStreamGuardSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->NoteLevelEvent(SourceName, Level.Get(), EStreamGuardLevelEvent::Loaded);
	}
}

void UStreamGuardLevelWatcher::HandleLevelShown()
{
	if (UStreamGuardSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->NoteLevelEvent(SourceName, Level.Get(), EStreamGuardLevelEvent::Shown);
	}
}

void UStreamGuardLevelWatcher::HandleLevelUnloaded()
{
	if (UStreamGuardSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->NoteLevelEvent(SourceName, Level.Get(), EStreamGuardLevelEvent::Unloaded);
	}
}

void UStreamGuardLevelWatcher::HandleLevelHidden()
{
	if (UStreamGuardSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->NoteLevelEvent(SourceName, Level.Get(), EStreamGuardLevelEvent::Hidden);
	}
}
