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

		// Net/: the saved room's host token is sealed with DPAPI (CryptProtectData). Other platforms keep it in plaintext.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Crypt32.lib");
		}

		// Store/: the Steam Inventory backend (the engine's Steamworks SDK, no plugin). A Shipping game always has it:
		// without Steam the store can't sell and nothing confirms what a player owns. Development builds add it only
		// with MOVECADE_STEAM=1 in the environment, so the editor and tests need no Steam. UBT doesn't notice an
		// environment change on its own: after toggling it, touch this file. Docs/Store/README.md, Docs/Release/.
		bool bDesktop = Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac;
		bool bShippingGame = Target.Configuration == UnrealTargetConfiguration.Shipping && Target.Type == TargetType.Game;
		bool bWithSteam = bDesktop && (bShippingGame || System.Environment.GetEnvironmentVariable("MOVECADE_STEAM") == "1");
		if (bWithSteam)
		{
			PrivateDependencyModuleNames.Add("Steamworks");
		}
		PrivateDefinitions.Add("MOVECADE_WITH_STEAM=" + (bWithSteam ? "1" : "0"));

		// Store/ on phones (Docs/Store/README.md "Mobile"). The Store/IOS and Store/Android folders build only on their
		// platform (UBT skips other platforms' folders). iOS: StoreKit 2 through a small Swift file, so the module gets a
		// Swift bridging header. Android: Google Play Billing through the engine's OnlineSubsystemGooglePlay.
		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			SwiftInteropHeader = System.IO.Path.Combine(ModuleDirectory, "Private/Store/IOS/KartStoreKitInterop.h");
			PublicFrameworks.Add("StoreKit");
		}
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "CoreOnline" });
		}
		// The Google Play purchase signature check (Store/GooglePlayReceipt.cpp) runs on Android and is tested on Win64.
		bool bWithPlayVerify = Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Win64;
		if (bWithPlayVerify)
		{
			PrivateDependencyModuleNames.Add("OpenSSL");
		}
		PrivateDefinitions.Add("MOVECADE_WITH_PLAY_VERIFY=" + (bWithPlayVerify ? "1" : "0"));
	}
}
