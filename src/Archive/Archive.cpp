
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    Archive.cpp
// Description: Archive, the 'base' archive class (Abstract)
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "Archive.h"
#include "General/UndoRedo.h"
#include "Utility/Parser.h"
#include "Utility/StringUtils.h"
#include <filesystem>


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Bool, archive_load_data, false, CVar::Flag::Save)
CVAR(Bool, backup_archives, true, CVar::Flag::Save)
bool                  Archive::save_backup = true;
vector<ArchiveFormat> Archive::formats;


// -----------------------------------------------------------------------------
//
// Undo Steps
//
// -----------------------------------------------------------------------------

class EntryRenameUS : public UndoStep
{
public:
	EntryRenameUS(ArchiveEntry* entry, std::string_view new_name) :
		archive_{ entry->parent() },
		entry_path_{ entry->path() },
		entry_index_{ entry->parentDir()->entryIndex(entry) },
		old_name_{ entry->name() },
		new_name_{ new_name }
	{
	}

	bool doUndo() override
	{
		// Get entry parent dir
		auto dir = archive_->dir(entry_path_);
		if (dir)
		{
			// Rename entry
			auto entry = dir->entryAt(entry_index_);
			return archive_->renameEntry(entry, old_name_);
		}

		return false;
	}

	bool doRedo() override
	{
		// Get entry parent dir
		auto dir = archive_->dir(entry_path_);
		if (dir)
		{
			// Rename entry
			auto entry = dir->entryAt(entry_index_);
			return archive_->renameEntry(entry, new_name_);
		}

		return false;
	}

private:
	Archive*    archive_;
	std::string entry_path_;
	int         entry_index_;
	std::string old_name_;
	std::string new_name_;
};

class DirRenameUS : public UndoStep
{
public:
	DirRenameUS(ArchiveTreeNode* dir, std::string_view new_name) :
		archive_{ dir->archive() },
		path_{ dir->parent()->path().append(new_name) },
		old_name_{ dir->name() },
		new_name_{ new_name },
		prev_state_{ dir->dirEntry()->state() }
	{
	}

	void swapNames()
	{
		auto dir = archive_->dir(path_);
		archive_->renameDir(dir, old_name_);
		old_name_ = new_name_;
		new_name_ = dir->name();
		path_     = dir->path();
	}

	bool doUndo() override
	{
		swapNames();
		archive_->dir(path_)->dirEntry()->setState(prev_state_);
		return true;
	}

	bool doRedo() override
	{
		swapNames();
		return true;
	}

private:
	Archive*            archive_;
	std::string         path_;
	std::string         old_name_;
	std::string         new_name_;
	ArchiveEntry::State prev_state_;
};

class EntrySwapUS : public UndoStep
{
public:
	EntrySwapUS(ArchiveTreeNode* dir, unsigned index1, unsigned index2) :
		archive_{ dir->archive() },
		path_{ dir->path() },
		index1_{ index1 },
		index2_{ index2 }
	{
	}

	bool doSwap() const
	{
		// Get parent dir
		auto dir = archive_->dir(path_);
		if (dir)
			return dir->swapEntries(index1_, index2_);
		return false;
	}

	bool doUndo() override { return doSwap(); }
	bool doRedo() override { return doSwap(); }

private:
	Archive*    archive_;
	std::string path_;
	unsigned    index1_;
	unsigned    index2_;
};

class EntryCreateDeleteUS : public UndoStep
{
public:
	EntryCreateDeleteUS(bool created, ArchiveEntry* entry) :
		created_{ created },
		archive_{ entry->parent() },
		entry_copy_{ new ArchiveEntry(*entry) },
		path_{ entry->path() },
		index_{ (unsigned)entry->parentDir()->entryIndex(entry) }
	{
	}

	bool deleteEntry() const
	{
		// Get parent dir
		auto dir = archive_->dir(path_);
		return dir ? archive_->removeEntry(dir->entryAt(index_)) : false;
	}

	bool createEntry() const
	{
		// Get parent dir
		auto dir = archive_->dir(path_);
		if (dir)
		{
			archive_->addEntry(entry_copy_.get(), index_, dir, true);
			return true;
		}

		return false;
	}

	bool doUndo() override { return created_ ? deleteEntry() : createEntry(); }

	bool doRedo() override { return !created_ ? deleteEntry() : createEntry(); }

private:
	bool               created_;
	Archive*           archive_;
	ArchiveEntry::UPtr entry_copy_;
	std::string        path_;
	unsigned           index_;
};


class DirCreateDeleteUS : public UndoStep
{
public:
	DirCreateDeleteUS(bool created, ArchiveTreeNode* dir) :
		created_{ created },
		archive_{ dir->archive() },
		path_{ dir->path() }
	{
		StrUtil::removePrefixIP(path_, '/');

		// Backup child entries and subdirs if deleting
		if (!created)
		{
			tree_ = std::make_unique<ArchiveTreeNode>();

			// Get child entries
			vector<ArchiveEntry*> entries;
			for (unsigned a = 0; a < dir->numEntries(); a++)
				entries.push_back(dir->entryAt(a));

			// Get subdirectories
			vector<ArchiveTreeNode*> subdirs;
			for (unsigned a = 0; a < dir->nChildren(); a++)
				subdirs.push_back(dynamic_cast<ArchiveTreeNode*>(dir->child(a)));

			// Copy entries
			for (auto& entry : entries)
				tree_->addEntry(new ArchiveEntry(*entry));

			// Copy dirs
			for (auto& subdir : subdirs)
				tree_->addChild(subdir->clone());
		}
	}

