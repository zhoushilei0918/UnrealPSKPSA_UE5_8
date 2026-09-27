#pragma once

#include "ActorXModels.h"

class UNREALPSKPSA_API FPskReader
{
public:
	FPskReader(const FString& Filepath);
	
	bool bIsValid = false;
	bool bHasVertexNormals = false;
	bool bHasVertexColors = false;
	bool bHasMorphData = false;
	FString ErrorMessage;
	
	TArray<FVector3f> Vertices;
	TArray<VVertex> Wedges;
	TArray<VTriangle> Faces;
	TArray<VMaterial> Materials;
	TArray<FVector3f> Normals;
	TArray<FColor> VertexColors;
	TArray<TArray<FVector2f>> ExtraUVs;
	TArray<VMorphInfo> MorphInfos;
	TArray<VMorphData> MorphDatas;

	TArray<VNamedBoneBinary> Bones;
	TArray<VRawBoneInfluence> Influences;
};