#pragma once
#include "ActorXModels.h"
#include "Misc/FileHelper.h"

namespace PskFixtureUtils
{
    // A renamed skeletal PSK is still skeletal. Build genuine static fixtures by
    // removing skeletal chunks, preserving the original geometry and extensions.
    inline bool WriteWithoutChunks(const FString& Source, const FString& Destination, const TSet<FString>& Excluded)
    {
        TArray<uint8> Bytes, Output;
        if (!FFileHelper::LoadFileToArray(Bytes, *Source)) return false;
        for (int64 Offset = 0; Offset < Bytes.Num();)
        {
            if (Bytes.Num() - Offset < 32) return false;
            VChunkHeader Header{}; FMemory::Memcpy(&Header, Bytes.GetData() + Offset, 32);
            const int64 Size = int64(Header.DataSize) * Header.DataCount;
            if (Header.DataSize < 0 || Header.DataCount < 0 || Size > Bytes.Num() - Offset - 32) return false;
            ANSICHAR Name[21]{}; FMemory::Memcpy(Name, Header.ChunkID, 20);
            if (!Excluded.Contains(UTF8_TO_TCHAR(Name))) Output.Append(Bytes.GetData() + Offset, int32(32 + Size));
            Offset += 32 + Size;
        }
        return FFileHelper::SaveArrayToFile(Output, *Destination);
    }
    inline bool WriteStatic(const FString& Source, const FString& Destination)
    {
        return WriteWithoutChunks(Source, Destination, {TEXT("REFSKELT"), TEXT("REFSKEL0"), TEXT("RAWWEIGHTS")});
    }
}
