
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    App.cpp
// Description: The App namespace, with various general application related
//              functions
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
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "Dialogs/SetupWizard/SetupWizardDialog.h"
#include "Game/Configuration.h"
#include "General/Clipboard.h"
#include "General/ColourConfiguration.h"
#include "General/Console/Console.h"
#include "General/Executables.h"
#include "General/KeyBind.h"
#include "General/Misc.h"
#include "General/ResourceManager.h"
#include "General/SAction.h"
#include "General/UI.h"
#include "Graphics/Icons.h"
#include "Graphics/Palette/PaletteManager.h"
#include "Graphics/SImage/SIFormat.h"
#include "MainEditor/MainEditor.h"
#include "MapEditor/NodeBuilders.h"
#include "OpenGL/Drawing.h"
#include "OpenGL/GLTexture.h"
#include "SLADEWxApp.h"
#include "Scripting/Lua.h"
#include "Scripting/ScriptManager.h"
#include "TextEditor/TextLanguage.h"
#include "TextEditor/TextStyle.h"
#include "UI/SBrush.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"
#include "Utility/Tokenizer.h"
#include "thirdparty/dumb/dumb.h"
#include <filesystem>


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace App
{
wxStopWatch     timer;
int             temp_fail_count = 0;
bool            init_ok         = false;
bool            exiting         = false;
std::thread::id main_thread_id;

// Version
Version version_num{ 3, 2, 0, 1 };

// Directory paths
std::string dir_data = "";
std::string dir_user = "";
std::string dir_app  = "";
std::string dir_res  = "";
std::string dir_temp = "";
#ifdef WIN32
std::string dir_separator = "\\";
#else
std::string dir_separator = "/";
#endif

// App objects (managers, etc.)
Console         console_main;
PaletteManager  palette_manager;
ArchiveManager  archive_manager;
Clipboard       clip_board;
ResourceManager resource_manager;
} // namespace App

CVAR(Int, temp_location, 0, CVar::Flag::Save)
CVAR(String, temp_location_custom, "", CVar::Flag::Save)
CVAR(Bool, setup_wizard_run, false, CVar::Flag::Save)


// -----------------------------------------------------------------------------
//
// App::Version Struct Functions
//
// ----------------------------------------------------------------------------


// ----------------------------------------------------------------------------
// Compares with another version [rhs].
// Returns 0 if equal, 1 if this is newer or -1 if this is older
// ----------------------------------------------------------------------------
int App::Version::cmp(const Version& rhs) const
{
	if (major == rhs.major)
	{
		if (minor == rhs.minor)
		{
			if (revision == rhs.revision)
			{
				if (beta == rhs.beta)
					return 0;
				if (beta == 0 && rhs.beta > 0)
					return 1;
				if (beta > 0 && rhs.beta == 0)
					return -1;

				return beta > rhs.beta ? 1 : -1;
			}

			return revision > rhs.revision ? 1 : -1;
		}

		return minor > rhs.minor ? 1 : -1;
	}

	return major > rhs.major ? 1 : -1;
}

// ----------------------------------------------------------------------------
// Returns a string representation of the version (eg. "3.2.1 beta 4")
// ----------------------------------------------------------------------------
std::string App::Version::toString() const
{
	auto vers = fmt::format("{}.{}.{}", major, minor, revision);
	if (beta > 0)
		vers += fmt::format(" beta {}", beta);
	return vers;
}


