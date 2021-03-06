
vector<Archive*> allArchives(bool resources_only)
{
	vector<Archive*> list;
	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		auto archive = App::archiveManager().getArchive(a);

		if (resources_only && !App::archiveManager().archiveIsResource(archive))
			continue;

		list.push_back(archive);
	}
	return list;
}

wxString formattedEntryName(ArchiveEntry& self, bool include_path, bool include_extension, bool name_uppercase)
{
	wxString name;
	if (include_path)
		name = self.path();
	if (name_uppercase)
		name += std::string{ include_extension ? self.upperName() : self.upperNameNoExt() };
	else
		name += std::string{ include_extension ? self.name() : self.nameNoExt() };
	return name;
}

vector<ArchiveEntry*> archiveAllEntries(Archive& self)
{
	vector<ArchiveEntry*> list;
	self.putEntryTreeAsList(list);
	return list;
}

ArchiveEntry* archiveCreateEntry(Archive& self, const wxString& full_path, int position)
{
	auto dir = self.dir(StrUtil::beforeLast(full_path.ToStdString(), '/'));
	return self.addNewEntry(StrUtil::afterLast(full_path.ToStdString(), '/'), position, dir);
}

ArchiveEntry* archiveCreateEntryInNamespace(Archive& self, const wxString& name, const wxString& ns)
{
	return self.addNewEntry(name.ToStdString(), ns.ToStdString());
}

vector<ArchiveEntry*> archiveDirEntries(ArchiveTreeNode& self)
{
	vector<ArchiveEntry*> list;
	for (auto e : self.entries())
		list.push_back(e.get());
	return list;
}

vector<ArchiveTreeNode*> archiveDirSubDirs(ArchiveTreeNode& self)
{
	vector<ArchiveTreeNode*> dirs;
	for (auto child : self.allChildren())
		dirs.push_back(dynamic_cast<ArchiveTreeNode*>(child));
	return dirs;
}

void registerArchiveFormat(sol::state& lua)
{
	lua.new_simple_usertype<ArchiveFormat>(
		"ArchiveFormat",

		// No constructor
		"new",
		sol::no_constructor,

		// Properties
		// ---------------------------------------------------------------------------
		"id",
		sol::readonly(&ArchiveFormat::id),

		"name",
		sol::readonly(&ArchiveFormat::name),

		"supportsDirs",
		sol::readonly(&ArchiveFormat::supports_dirs),

		"hasExtensions",
		sol::readonly(&ArchiveFormat::names_extensions),

		"maxNameLength",
		sol::readonly(&ArchiveFormat::max_name_length)

		//"entryFormat",	sol::readonly(&ArchiveFormat::entry_format)
		//"extensions" - need to export key_value_t or do something custom
	);
}

void registerArchive(sol::state& lua)
{
	lua.new_usertype<Archive>(
		"Archive",

		// No constructor
		"new",
		sol::no_constructor,


		// Properties
		// ---------------------------------------------------------------------------
		"filename",
		sol::property([](Archive& self) { return self.filename(); }),

		"entries",
		sol::property(&archiveAllEntries),

		"rootDir",
		sol::property(&Archive::rootDir),

		"format",
		sol::property(&Archive::formatDesc),


		// Functions
		// ---------------------------------------------------------------------------
		"filenameNoPath",
		[](Archive& self) { return self.filename(false); },

		"entryAtPath",
		&Archive::entryAtPath,

		"dirAtPath",
		[](Archive& self, const wxString& path) { return self.dir(path.ToStdString()); },

		"createEntry",
		&archiveCreateEntry,

		"createEntryInNamespace",
		&archiveCreateEntryInNamespace,

		"removeEntry",
		&Archive::removeEntry,

		"renameEntry",
		&Archive::renameEntry,

		"save",
		sol::overload(
			[](Archive& self) { return self.save(); },
			[](Archive& self, const wxString& filename) { return self.save(filename.ToStdString()); }));

// Register all subclasses
// (perhaps it'd be a good idea to make Archive not abstract and handle
//  the format-specific stuff somewhere else, rather than in subclasses)
#define REGISTER_ARCHIVE(type) lua.new_usertype<type>(#type, sol::base_classes, sol::bases<Archive>())
	REGISTER_ARCHIVE(WadArchive);
	REGISTER_ARCHIVE(WadArchive);
	REGISTER_ARCHIVE(ZipArchive);
	REGISTER_ARCHIVE(LibArchive);
	REGISTER_ARCHIVE(DatArchive);
	REGISTER_ARCHIVE(ResArchive);
	REGISTER_ARCHIVE(PakArchive);
	REGISTER_ARCHIVE(BSPArchive);
	REGISTER_ARCHIVE(GrpArchive);
	REGISTER_ARCHIVE(RffArchive);
	REGISTER_ARCHIVE(GobArchive);
	REGISTER_ARCHIVE(LfdArchive);
	REGISTER_ARCHIVE(HogArchive);
	REGISTER_ARCHIVE(ADatArchive);
	REGISTER_ARCHIVE(Wad2Archive);
	REGISTER_ARCHIVE(WadJArchive);
	REGISTER_ARCHIVE(WolfArchive);
	REGISTER_ARCHIVE(GZipArchive);
	REGISTER_ARCHIVE(BZip2Archive);
	REGISTER_ARCHIVE(TarArchive);
	REGISTER_ARCHIVE(DiskArchive);
	REGISTER_ARCHIVE(PodArchive);
	REGISTER_ARCHIVE(ChasmBinArchive);
#undef REGISTER_ARCHIVE
}

