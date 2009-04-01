/*
 * Automatically generated, do not edit this file directly
 */

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * type.h: Generated code for the type system.
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */
#ifndef __TYPE_H__
#define __TYPE_H__

#include <glib.h>
#include "list.h"

class DependencyObject;
class DependencyProperty;
class Surface;
class Types;

typedef gint64 TimeSpan;
typedef DependencyObject *create_inst_func (void);

class Type {
public:
	enum Kind {
		// START_MANAGED_MAPPING
		INVALID,
				ALSASOURCE,
		ANIMATION,
		ANIMATIONCLOCK,
		APPLICATION,
		ARCSEGMENT,
		ASFDEMUXER,
		ASFMARKERDECODER,
		ASFPACKET,
		ASFPARSER,
		ASSEMBLYPART,
		ASSEMBLYPART_COLLECTION,
		ASXDEMUXER,
		AUDIOSOURCE,
		AUDIOSTREAM,
		BEGINSTORYBOARD,
		BEZIERSEGMENT,
		BITMAPIMAGE,
		BITMAPSOURCE,
		BOOL,
		BORDER,
		BRUSH,
		CANVAS,
		CHAR,
		CLOCK,
		CLOCKGROUP,
		CODECDOWNLOADER,
		COLLECTION,
		COLLECTIONCHANGEDEVENTARGS,
		COLLECTIONITEMCHANGEDEVENTARGS,
		COLOR,
		COLORANIMATION,
		COLORANIMATIONUSINGKEYFRAMES,
		COLORKEYFRAME,
		COLORKEYFRAME_COLLECTION,
		COLUMNDEFINITION,
		COLUMNDEFINITION_COLLECTION,
		CONTENTCHANGEDEVENTARGS,
		CONTENTCONTROL,
		CONTROL,
		CONTROLTEMPLATE,
		CORNERRADIUS,
		CURSOR,
		CURSORPOSITIONCHANGEDEVENTARGS,
		DATATEMPLATE,
		DEEPZOOMIMAGETILESOURCE,
		DEPENDENCY_OBJECT,
		DEPENDENCY_OBJECT_COLLECTION,
		DEPENDENCYPROPERTY,
		DEPLOYMENT,
		DISCRETECOLORKEYFRAME,
		DISCRETEDOUBLEKEYFRAME,
		DISCRETEOBJECTKEYFRAME,
		DISCRETEPOINTKEYFRAME,
		DISPATCHERTIMER,
		DOUBLE,
		DOUBLE_COLLECTION,
		DOUBLEANIMATION,
		DOUBLEANIMATIONUSINGKEYFRAMES,
		DOUBLEKEYFRAME,
		DOUBLEKEYFRAME_COLLECTION,
		DOWNLOADER,
		DOWNLOADPROGRESSEVENTARGS,
		DRAWINGATTRIBUTES,
		DURATION,
		ELLIPSE,
		ELLIPSEGEOMETRY,
		ERROREVENTARGS,
		EVENTARGS,
		EVENTLISTENERPROXY,
		EVENTOBJECT,
		EVENTTRIGGER,
		EXCEPTIONROUTEDEVENTARGS,
		EXPRESSION,
		EXTERNALDEMUXER,
		FFMPEGDECODER,
		FFMPEGDEMUXER,
		FILESOURCE,
		FONTFAMILY,
		FONTSTRETCH,
		FONTSTYLE,
		FONTWEIGHT,
		FRAMEWORKELEMENT,
		FRAMEWORKTEMPLATE,
		GENERALTRANSFORM,
		GEOMETRY,
		GEOMETRY_COLLECTION,
		GEOMETRYGROUP,
		GLYPHS,
		GRADIENTBRUSH,
		GRADIENTSTOP,
		GRADIENTSTOP_COLLECTION,
		GRID,
		GRIDLENGTH,
		HITTEST_COLLECTION,
		IIMAGECONVERTER,
		IMAGE,
		IMAGEBRUSH,
		IMAGEERROREVENTARGS,
		IMAGESOURCE,
		IMEDIADECODER,
		IMEDIADEMUXER,
		IMEDIAOBJECT,
		IMEDIASOURCE,
		IMEDIASTREAM,
		INKPRESENTER,
		INLINE,
		INLINE_COLLECTION,
		INPUTMETHOD,
		INT32,
		INT64,
		ITEM_COLLECTION,
		KEYEVENTARGS,
		KEYFRAME,
		KEYFRAME_COLLECTION,
		KEYSPLINE,
		KEYTIME,
		LAYOUTINFORMATION,
		LINE,
		LINEARCOLORKEYFRAME,
		LINEARDOUBLEKEYFRAME,
		LINEARGRADIENTBRUSH,
		LINEARPOINTKEYFRAME,
		LINEBREAK,
		LINEGEOMETRY,
		LINESEGMENT,
		MANAGED,// Silverlight 2.0 only
		MANAGEDSTREAMSOURCE,
		MANAGEDTYPEINFO,
		MANUALTIMESOURCE,
		MARKERREACHEDEVENTARGS,
		MARKERSTREAM,
		MATRIX,
		MATRIXTRANSFORM,
		MEDIA,
		MEDIAATTRIBUTE,
		MEDIAATTRIBUTE_COLLECTION,
		MEDIABASE,
		MEDIACLOSURE,
		MEDIADECODEFRAMECLOSURE,
		MEDIAELEMENT,
		MEDIAFRAME,
		MEDIAGETFRAMECLOSURE,
		MEDIAMARKER,
		MEDIAMARKERFOUNDCLOSURE,
		MEDIAPLAYER,
		MEDIASEEKCLOSURE,
		MEMORYNESTEDSOURCE,
		MEMORYQUEUESOURCE,
		MEMORYSOURCE,
		MOUSEEVENTARGS,
		MOUSEWHEELEVENTARGS,
		MP3DEMUXER,
		MULTISCALEIMAGE,
		MULTISCALESUBIMAGE,
		MULTISCALESUBIMAGE_COLLECTION,
		MULTISCALETILESOURCE,
		NAMESCOPE,
		NPOBJ,
		NULLDECODER,
		OBJECT,
		OBJECTANIMATIONUSINGKEYFRAMES,
		OBJECTKEYFRAME,
		OBJECTKEYFRAME_COLLECTION,
		PANEL,
		PARALLELTIMELINE,
		PARSERERROREVENTARGS,
		PASSTHROUGHDECODER,
		PASSWORDBOX,
		PATH,
		PATHFIGURE,
		PATHFIGURE_COLLECTION,
		PATHGEOMETRY,
		PATHSEGMENT,
		PATHSEGMENT_COLLECTION,
		PLAYLIST,
		PLAYLISTENTRY,
		PLAYLISTROOT,
		POINT,
		POINT_COLLECTION,
		POINTANIMATION,
		POINTANIMATIONUSINGKEYFRAMES,
		POINTKEYFRAME,
		POINTKEYFRAME_COLLECTION,
		POLYBEZIERSEGMENT,
		POLYGON,
		POLYLINE,
		POLYLINESEGMENT,
		POLYQUADRATICBEZIERSEGMENT,
		POPUP,
		PROGRESSIVESOURCE,
		PROPERTYPATH,
		PULSESOURCE,
		QUADRATICBEZIERSEGMENT,
		RADIALGRADIENTBRUSH,
		RECT,
		RECTANGLE,
		RECTANGLEGEOMETRY,
		REPEATBEHAVIOR,
		RESOURCE_DICTIONARY,
		ROTATETRANSFORM,
		ROUTEDEVENTARGS,
		ROWDEFINITION,
		ROWDEFINITION_COLLECTION,
		RUN,
		SCALETRANSFORM,
		SETTER,
		SETTERBASE,
		SETTERBASE_COLLECTION,
		SHAPE,
		SIZE,
		SIZECHANGEDEVENTARGS,
		SKEWTRANSFORM,
		SOLIDCOLORBRUSH,
		SPLINECOLORKEYFRAME,
		SPLINEDOUBLEKEYFRAME,
		SPLINEPOINTKEYFRAME,
		STORYBOARD,
		STRING,
		STROKE,
		STROKE_COLLECTION,
		STYLE,
		STYLUSINFO,
		STYLUSPOINT,
		STYLUSPOINT_COLLECTION,
		SURFACE,
		SYSTEMTIMESOURCE,
		TEMPLATEBINDING,
		TEXTBLOCK,
		TEXTBOX,
		TEXTBOXBASE,
		TEXTBOXMODELCHANGEDEVENTARGS,
		TEXTBOXVIEW,
		TEXTCHANGEDEVENTARGS,
		THICKNESS,
		TILEBRUSH,
		TIMELINE,
		TIMELINE_COLLECTION,
		TIMELINEGROUP,
		TIMELINEMARKER,
		TIMELINEMARKER_COLLECTION,
		TIMEMANAGER,
		TIMESOURCE,
		TIMESPAN,
		TRANSFORM,
		TRANSFORM_COLLECTION,
		TRANSFORMGROUP,
		TRANSLATETRANSFORM,
		TRIGGER_COLLECTION,
		TRIGGERACTION,
		TRIGGERACTION_COLLECTION,
		TRIGGERBASE,
		UIELEMENT,
		UIELEMENT_COLLECTION,
		UINT32,
		UINT64,
		UNMANAGEDMATRIX,
		URI,
		USERCONTROL,
		VIDEOBRUSH,
		VIDEOSTREAM,
		VISUALBRUSH,
		WRITEABLEBITMAP,
		XAMLTEMPLATEBINDING,
		XMLLANGUAGE,
		YUVCONVERTER,