	bool doUndo() override
	{
		if (created_)
			return archive_->removeDir(path_);
		else
		{
			// Create directory
			auto dir = archive_->createDir(path_);

			// Restore entries/subdirs if needed
			if (dir && tree_)
				dir->merge(tree_.get(), 0, ArchiveEntry::State::Unmodified);

			if (dir)
				dir->dirEntry()->setState(ArchiveEntry::State::Unmodified);

			return !!dir;
		}
	}

	bool doRedo() override
	{
		if (!created_)
			return archive_->removeDir(path_);
		else
			return archive_->createDir(path_) != nullptr;
	}

private:
	bool                             created_;
	Archive*                         archive_;
	std::string                      path_;
	std::unique_ptr<ArchiveTreeNode> tree_;
};


// -----------------------------------------------------------------------------
//
// Archive Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Archive class constructor
// -----------------------------------------------------------------------------
Archive::Archive(std::string_view format) :
	format_{ format },
	parent_{ nullptr },
	on_disk_{ false },
	read_only_{ false },
	modified_{ true },
	dir_root_{ nullptr, this }
{
}

// -----------------------------------------------------------------------------
// Archive class destructor
// -----------------------------------------------------------------------------
Archive::~Archive()
{
	if (parent_)
		parent_->unlock();
}

// -----------------------------------------------------------------------------
// Returns the ArchiveFormat descriptor for this archive
// -----------------------------------------------------------------------------
ArchiveFormat Archive::formatDesc() const
{
	for (auto fmt : formats)
		if (fmt.id == format_)
			return fmt;

	return ArchiveFormat("unknown");
}

// -----------------------------------------------------------------------------
// Gets the wxWidgets file dialog filter string for the archive type
// -----------------------------------------------------------------------------
std::string Archive::fileExtensionString() const
{
	auto fmt = formatDesc();

	// Multiple extensions
	if (fmt.extensions.size() > 1)
	{
		auto                ext_all = fmt::format("Any {} File|", fmt.name);
		vector<std::string> ext_strings;
		for (const auto& ext : fmt.extensions)
		{
			auto ext_case = fmt::format("*.{};", StrUtil::lower(ext.first));
			ext_case += fmt::format("*.{};", StrUtil::upper(ext.first));
			ext_case += fmt::format("*.{}", StrUtil::capitalize(ext.first));

			ext_all += fmt::format("{};", ext_case);
			ext_strings.push_back(fmt::format("{} File (*.{})|{}", ext.second, ext.first, ext_case));
		}

		ext_all.pop_back();
		for (const auto& ext : ext_strings)
			ext_all += fmt::format("|{}", ext);

		return ext_all;
	}

	// Single extension
	if (fmt.extensions.size() == 1)
	{
		auto& ext      = fmt.extensions[0];
		auto  ext_case = fmt::format("*.{};", StrUtil::lower(ext.first));
		ext_case += fmt::format("*.{};", StrUtil::upper(ext.first));
		ext_case += fmt::format("*.{}", StrUtil::capitalize(ext.first));

		return fmt::format("{} File (*.{})|{}", ext.second, ext.first, ext_case);
	}

	// No extension (probably unknown type)
	return "Any File|*.*";
}

// -----------------------------------------------------------------------------
// Returns the archive's filename, including the path if specified
// -----------------------------------------------------------------------------
std::string Archive::filename(bool full) const
{
	// If the archive is within another archive, return "<parent archive>/<entry name>"
	if (parent_)
	{
		std::string parent_archive;
		if (parentArchive())
			parent_archive = parentArchive()->filename(false) + "/";

		return parent_archive.append(StrUtil::Path::fileNameOf(parent_->name(), false));
	}

	return full ? filename_ : std::string{ StrUtil::Path::fileNameOf(filename_) };
}

// -----------------------------------------------------------------------------
// Reads an archive from disk
// Returns true if successful, false otherwise
// -----------------------------------------------------------------------------
bool Archive::open(std::string_view filename)
{
	// Read the file into a MemChunk
	MemChunk mc;
	if (!mc.importFile(filename))
	{
		Global::error = "Unable to open file. Make sure it isn't in use by another program.";
		return false;
	}

	// Update filename before opening
	auto backupname = filename_;
	filename_       = filename;

	// Load from MemChunk
	sf::Clock timer;
	if (open(mc))
	{
		Log::info(2, "Archive::open took {}ms", timer.getElapsedTime().asMilliseconds());
		on_disk_ = true;
		return true;
	}
	else
	{
		filename_ = backupname;
		return false;
	}
}