// ----------------------------------------------------------------------------
//
// App Namespace Functions
//
// -----------------------------------------------------------------------------
namespace App
{
// -----------------------------------------------------------------------------
// Checks for and creates necessary application directories. Returns true
// if all directories existed and were created successfully if needed,
// false otherwise
// -----------------------------------------------------------------------------
bool initDirectories()
{
	// If we're passed in a INSTALL_PREFIX (from CMAKE_INSTALL_PREFIX),
	// use this for the installation prefix
#if defined(__WXGTK__) && defined(INSTALL_PREFIX)
	wxStandardPaths::Get().SetInstallPrefix(INSTALL_PREFIX);
#endif // defined(__UNIX__) && defined(INSTALL_PREFIX)

	// Setup app dir
	dir_app = StrUtil::Path::pathOf(wxStandardPaths::Get().GetExecutablePath().ToStdString(), false);

	// Check for portable install
	if (wxFileExists(path("portable", Dir::Executable)))
	{
		// Setup portable user/data dirs
		dir_data = dir_app;
		dir_res  = dir_app;
		dir_user = dir_app + dir_separator + "config";
	}
	else
	{
		// Setup standard user/data dirs
		dir_user = wxStandardPaths::Get().GetUserDataDir();
		dir_data = wxStandardPaths::Get().GetDataDir();
		dir_res  = wxStandardPaths::Get().GetResourcesDir();
	}

	// Create user dir if necessary
	if (!wxDirExists(dir_user))
	{
		if (!wxMkdir(dir_user))
		{
			wxMessageBox(wxString::Format("Unable to create user directory \"%s\"", dir_user), "Error", wxICON_ERROR);
			return false;
		}
	}

	// Create temp dir if necessary
	dir_temp = dir_user + dir_separator + "temp";
	if (!wxDirExists(dir_temp))
	{
		if (!wxMkdir(dir_temp))
		{
			wxMessageBox(wxString::Format("Unable to create temp directory \"%s\"", dir_temp), "Error", wxICON_ERROR);
			return false;
		}
	}

	// Check data dir
	if (!wxDirExists(dir_data))
		dir_data = dir_app; // Use app dir if data dir doesn't exist

	// Check res dir
	if (!wxDirExists(dir_res))
		dir_res = dir_app; // Use app dir if res dir doesn't exist

	return true;
}

// -----------------------------------------------------------------------------
// Reads and parses the SLADE configuration file
// -----------------------------------------------------------------------------
void readConfigFile()
{
	// Open SLADE.cfg
	Tokenizer tz;
	if (!tz.openFile(App::path("slade3.cfg", App::Dir::User)))
		return;

	// Go through the file with the tokenizer
	while (!tz.atEnd())
	{
		// If we come across a 'cvars' token, read in the cvars section
		if (tz.advIf("cvars", 2))
		{
			// Keep reading name/value pairs until we hit the ending '}'
			while (!tz.checkOrEnd("}"))
			{
				CVar::set(tz.current().text, tz.peek().text);
				tz.adv(2);
			}

			tz.adv(); // Skip ending }
		}

		// Read base resource archive paths
		if (tz.advIf("base_resource_paths", 2))
		{
			while (!tz.checkOrEnd("}"))
			{
				archive_manager.addBaseResourcePath(tz.current().text);
				tz.adv();
			}

			tz.adv(); // Skip ending }
		}

		// Read recent files list
		if (tz.advIf("recent_files", 2))
		{
			while (!tz.checkOrEnd("}"))
			{
				archive_manager.addRecentFile(tz.current().text);
				tz.adv();
			}

			tz.adv(); // Skip ending }
		}

		// Read keybinds
		if (tz.advIf("keys", 2))
			KeyBind::readBinds(tz);

		// Read nodebuilder paths
		if (tz.advIf("nodebuilder_paths", 2))
		{
			while (!tz.checkOrEnd("}"))
			{
				NodeBuilders::addBuilderPath(tz.current().text, tz.peek().text);
				tz.adv(2);
			}

			tz.adv(); // Skip ending }
		}

		// Read game exe paths
		if (tz.advIf("executable_paths", 2))
		{
			while (!tz.checkOrEnd("}"))
			{
				Executables::setGameExePath(tz.current().text, tz.peek().text);
				tz.adv(2);
			}

			tz.adv(); // Skip ending }
		}

		// Read window size/position info
		if (tz.advIf("window_info", 2))
			Misc::readWindowInfo(tz);

		// Next token
		tz.adv();
	}
}

// -----------------------------------------------------------------------------
// Processes command line [args]
// -----------------------------------------------------------------------------
vector<std::string> processCommandLine(vector<std::string>& args)
{
	vector<std::string> to_open;

	// Process command line args (except the first as it is normally the executable name)
	for (auto& arg : args)
	{
		// -nosplash: Disable splash window
		if (StrUtil::equalCI(arg, "-nosplash"))
			UI::enableSplash(false);

		// -debug: Enable debug mode
		else if (StrUtil::equalCI(arg, "-debug"))
		{
			Global::debug = true;
			Log::info("Debugging stuff enabled");
		}

		// Other (no dash), open as archive
		else if (!StrUtil::startsWith(arg, '-'))
			to_open.push_back(arg);

		// Unknown parameter
		else
			Log::warning("Unknown command line parameter: \"{}\"", arg);
	}

	return to_open;
}
} // namespace App

// -----------------------------------------------------------------------------
// Returns true if the application has been initialised
// -----------------------------------------------------------------------------
bool App::isInitialised()
{
	return init_ok;
}

// -----------------------------------------------------------------------------
// Returns the global Console
// -----------------------------------------------------------------------------
Console* App::console()
{
	return &console_main;
}

// -----------------------------------------------------------------------------
// Returns the Palette Manager
// -----------------------------------------------------------------------------
PaletteManager* App::paletteManager()
{
	return &palette_manager;
}

// -----------------------------------------------------------------------------
// Returns the Archive Manager
// -----------------------------------------------------------------------------
ArchiveManager& App::archiveManager()
{
	return archive_manager;
}

// -----------------------------------------------------------------------------
// Returns the Clipboard
// -----------------------------------------------------------------------------
Clipboard& App::clipboard()
{
	return clip_board;
}

