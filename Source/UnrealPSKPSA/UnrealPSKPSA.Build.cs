using UnrealBuildTool;

public class UnrealPSKPSA : ModuleRules
{
    public UnrealPSKPSA(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "MeshDescription", "RawMesh", "RenderCore",
            "MeshBuilder", "MeshUtilitiesCommon", "TargetPlatform",
            "AnimationDataController", "AssetTools", "Slate", "SlateCore",
            "ToolMenus", "ContentBrowser", "DesktopPlatform", "PropertyEditor", "Json"
        });
    }
}
