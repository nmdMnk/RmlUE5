#include "RmlInterface/UERmlFileInterface.h"
#include "Logging.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

Rml::FileHandle FUERmlFileInterface::Open(const Rml::String& path)
{
	FString UEPath = UTF8_TO_TCHAR(path.c_str());

	// Resolve relative paths against the project directory.
	// e.g. "Content/UI/Web/TestRmlUi.html" → "<ProjectDir>/Content/UI/Web/TestRmlUi.html"
	if (FPaths::IsRelative(UEPath))
	{
		UEPath = FPaths::ProjectDir() / UEPath;
	}
	UEPath = FPaths::ConvertRelativePathToFull(UEPath);

	IFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*UEPath);
	if (!Handle)
	{
		UE_LOG(LogUERmlUI, Warning, TEXT("FUERmlFileInterface::Open failed: %s"), *UEPath);
		return 0;
	}
	return (Rml::FileHandle)Handle;
}

void FUERmlFileInterface::Close(Rml::FileHandle file)
{
	delete reinterpret_cast<IFileHandle*>(file);
}

size_t FUERmlFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
{
	IFileHandle* Handle = reinterpret_cast<IFileHandle*>(file);
	// IFileHandle::Read returns false if fewer bytes are available than requested
	// (e.g. last chunk of a small file). Cap to remaining bytes so every Read
	// call succeeds and we return the actual number of bytes read, not the request size.
	const int64 Remaining = Handle->Size() - Handle->Tell();
	const int64 ToRead = FMath::Min(static_cast<int64>(size), Remaining);
	if (ToRead <= 0) return 0;
	Handle->Read(static_cast<uint8*>(buffer), ToRead);
	return static_cast<size_t>(ToRead);
}

bool FUERmlFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	IFileHandle* Handle = reinterpret_cast<IFileHandle*>(file);
	int64 NewPos = 0;
	switch (origin)
	{
	case SEEK_SET: NewPos = offset; break;
	case SEEK_CUR: NewPos = Handle->Tell() + offset; break;
	case SEEK_END: NewPos = Handle->Size() + offset; break;
	default: return false;
	}
	return Handle->Seek(NewPos);
}

size_t FUERmlFileInterface::Tell(Rml::FileHandle file)
{
	return static_cast<size_t>(reinterpret_cast<IFileHandle*>(file)->Tell());
}

size_t FUERmlFileInterface::Length(Rml::FileHandle file)
{
	return static_cast<size_t>(reinterpret_cast<IFileHandle*>(file)->Size());
}
