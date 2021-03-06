
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ColourConfiguration.cpp
// Description: Functions for handling colour configurations
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
#include "ColourConfiguration.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "Console/Console.h"
#include "Utility/Parser.h"
#include "Utility/StringUtils.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace ColourConfiguration
{
typedef std::map<wxString, Colour> ColourHashMap;

double        line_hilight_width;
double        line_selection_width;
double        flat_alpha;
ColourHashMap cc_colours;
} // namespace ColourConfiguration


// -----------------------------------------------------------------------------
//
// ColourConfiguration Namespace Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Returns the colour [name]
// -----------------------------------------------------------------------------
ColRGBA ColourConfiguration::colour(const wxString& name)
{
	auto& col = cc_colours[name];
	if (col.exists)
		return col.colour;
	else
		return ColRGBA::WHITE;
}

// -----------------------------------------------------------------------------
// Returns the colour definition [name]
// -----------------------------------------------------------------------------
const ColourConfiguration::Colour& ColourConfiguration::colDef(const wxString& name)
{
	return cc_colours[name];
}

// -----------------------------------------------------------------------------
// Sets the colour definition [name]
// -----------------------------------------------------------------------------
void ColourConfiguration::setColour(const wxString& name, int red, int green, int blue, int alpha, bool blend_additive)
{
	auto& col = cc_colours[name];
	if (red >= 0)
		col.colour.r = red;
	if (green >= 0)
		col.colour.g = green;
	if (blue >= 0)
		col.colour.b = blue;
	if (alpha >= 0)
		col.colour.a = alpha;

	col.blend_additive = blend_additive;
	col.exists         = true;
}

// -----------------------------------------------------------------------------
// Sets the current OpenGL colour and blend mode to match definition [name]
// -----------------------------------------------------------------------------
void ColourConfiguration::setGLColour(const wxString& name, float alpha_mult)
{
	auto& col = cc_colours[name];
	OpenGL::setColour(col.colour.r, col.colour.g, col.colour.b, col.colour.a * alpha_mult, col.blendMode());
}

// -----------------------------------------------------------------------------
// Returns the line hilight width multiplier
// -----------------------------------------------------------------------------
double ColourConfiguration::lineHilightWidth()
{
	return line_hilight_width;
}

// -----------------------------------------------------------------------------
// Returns the line selection width multiplier
// -----------------------------------------------------------------------------
double ColourConfiguration::lineSelectionWidth()
{
	return line_selection_width;
}

// -----------------------------------------------------------------------------
// Returns the flat alpha multiplier
// -----------------------------------------------------------------------------
double ColourConfiguration::flatAlpha()
{
	return flat_alpha;
}

// -----------------------------------------------------------------------------
// Sets the line hilight width multiplier
// -----------------------------------------------------------------------------
void ColourConfiguration::setLineHilightWidth(double mult)
{
	line_hilight_width = mult;
}

// -----------------------------------------------------------------------------
// Sets the line selection width multiplier
// -----------------------------------------------------------------------------
void ColourConfiguration::setLineSelectionWidth(double mult)
{
	line_selection_width = mult;
}

// -----------------------------------------------------------------------------
// Sets the flat alpha multiplier
// -----------------------------------------------------------------------------
void ColourConfiguration::setFlatAlpha(double alpha)
{
	flat_alpha = alpha;
}