// -----------------------------------------------------------------------------
// Returns the Resource Manager
// -----------------------------------------------------------------------------
ResourceManager& App::resources()
{
	return resource_manager;
}

// -----------------------------------------------------------------------------
// Returns the number of ms elapsed since the application was started
// -----------------------------------------------------------------------------
long App::runTimer()
{
	return timer.Time();
}

// -----------------------------------------------------------------------------
// Returns true if the application is exiting
// -----------------------------------------------------------------------------
bool App::isExiting()
{
	return exiting;
}

// -----------------------------------------------------------------------------
// Application initialisation
// -----------------------------------------------------------------------------
bool App::init(vector<std::string>& args, double ui_scale)
{
	// Get the id of the current thread (should be the main one)
	main_thread_id = std::this_thread::get_id();

	// Set locale to C so that the tokenizer will work properly
	// even in locales where the decimal separator is a comma.
	setlocale(LC_ALL, "C");

	// Init application directories
	if (!initDirectories())
		return false;

	// Init log
	Log::init();

	// Process the command line arguments
	auto paths_to_open = processCommandLine(args);

	// Init keybinds
	KeyBind::initBinds();

	// Load configuration file
	Log::info("Loading configuration");
	readConfigFile();

	// Check that SLADE.pk3 can be found
	Log::info("Loading resources");
	archive_manager.init();
	if (!archive_manager.resArchiveOK())
	{
		wxMessageBox(
			"Unable to find slade.pk3, make sure it exists in the same directory as the "
			"SLADE executable",
			"Error",
			wxICON_ERROR);
		return false;
	}

	// Init SActions
	SAction::initWxId(26000);
	SAction::initActions();

	// Init lua
	Lua::init();

	// Init UI
	UI::init(ui_scale);

	// Show splash screen
	UI::showSplash("Starting up...");

	// Init palettes
	if (!palette_manager.init())
	{
		Log::error("Failed to initialise palettes");
		return false;
	}

	// Init SImage formats
	SIFormat::initFormats();

	// Init brushes
	SBrush::initBrushes();

	// Load program icons
	Log::info("Loading icons");
	Icons::loadIcons();

	// Load program fonts
	Drawing::initFonts();

	// Load entry types
	Log::info("Loading entry types");
	EntryDataFormat::initBuiltinFormats();
	EntryType::loadEntryTypes();

	// Load text languages
	Log::info("Loading text languages");
	TextLanguage::loadLanguages();

	// Init text stylesets
	Log::info("Loading text style sets");
	StyleSet::loadResourceStyles();
	StyleSet::loadCustomStyles();

	// Init colour configuration
	Log::info("Loading colour configuration");
	ColourConfiguration::init();

	// Init nodebuilders
	NodeBuilders::init();

	// Init game executables
	Executables::init();

	// Init main editor
	MainEditor::init();

	// Init base resource
	Log::info("Loading base resource");
	archive_manager.initBaseResource();
	Log::info("Base resource loaded");

	// Init game configuration
	Log::info("Loading game configurations");
	Game::init();

	// Init script manager
	ScriptManager::init();

	// Show the main window
	MainEditor::windowWx()->Show(true);
	wxGetApp().SetTopWindow(MainEditor::windowWx());
	UI::showSplash("Starting up...", false, MainEditor::windowWx());

	// Open any archives from the command line
	for (auto& path : paths_to_open)
		archive_manager.openArchive(path);

	// Hide splash screen
	UI::hideSplash();

	init_ok = true;
	Log::info("SLADE Initialisation OK");

	// Show Setup Wizard if needed
	if (!setup_wizard_run)
	{
		SetupWizardDialog dlg(MainEditor::windowWx());
		dlg.ShowModal();
		setup_wizard_run = true;
		MainEditor::windowWx()->Update();
		MainEditor::windowWx()->Refresh();
	}

	return true;
}