		LASTTYPE,
		// END_MANAGED_MAPPING
	};
	
	static Type *Find (const char *name);
	static Type *Find (Type::Kind type);
	static Type *Find (const char *name, bool ignore_case);
	
	bool IsSubclassOf (Type::Kind super);
	static bool IsSubclassOf (Type::Kind type, Type::Kind super);

	int LookupEvent (const char *event_name);
	const char *LookupEventName (int id);
	DependencyObject *CreateInstance ();
	const char *GetContentPropertyName ();
	
	DependencyProperty *LookupProperty (const char *name);
	void AddProperty (DependencyProperty *property);
	
	GHashTable *CopyProperties (bool inherited);
	
	Type::Kind GetKind () { return type; }
	void SetKind (Type::Kind value) { type = value; }
	Type::Kind GetParent () { return parent; }
	bool IsValueType () { return value_type; }
	bool IsCustomType () { return type > LASTTYPE; }
	const char *GetName () { return name; }
	int GetEventCount () { return total_event_count; }
	
	~Type ();
	Type (Type::Kind type, Type::Kind parent, bool value_type, const char *name, 
		const char *kindname, int event_count, int total_event_count, const char **events, 
		create_inst_func *create_inst, const char *content_property);
	
private:
	Type () {}
	
	Type::Kind type; // this type
	Type::Kind parent; // parent type, INVALID if no parent
	bool value_type; // if this type is a value type