// -----------------------------------------------------------------------------
// Reads an archive from an ArchiveEntry
// Returns true if successful, false otherwise
// -----------------------------------------------------------------------------
bool Archive::open(ArchiveEntry* entry)
{
	// Load from entry's data
	if (entry && open(entry->data()))
	{
		// Update variables and return success
		parent_ = entry;
		parent_->lock();
		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Sets the Archive's modified status and announces the change
// -----------------------------------------------------------------------------
void Archive::setModified(bool modified)
{
	// Set modified
	modified_ = modified;

	// Announce
	announce("modified");
}

// -----------------------------------------------------------------------------
// Checks that the given entry is valid and part of this archive
// -----------------------------------------------------------------------------
bool Archive::checkEntry(ArchiveEntry* entry)
{
	// Check entry is valid
	if (!entry)
		return false;

	// Check entry is part of this archive
	if (entry->parent() != this)
		return false;

	// Entry is ok
	return true;
}

// -----------------------------------------------------------------------------
// Returns the entry matching [name] within [dir].
// If no dir is given the root dir is used
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::entry(std::string_view name, bool cut_ext, ArchiveTreeNode* dir)
{
	// Check if dir was given
	if (!dir)
		dir = &dir_root_; // None given, use root

	return dir->entry(name, cut_ext);
}

// -----------------------------------------------------------------------------
// Returns the entry at [index] within [dir].
// If no dir is given the  root dir is used.
// Returns null if [index] is out of bounds
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::entryAt(unsigned index, ArchiveTreeNode* dir)
{
	// Check if dir was given
	if (!dir)
		dir = &dir_root_; // None given, use root

	return dir->entryAt(index);
}

// -----------------------------------------------------------------------------
// Returns the index of [entry] within [dir].
// If no dir is given the root dir is used.
// Returns -1 if [entry] doesn't exist within [dir]
// -----------------------------------------------------------------------------
int Archive::entryIndex(ArchiveEntry* entry, ArchiveTreeNode* dir)
{
	// Check if dir was given
	if (!dir)
		dir = &dir_root_; // None given, use root

	return dir->entryIndex(entry);
}

// -----------------------------------------------------------------------------
// Returns the entry at the given path in the archive, or null if it doesn't
// exist
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::entryAtPath(std::string_view path)
{
	// Get path as wxFileName for processing
	StrUtil::Path fn(StrUtil::startsWith(path, '/') ? path.substr(1) : path);

	// Get directory from path
	ArchiveTreeNode* dir;
	if (fn.path(false).empty())
		dir = &dir_root_;
	else
		dir = this->dir(std::string{ fn.path(true) });

	// If dir doesn't exist, return nullptr
	if (!dir)
		return nullptr;

	// Return entry
	return dir->entry(fn.fileName());
}

// -----------------------------------------------------------------------------
// Returns the entry at the given path in the archive, or null if it doesn't
// exist
// -----------------------------------------------------------------------------
ArchiveEntry::SPtr Archive::entryAtPathShared(std::string_view path)
{
	// Get path as wxFileName for processing
	StrUtil::Path fn(StrUtil::startsWith(path, '/') ? path.substr(1) : path);

	// Get directory from path
	ArchiveTreeNode* dir;
	if (fn.path(false).empty())
		dir = &dir_root_;
	else
		dir = this->dir(std::string{ fn.path(true) });

	// If dir doesn't exist, return nullptr
	if (!dir)
		return nullptr;

	// Return entry
	return dir->sharedEntry(fn.fileName());
}

// -----------------------------------------------------------------------------
// Writes the archive to a file
// Returns true if successful, false otherwise
// -----------------------------------------------------------------------------
bool Archive::write(std::string_view filename, bool update)
{
	// Write to a MemChunk, then export it to a file
	MemChunk mc;
	if (write(mc, true))
		return mc.exportFile(filename);
	else
		return false;
}

// -----------------------------------------------------------------------------
// This is the general, all-purpose 'save archive' function.
// Takes into account whether the archive is contained within another, is
// already on the disk, etc etc.
// Does a 'save as' if [filename] is specified, unless the archive is contained
// within another.
// Returns false if saving was unsuccessful, true otherwise
// -----------------------------------------------------------------------------
bool Archive::save(std::string_view filename)
{
	bool success = false;

	// Check if the archive is read-only
	if (read_only_)
	{
		Global::error = "Archive is read-only";
		return false;
	}

	// If the archive has a parent ArchiveEntry, just write it to that
	if (parent_)
	{
		success = write(parent_->data());
		parent_->setState(ArchiveEntry::State::Modified);
	}
	else
	{
		// Otherwise, file stuff
		if (!filename.empty())
		{
			// New filename is given (ie 'save as'), write to new file and change archive filename accordingly
			success = write(filename);
			if (success)
				filename_ = filename;

			// Update variables
			on_disk_ = true;
		}
		else if (!filename_.empty())
		{
			// No filename is given, but the archive has a filename, so overwrite it (and make a backup)

			// Create backup
			if (backup_archives && wxFileName::FileExists(filename_) && save_backup)
			{
				// Copy current file contents to new backup file
				wxString bakfile = filename_ + ".bak";
				Log::info(wxString::Format("Creating backup %s", bakfile));
				wxCopyFile(filename_, bakfile, true);
			}

			// Write it to the file
			success = write(filename_);

			// Update variables
			on_disk_ = true;
		}
	}

	// If saving was successful, update variables and announce save
	if (success)
	{
		setModified(false);
		announce("saved");
	}

	return success;
}

// -----------------------------------------------------------------------------
// Returns the total number of entries in the archive
// -----------------------------------------------------------------------------
unsigned Archive::numEntries()
{
	return dir_root_.numEntries(true);
}

// -----------------------------------------------------------------------------
// 'Closes' the archive
// -----------------------------------------------------------------------------
void Archive::close()
{
	// Announce
	announce("closing");

	// Clear the root dir
	dir_root_.clear();

	// Unlock parent entry if it exists
	if (parent_)
		parent_->unlock();

	// Announce
	announce("closed");
}

// -----------------------------------------------------------------------------
// Updates the archive variables and announces if necessary that an entry's
// state has changed
// -----------------------------------------------------------------------------
void Archive::entryStateChanged(ArchiveEntry* entry)
{
	// Check the entry is valid and part of this archive
	if (!checkEntry(entry))
		return;

	// Get the entry index and announce the change
	MemChunk  mc(8);
	wxUIntPtr ptr   = wxPtrToUInt(entry);
	uint32_t  index = entryIndex(entry);
	mc.write(&index, sizeof(uint32_t));
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("entry_state_changed", mc);


	// If entry was set to unmodified, don't set the archive to modified
	if (entry->state() == ArchiveEntry::State::Unmodified)
		return;

	// Set the archive state to modified
	setModified(true);
}

// -----------------------------------------------------------------------------
// Adds the directory structure starting from [start] to [list]
// -----------------------------------------------------------------------------
void Archive::putEntryTreeAsList(vector<ArchiveEntry*>& list, ArchiveTreeNode* start)
{
	// If no start dir is specified, use the root dir
	if (!start)
		start = &dir_root_;

	// Add the directory entry to the list if it isn't the root dir
	if (start != &dir_root_)
		list.push_back(start->dir_entry_.get());

	// Add all entries to the list
	for (unsigned a = 0; a < start->numEntries(); a++)
		list.push_back(start->entryAt(a));

	// Go through subdirectories and add them to the list
	for (unsigned a = 0; a < start->nChildren(); a++)
		putEntryTreeAsList(list, (ArchiveTreeNode*)start->child(a));
}

// -----------------------------------------------------------------------------
// Adds the directory structure starting from [start] to [list]
// -----------------------------------------------------------------------------
void Archive::putEntryTreeAsList(vector<ArchiveEntry::SPtr>& list, ArchiveTreeNode* start)
{
	// If no start dir is specified, use the root dir
	if (!start)
		start = &dir_root_;

	// Add the directory entry to the list if it isn't the root dir
	if (start != &dir_root_)
		list.push_back(start->dir_entry_);

	// Add all entries to the list
	for (unsigned a = 0; a < start->numEntries(); a++)
		list.push_back(start->sharedEntryAt(a));

	// Go through subdirectories and add them to the list
	for (unsigned a = 0; a < start->nChildren(); a++)
		putEntryTreeAsList(list, (ArchiveTreeNode*)start->child(a));
}

// -----------------------------------------------------------------------------
// 'Pastes' the [tree] into the archive, with its root entries starting at
// [position] in [base] directory.
// If [base] is null, the root directory is used
// -----------------------------------------------------------------------------
bool Archive::paste(ArchiveTreeNode* tree, unsigned position, ArchiveTreeNode* base)
{
	// Check tree was given to paste
	if (!tree)
		return false;

	// Paste to root dir if none specified
	if (!base)
		base = &dir_root_;

	// Set modified
	setModified(true);

	// Just do a merge
	return base->merge(tree, position);
}

// -----------------------------------------------------------------------------
// Gets the directory matching [path], starting from [base].
// If [base] is null, the root directory is used.
// Returns null if the requested directory does not exist
// -----------------------------------------------------------------------------
ArchiveTreeNode* Archive::dir(std::string_view path, ArchiveTreeNode* base)
{
	// Check if base dir was given
	if (!base)
	{
		// None given, use root
		base = &dir_root_;

		// If empty directory, just return the root
		if (path == "/" || path.empty())
			return &dir_root_;
	}

	return (ArchiveTreeNode*)base->child(std::string{ StrUtil::startsWith(path, '/') ? path.substr(1) : path });
}

// -----------------------------------------------------------------------------
// Creates a directory at [path], starting from [base].
// If [base] is null, the root directory is used.
// Returns the created directory, or if the directory requested to be created
// already exists, it will be returned
// -----------------------------------------------------------------------------
ArchiveTreeNode* Archive::createDir(std::string_view path, ArchiveTreeNode* base)
{
	// Abort if read only
	if (read_only_)
		return &dir_root_;

	// If no base dir specified, set it to root
	if (!base)
		base = &dir_root_;

	if (path.empty())
		return base;

	// Create the directory
	auto dir = (ArchiveTreeNode*)((STreeNode*)base)->addChild(std::string{ path });

	// Record undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<DirCreateDeleteUS>(true, dir));

	// Set the archive state to modified
	setModified(true);

	// Announce
	MemChunk  mc;
	wxUIntPtr ptr = wxPtrToUInt(dir);
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("directory_added", mc);

	return dir;
}

// -----------------------------------------------------------------------------
// Deletes the directory matching [path], starting from [base].
// If [base] is null, the root directory is used.
// Returns false if the directory does not exist, true otherwise
// -----------------------------------------------------------------------------
bool Archive::removeDir(std::string_view path, ArchiveTreeNode* base)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Get the dir to remove
	auto dir = this->dir(path, base);

	// Check it exists (and that it isn't the root dir)
	if (!dir || dir == &dir_root_)
		return false;

	// Record undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<DirCreateDeleteUS>(false, dir));

	// Remove the directory from its parent
	if (dir->parent())
		dir->parent()->removeChild(dir);

	// Delete the directory
	delete dir;

	// Set the archive state to modified
	setModified(true);

	return true;
}

// -----------------------------------------------------------------------------
// Renames [dir] to [new_name].
// Returns false if [dir] isn't part of the archive, true otherwise
// -----------------------------------------------------------------------------
bool Archive::renameDir(ArchiveTreeNode* dir, std::string_view new_name)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Check the directory is part of this archive
	if (dir->archive() != this)
		return false;

	// Rename the directory if needed
	if (dir->name() == new_name)
	{
		if (UndoRedo::currentlyRecording())
			UndoRedo::currentManager()->recordUndoStep(std::make_unique<DirRenameUS>(dir, new_name));

		dir->setName(std::string{ new_name });
		dir->dirEntry()->setState(ArchiveEntry::State::Modified);
	}
	else
		return true;

	// Announce
	MemChunk  mc;
	wxUIntPtr ptr = wxPtrToUInt(dir);
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("directory_modified", mc);

	// Update variables etc
	setModified(true);

	return true;
}

