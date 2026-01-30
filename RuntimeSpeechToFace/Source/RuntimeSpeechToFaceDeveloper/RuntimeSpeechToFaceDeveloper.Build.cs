// Some copyright should be here...

using UnrealBuildTool;
using System.IO;

public class RuntimeSpeechToFaceDeveloper : ModuleRules
{
	public RuntimeSpeechToFaceDeveloper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RuntimeSpeechToFace",
				"UnrealEd",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"BlueprintGraph",
			}
		);
	}
}
