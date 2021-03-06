
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MapSide.cpp
// Description: MapSide class, represents a line side object in a map
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
#include "MapSide.h"
#include "Game/Configuration.h"
#include "SLADEMap/SLADEMap.h"
#include "Utility/Parser.h"


// -----------------------------------------------------------------------------
//
// MapSide Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// MapSide class constructor
// -----------------------------------------------------------------------------
MapSide::MapSide(
	MapSector*      sector,
	const wxString& tex_upper,
	const wxString& tex_middle,
	const wxString& tex_lower,
	Vec2i           tex_offset) :
	MapObject{ Type::Side },
	sector_{ sector },
	tex_upper_{ tex_upper },
	tex_middle_{ tex_middle },
	tex_lower_{ tex_lower },
	tex_offset_{ tex_offset }
{
	if (sector)
		sector->connectSide(this);
}

// -----------------------------------------------------------------------------
// MapSide class constructor from UDMF definition
// -----------------------------------------------------------------------------
MapSide::MapSide(MapSector* sector, ParseTreeNode* udmf_def) : MapObject{ Type::Side }, sector_{ sector }
{
	if (sector)
		sector->connectSide(this);

	// Set properties from UDMF definition
	ParseTreeNode* prop;
	for (unsigned a = 0; a < udmf_def->nChildren(); a++)
	{
		prop = udmf_def->childPTN(a);

		// Skip required properties
		if (prop->nameIsCI(PROP_SECTOR))
			continue;

		if (prop->nameIsCI(PROP_TEXUPPER))
			tex_upper_ = prop->stringValue();
		else if (prop->nameIsCI(PROP_TEXMIDDLE))
			tex_middle_ = prop->stringValue();
		else if (prop->nameIsCI(PROP_TEXLOWER))
			tex_lower_ = prop->stringValue();
		else if (prop->nameIsCI(PROP_OFFSETX))
			tex_offset_.x = prop->intValue();
		else if (prop->nameIsCI(PROP_OFFSETY))
			tex_offset_.y = prop->intValue();
		else
			properties_[prop->name()] = prop->value();
		// Log::info(1, "Property %s type %s (%s)", prop->getName(), prop->getValue().typeString(),
		// prop->getValue().getStringValue());
	}
}

// -----------------------------------------------------------------------------
// Copies another MapSide object [c]
// -----------------------------------------------------------------------------
void MapSide::copy(MapObject* c)
{
	if (c->objType() != Type::Side)
		return;

	// Copy properties
	auto side = dynamic_cast<MapSide*>(c);
	setTexLower(side->tex_lower_, false);
	setTexMiddle(side->tex_middle_, false);
	setTexUpper(side->tex_upper_, false);
	tex_offset_ = side->tex_offset_;

	MapObject::copy(c);
}

// -----------------------------------------------------------------------------
// Returns the light level of the given side
// -----------------------------------------------------------------------------
uint8_t MapSide::light()
{
	int  light          = 0;
	bool include_sector = true;

	if (parent_map_->currentFormat() == MapFormat::UDMF
		&& Game::configuration().featureSupported(Game::UDMFFeature::SideLighting))
	{
		light += intProperty("light");
		if (boolProperty("lightabsolute"))
			include_sector = false;
	}

	if (include_sector && sector_)
		light += sector_->lightAt(0);

	// Clamp range
	if (light > 255)
		return 255;
	if (light < 0)
		return 0;
	return light;
}

// -----------------------------------------------------------------------------
// Changes the light level of a side, if supported
// -----------------------------------------------------------------------------
void MapSide::changeLight(int amount)
{
	if (parent_map_->currentFormat() == MapFormat::UDMF
		&& Game::configuration().featureSupported(Game::UDMFFeature::SideLighting))
		setIntProperty("light", intProperty("light") + amount);
}

// -----------------------------------------------------------------------------
// Sets the upper texture to [tex]
// -----------------------------------------------------------------------------
void MapSide::setTexUpper(const wxString& tex, bool modify)
{
	if (modify)
		setModified();

	if (parent_map_)
	{
		parent_map_->sides().updateTexUsage(tex_upper_, -1);
		parent_map_->sides().updateTexUsage(tex, 1);
	}

	tex_upper_ = tex;
}

// -----------------------------------------------------------------------------
// Sets the middle texture to [tex]
// -----------------------------------------------------------------------------
void MapSide::setTexMiddle(const wxString& tex, bool modify)
{
	if (modify)
		setModified();

	if (parent_map_)
	{
		parent_map_->sides().updateTexUsage(tex_middle_, -1);
		parent_map_->sides().updateTexUsage(tex, 1);
	}

	tex_middle_ = tex;
}

// -----------------------------------------------------------------------------
// Sets the lower texture to [tex]
// -----------------------------------------------------------------------------
void MapSide::setTexLower(const wxString& tex, bool modify)
{
	if (modify)
		setModified();

	if (parent_map_)
	{
		parent_map_->sides().updateTexUsage(tex_lower_, -1);
		parent_map_->sides().updateTexUsage(tex, 1);
	}

	tex_lower_ = tex;
}

// -----------------------------------------------------------------------------
// Sets the X offset to [offset]
// -----------------------------------------------------------------------------
void MapSide::setTexOffsetX(int offset)
{
	setModified();
	tex_offset_.x = offset;
}

// -----------------------------------------------------------------------------
// Sets the Y offset to [offset]
// -----------------------------------------------------------------------------
void MapSide::setTexOffsetY(int offset)
{
	setModified();
	tex_offset_.y = offset;
}