// -----------------------------------------------------------------------------
// Adds [entry] to [dir] at [position].
// If [dir] is null it is added to the root dir.
// If [position] is out of bounds, it is added to the end of the dir.
// If [copy] is true, a copy of [entry] is added (rather than [entry] itself).
// Returns the added entry, or null if [entry] is invalid or the archive is
// read-only
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::addEntry(ArchiveEntry* entry, unsigned position, ArchiveTreeNode* dir, bool copy)
{
	// Abort if read only
	if (read_only_)
		return nullptr;

	// Check valid entry
	if (!entry)
		return nullptr;

	// If no dir given, set it to the root dir
	if (!dir)
		dir = &dir_root_;

	// Make a copy of the entry to add if needed
	if (copy)
		entry = new ArchiveEntry(*entry);

	// Add the entry
	dir->addEntry(entry, position);
	entry->formatName(formatDesc());

	// Update variables etc
	setModified(true);
	entry->state_ = ArchiveEntry::State::New;

	// Announce
	MemChunk  mc;
	wxUIntPtr ptr = wxPtrToUInt(entry);
	mc.write(&position, sizeof(uint32_t));
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("entry_added", mc);

	// Create undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<EntryCreateDeleteUS>(true, entry));

	return entry;
}

// -----------------------------------------------------------------------------
// Creates a new entry with [name] and adds it to [dir] at [position].
// If [dir] is null it is added to the root dir.
// If [position] is out of bounds, it is added tothe end of the dir.
// Return false if the entry is invalid, true otherwise
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::addNewEntry(std::string_view name, unsigned position, ArchiveTreeNode* dir)
{
	// Abort if read only
	if (read_only_)
		return nullptr;

	// Create the new entry
	auto entry = new ArchiveEntry(name);

	// Add it to the archive
	addEntry(entry, position, dir);

	// Return the newly created entry
	return entry;
}