	const char *name; // The name as it appears in code.
	const char *kindname; // The name as it appears in the Type::Kind enum.

	int event_count; // number of events in this class
	int total_event_count; // number of events in this class and all base classes
	const char **events; // the events this class has

	create_inst_func *create_inst; // a function pointer to create an instance of this type

	const char *content_property;
	
	// The catch here is that SL allows us to register several DPs with the same name,
	// and when looking up DP on name they seem to return the latest DP registered
	// with that name.
	GHashTable *properties; // Registered DependencyProperties for this type
};

class Types {
	friend class Type;
	
private:
	ArrayList types;
	ArrayList properties;
	
	void RegisterNativeTypes ();
	void RegisterNativeProperties ();
	
public:
	/* @GenerateCBinding,GeneratePInvoke,Version=2.0 */
	Types ();
	/* @GenerateCBinding,GeneratePInvoke,Version=2.0 */
	~Types ();
	
	/* @GenerateCBinding,GeneratePInvoke,Version=2.0 */
	Type::Kind RegisterType (const char *name, void *gc_handle, Type::Kind parent);
	
	void AddProperty (DependencyProperty *property);
	DependencyProperty *GetProperty (int id);
	
	/* @GenerateCBinding,GeneratePInvoke,Version=2.0 */
	Type *Find (Type::Kind type);
	Type *Find (const char *name);
	Type *Find (const char *name, bool ignore_case);
	
	bool IsSubclassOf (Type::Kind type, Type::Kind super);
	
	void Initialize ();
};

G_BEGIN_DECLS

bool type_get_value_type (Type::Kind type);
DependencyObject *type_create_instance (Type *type);
DependencyObject *type_create_instance_from_kind (Type::Kind kind);

void types_init (void);
const char *type_get_name (Type::Kind type);
bool type_is_dependency_object (Type::Kind type);

/* @IncludeInKinds */
struct ManagedTypeInfo {
	char *assembly_name;
	char *full_name;
	
	ManagedTypeInfo (const char *assembly_name, const char *full_name)
	{
		this->assembly_name = g_strdup (assembly_name);
		this->full_name = g_strdup (full_name);
	}
};

G_END_DECLS

#endif

