#pragma once

#ifdef __clang__
#pragma clang diagnostic ignored "-Wundefined-bool-conversion"
#endif

#include "SLADEMap/MobjPropertyList.h"
#include <array>

class ParseTreeNode;
class SLADEMap;

// Forward declare map object types
class MapVertex;
class MapSide;
class MapLine;
class MapSector;
class MapThing;

class MapObject
{
	friend class SLADEMap;
	friend class MapObjectCollection;

public:
	enum class Type
	{
		Object = 0,
		Vertex,
		Line,
		Side,
		Sector,
		Thing
	};

	enum class Point
	{
		Mid = 0,
		Within,
		Text
	};

	struct Backup
	{
		MobjPropertyList properties;
		MobjPropertyList props_internal;
		unsigned         id   = 0;
		Type             type = Type::Object;
	};

	typedef std::array<int, 5> ArgSet;

	MapObject(Type type = Type::Object, SLADEMap* parent = nullptr);
	virtual ~MapObject() = default;

	virtual void readUDMF(ParseTreeNode* def) {}

	bool operator<(const MapObject& right) const { return (index_ < right.index_); }
	bool operator>(const MapObject& right) const { return (index_ > right.index_); }

	Type      objType() const { return type_; }
	unsigned  index() const;
	SLADEMap* parentMap() const { return parent_map_; }
	bool      isFiltered() const { return filtered_; }
	long      modifiedTime() const { return modified_time_; }
	unsigned  objId() const { return obj_id_; }
	wxString  typeName() const;
	void      setModified();
	void      setIndex(unsigned index) { index_ = index; }

	MobjPropertyList& props() { return properties_; }
	bool              hasProp(const wxString& key);

	// Generic property modification
	virtual bool     boolProperty(const wxString& key);
	virtual int      intProperty(const wxString& key);
	virtual double   floatProperty(const wxString& key);
	virtual wxString stringProperty(const wxString& key);
	virtual void     setBoolProperty(const wxString& key, bool value);
	virtual void     setIntProperty(const wxString& key, int value);
	virtual void     setFloatProperty(const wxString& key, double value);
	virtual void     setStringProperty(const wxString& key, const wxString& value);
	virtual bool     scriptCanModifyProp(const wxString& key) { return true; }

	virtual Vec2d getPoint(Point point) { return { 0, 0 }; }

	void filter(bool f = true) { filtered_ = f; }

	virtual void copy(MapObject* c);

	void    backupTo(Backup* backup);
	void    loadFromBackup(Backup* backup);
	Backup* backup(bool remove = false);

	virtual void writeBackup(Backup* backup) = 0;
	virtual void readBackup(Backup* backup)  = 0;

	virtual void writeUDMF(wxString& def) {}

	static long propBackupTime();
	static void beginPropBackup(long current_time);
	static void endPropBackup();

	static bool multiBoolProperty(vector<MapObject*>& objects, wxString prop, bool& value);
	static bool multiIntProperty(vector<MapObject*>& objects, wxString prop, int& value);
	static bool multiFloatProperty(vector<MapObject*>& objects, wxString prop, double& value);
	static bool multiStringProperty(vector<MapObject*>& objects, wxString prop, wxString& value);

protected:
	unsigned                index_      = 0;
	SLADEMap*               parent_map_ = nullptr;
	MobjPropertyList        properties_;
	bool                    filtered_      = false;
	long                    modified_time_ = 0;
	unsigned                obj_id_        = 0;
	std::unique_ptr<Backup> obj_backup_;

private:
	Type type_ = Type::Object;
};
