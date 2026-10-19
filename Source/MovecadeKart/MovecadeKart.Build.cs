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
		});
	}
}