// -----------------------------------------------------------------------------
// Reads a colour configuration from text data [mc]
// -----------------------------------------------------------------------------
bool ColourConfiguration::readConfiguration(MemChunk& mc)
{
	// Parse text
	Parser parser;
	parser.parseText(mc);

	// Get 'colours' block
	auto colours = parser.parseTreeRoot()->childPTN("colours");
	if (colours)
	{
		// Read all colour definitions
		for (unsigned a = 0; a < colours->nChildren(); a++)
		{
			auto def = colours->childPTN(a);

			// Read properties
			for (unsigned b = 0; b < def->nChildren(); b++)
			{
				auto  prop = def->childPTN(b);
				auto& col  = cc_colours[def->name()];
				col.exists = true;

				// Colour name
				if (prop->name() == "name")
					col.name = prop->stringValue();

				// Colour group (for config ui)
				else if (prop->name() == "group")
					col.group = prop->stringValue();

				// Colour
				else if (prop->name() == "rgb")
					col.colour.set(prop->intValue(0), prop->intValue(1), prop->intValue(2));

				// Alpha
				else if (prop->name() == "alpha")
					col.colour.a = prop->intValue();

				// Additive
				else if (prop->name() == "additive")
					col.blend_additive = prop->boolValue();

				else
					Log::warning(wxString::Format("Unknown colour definition property \"%s\"", prop->name()));
			}
		}
	}

	// Get 'theme' block
	auto theme = parser.parseTreeRoot()->childPTN("theme");
	if (theme)
	{
		// Read all theme definitions
		for (unsigned a = 0; a < theme->nChildren(); a++)
		{
			auto prop = theme->childPTN(a);

			if (prop->name() == "line_hilight_width")
				line_hilight_width = prop->floatValue();

			else if (prop->name() == "line_selection_width")
				line_selection_width = prop->floatValue();

			else if (prop->name() == "flat_alpha")
				flat_alpha = prop->floatValue();

			else
				Log::warning(wxString::Format("Unknown theme property \"%s\"", prop->name()));
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Writes the current colour configuration to text data [mc]
// -----------------------------------------------------------------------------
bool ColourConfiguration::writeConfiguration(MemChunk& mc)
{
	wxString cfgstring = "colours\n{\n";

	// Go through all properties
	for (const auto& i : cc_colours)
	{
		// Skip if it doesn't 'exist'
		const auto& cc = i.second;
		if (!cc.exists)
			continue;

		// Colour definition name
		cfgstring += wxString::Format("\t%s\n\t{\n", i.first);

		// Full name
		cfgstring += wxString::Format("\t\tname = \"%s\";\n", cc.name);

		// Group
		cfgstring += wxString::Format("\t\tgroup = \"%s\";\n", cc.group);

		// Colour values
		cfgstring += wxString::Format("\t\trgb = %d, %d, %d;\n", cc.colour.r, cc.colour.g, cc.colour.b);

		// Alpha
		if (cc.colour.a < 255)
			cfgstring += wxString::Format("\t\talpha = %d;\n", cc.colour.a);

		// Additive
		if (cc.blend_additive)
			cfgstring += "\t\tadditive = true;\n";

		cfgstring += "\t}\n\n";
	}

	cfgstring += "}\n\ntheme\n{\n";

	cfgstring += wxString::Format("\tline_hilight_width = %1.3f;\n", line_hilight_width);
	cfgstring += wxString::Format("\tline_selection_width = %1.3f;\n", line_selection_width);
	cfgstring += wxString::Format("\tflat_alpha = %1.3f;\n", flat_alpha);
	cfgstring += "}\n";

	mc.write(cfgstring.ToAscii(), cfgstring.size());

	return true;
}

// -----------------------------------------------------------------------------
// Initialises the colour configuration
// -----------------------------------------------------------------------------
bool ColourConfiguration::init()
{
	// Load default configuration
	loadDefaults();

	// Check for saved colour configuration
	if (wxFileExists(App::path("colours.cfg", App::Dir::User)))
	{
		MemChunk ccfg;
		ccfg.importFile(App::path("colours.cfg", App::Dir::User));
		readConfiguration(ccfg);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Sets all colours in the current configuration to default
// -----------------------------------------------------------------------------
void ColourConfiguration::loadDefaults()
{
	// Read default colours
	auto pres             = App::archiveManager().programResourceArchive();
	auto entry_default_cc = pres->entryAtPath("config/colours/default.txt");
	if (entry_default_cc)
		readConfiguration(entry_default_cc->data());
}

// -----------------------------------------------------------------------------
// Reads saved colour configuration [name]
// -----------------------------------------------------------------------------
bool ColourConfiguration::readConfiguration(const wxString& name)
{
	// TODO: search custom folder

	// Search resource pk3
	auto res = App::archiveManager().programResourceArchive();
	auto dir = res->dir("config/colours");
	for (unsigned a = 0; a < dir->numEntries(); a++)
	{
		if (StrUtil::equalCI(dir->entryAt(a)->nameNoExt(), name.ToStdString()))
			return readConfiguration(dir->entryAt(a)->data());
	}

	return false;
}

// -----------------------------------------------------------------------------
// Adds all available colour configuration names to [names]
// -----------------------------------------------------------------------------
void ColourConfiguration::putConfigurationNames(vector<wxString>& names)
{
	// TODO: search custom folder

	// Search resource pk3
	auto res = App::archiveManager().programResourceArchive();
	auto dir = res->dir("config/colours");
	for (unsigned a = 0; a < dir->numEntries(); a++)
		names.push_back(std::string{ dir->entryAt(a)->nameNoExt() });
}

// -----------------------------------------------------------------------------
// Adds all colour names to [list]
// -----------------------------------------------------------------------------
void ColourConfiguration::putColourNames(vector<wxString>& list)
{
	for (auto& i : cc_colours)
		list.push_back(i.first);
}


// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------

CONSOLE_COMMAND(ccfg, 1, false)
{
	// Check for 'list'
	if (StrUtil::equalCI(args[0], "list"))
	{
		// Get (sorted) list of colour names
		vector<wxString> list;
		ColourConfiguration::putColourNames(list);
		sort(list.begin(), list.end());

		// Dump list to console
		Log::console(wxString::Format("%lu Colours:", list.size()));
		for (const auto& a : list)
			Log::console(a);
	}
	else
	{
		// Check for enough args to set the colour
		if (args.size() >= 4)
		{
			// Read RGB
			int red   = StrUtil::toInt(args[1]);
			int green = StrUtil::toInt(args[2]);
			int blue  = StrUtil::toInt(args[3]);

			// Read alpha (if specified)
			int alpha = -1;
			if (args.size() >= 5)
				alpha = StrUtil::toInt(args[4]);

			// Read blend (if specified)
			int blend = -1;
			if (args.size() >= 6)
				blend = StrUtil::toInt(args[5]);

			// Set colour
			ColourConfiguration::setColour(args[0], red, green, blue, alpha, blend);
		}

		// Print colour
		auto def = ColourConfiguration::colDef(args[0]);
		Log::console(wxString::Format(
			"Colour \"%s\" = %d %d %d %d %d", args[0], def.colour.r, def.colour.g, def.colour.b, def.blend_additive));
	}
}

CONSOLE_COMMAND(load_ccfg, 1, false)
{
	ColourConfiguration::readConfiguration(args[0]);
}
