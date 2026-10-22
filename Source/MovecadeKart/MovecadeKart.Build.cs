using UnrealBuildTool;

public class MovecadeKart : ModuleRules
{
	public MovecadeKart(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Files keep file-local helpers and log categories; unity batches would collide them as the module grows.
		bUseUnity = false;

		// Track data is read from <Project>/Data/Tracks at runtime; stage it with packaged builds.
		RuntimeDependencies.Add("$(ProjectDir)/Data/Tracks/*.json", StagedFileType.UFS);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"EnhancedInput",
			"ProceduralMeshComponent",
			"Json",
			// Phone host (Net/): room server socket and the lobby UI.
			"WebSockets",
			"UMG",
			// Render/: GPU frame timing, ray tracing support queries, dynamic resolution state.
			"RenderCore",
			"RHI",
			// FX/: Niagara particles and the item meshes built in code.
			"Niagara",
			"MeshDescription",
			"StaticMeshDescription",
		});

		// Store/: the Steam Inventory backend builds only with MOVECADE_STEAM=1 in the environment (the engine's
		// Steamworks SDK, no plugin). The default build has no Steam dependency and uses the offline store.
		// Docs/Store/README.md.
		bool bWithSteam = System.Environment.GetEnvironmentVariable("MOVECADE_STEAM") == "1"
			&& (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac);
		if (bWithSteam)
		{
			PrivateDependencyModuleNames.Add("Steamworks");
		}
		PrivateDefinitions.Add("MOVECADE_WITH_STEAM=" + (bWithSteam ? "1" : "0"));
	}
}