// -----------------------------------------------------------------------------
// Creates a new entry with [name] and adds it to [namespace] within the archive
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::addNewEntry(std::string_view name, std::string_view add_namespace)
{
	// Abort if read only
	if (read_only_)
		return nullptr;

	// Create the new entry
	auto entry = new ArchiveEntry(name);

	// Add it to the archive
	addEntry(entry, add_namespace);

	// Return the newly created entry
	return entry;
}

// -----------------------------------------------------------------------------
// Removes [entry] from the archive.
// If [delete_entry] is true, the entry will also be deleted.
// Returns true if the removal succeeded
// -----------------------------------------------------------------------------
bool Archive::removeEntry(ArchiveEntry* entry)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Check entry
	if (!checkEntry(entry))
		return false;

	// Check if entry is locked
	if (entry->isLocked())
		return false;

	// Get its directory
	auto dir = entry->parentDir();

	// Error if entry has no parent directory
	if (!dir)
		return false;

	// Create undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<EntryCreateDeleteUS>(false, entry));

	// Get the entry index
	int index = dir->entryIndex(entry);

	// Announce (before actually removing in case entry is still needed)
	MemChunk  mc;
	wxUIntPtr ptr = wxPtrToUInt(entry);
	mc.write(&index, sizeof(int));
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("entry_removing", mc);

	// Remove it from its directory
	bool ok = dir->removeEntry(index);

	// If it was removed ok
	if (ok)
	{
		// Announce removed
		announce("entry_removed", mc);

		// Update variables etc
		setModified(true);
	}

	return ok;
}

