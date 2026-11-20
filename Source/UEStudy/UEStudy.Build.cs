using UnrealBuildTool;

public class UEStudy : ModuleRules
{
	public UEStudy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"PhysicsCore",
			"ProceduralMeshComponent",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"UEStudy",
			"UEStudy/Sandbox",
			"UEStudy/Sandbox/MiniProject"
		});
	}
}
