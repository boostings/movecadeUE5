using UnrealBuildTool;

public class MovecadeKart : ModuleRules
{
	public MovecadeKart(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Files keep file-local helpers and log categories; unity batches would collide them as the module grows.
		bUseUnity = false;
		// The race must be bit-identical run to run and match the web build (JS doubles never fuse or reassociate).
		// Game targets default to /fp:fast on Windows; only Editor targets get precise, which is what the tests prove.
		FPSemantics = FPSemanticsMode.Precise;

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
	}
}
