#pragma once

#include "Archive/Archive.h"

class PakArchive : public Archive
{
public:
	PakArchive() : Archive("pak") {}
	~PakArchive() = default;

	// Opening/writing
	bool open(MemChunk& mc) override;                      // Open from MemChunk
	bool write(MemChunk& mc, bool update = true) override; // Write to MemChunk

	// Misc
	bool loadEntryData(ArchiveEntry* entry) override;

	// Static functions
	static bool isPakArchive(MemChunk& mc);
	static bool isPakArchive(const std::string& filename);
};