// -----------------------------------------------------------------------------
// Saves the SLADE configuration file
// -----------------------------------------------------------------------------
void App::saveConfigFile()
{
	// Open SLADE.cfg for writing text
	wxFile file(App::path("slade3.cfg", App::Dir::User), wxFile::write);

	// Do nothing if it didn't open correctly
	if (!file.IsOpened())
		return;

	// Write cfg header
	file.Write("/*****************************************************\n");
	file.Write(" * SLADE Configuration File\n");
	file.Write(" * Don't edit this unless you know what you're doing\n");
	file.Write(" *****************************************************/\n\n");

	// Write cvars
	file.Write(CVar::writeAll());

	// Write base resource archive paths
	file.Write("\nbase_resource_paths\n{\n");
	for (size_t a = 0; a < archive_manager.numBaseResourcePaths(); a++)
	{
		auto path = archive_manager.getBaseResourcePath(a);
		std::replace(path.begin(), path.end(), '\\', '/');
		file.Write(wxString::Format("\t\"%s\"\n", path), wxConvUTF8);
	}
	file.Write("}\n");

	// Write recent files list (in reverse to keep proper order when reading back)
	file.Write("\nrecent_files\n{\n");
	for (int a = archive_manager.numRecentFiles() - 1; a >= 0; a--)
	{
		auto path = archive_manager.recentFile(a);
		std::replace(path.begin(), path.end(), '\\', '/');
		file.Write(wxString::Format("\t\"%s\"\n", path), wxConvUTF8);
	}
	file.Write("}\n");

	// Write keybinds
	file.Write("\nkeys\n{\n");
	file.Write(KeyBind::writeBinds());
	file.Write("}\n");

	// Write nodebuilder paths
	file.Write("\n");
	NodeBuilders::saveBuilderPaths(file);

	// Write game exe paths
	file.Write("\nexecutable_paths\n{\n");
	file.Write(Executables::writePaths());
	file.Write("}\n");

	// Write window info
	file.Write("\nwindow_info\n{\n");
	Misc::writeWindowInfo(file);
	file.Write("}\n");

	// Close configuration file
	file.Write("\n// End Configuration File\n\n");
}

// -----------------------------------------------------------------------------
// Application exit, shuts down and cleans everything up.
// If [save_config] is true, saves all configuration related files
// -----------------------------------------------------------------------------
void App::exit(bool save_config)
{
	exiting = true;

	if (save_config)
	{
		// Save configuration
		saveConfigFile();

		// Save text style configuration
		StyleSet::saveCurrent();

		// Save colour configuration
		MemChunk ccfg;
		ColourConfiguration::writeConfiguration(ccfg);
		ccfg.exportFile(App::path("colours.cfg", App::Dir::User));

		// Save game exes
		wxFile f;
		f.Open(App::path("executables.cfg", App::Dir::User), wxFile::write);
		f.Write(Executables::writeExecutables());
		f.Close();

		// Save custom special presets
		Game::saveCustomSpecialPresets();

		// Save custom scripts
		ScriptManager::saveUserScripts();
	}

	// Close all open archives
	archive_manager.closeAll();

	// Clean up
	EntryType::cleanupEntryTypes();
	Drawing::cleanupFonts();
	OpenGL::Texture::clearAll();

	// Clear temp folder
	std::error_code error;
	for (auto& item : std::filesystem::directory_iterator{ App::path("", App::Dir::Temp) })
	{
		if (!item.is_regular_file())
			continue;

		if (!std::filesystem::remove(item, error))
			Log::warning("Could not clean up temporary file \"{}\": {}", item.path().string(), error.message());
	}

	// Close lua
	Lua::close();

	// Close DUMB
	dumb_exit();

	// Exit wx Application
	wxGetApp().Exit();
}


// ----------------------------------------------------------------------------
// Returns the current version of SLADE
// ----------------------------------------------------------------------------
const App::Version& App::version()
{
	return version_num;
}

// -----------------------------------------------------------------------------
// Prepends an application-related path to a [filename]
//
// App::Dir::Data: SLADE application data directory (for SLADE.pk3)
// App::Dir::User: User configuration and resources directory
// App::Dir::Executable: Directory of the SLADE executable
// App::Dir::Temp: Temporary files directory
// -----------------------------------------------------------------------------
std::string App::path(std::string_view filename, Dir dir)
{
	switch (dir)
	{
	case Dir::User: return fmt::format("{}{}{}", dir_user, dir_separator, filename);
	case Dir::Data: return fmt::format("{}{}{}", dir_data, dir_separator, filename);
	case Dir::Executable: return fmt::format("{}{}{}", dir_app, dir_separator, filename);
	case Dir::Resources: return fmt::format("{}{}{}", dir_res, dir_separator, filename);
	case Dir::Temp: return fmt::format("{}{}{}", dir_temp, dir_separator, filename);
	default: return std::string{ filename };
	}
}

App::Platform App::platform()
{
#ifdef __WXMSW__
	return Platform::Windows;
#elif __WXGTK__
	return Platform::Linux;
#elif __WXOSX__
	return Platform::MacOS;
#else
	return Platform::Unknown;
#endif
}

bool App::useWebView()
{
#ifdef USE_WEBVIEW_STARTPAGE
	return true;
#else
	return false;
#endif
}

bool App::useSFMLRenderWindow()
{
#ifdef USE_SFML_RENDERWINDOW
	return true;
#else
	return false;
#endif
}

const std::string& App::iconFile()
{
	static std::string icon = "slade.ico";
	return icon;
}

std::thread::id App::mainThreadId()
{
	return main_thread_id;
}


CONSOLE_COMMAND(setup_wizard, 0, false)
{
	SetupWizardDialog dlg(MainEditor::windowWx());
	dlg.ShowModal();
}