// -----------------------------------------------------------------------------
// Sets the side's sector to [sector]
// -----------------------------------------------------------------------------
void MapSide::setSector(MapSector* sector)
{
	if (!sector)
		return;

	// Remove side from current sector, if any
	if (sector_)
		sector_->disconnectSide(this);

	// Update modified time
	setModified();

	// Add side to new sector
	sector_ = sector;
	sector->connectSide(this);
}

// -----------------------------------------------------------------------------
// Returns the value of the integer property matching [key]
// -----------------------------------------------------------------------------
int MapSide::intProperty(const wxString& key)
{
	if (key == PROP_SECTOR)
	{
		if (sector_)
			return sector_->index();
		else
			return -1;
	}
	else if (key == PROP_OFFSETX)
		return tex_offset_.x;
	else if (key == PROP_OFFSETY)
		return tex_offset_.y;
	else
		return MapObject::intProperty(key);
}

// -----------------------------------------------------------------------------
// Sets the integer value of the property [key] to [value]
// -----------------------------------------------------------------------------
void MapSide::setIntProperty(const wxString& key, int value)
{
	// Update modified time
	setModified();

	if (key == PROP_SECTOR && parent_map_)
		setSector(parent_map_->sector(value));
	else if (key == PROP_OFFSETX)
		tex_offset_.x = value;
	else if (key == PROP_OFFSETY)
		tex_offset_.y = value;
	else
		MapObject::setIntProperty(key, value);
}

// -----------------------------------------------------------------------------
// Returns the value of the string property matching [key]
// -----------------------------------------------------------------------------
wxString MapSide::stringProperty(const wxString& key)
{
	if (key == PROP_TEXUPPER)
		return tex_upper_;
	else if (key == PROP_TEXMIDDLE)
		return tex_middle_;
	else if (key == PROP_TEXLOWER)
		return tex_lower_;
	else
		return MapObject::stringProperty(key);
}

// -----------------------------------------------------------------------------
// Sets the string value of the property [key] to [value]
// -----------------------------------------------------------------------------
void MapSide::setStringProperty(const wxString& key, const wxString& value)
{
	// Update modified time
	setModified();

	if (key == PROP_TEXUPPER)
		setTexUpper(value, false);
	else if (key == PROP_TEXMIDDLE)
		setTexMiddle(value, false);
	else if (key == PROP_TEXLOWER)
		setTexLower(value, false);
	else
		MapObject::setStringProperty(key, value);
}

// -----------------------------------------------------------------------------
// Returns true if the property [key] can be modified via script
// -----------------------------------------------------------------------------
bool MapSide::scriptCanModifyProp(const wxString& key)
{
	return key != PROP_SECTOR;
}

// -----------------------------------------------------------------------------
// Write all side info to a Backup struct
// -----------------------------------------------------------------------------
void MapSide::writeBackup(Backup* backup)
{
	// Sector
	if (sector_)
		backup->props_internal[PROP_SECTOR] = sector_->objId();
	else
		backup->props_internal[PROP_SECTOR] = 0;

	// Textures
	backup->props_internal[PROP_TEXUPPER]  = tex_upper_.ToStdString();
	backup->props_internal[PROP_TEXMIDDLE] = tex_middle_.ToStdString();
	backup->props_internal[PROP_TEXLOWER]  = tex_lower_.ToStdString();

	// Offsets
	backup->props_internal[PROP_OFFSETX] = tex_offset_.x;
	backup->props_internal[PROP_OFFSETY] = tex_offset_.y;
}

// -----------------------------------------------------------------------------
// Reads all side info from a Backup struct
// -----------------------------------------------------------------------------
void MapSide::readBackup(Backup* backup)
{
	// Sector
	auto s = parent_map_->mapData().getObjectById(backup->props_internal[PROP_SECTOR]);
	if (s)
	{
		sector_->disconnectSide(this);
		sector_ = dynamic_cast<MapSector*>(s);
		sector_->connectSide(this);
	}
	else
	{
		if (sector_)
			sector_->disconnectSide(this);
		sector_ = nullptr;
	}

	// Textures
	setTexUpper(backup->props_internal[PROP_TEXUPPER].stringValue(), false);
	setTexMiddle(backup->props_internal[PROP_TEXMIDDLE].stringValue(), false);
	setTexLower(backup->props_internal[PROP_TEXLOWER].stringValue(), false);

	// Offsets
	tex_offset_.x = backup->props_internal[PROP_OFFSETX].intValue();
	tex_offset_.y = backup->props_internal[PROP_OFFSETY].intValue();
}

// -----------------------------------------------------------------------------
// Writes the side as a UDMF text definition to [def]
// -----------------------------------------------------------------------------
void MapSide::writeUDMF(wxString& def)
{
	def = wxString::Format("sidedef//#%u\n{\n", index_);

	// Basic properties
	def += wxString::Format("sector=%u;\n", sector_->index());
	if (tex_upper_ != "-")
		def += wxString::Format("texturetop=\"%s\";\n", tex_upper_);
	if (tex_middle_ != "-")
		def += wxString::Format("texturemiddle=\"%s\";\n", tex_middle_);
	if (tex_lower_ != "-")
		def += wxString::Format("texturebottom=\"%s\";\n", tex_lower_);
	if (tex_offset_.x != 0)
		def += wxString::Format("offsetx=%d;\n", tex_offset_.x);
	if (tex_offset_.y != 0)
		def += wxString::Format("offsety=%d;\n", tex_offset_.y);

	// Other properties
	if (!properties_.isEmpty())
		def += properties_.toString(true);

	def += "}\n\n";
}
