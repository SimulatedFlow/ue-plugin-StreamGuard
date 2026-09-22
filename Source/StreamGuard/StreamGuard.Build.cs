// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class StreamGuard : ModuleRules
{
	public StreamGuard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else. The promise on the store page is that the board appears in the
		// build where the hitch actually happens - which is the packaged one, on the machine that is slower
		// than yours - so there is nowhere an editor-only module could sit without breaking it.
		//
		// LoadingPhase is PreDefault so the console commands are registered and the world subsystem exists
		// before any game module starts asking for levels.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",

			// Engine carries every hook this plugin stands on: UWorld::GetStreamingLevels, ULevelStreaming's
			// state machine and its OnLevelLoaded/OnLevelShown delegates, UWorldPartitionSubsystem (World
			// Partition is part of the Engine module in UE5, not a separate one), ALevelBounds and
			// AHUD::OnHUDPostRender.
			"Engine",

			"DeveloperSettings",
		});

		// RenderCore - GWhiteTexture, the one-pixel texture the board background, the cost bars and every
		//              column of the frame-time timeline are tiled from. UCanvas has no untextured rectangle,
		//              so there is no way to draw a filled bar without it.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});

		// Deliberately NOT here:
		//   UMG      - the board is drawn on UCanvas from AHUD. A widget tree is the one thing that is
		//              reliably missing from the build where you most need to see these numbers.
		//   UnrealEd - everything in this plugin ships.
		//   Any third-party code, and any external trace session.
	}
}