std::string entryData(ArchiveEntry& self)
{
	return std::string((const char*)self.rawData(), self.size());
}

bool entryImportString(ArchiveEntry& self, const std::string& string)
{
	return self.importMem(string.data(), string.size());
}

void registerArchiveEntry(sol::state& lua)
{
	lua.new_usertype<ArchiveEntry>(
		"ArchiveEntry",

		// No constructor
		"new",
		sol::no_constructor,


		// Properties
		// ---------------------------------------------------------------------------
		"name",
		sol::property([](ArchiveEntry& self) { return self.name(); }),

		"path",
		sol::property([](ArchiveEntry& self) { return self.path(); }),

		"type",
		sol::property(&ArchiveEntry::type),

		"size",
		sol::property(&ArchiveEntry::size),

		"index",
		sol::property([](ArchiveEntry& self) { return self.parentDir()->entryIndex(&self); }),

		"crc32",
		sol::property([](ArchiveEntry& self) { return Misc::crc(self.rawData(), self.size()); }),

		"data",
		sol::property(&entryData),


		// Functions
		// ---------------------------------------------------------------------------
		"formattedName",
		sol::overload(
			[](ArchiveEntry& self) { return formattedEntryName(self, true, true, false); },
			[](ArchiveEntry& self, bool include_path) { return formattedEntryName(self, include_path, true, false); },
			[](ArchiveEntry& self, bool include_path, bool include_extension) {
				return formattedEntryName(self, include_path, include_extension, false);
			},
			&formattedEntryName),

		"formattedSize",
		&ArchiveEntry::sizeString,

		"importFile",
		[](ArchiveEntry& self, const std::string& filename) { return self.importFile(filename); },

		"importEntry",
		&ArchiveEntry::importEntry,

		"importData",
		&entryImportString,

		"exportFile",
		&ArchiveEntry::exportFile);
}

void registerArchiveTreeNode(sol::state& lua)
{
	lua.new_usertype<ArchiveTreeNode>(
		"ArchiveDir",

		// No constructor
		"new",
		sol::no_constructor,


		// Properties
		// ---------------------------------------------------------------------------
		"name",
		sol::property(&ArchiveTreeNode::name),

		"archive",
		sol::property(&ArchiveTreeNode::archive),

		"entries",
		sol::property(&archiveDirEntries),

		"parent",
		sol::property([](ArchiveTreeNode& self) { return dynamic_cast<ArchiveTreeNode*>(self.parent()); }),

		"path",
		sol::property(&ArchiveTreeNode::path),

		"subDirectories",
		sol::property(&archiveDirSubDirs));
}

void registerEntryType(sol::state& lua)
{
	lua.new_usertype<EntryType>(
		"EntryType",

		// No constructor
		"new",
		sol::no_constructor,


		// Properties
		// ---------------------------------------------------------------------------
		"id",
		sol::property(&EntryType::id),

		"name",
		sol::property(&EntryType::name),

		"extension",
		sol::property(&EntryType::extension),

		"formatId",
		sol::property(&EntryType::formatId),

		"editor",
		sol::property(&EntryType::editor),

		"category",
		sol::property(&EntryType::category));
}

void registerArchivesNamespace(sol::state& lua)
{
	auto archives = lua.create_table("Archives");

	archives.set_function("all", sol::overload(&allArchives, []() { return allArchives(false); }));

	archives.set_function("create", [](const char* format) { return App::archiveManager().newArchive(format); });

	archives.set_function("openFile", [](const char* filename) { return App::archiveManager().openArchive(filename); });

	archives.set_function(
		"close",
		sol::overload(
			[](Archive* archive) { return App::archiveManager().closeArchive(archive); },
			[](int index) { return App::archiveManager().closeArchive(index); }));

	archives.set_function("closeAll", []() { App::archiveManager().closeAll(); });

	archives.set_function("fileExtensionsString", []() { return App::archiveManager().getArchiveExtensionsString(); });

	archives.set_function("baseResource", []() { return App::archiveManager().baseResourceArchive(); });

	archives.set_function("baseResourcePaths", []() { return App::archiveManager().baseResourcePaths(); });

	archives.set_function(
		"openBaseResource", [](int index) { return App::archiveManager().openBaseResource(index - 1); });

	archives.set_function("programResource", []() { return App::archiveManager().programResourceArchive(); });

	archives.set_function("recentFiles", []() { return App::archiveManager().recentFiles(); });

	archives.set_function("bookmarks", []() { return App::archiveManager().bookmarks(); });

	archives.set_function("addBookmark", [](ArchiveEntry* entry) { App::archiveManager().addBookmark(entry); });

	archives.set_function("removeBookmark", [](ArchiveEntry* entry) { App::archiveManager().deleteBookmark(entry); });
}

void registerArchiveTypes(sol::state& lua)
{
	registerArchiveFormat(lua);
	registerArchive(lua);
	registerArchiveEntry(lua);
	registerEntryType(lua);
	registerArchiveTreeNode(lua);
}