// -----------------------------------------------------------------------------
// Swaps the entries at [index1] and [index2] in [dir].
// If [dir] is not specified, the root dir is used.
// Returns true if the swap succeeded, false otherwise
// -----------------------------------------------------------------------------
bool Archive::swapEntries(unsigned index1, unsigned index2, ArchiveTreeNode* dir)
{
	// Get directory
	if (!dir)
		dir = &dir_root_;

	// Check if either entry is locked
	if (dir->entryAt(index1)->isLocked() || dir->entryAt(index2)->isLocked())
		return false;

	// Create undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<EntrySwapUS>(dir, index1, index2));

	// Do swap
	if (dir->swapEntries(index1, index2))
	{
		// Announce the swap
		announce("entries_swapped");

		// Set modified
		setModified(true);

		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Swaps [entry1] and [entry2].
// Returns false if either entry is invalid or if both entries are not in the
// same directory, true otherwise
// -----------------------------------------------------------------------------
bool Archive::swapEntries(ArchiveEntry* entry1, ArchiveEntry* entry2)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Check both entries
	if (!checkEntry(entry1) || !checkEntry(entry2))
		return false;

	// Check neither entry is locked
	if (entry1->isLocked() || entry2->isLocked())
		return false;

	// Get their directory
	auto dir = entry1->parentDir();

	// Error if no dir
	if (!dir)
		return false;

	// Check they are both in the same directory
	if (entry2->parentDir() != dir)
	{
		Log::error("Can't swap two entries in different directories");
		return false;
	}

	// Get entry indices
	int i1 = dir->entryIndex(entry1);
	int i2 = dir->entryIndex(entry2);

	// Check indices
	if (i1 < 0 || i2 < 0)
		return false;

	// Create undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<EntrySwapUS>(dir, i1, i2));

	// Swap entries
	dir->swapEntries(i1, i2);

	// Announce the swap
	announce("entries_swapped");

	// Set modified
	setModified(true);

	// Return success
	return true;
}

// -----------------------------------------------------------------------------
// Moves [entry] to [position] in [dir].
// If [dir] is null, the root dir is used.
// Returns false if the entry was invalid, true otherwise
// -----------------------------------------------------------------------------
bool Archive::moveEntry(ArchiveEntry* entry, unsigned position, ArchiveTreeNode* dir)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Check the entry
	if (!checkEntry(entry))
		return false;

	// Check if the entry is locked
	if (entry->isLocked())
		return false;

	// Get the entry's current directory
	auto cdir = entry->parentDir();

	// Error if no dir
	if (!cdir)
		return false;

	// If no destination dir specified, use root
	if (!dir)
		dir = &dir_root_;

	// Remove the entry from its current dir
	auto sptr = dir->sharedEntryAt(dir->entryIndex(entry)); // Get a shared pointer so it isn't deleted
	removeEntry(entry);

	// Add it to the destination dir
	addEntry(entry, position, dir);

	// Set modified
	setModified(true);

	// Return success
	return true;
}

// -----------------------------------------------------------------------------
// Renames [entry] with [name].
// Returns false if the entry was invalid, true otherwise
// -----------------------------------------------------------------------------
bool Archive::renameEntry(ArchiveEntry* entry, std::string_view name)
{
	// Abort if read only
	if (read_only_)
		return false;

	// Check entry
	if (!checkEntry(entry))
		return false;

	// Check if entry is locked
	if (entry->isLocked())
		return false;

	// Check for directory
	if (entry->type() == EntryType::folderType())
		return renameDir(dir(entry->path(true)), name);

	// Announce (before actually renaming in case old name is still needed)
	MemChunk  mc;
	int       index = entryIndex(entry);
	wxUIntPtr ptr   = wxPtrToUInt(entry);
	mc.write(&index, sizeof(int));
	mc.write(&ptr, sizeof(wxUIntPtr));
	announce("entry_renaming", mc);

	// Create undo step
	if (UndoRedo::currentlyRecording())
		UndoRedo::currentManager()->recordUndoStep(std::make_unique<EntryRenameUS>(entry, name));

	// Rename the entry
	entry->setName(name);
	entry->formatName(formatDesc());
	entry->setState(ArchiveEntry::State::Modified, true);

	// Announce modification
	entryStateChanged(entry);

	return true;
}

