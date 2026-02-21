#pragma once
#include "RmlUi/Core/FileInterface.h"

/**
 * RmlUi FileInterface backed by UE's IPlatformFile.
 * - Relative paths are resolved against FPaths::ProjectDir()
 * - Works with pak files in packaged builds (IPlatformFile reads from pak automatically)
 */
class UERMLUI_API FUERmlFileInterface : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String& path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;
	size_t Length(Rml::FileHandle file) override;
};
