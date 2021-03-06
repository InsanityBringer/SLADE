#pragma once

#include "Archive/ArchiveEntry.h"

class SLADEMap;

namespace ScriptManager
{
enum class ScriptType
{
	Editor,
	Custom,
	Global,
	Archive,
	Entry,
	Map,
	NonEditor
};

struct Script
{
	ScriptType         type = ScriptType::Editor;
	wxString           text;
	wxString           name;
	wxString           path;
	bool               read_only = false;
	ArchiveEntry::WPtr source;

	typedef std::unique_ptr<Script> UPtr;
};

typedef vector<Script::UPtr> ScriptList;

ScriptList& editorScripts(ScriptType type = ScriptType::Editor);

void init();
void open();
void saveUserScripts();

bool renameScript(Script* script, const wxString& new_name);
bool deleteScript(Script* script);

Script* createEditorScript(const wxString& name, ScriptType type);
void    populateEditorScriptMenu(wxMenu* menu, ScriptType type, const wxString& action_id);

void runArchiveScript(Archive* archive, int index, wxWindow* parent = nullptr);
void runEntryScript(vector<ArchiveEntry*> entries, int index, wxWindow* parent = nullptr);
void runMapScript(SLADEMap* map, int index, wxWindow* parent = nullptr);
} // namespace ScriptManager