// -----------------------------------------------------------------------------
// Imports all files (including subdirectories) from [directory] into the
// archive
// -----------------------------------------------------------------------------
bool Archive::importDir(std::string_view directory)
{
	// Get a list of all files in the directory
	vector<std::string> files;
	for (const auto& item : std::filesystem::recursive_directory_iterator{ directory })
		if (item.is_regular_file())
			files.push_back(item.path().string());

	// Go through files
	for (const auto& file : files)
	{
		StrUtil::Path fn{ StrUtil::replace(file, directory, "") }; // Remove directory from entry name

		// Split filename into dir+name
		auto ename = fn.fileName();
		auto edir  = fn.path();

		// Remove beginning \ or / from dir
		if (StrUtil::startsWith(edir, '\\') || StrUtil::startsWith(edir, '/'))
			edir.remove_prefix(1);

		// Add the entry
		auto dir   = createDir(edir);
		auto entry = addNewEntry(ename, dir->numEntries() + 1, dir);

		// Load data
		entry->importFile(file);

		// Set unmodified
		entry->setState(ArchiveEntry::State::Unmodified);
		dir->dirEntry()->setState(ArchiveEntry::State::Unmodified);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Reverts [entry] to the data it contained at the last time the archive was
// saved.
// Returns false if entry was invalid, true otherwise
// -----------------------------------------------------------------------------
bool Archive::revertEntry(ArchiveEntry* entry)
{
	// Check entry
	if (!checkEntry(entry))
		return false;

	// Check if entry is locked
	if (entry->isLocked())
		return false;

	// No point if entry is unmodified or newly created
	if (entry->state() != ArchiveEntry::State::Modified)
		return true;

	// Reload entry data from the archive on disk
	entry->setState(ArchiveEntry::State::Unmodified);
	entry->unloadData();
	if (loadEntryData(entry))
	{
		EntryType::detectEntryType(entry);
		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Returns the namespace of the entry at [index] within [dir]
// -----------------------------------------------------------------------------
std::string Archive::detectNamespace(size_t index, ArchiveTreeNode* dir)
{
	if (dir && index < dir->numEntries())
		return detectNamespace(dir->entryAt(index));

	return "global";
}

// -----------------------------------------------------------------------------
// Returns the namespace that [entry] is within
// -----------------------------------------------------------------------------
std::string Archive::detectNamespace(ArchiveEntry* entry)
{
	// Check entry
	if (!checkEntry(entry))
		return "global";

	// If the entry is in the root dir, it's in the global namespace
	if (entry->parentDir() == &dir_root_)
		return "global";

	// Get the entry's *first* parent directory after root (ie <root>/namespace/)
	auto dir = entry->parentDir();
	while (dir && dir->parent() != &dir_root_)
		dir = (ArchiveTreeNode*)dir->parent();

	// Namespace is the directory's name (in lowercase)
	if (dir)
		return StrUtil::lower(dir->name());
	else
		return "global"; // Error, just return global
}

// -----------------------------------------------------------------------------
// Returns the first entry matching the search criteria in [options], or null if
// no matching entry was found
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::findFirst(SearchOptions& options)
{
	// Init search variables
	auto dir = options.dir;
	if (!dir)
		dir = &dir_root_;
	StrUtil::upperIP(options.match_name); // Force case-insensitive

	// Begin search

	// Search entries
	for (unsigned a = 0; a < dir->numEntries(); a++)
	{
		auto entry = dir->entryAt(a);

		// Check type
		if (options.match_type)
		{
			if (entry->type() == EntryType::unknownType())
			{
				if (!options.match_type->isThisType(entry))
					continue;
			}
			else if (options.match_type != entry->type())
				continue;
		}

		// Check name
		if (!options.match_name.empty())
		{
			// Cut extension if ignoring
			auto check_name = options.ignore_ext ? entry->upperNameNoExt() : entry->upperName();
			if (!StrUtil::matches(check_name, options.match_name))
				continue;
		}

		// Check namespace
		if (!options.match_namespace.empty())
		{
			if (!StrUtil::equalCI(detectNamespace(entry), options.match_namespace))
				continue;
		}

		// Entry passed all checks so far, so we found a match
		return entry;
	}

	// Search subdirectories (if needed)
	if (options.search_subdirs)
	{
		for (unsigned a = 0; a < dir->nChildren(); a++)
		{
			auto opt   = options;
			opt.dir    = (ArchiveTreeNode*)dir->child(a);
			auto match = findFirst(opt);

			// If a match was found in this subdir, return it
			if (match)
				return match;
		}
	}

	// No matches found
	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns the last entry matching the search criteria in [options], or null if
// no matching entry was found
// -----------------------------------------------------------------------------
ArchiveEntry* Archive::findLast(SearchOptions& options)
{
	// Init search variables
	auto dir = options.dir;
	if (!dir)
		dir = &dir_root_;
	StrUtil::upperIP(options.match_name); // Force case-insensitive

	// Begin search

	// Search subdirectories (if needed) (bottom-up)
	if (options.search_subdirs)
	{
		for (int a = dir->nChildren() - 1; a >= 0; a--)
		{
			auto opt   = options;
			opt.dir    = (ArchiveTreeNode*)dir->child(a);
			auto match = findLast(opt);

			// If a match was found in this subdir, return it
			if (match)
				return match;
		}
	}

	// Search entries (bottom-up)
	for (int a = dir->numEntries() - 1; a >= 0; a--)
	{
		auto entry = dir->entryAt(a);

		// Check type
		if (options.match_type)
		{
			if (entry->type() == EntryType::unknownType())
			{
				if (!options.match_type->isThisType(entry))
					continue;
			}
			else if (options.match_type != entry->type())
				continue;
		}

		// Check name
		if (!options.match_name.empty())
		{
			// Cut extension if ignoring
			auto check_name = options.ignore_ext ? entry->upperNameNoExt() : entry->upperName();
			if (!StrUtil::matches(check_name, options.match_name))
				continue;
		}

		// Check namespace
		if (!options.match_namespace.empty())
		{
			if (!StrUtil::equalCI(detectNamespace(entry), options.match_namespace))
				continue;
		}

		// Entry passed all checks so far, so we found a match
		return entry;
	}

	// No matches found
	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns a list of entries matching the search criteria in [options]
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> Archive::findAll(SearchOptions& options)
{
	// Init search variables
	auto dir = options.dir;
	if (!dir)
		dir = &dir_root_;
	vector<ArchiveEntry*> ret;
	StrUtil::upperIP(options.match_name); // Force case-insensitive

	// Begin search

	// Search entries
	for (unsigned a = 0; a < dir->numEntries(); a++)
	{
		auto entry = dir->entryAt(a);

		// Check type
		if (options.match_type)
		{
			if (entry->type() == EntryType::unknownType())
			{
				if (!options.match_type->isThisType(entry))
					continue;
			}
			else if (options.match_type != entry->type())
				continue;
		}

		// Check name
		if (!options.match_name.empty())
		{
			// Cut extension if ignoring
			auto check_name = options.ignore_ext ? entry->upperNameNoExt() : entry->upperName();
			if (!StrUtil::matches(check_name, options.match_name))
				continue;
		}

		// Check namespace
		if (!options.match_namespace.empty())
		{
			if (!StrUtil::equalCI(detectNamespace(entry), options.match_namespace))
				continue;
		}

		// Entry passed all checks so far, so we found a match
		ret.push_back(entry);
	}

	// Search subdirectories (if needed)
	if (options.search_subdirs)
	{
		for (unsigned a = 0; a < dir->nChildren(); a++)
		{
			auto opt = options;
			opt.dir  = (ArchiveTreeNode*)dir->child(a);

			// Add any matches to the list
			auto vec = findAll(opt);
			ret.insert(ret.end(), vec.begin(), vec.end());
		}
	}

	// Return matches
	return ret;
}

// -----------------------------------------------------------------------------
// Returns a list of modified entries, and set archive to unmodified status if
// the list is empty
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> Archive::findModifiedEntries(ArchiveTreeNode* dir)
{
	// Init search variables
	if (dir == nullptr)
		dir = &dir_root_;
	vector<ArchiveEntry*> ret;

	// Search entries
	for (unsigned a = 0; a < dir->numEntries(); a++)
	{
		auto entry = dir->entryAt(a);

		// Add new and modified entries
		if (entry->state() != ArchiveEntry::State::Unmodified)
			ret.push_back(entry);
	}

	// Search subdirectories
	for (unsigned a = 0; a < dir->nChildren(); a++)
	{
		auto vec = findModifiedEntries((ArchiveTreeNode*)dir->child(a));
		ret.insert(ret.end(), vec.begin(), vec.end());
	}

	// If there aren't actually any, set archive to be unmodified
	if (ret.empty())
		setModified(false);

	// Return matches
	return ret;
}


// -----------------------------------------------------------------------------
//
// Archive Class Static Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Reads archive formats configuration file from [mc]
// -----------------------------------------------------------------------------
bool Archive::loadFormats(MemChunk& mc)
{
	Parser parser;
	if (!parser.parseText(mc))
		return false;

	auto root         = parser.parseTreeRoot();
	auto formats_node = root->child("archive_formats");
	for (unsigned a = 0; a < formats_node->nChildren(); a++)
	{
		auto          fmt_desc = (ParseTreeNode*)formats_node->child(a);
		ArchiveFormat fmt{ fmt_desc->name() };

		for (unsigned p = 0; p < fmt_desc->nChildren(); p++)
		{
			auto prop = (ParseTreeNode*)fmt_desc->child(p);

			// Format name
			if (StrUtil::equalCI(prop->name(), "name"))
				fmt.name = prop->stringValue();

			// Supports dirs
			else if (StrUtil::equalCI(prop->name(), "supports_dirs"))
				fmt.supports_dirs = prop->boolValue();

			// Entry names have extensions
			else if (StrUtil::equalCI(prop->name(), "names_extensions"))
				fmt.names_extensions = prop->boolValue();

			// Max entry name length
			else if (StrUtil::equalCI(prop->name(), "max_name_length"))
				fmt.max_name_length = prop->intValue();

			// Entry format (id)
			else if (StrUtil::equalCI(prop->name(), "entry_format"))
				fmt.entry_format = prop->stringValue();

			// Extensions
			else if (StrUtil::equalCI(prop->name(), "extensions"))
			{
				for (unsigned e = 0; e < prop->nChildren(); e++)
				{
					auto ext = (ParseTreeNode*)prop->child(e);
					fmt.extensions.emplace_back(ext->name(), ext->stringValue());
				}
			}

			// Prefer uppercase entry names
			else if (StrUtil::equalCI(prop->name(), "prefer_uppercase"))
				fmt.prefer_uppercase = prop->boolValue();
		}

		Log::info(3, wxString::Format("Read archive format %s: \"%s\"", fmt.id, fmt.name));
		if (fmt.supports_dirs)
			Log::info(3, "  Supports folders");
		if (fmt.names_extensions)
			Log::info(3, "  Entry names have extensions");
		if (fmt.max_name_length >= 0)
			Log::info(3, wxString::Format("  Max entry name length: %d", fmt.max_name_length));
		for (auto ext : fmt.extensions)
			Log::info(3, wxString::Format("  Extension \"%s\" = \"%s\"", ext.first, ext.second));

		formats.push_back(fmt);
	}

	// Add builtin 'folder' format
	ArchiveFormat fmt_folder("folder");
	fmt_folder.name             = "Folder";
	fmt_folder.names_extensions = true;
	fmt_folder.supports_dirs    = true;
	formats.push_back(fmt_folder);

	return true;
}


// -----------------------------------------------------------------------------
//
// TreelessArchive Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Treeless version of Archive::paste.
// Pastes all entries in [tree] and its subdirectories straight into the root
// dir at [position]
// -----------------------------------------------------------------------------
bool TreelessArchive::paste(ArchiveTreeNode* tree, unsigned position, ArchiveTreeNode* base)
{
	// Check tree was given to paste
	if (!tree)
		return false;

	// Paste root entries only
	for (unsigned a = 0; a < tree->numEntries(); a++)
	{
		// Add entry to archive
		addEntry(tree->entryAt(a), position, nullptr, true);

		// Update [position] if needed
		if (position < 0xFFFFFFFF)
			position++;
	}

	// Go through paste tree subdirs and paste their entries recursively
	for (unsigned a = 0; a < tree->nChildren(); a++)
	{
		auto dir = (ArchiveTreeNode*)tree->child(a);
		paste(dir, position);
	}

	// Set modified
	setModified(true);

	return true;
}
