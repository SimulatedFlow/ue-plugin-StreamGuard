// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StreamGuardHUD.generated.h"

/**
 * A HUD that draws the StreamGuard board, for projects that would rather be explicit than rely on a delegate.
 *
 * Most projects should not need this. StreamGuard draws itself through AHUD::OnHUDPostRender on whatever HUD
 * class the project already has, which is the setting you want in a shipping game where the HUD class is
 * already spoken for. This class is here for two cases: a project that prefers its diagnostics wired up where
 * it can see them, and a demo map, where the whole point is that the board appears with no configuration at
 * all.
 *
 * Using both at once is safe. The board is drawn by one function that refuses to draw twice in a frame, so
 * whichever route arrives first wins and the other does nothing.
 *
 * Nothing here is UMG. The board is UCanvas from end to end, which is what lets it survive into a packaged
 * build with no widget tree - and a packaged build on a slower disk is precisely where the hitch you are
 * chasing lives.
 */
UCLASS(meta = (DisplayName = "StreamGuard HUD"))
class STREAMGUARD_API AStreamGuardHUD : public AHUD
{
	GENERATED_BODY()

public:
	AStreamGuardHUD();

	//~ AHUD interface
	virtual void DrawHUD() override;

	/**
	 * Draw the board from this HUD.
	 *
	 * Off does not hide the board - the any-HUD route may still be drawing it, and the console command still
	 * governs whether anything is drawn at all. It only stops *this* class from being one of the routes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard")
	bool bDrawStreamGuardBoard = true;

	/**
	 * Turn the board on when this HUD starts, whatever the project setting says.
	 *
	 * The reason to place this HUD at all is usually that you want the board; having to type a console
	 * command afterwards would make that a strange way to want it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard")
	bool bShowBoardOnBeginPlay = true;

	/** Override where the board sits for this HUD. Negative on either axis uses the project setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard")
	FVector2D BoardOrigin = FVector2D(-1.0f, -1.0f);

	/** Override how wide the board is for this HUD. Zero or less uses the project setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StreamGuard")
	float BoardWidth = 0.0f;

protected:
	virtual void BeginPlay() override;
};
