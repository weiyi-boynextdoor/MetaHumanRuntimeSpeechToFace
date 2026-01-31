// Some copyright should be here...

using UnrealBuildTool;

public class RuntimeSpeechToFace : ModuleRules
{
	public RuntimeSpeechToFace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
            }
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AudioExtensions",
				"MetaHumanCoreTech",
				"MetaHumanSpeech2Face",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"MetaHumanPipeline",
				"MetaHumanPipelineCore",
				"AnimGraphRuntime",
				"TargetPlatform",
				"NNE",
				"SignalProcessing",
				"AudioPlatformConfiguration",
				// ... add private dependencies that you statically link with here ...
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PublicDependencyModuleNames.Add("UnrealEd");
		}
	}
}
