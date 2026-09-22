// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "StreamGuardHUD.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"

#include "StreamGuardSettings.h"
#include "StreamGuardSubsystem.h"

AStreamGuardHUD::AStreamGuardHUD()
{
	// The HUD ticks for its own reasons; nothing here needs a tick of its own. DrawHUD is called by the
	// renderer when there is a canvas to draw on, which is the only moment this class has anything to do.
	PrimaryActorTick.bCanEverTick = false;
}

void AStreamGuardHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!bShowBoardOnBeginPlay)
	{
		return;
	}

	if (UStreamGuardSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UStreamGuardSubsystem>() : nullptr)
	{
		Subsystem->SetShowBoard(true);
	}
}

void AStreamGuardHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bDrawStreamGuardBoard || !Canvas)
	{
		return;
	}

	UWorld* World = GetWorld();
	UStreamGuardSubsystem* Subsystem = World ? World->GetSubsystem<UStreamGuardSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->IsShowingBoard())
	{
		return;
	}

	const UStreamGuardSettings& Settings = UStreamGuardSettings::Get();

	// Negative means "not overridden here". Zero is a real coordinate - the top left corner - so it cannot be
	// the sentinel, and a board silently jumping to the project default because somebody typed 0 would be an
	// annoying afternoon.
	const FVector2D Origin = (BoardOrigin.X >= 0.0 && BoardOrigin.Y >= 0.0)
		? BoardOrigin
		: Settings.BoardOrigin;

	const float Width = BoardWidth > 0.0f ? BoardWidth : Settings.BoardWidth;

	Subsystem->DrawBoard(Canvas, Origin, Width);
}
