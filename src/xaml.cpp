/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xaml.cpp: xaml parser
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#define DEBUG

#include <config.h>
#include <string.h>
#include <malloc.h>
#include <glib.h>
#include <stdlib.h>
#include <expat.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#include "xaml.h"
#include "error.h"
#include "shape.h"
#include "animation.h"
#include "geometry.h"
#include "text.h"
#include "media.h"
#include "list.h"
#include "rect.h"
#include "point.h"
#include "canvas.h"
#include "color.h"
#include "namescope.h"
#include "stylus.h"
#include "runtime.h"
#include "utils.h"
#include "control.h"
#include "template.h"

#if SL_2_0
#include "thickness.h"
#include "cornerradius.h"
#include "deployment.h"
#include "grid.h"
#include "deepzoomimagetilesource.h"
#endif


class XamlElementInfo;
class XamlElementInstance;
class XamlParserInfo;
class XamlNamespace;
class DefaultNamespace;
class XNamespace;
class XamlElementInfoNative;
class XamlElementInstanceNative;
class XamlElementInstanceValueType;
class XamlElementInfoManaged;
class XamlElementInstanceManaged;
class XamlElementInfoImportedManaged;
class XamlElementInstanceTemplate;

		

static DefaultNamespace *default_namespace = NULL;
static XNamespace *x_namespace = NULL;

static const char* default_namespace_names [] = {
	"http://schemas.microsoft.com/winfx/2006/xaml/presentation",
	"http://schemas.microsoft.com/client/2007",
	"http://schemas.microsoft.com/xps/2005/06",
	"http://schemas.microsoft.com/client/2007/deployment",
	NULL
};


static bool dependency_object_set_property (XamlParserInfo *p, XamlElementInstance *item, XamlElementInstance *property, XamlElementInstance *value);
static void dependency_object_add_child (XamlParserInfo *p, XamlElementInstance *parent, XamlElementInstance *child);
static void dependency_object_set_attributes (XamlParserInfo *p, XamlElementInstance *item, const char **attr);
static void value_type_set_attributes (XamlParserInfo *p, XamlElementInstance *item, const char **attr);
void parser_error (XamlParserInfo *p, const char *el, const char *attr, int error_code, const char *message);

static XamlElementInfo *create_element_info_from_imported_managed_type (XamlParserInfo *p, const char *name);


class XamlElementInfo {
 protected:
	Type::Kind kind;
	
 public:
	XamlElementInfo *parent;
	const char *name;

	XamlElementInfo (const char *name, Type::Kind kind)
	{
		this->parent = NULL;
		this->kind = kind;
		this->name = name;
	}

	~XamlElementInfo ()
	{
	}

	virtual Type::Kind GetKind () { return kind; }
	virtual const char *GetContentProperty (XamlParserInfo *p) { return Type::Find (kind)->GetContentPropertyName (); }

	virtual XamlElementInstance *CreateElementInstance (XamlParserInfo *p) = 0;
	virtual XamlElementInstance *CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o) = 0;
	virtual XamlElementInstance *CreatePropertyElementInstance (XamlParserInfo *p, const char *name) = 0;
};


class XamlElementInstance : public List::Node {

 protected:
	DependencyObject *item;

 public:
	const char *element_name;
	XamlElementInfo *info;
	
	XamlElementInstance *parent;
	List *children;

	enum ElementType {
		ELEMENT,
		PROPERTY,
		INVALID
	};

	int element_type;
	char *x_key;

	GHashTable *set_properties;

	XamlElementInstance (XamlElementInfo *info, const char* element_name, ElementType type)
	{
		this->element_name = element_name;
		this->set_properties = NULL;
		this->element_type = type;
		this->parent = NULL;
		this->info = info;
		this->item = NULL;
		this->x_key = NULL;
		
		children = new List ();
	}
	
	virtual ~XamlElementInstance ()
	{
		children->Clear (true);
		delete children;
		delete info;
		g_free (x_key);

		if (set_properties)
			g_hash_table_destroy (set_properties);

		if (element_name && element_type == PROPERTY)
			g_free ((void*) element_name);
	}

	
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value) = 0;
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char* value) = 0;
	virtual void AddChild (XamlParserInfo *p, XamlElementInstance *child) = 0;
	virtual void SetAttributes (XamlParserInfo *p, const char **attr) = 0;

	virtual bool TrySetContentProperty (XamlParserInfo *p, XamlElementInstance *value);
	virtual bool TrySetContentProperty (XamlParserInfo *p, const char *value);
	
	void SetKey (const char *key) { this->x_key = g_strdup (key); }
	char *GetKey () { return x_key; }

	virtual bool IsDependencyObject ()
	{
		return true;
	}

	virtual Value *GetAsValue ()
	{
		return new Value (item);
	}

	virtual DependencyObject *GetAsDependencyObject ()
	{
		return item;
	}

	virtual void SetDependencyObject (DependencyObject *value)
	{
		item = value;
	}
	
	virtual void* GetManagedPointer ()
	{
		return item;
	}

	virtual bool IsTemplate ()
	{
		return false;
	}

	bool IsPropertySet (const char *name)
	{
		if (!set_properties)
			return false;

		return g_hash_table_lookup (set_properties, name) != NULL;
	}

	void MarkPropertyAsSet (const char *name)
	{
		if (!set_properties)
			set_properties = g_hash_table_new (g_str_hash, g_str_equal);

		g_hash_table_insert (set_properties, (void *) name, GINT_TO_POINTER (TRUE));
	}

	void ClearSetProperties ()
	{
		if (!set_properties)
			return;

#if GLIB_CHECK_VERSION(2,12,0)
		g_hash_table_remove_all (set_properties);
#else
		// this will cause the hash table to be recreated the
		// next time a property is set.
		g_hash_table_destroy (set_properties);
		set_properties = NULL;
#endif
	}

	void LookupNamedResource (const char *name, Value **v)
	{
		if (!item) {
			*v = NULL;
			return;
		}

		if (item->Is(Type::FRAMEWORKELEMENT)) {

			ResourceDictionary *rd = item->GetValue(UIElement::ResourcesProperty)->AsResourceDictionary();

			bool exists = false;
			Value *resource_value = rd->Get (name, &exists);

			if (exists) {
				*v = new Value (*resource_value);
				return;
			}
		}

		// XXX I'm guessing this parent lookup is the reason that we're failing for cases like the following xaml:
		//
		// <Rectangle><Rectangle.Stroke><SolidColorBrush Color="{StaticResource color}"/></Rectangle.Stroke></Rectangle>
		//
		// check ResourceDictionaryTest.TestStaticResourceParentElement_Property
		//
		if (parent)
			parent->LookupNamedResource (name, v);
	}
};

void 
unref_xaml_element (gpointer data, gpointer user_data)
{
	DependencyObject* dob = (DependencyObject*) data;
	//printf ("unref_xaml_element: %i\n", dob->id);
	if (dob)
		dob->unref ();
}

class XamlParserInfo {
 public:
	XML_Parser parser;

	const char *file_name;

	NameScope *namescope;
	XamlElementInstance *top_element;
	XamlNamespace *current_namespace;
	XamlElementInstance *current_element;
	const char *next_element;

	GHashTable *namespace_map;
	bool cdata_content;
	GString *cdata;
	
	bool implicit_default_namespace;

	ParserErrorEventArgs *error_args;

	XamlLoader *loader;
	GList *created_elements;

	//
	// If set, this is used to hydrate an existing object, not to create a new toplevel one
	//
	DependencyObject *hydrate_expecting;
	bool hydrating;
	
	void AddCreatedElement (DependencyObject* element)
	{
		created_elements = g_list_prepend (created_elements, element);
	}

	XamlParserInfo (XML_Parser parser, const char *file_name) :
		parser (parser), file_name (file_name),
		namescope (new NameScope()), top_element (NULL),
		current_namespace (NULL), current_element (NULL), cdata_content (false), cdata (NULL),
		implicit_default_namespace (false), error_args (NULL), loader (NULL), created_elements (NULL),
		hydrate_expecting(NULL), hydrating(false)
	{
		namespace_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	}

	~XamlParserInfo ()
	{
		created_elements = g_list_reverse (created_elements);
		g_list_foreach (created_elements, unref_xaml_element, NULL);
		g_list_free (created_elements);

		g_hash_table_destroy (namespace_map);

		if (cdata)
			g_string_free (cdata, TRUE);
		if (top_element)
			delete top_element;
		namescope->unref ();
	}
};


class XamlElementInfoNative : public XamlElementInfo {
	Type *type;
	
 public:
	XamlElementInfoNative (Type *t) : XamlElementInfo (t->name, t->type)
	{
		type = t;
	}

	Type* GetType ()
	{
		return type;
	}

	const char* GetName ()
	{
		return type->name;
	}

	const char* GetContentProperty (XamlParserInfo *p);

	XamlElementInstance* CreateElementInstance (XamlParserInfo *p);
	XamlElementInstance* CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o);
	XamlElementInstance* CreatePropertyElementInstance (XamlParserInfo *p, const char *name);
};


class XamlElementInstanceNative : public XamlElementInstance {
	XamlElementInfoNative *element_info;
	XamlParserInfo *parser_info;
	
 public:
	XamlElementInstanceNative (XamlElementInfoNative *element_info, XamlParserInfo *parser_info, const char *name, ElementType type, bool create_item = true);

	virtual DependencyObject *CreateItem ();

	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value);
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char* value);
	virtual void AddChild (XamlParserInfo *p, XamlElementInstance *child);
	virtual void SetAttributes (XamlParserInfo *p, const char **attr);
};


class XamlElementInstanceValueType : public XamlElementInstance {
	XamlElementInfoNative *element_info;
	XamlParserInfo *parser_info;
	Value *value_item;

 public:
	XamlElementInstanceValueType (XamlElementInfoNative *element_info, XamlParserInfo *parser_info, const char *name, ElementType type);

	virtual bool IsDependencyObject ()
	{
		return false;
	}

	virtual Value *GetAsValue ()
	{
		return value_item;
	}

	bool CreateValueItemFromString (const char* str);

	// A Value type doesn't really support anything. It's just here so people can do <SolidColorBrush.Color><Color>#FF00FF</Color></SolidColorBrush.Color>
	virtual DependencyObject *CreateItem () { return NULL; }
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value) { return false; }
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char *value) { return false; }
	virtual void AddChild (XamlParserInfo *p, XamlElementInstance *child) { }
	virtual void SetAttributes (XamlParserInfo *p, const char **attr);

	virtual bool TrySetContentProperty (XamlParserInfo *p, XamlElementInstance *value) { return false; }
	virtual bool TrySetContentProperty (XamlParserInfo *p, const char *value) { return CreateValueItemFromString (value); }
};


class XamlElementInstanceTemplate : public XamlElementInstanceNative {
public:
	XamlElementInstanceTemplate (XamlElementInfoNative *element_info, XamlParserInfo *parser_info, const char *name, ElementType type, bool create_item = true)
		: XamlElementInstanceNative (element_info, parser_info, name, type, create_item)
	{
		bindings = new List ();
	}

	virtual bool IsTemplate ()
	{
		return true;
	}

	void AddTemplateBinding (XamlElementInstance *element, const char *source_property, const char *target_property)
	{
		XamlTemplateBinding *b = new XamlTemplateBinding ((FrameworkElement*)element->GetManagedPointer (), source_property, target_property);
		((FrameworkTemplate*)GetManagedPointer ())->AddXamlBinding (b);
		b->unref ();
	}

private:
	List* bindings;
};


class XamlNamespace {
 public:
	const char *name;

	XamlNamespace () : name (NULL) { }

	virtual ~XamlNamespace () { }
	virtual XamlElementInfo* FindElement (XamlParserInfo *p, const char *el) = 0;
	virtual bool SetAttribute (XamlParserInfo *p, XamlElementInstance *item, const char *attr, const char *value, bool *reparse) = 0;
};

class DefaultNamespace : public XamlNamespace {
 public:
	DefaultNamespace () { }

	virtual ~DefaultNamespace () { }

	virtual XamlElementInfo* FindElement (XamlParserInfo *p, const char *el)
	{
		Type* t = Type::Find (el);
		if (t)
			return new XamlElementInfoNative (t);

		XamlElementInfo* managed_element = create_element_info_from_imported_managed_type (p, el);
		if (managed_element)			
			return managed_element;

		return NULL;
		
	}

	virtual bool SetAttribute (XamlParserInfo *p, XamlElementInstance *item, const char *attr, const char *value, bool *reparse)
	{
		return false;
	}
};

class XNamespace : public XamlNamespace {
 public:
	XNamespace () { }

	virtual ~XNamespace () { }

	virtual XamlElementInfo* FindElement (XamlParserInfo *p, const char *el)
	{
		return NULL;
	}

	virtual bool SetAttribute (XamlParserInfo *p, XamlElementInstance *item, const char *attr, const char *value, bool *reparse)
	{
		*reparse = false;

		if (!strcmp ("Name", attr)) {

			//
			// Causes breakage in airlines
			// Maybe x:Name overwrites but Name does not?
			//
			// if (p->namescope->FindName (value)) {
			//	parser_error (p, p->current_element->element_name, "x:Name", 2028, g_strdup_printf ("The name already exists in the tree: %s.", value));
			//	return false;
			// }
			//

			if (item->GetKey ()) {
				// XXX don't know the proper values here...
				parser_error (p, item->element_name, NULL, 2007,
					      g_strdup ("You can't specify x:Name along with x:Key, or x:Key twice."));
				return false;
			}
			item->SetKey (value);

			if (item->IsDependencyObject ()) {
				item->GetAsDependencyObject ()->SetValue (DependencyObject::NameProperty, Value (value));
				return true;
			}

			return false;
		}

		if (!strcmp ("Key", attr)) {
			if (item->GetKey ()) {
				// XXX don't know the proper values here...
				parser_error (p, item->element_name, NULL, 2007,
					      g_strdup ("You can't specify x:Name along with x:Key, or x:Key twice."));
				return false;
			}
			item->SetKey (value);
			return true;
		}

		if (!strcmp ("Class", attr)) {
			if (!item->IsDependencyObject ()) {
				// XXX don't know the proper values here...
				parser_error (p, item->element_name, attr, -1,
					      g_strdup_printf ("Cannot specify x:Class type '%s' on value type element\n", value));
				return false;
			}
				
			// While hydrating, we do not need to create the toplevel class, its created already
			if (p->hydrating)
				return true;

			//
			// FIXME: On Silverlight 2.0 only the toplevel node should contain an x:Class
			// must validate that.
			//
			DependencyObject *old = item->GetAsDependencyObject ();

			item->SetDependencyObject (NULL);
			DependencyObject *dob = NULL;
			if (p->loader) {
				// The managed DependencyObject will unref itself
				// once it's finalized, so don't unref anything here.
				Value *v = new Value ();
				if (p->loader->CreateObject (p->top_element ? p->top_element->GetAsDependencyObject () : NULL, NULL, value, v) && v->Is (Type::DEPENDENCY_OBJECT)) {
					dob = v->AsDependencyObject ();
					dob->SetSurface (p->loader->GetSurface ());
				} else
					delete v;
			}

			if (!dob) {
				parser_error (p, item->element_name, attr, -1,
					      g_strdup_printf ("Unable to resolve x:Class type '%s'\n", value));
				return false;
			}

			// Special case the namescope for now, since attached properties aren't copied
			NameScope *ns = NameScope::GetNameScope (old);
			if (ns)
				NameScope::SetNameScope (dob, ns);
			item->SetDependencyObject (dob);

			p->AddCreatedElement (item->GetAsDependencyObject ());

			*reparse = true;
			return true;
		}

		return false;
	}
};


class XamlElementInfoManaged : public XamlElementInfo {
 public:
	XamlElementInfoManaged (const char *xmlns, const char *name, XamlElementInfo *parent, Type::Kind dependency_type, Value *obj) : XamlElementInfo (name, dependency_type)
	{
		this->xmlns = xmlns;
		this->obj = obj;
	}

	const char *xmlns;
	Value *obj;

	const char* GetName () { return name; }

	const char* GetContentProperty (XamlParserInfo *p);

	XamlElementInstance* CreateElementInstance (XamlParserInfo *p);
	XamlElementInstance* CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o);
	XamlElementInstance* CreatePropertyElementInstance (XamlParserInfo *p, const char *name);
};


class XamlElementInstanceManaged : public XamlElementInstance {
 public:
	XamlElementInstanceManaged (XamlElementInfo *info, const char *name, ElementType type, Value *obj);

	virtual bool IsDependencyObject ()
	{
		return false;
	}

	virtual Value *GetAsValue () { return obj; }

	virtual bool SetAttachedProperty (XamlParserInfo *p, XamlElementInstance *target, XamlElementInstance *value);

	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value);
	virtual bool SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char* value);
	virtual void AddChild (XamlParserInfo *p, XamlElementInstance *child);
	virtual void SetAttributes (XamlParserInfo *p, const char **attr);

	virtual bool TrySetContentProperty (XamlParserInfo *p, const char *value);

	virtual void* GetManagedPointer ();
 protected:
	Value *obj;
};


class XamlElementInfoImportedManaged : public XamlElementInfoManaged {

 public:
	XamlElementInfoImportedManaged (const char *name, XamlElementInfo *parent, Value *obj) : XamlElementInfoManaged (NULL, name, parent, obj->GetKind (), obj)
	{
	}

	const char* GetContentProperty (XamlParserInfo *p);

	XamlElementInstance* CreateElementInstance (XamlParserInfo *p);
	XamlElementInstance* CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o);
	XamlElementInstance* CreatePropertyElementInstance (XamlParserInfo *p, const char *name);
};



class ManagedNamespace : public XamlNamespace {
 public:
	char *xmlns;

	ManagedNamespace (char *xmlns)
	{
		this->xmlns = xmlns;
	}

	virtual ~ManagedNamespace ()
	{
		g_free (xmlns);
	}

	virtual XamlElementInfo* FindElement (XamlParserInfo *p, const char *el)
	{
		if (!p->loader)
			return NULL;

		Value *value = new Value ();
		if (!p->loader->CreateObject (p->top_element ? p->top_element->GetManagedPointer () : NULL, xmlns, el, value)) {
			parser_error (p, el, NULL, -1, g_strdup_printf ("Unable to resolve managed type %s\n", el));
			return  NULL;
		}

		XamlElementInfoManaged *info = new XamlElementInfoManaged (xmlns, g_strdup (el), NULL, value->GetKind (), value);
		return info;
	}

	virtual bool SetAttribute (XamlParserInfo *p, XamlElementInstance *item, const char *attr, const char *value, bool *reparse)
	{
		XamlElementInstanceManaged *instance = (XamlElementInstanceManaged *) item;

		if (p->loader) {
			Value v = Value (value);
			return p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, xmlns, instance->GetManagedPointer (), attr, &v);
		}
		return false;
	}
};

bool
XamlLoader::CreateObject (void *top_level, const char* xmlns, const char* type_name, Value *value)
{
	if (callbacks.create_object) {
		if (!vm_loaded && !LoadVM ())
			return NULL;
		bool res = callbacks.create_object (top_level, xmlns, type_name, value);
		return res;
	}
		
	return NULL;
}

const char *
XamlLoader::GetContentPropertyName (void *obj)
{
	if (callbacks.get_content_property_name) {
		return callbacks.get_content_property_name (obj);
	}
	return NULL;
}

bool
XamlLoader::SetProperty (void *top_level, const char* xmlns, void *target, const char *name, Value *value)
{
	if (callbacks.set_property)
		return callbacks.set_property (top_level, xmlns, target, name, value);

	return false;
}
		
gboolean
xaml_loader_find_any (gpointer key, gpointer value, gpointer user_data)
{
	return TRUE;
}


XamlLoader::XamlLoader (const char* filename, const char* str, Surface* surface)
{
	this->filename = g_strdup (filename);
	this->str = g_strdup (str);
	this->surface = surface;
	surface->ref ();
	this->vm_loaded = false;
	this->error_args = NULL;
	
#if DEBUG
	if (!surface && debug_flags & RUNTIME_DEBUG_XAML) {
		printf ("XamlLoader::XamlLoader ('%s', '%s', %p): Initializing XamlLoader without a surface.\n",
			filename, str, surface);
	}
#endif
}

XamlLoader::~XamlLoader ()
{
	g_free (filename);
	g_free (str);
	surface->unref ();
	surface = NULL;
	filename = NULL;
	str = NULL;
}

bool
XamlLoader::LoadVM ()
{
	return false;
}

XamlLoader* 
xaml_loader_new (const char* filename, const char* str, Surface* surface)
{
	return new XamlLoader (filename, str, surface);
}

void
xaml_loader_free (XamlLoader* loader)
{
	delete loader;
}

void 
xaml_loader_set_callbacks (XamlLoader* loader, XamlLoaderCallbacks callbacks)
{
	if (!loader) {
		LOG_XAML ("Trying to set callbacks for a null object\n");
		return;
	}
	
	loader->callbacks = callbacks;
	loader->vm_loaded = true;
}


//
// Called when we encounter an error.  Note that memory ownership is taken for everything
// except the message, this allows you to use g_strdup_printf when creating the error message
//
void
parser_error (XamlParserInfo *p, const char *el, const char *attr, int error_code, const char *message)
{
	// Already failed
	if (p->error_args)
		return;

	// if parsing fails too early it's not safe (i.e. sigsegv) to call some functions, e.g. XML_GetCurrentLineNumber
	bool report_line_col = (error_code != XML_ERROR_XML_DECL);
	int line_number = report_line_col ? XML_GetCurrentLineNumber (p->parser) : 0;
	int char_position = report_line_col ? XML_GetCurrentColumnNumber (p->parser) : 0;

	p->error_args = new ParserErrorEventArgs (message, p->file_name, line_number, char_position, error_code, el, attr);

	LOG_XAML ("PARSER ERROR, STOPPING PARSING:  (%d) %s  line: %d   char: %d\n", error_code, message,
		  line_number, char_position);

	XML_StopParser (p->parser, FALSE);
}

void
expat_parser_error (XamlParserInfo *p, XML_Error expat_error)
{
	// Already had an error
	if (p->error_args)
		return;

	int error_code;
	char *message;
	
	LOG_XAML ("expat error is:  %d\n", expat_error);
	
	switch (expat_error) {
	case XML_ERROR_DUPLICATE_ATTRIBUTE:
		error_code = 5031;
		message = g_strdup ("wfc: unique attribute spec");
		break;
	case XML_ERROR_UNBOUND_PREFIX:
		error_code = 5055;
		message = g_strdup ("undeclared prefix");
		break;
	case XML_ERROR_NO_ELEMENTS:
		error_code = 5000;
		message = g_strdup ("unexpected end of input");
		break;
	default:
		error_code = expat_error;
		message = g_strdup_printf ("Unhandled XML error %s", XML_ErrorString (expat_error));
		break;
	}

	parser_error (p, NULL, NULL, error_code, message);

	g_free (message);
}

static void
start_element (void *data, const char *el, const char **attr)
{
	XamlParserInfo *p = (XamlParserInfo *) data;
	XamlElementInfo *elem = NULL;
	XamlElementInstance *inst;

	const char *dot = strchr (el, '.');
	if (!dot)
		elem = p->current_namespace->FindElement (p, el);

	if (p->error_args)
		return;

	
	if (elem) {
		if (p->hydrate_expecting){
			Type::Kind expecting_type =  p->hydrate_expecting->GetObjectType ();

			if (elem->GetKind () != expecting_type){
				parser_error (p, el, NULL, -1,
					      g_strdup_printf ("Invalid top-level element found %s, expecting %s", el,
							       Type::Find (expecting_type)->GetName ()));
				return;
			}
			inst = elem->CreateWrappedElementInstance (p, p->hydrate_expecting);
			p->hydrate_expecting = NULL;
		} else
			inst = elem->CreateElementInstance (p);

		if (!inst)
			return;

		inst->parent = p->current_element;

		if (!p->top_element) {
			p->top_element = inst;
			if (inst->GetAsDependencyObject ())
				NameScope::SetNameScope (inst->GetAsDependencyObject (), p->namescope);
		}

		inst->SetAttributes (p, attr);
		if (p->error_args)
			return;

		if (inst->IsDependencyObject ()) {
			if (p->current_element){
				if (p->current_element->info) {
					p->current_element->AddChild (p, inst);
					if (p->error_args)
						return;
				}
			}
		}
	} else {
		// it's actually valid (from SL point of view) to have <Ellipse.Triggers> inside a <Rectangle>
		// however we can't add properties to something bad, like a <Recta.gle> element
		XamlElementInfo *prop_info = NULL;

		if (dot) {
			gchar *prop_elem = g_strndup (el, dot - el);
			prop_info = p->current_namespace->FindElement (p, prop_elem);
			g_free (prop_elem);
		}

		if (prop_info != NULL) {
			inst = prop_info->CreatePropertyElementInstance (p, g_strdup (el));
			inst->parent = p->current_element;

			if (attr [0] != NULL) {
				// It appears there is a bug in the error string but it matches the MS runtime
				parser_error (p, el, NULL, 2018, g_strdup_printf ("The element %s does not support attributes.", attr [0]));
				return;
			}

			if (!p->top_element) {
				if (Type::Find (prop_info->GetKind ())->IsSubclassOf (Type::COLLECTION)) {
					XamlElementInstance *wrap = prop_info->CreateElementInstance (p);
					NameScope::SetNameScope (wrap->GetAsDependencyObject (), p->namescope);
					p->top_element = wrap;
					p->current_element = wrap;
					return;
				}
			}
		} else {
			g_warning ("Unknown element 1: %s.", el);
			parser_error (p, el, NULL, 2007, g_strdup_printf ("Unknown element: %s.", el));
			return;
		}
	}

	if (p->current_element) {
		p->current_element->children->Append (inst);
	}
	p->current_element = inst;
}

static void
flush_char_data (XamlParserInfo *p)
{
	if (!p->cdata || !p->current_element)
		return;

	if (p->current_element->element_type == XamlElementInstance::ELEMENT) {
		if (!p->current_element->TrySetContentProperty (p, p->cdata->str) && p->cdata_content) {
			char *err = g_strdup_printf ("%s does not support text content.", p->current_element->element_name);
			parser_error (p, p->current_element->element_name, NULL, 2011, err);
		}
	} else if (p->current_element->element_type == XamlElementInstance::PROPERTY) {
		if (p->cdata_content && p->current_element->parent && !p->current_element->parent->SetProperty (p, p->current_element, p->cdata->str)) {
			char *err = g_strdup_printf ("%s does not support text content.", p->current_element->element_name);
			parser_error (p, p->current_element->element_name, NULL, 2011, err);
		}
	}
	
	if (p->cdata) {
		g_string_free (p->cdata, TRUE);
		p->cdata_content = false;
		p->cdata = NULL;
	}
}

static void
start_element_handler (void *data, const char *el, const char **attr)
{
	XamlParserInfo *p = (XamlParserInfo *) data;

	if (p->error_args)
		return;

	char **name = g_strsplit (el, "|",  -1);
	XamlNamespace *next_namespace = NULL;
	char *element = NULL;
	char *err;
	
	if (g_strv_length (name) == 2) {
		// Find the proper namespace for our next element
		next_namespace = (XamlNamespace *) g_hash_table_lookup (p->namespace_map, name [0]);
		element = name [1];
	}
	
	if (!next_namespace && p->implicit_default_namespace) {
		// Use the default namespace for the next element
		next_namespace = default_namespace;
		element = name [0];
	}
	p->next_element = element;

	flush_char_data (p);
	
	// Now update our namespace
	p->current_namespace = next_namespace;
	
	if (!p->current_namespace) {
		if (name [1])
			err = g_strdup_printf ("No handlers available for namespace: '%s' (%s)\n", name[0], el);
		else
			err = g_strdup_printf ("No namespace mapping available for element: '%s'\n", el);
		
		parser_error (p, name [1], NULL, -1, err);
		g_strfreev (name);
		return;
	}

	p->next_element = NULL;
	start_element (data, element, attr);

	g_strfreev (name);
}

static void
end_element_handler (void *data, const char *el)
{
	XamlParserInfo *info = (XamlParserInfo *) data;

	if (info->error_args)
		return;

	if (!info->current_element) {
		g_warning ("info->current_element == NULL, current_element = %p (%s)\n",
			   info->current_element, info->current_element ? info->current_element->element_name : "<NULL>");
		return;
	}

	switch (info->current_element->element_type) {
	case XamlElementInstance::ELEMENT:
		flush_char_data (info);
		if (!info->current_element->IsDependencyObject () && info->current_element->parent) {
			info->current_element->parent->AddChild (info, info->current_element);
		}
		break;
	case XamlElementInstance::PROPERTY: {
		List::Node *walk = info->current_element->children->First ();
		while (walk) {
			XamlElementInstance *child = (XamlElementInstance *) walk;
			if (info->current_element->parent) {
				info->current_element->parent->SetProperty (info, info->current_element, child);
				break;
			}
			walk = walk->next;
		}
		flush_char_data (info);
		break;
	}
	}

	info->current_element = info->current_element->parent;
}

static void
char_data_handler (void *data, const char *in, int inlen)
{
	XamlParserInfo *p = (XamlParserInfo *) data;
	register const char *inptr = in;
	const char *inend = in + inlen;
	const char *start;
	
	if (p->error_args)
		return;
	
	if (!p->cdata) {
		p->cdata = g_string_new ("");
		
		if (g_ascii_isspace (*inptr)) {
			g_string_append_c (p->cdata, ' ');
			inptr++;
			
			while (inptr < inend && g_ascii_isspace (*inptr))
				inptr++;
		}
		
		if (inptr == inend)
			return;
	} else if (g_ascii_isspace (p->cdata->str[p->cdata->len - 1])) {
		// previous cdata chunk ended with lwsp, skip over leading lwsp for this chunk
		while (inptr < inend && g_ascii_isspace (*inptr))
			inptr++;
	}
	
	while (inptr < inend) {
		start = inptr;
		while (inptr < inend && !g_ascii_isspace (*inptr))
			inptr++;
		
		if (inptr > start) {
			g_string_append_len (p->cdata, start, inptr - start);
			p->cdata_content = true;
		}
		
		if (inptr < inend) {
			g_string_append_c (p->cdata, ' ');
			inptr++;
			
			while (inptr < inend && g_ascii_isspace (*inptr))
				inptr++;
		}
	}
}

static void
start_namespace_handler (void *data, const char *prefix, const char *uri)
{
	XamlParserInfo *p = (XamlParserInfo *) data;

	if (p->error_args)
		return;

	if (p->loader != NULL && p->loader->callbacks.import_xaml_xmlns != NULL)
		p->loader->callbacks.import_xaml_xmlns (uri);

	for (int i = 0; default_namespace_names [i]; i++) {
		if (!strcmp (default_namespace_names [i], uri)) {
			g_hash_table_insert (p->namespace_map, g_strdup (uri), default_namespace);
			return;
		}
	}
		
	if (!strcmp ("http://schemas.microsoft.com/winfx/2006/xaml", uri)){
		g_hash_table_insert (p->namespace_map, g_strdup (uri), x_namespace);
	} else {
		if (!p->loader) {
			return parser_error (p, (p->current_element ? p->current_element->element_name : NULL), prefix, -1,
					     g_strdup_printf ("No managed element callback installed to handle %s", uri));
		}

		if (!prefix) {
			parser_error (p, (p->current_element ? p->current_element->element_name : NULL), NULL, 2262,
				      g_strdup ("AG_E_PARSER_NAMESPACE_NOT_SUPPORTED"));
			return;
		}
		
		ManagedNamespace *c = new ManagedNamespace (g_strdup (uri));
		g_hash_table_insert (p->namespace_map, g_strdup (c->xmlns), c);
	}
}

static void
start_doctype_handler (void *data,
		const XML_Char *doctype_name,
		const XML_Char *sysid,
		const XML_Char *pubid,
		int has_internal_subset)
{
	XamlParserInfo *p = (XamlParserInfo *) data;

	if (sysid)
		parser_error (p, NULL, NULL, 5050, g_strdup ("DTD was found but is prohibited"));
}

static void
add_default_namespaces (XamlParserInfo *p)
{
	p->implicit_default_namespace = true;
	g_hash_table_insert (p->namespace_map, g_strdup ("http://schemas.microsoft.com/winfx/2006/xaml/presentation"), default_namespace);
	g_hash_table_insert (p->namespace_map, g_strdup ("http://schemas.microsoft.com/winfx/2006/xaml"), x_namespace);
}

static void
print_tree (XamlElementInstance *el, int depth)
{
#if DEBUG
	if (debug_flags & RUNTIME_DEBUG_XAML) {
		for (int i = 0; i < depth; i++)
			printf ("\t");
	
		const char *name = NULL;

		if (el->element_type == XamlElementInstance::ELEMENT && el->IsDependencyObject ())
			name = el->GetAsDependencyObject ()->GetName ();
		printf ("%s  (%s)  (%p) (%s)\n", el->element_name, name ? name : "-no name-", el->parent, el->element_type == XamlElementInstance::PROPERTY ? "PROPERTY" : "ELEMENT");

		for (List::Node *walk = el->children->First (); walk != NULL; walk = walk->next) {
			XamlElementInstance *el = (XamlElementInstance *) walk;
			print_tree (el, depth + 1);
		}
	}
#endif
}

void		
xaml_parse_xmlns (const char* xmlns, char** type_name, char** ns, char** assembly)
{
	const char delimiters[]  = ";";
	char* decl;
	char* buffer = g_strdup (xmlns);
	
	*type_name = NULL;
	*ns = NULL;
	*assembly = NULL;
	
	decl = strtok (buffer, delimiters);
	while (decl != NULL) {
		if (strstr (decl, "clr-namespace:") == decl) {
			if (*ns)
				g_free (*ns);
			*ns = g_strdup (decl + 14);
		} else if (strstr (decl, "assembly=") == decl) {
			if (*assembly)
				g_free (*assembly);
			*assembly = g_strdup (decl + 9);
		} else {
			if (*type_name)
				g_free (*type_name);
			*type_name = g_strdup (decl);			
		}
		
		decl = strtok (NULL, delimiters);
	}
	g_free (buffer);
}

DependencyObject *
XamlLoader::CreateFromFile (const char *xaml_file, bool create_namescope,
			    Type::Kind *element_type)
{
	DependencyObject *res = NULL;
	XamlParserInfo *parser_info = NULL;
	XML_Parser p = NULL;
	bool first_read = true;
	const char *inptr, *inend;
	TextStream *stream;
	char buffer[4096];
	ssize_t nread, n;
	
	LOG_XAML ("attemtping to load xaml file: %s\n", xaml_file);
	
	stream = new TextStream ();
	if (!stream->Open (xaml_file, false)) {
		LOG_XAML ("can not open file\n");
		goto cleanup_and_return;
	}
	
	if (!(p = XML_ParserCreateNS ("UTF-8", '|'))) {
		LOG_XAML ("can not create parser\n");
		goto cleanup_and_return;
	}

	parser_info = new XamlParserInfo (p, xaml_file);
	
	parser_info->namescope->SetTemporary (!create_namescope);

	parser_info->loader = this;

	// TODO: This is just in here temporarily, to make life less difficult for everyone
	// while we are developing.  
	add_default_namespaces (parser_info);

	XML_SetUserData (p, parser_info);

	XML_SetElementHandler (p, start_element_handler, end_element_handler);
	XML_SetCharacterDataHandler (p, char_data_handler);
	XML_SetNamespaceDeclHandler(p, start_namespace_handler, NULL);
	XML_SetDoctypeDeclHandler(p, start_doctype_handler, NULL);

	/*
	XML_SetProcessingInstructionHandler (p, proc_handler);
	*/
	
	while ((nread = stream->Read (buffer, sizeof (buffer))) >= 0) {
		inptr = buffer;
		n = nread;
		
		if (first_read && nread > 0) {
			// Remove preceding white space
			inend = buffer + nread;
			
			while (inptr < inend && isspace ((unsigned char) *inptr))
				inptr++;
			
			if (inptr == inend)
				continue;
			
			n = (inend - inptr);
			first_read = false;
		}
		
		if (!XML_Parse (p, inptr, n, nread == 0)) {
			expat_parser_error (parser_info, XML_GetErrorCode (p));
			goto cleanup_and_return;
		}
		
		if (nread == 0)
			break;
	}
	
	print_tree (parser_info->top_element, 0);
	
	if (parser_info->top_element) {
		res = parser_info->top_element->GetAsDependencyObject ();
		if (element_type)
			*element_type = parser_info->top_element->info->GetKind ();

		if (parser_info->error_args) {
			*element_type = Type::INVALID;
			goto cleanup_and_return;
		}
		
		res->ref ();
	}
	
 cleanup_and_return:
	
	if (!parser_info)
		error_args = new ParserErrorEventArgs ("Error opening xaml file", xaml_file, 0, 0, 1, "", "");
	else if (parser_info->error_args)
		error_args = parser_info->error_args;
	
	delete stream;
	
	if (p)
		XML_ParserFree (p);
	
	if (parser_info)
		delete parser_info;
	
	return res;
}

DependencyObject *
XamlLoader::CreateFromString (const char *xaml, bool create_namescope,
			      Type::Kind *element_type)
{
	return HydrateFromString (xaml, NULL, create_namescope, element_type);
}

/**
 * Hydrates an existing DependencyObject (@object) with the contents from the @xaml
 * data
 */
DependencyObject *
XamlLoader::HydrateFromString (const char *xaml, DependencyObject *object, bool create_namescope, Type::Kind *element_type)
{
	XML_Parser p = XML_ParserCreateNS ("utf-8", '|');
	XamlParserInfo *parser_info = NULL;
	DependencyObject *res = NULL;
	char *start = (char*)xaml;

	if (!p) {
		LOG_XAML ("can not create parser\n");
		goto cleanup_and_return;
	}
	
#if 0
	if (true) {
		static int id = 0;
		char filename[64];
		FILE *fp;
		
		sprintf (filename, "createFromXaml.%d.xaml", id++);
		if ((fp = fopen (filename, "wt")) != NULL) {
			fwrite (xaml, strlen (xaml), 1, fp);
			fclose (fp);
		}
	}
#endif
	
	parser_info = new XamlParserInfo (p, NULL);

	parser_info->namescope->SetTemporary (!create_namescope);

	parser_info->loader = this;
	
	//
	// If we are hydrating, we are not null
	//
	if (object != NULL) {
		parser_info->hydrate_expecting = object;
		parser_info->hydrating = true;
		object->SetSurface (GetSurface());
		object->ref ();
	} else {
		parser_info->hydrate_expecting = NULL;
		parser_info->hydrating = false;
	}
	
	// from_str gets the default namespaces implictly added
	add_default_namespaces (parser_info);

	XML_SetUserData (p, parser_info);

	XML_SetElementHandler (p, start_element_handler, end_element_handler);
	XML_SetCharacterDataHandler (p, char_data_handler);
	XML_SetNamespaceDeclHandler(p, start_namespace_handler, NULL);
	XML_SetDoctypeDeclHandler(p, start_doctype_handler, NULL);

	/*
	XML_SetProcessingInstructionHandler (p, proc_handler);
	*/

	// don't freak out if the <?xml ... ?> isn't on the first line (see #328907)
	while (isspace ((unsigned char) *start))
		start++;

	if (!XML_Parse (p, start, strlen (start), TRUE)) {
		expat_parser_error (parser_info, XML_GetErrorCode (p));
		LOG_XAML ("error parsing:  %s\n\n", xaml);
		goto cleanup_and_return;
	}
	
	print_tree (parser_info->top_element, 0);
	
	if (parser_info->top_element) {
		res = parser_info->top_element->GetAsDependencyObject ();
		if (element_type)
			*element_type = parser_info->top_element->info->GetKind ();

		if (parser_info->error_args) {
			res = NULL;
			if (element_type)
				*element_type = Type::INVALID;
			goto cleanup_and_return;
		}
		if (object == NULL)
			res->ref ();
	}

 cleanup_and_return:
	
	if (parser_info->error_args) {
		error_args = parser_info->error_args;
		printf ("Could not parse element %s, attribute %s, error: %s\n",
			error_args->xml_element,
			error_args->xml_attribute,
			error_args->error_message);
	}
	
	if (p)
		XML_ParserFree (p);
	if (parser_info)
		delete parser_info;
	return res;
}

DependencyObject*
XamlLoader::CreateFromFileWithError (const char *xaml_file, bool create_namescope, Type::Kind *element_type, MoonError *error)
{
	DependencyObject *res = CreateFromFile (xaml_file, create_namescope, element_type);
	if (error_args && error_args->error_code != -1) {
		MoonError::FillIn (error, MoonError::XAML_PARSE_EXCEPTION, error_args->error_message);
	}
	return res;
}

DependencyObject*
XamlLoader::CreateFromStringWithError  (const char *xaml, bool create_namescope, Type::Kind *element_type, MoonError *error)
{
	DependencyObject *res = CreateFromString (xaml, create_namescope, element_type);
	if (error_args && error_args->error_code != -1) {
		MoonError::FillIn (error, MoonError::XAML_PARSE_EXCEPTION, error_args->error_message);
	}
	return res;
}

DependencyObject*
XamlLoader::HydrateFromStringWithError (const char *xaml, DependencyObject *object, bool create_namescope, Type::Kind *element_type, MoonError *error)
{
	DependencyObject *res = HydrateFromString (xaml, object, create_namescope, element_type);
	if (error_args && error_args->error_code != -1) {
		MoonError::FillIn (error, MoonError::XAML_PARSE_EXCEPTION, error_args->error_message);
	}
	return res;
}

int
property_name_index (const char *p)
{
	for (int i = 0; p [i]; i++) {
		if (p [i] == '.' && p [i + 1])
			return i + 1;
	}
	return -1;
}

static int
parse_int (const char **pp, const char *end)
{
	const char *p = *pp;
#if 0
	if (optional && AtEnd)
		return 0;
#endif

	int res = 0;
	int count = 0;

	while (p <= end && isdigit (*p)) {
		res = res * 10 + *p - '0';
		p++;
		count++;
	}

#if 0
	if (count == 0)
		formatError = true;
#endif

	*pp = p;
	return res;
}

static gint64
parse_ticks (const char **pp, const char *end)
{
	gint64 mag = 1000000;
	gint64 res = 0;
	bool digitseen = false;

	const char *p = *pp;

	while (mag > 0 && p <= end && isdigit (*p)) {
		res = res + (*p - '0') * mag;
		p++;
		mag = mag / 10;
		digitseen = true;
	}

#if 0
	if (!digitseen)
		formatError = true;
#endif

	*pp = p;
	return res;
}

bool
time_span_from_str (const char *str, TimeSpan *res)    
{
	const char *end = str + strlen (str);
	const char *p;

	bool negative = false;
	int days;
	int hours;
	int minutes;
	int seconds;
	gint64 ticks;

	p = str;

	if (*p == '-') {
		p++;
		negative = true;
	}

	days = parse_int (&p, end);
	
	if (*p == '.') {
		p++;
		hours = parse_int (&p, end);
	}
	else {
		hours = days;
		days = 0;
	}
	
	if (*p == ':') p++;
	minutes = parse_int (&p, end);
	if (*p == ':') p++;
	seconds = parse_int (&p, end);
	if (*p == '.') {
		p++;
		ticks = parse_ticks (&p, end);
	}
	else {
		ticks = 0;
	}

	gint64 t = (days * 86400) + (hours * 3600) + (minutes * 60) + seconds;
	t *= 10000000L;

	*res = negative ? (-t - ticks) : (t + ticks);

	return true;
}

bool
repeat_behavior_from_str (const char *str, RepeatBehavior *res)
{
	if (!g_strcasecmp ("Forever", str)) {
		*res = RepeatBehavior::Forever;
		return true;
	}

	// check for "<float>x".

	// XXX more validation work is needed here.. but how do we
	// report an error?
	const char *x = strchr (str, 'x');
	if (x) {
		if (*(x + 1) != '\0') {
			return false;
		}
		else {
			char *endptr;
			errno = 0;
			double d = g_ascii_strtod (str, &endptr);

			if (errno || endptr == str)
				return false;

			*res = RepeatBehavior (d);
			return true;
		}
	}

	/* XXX RepeatBehavior='XX:XX:XX' syntax is NOT correctly supported by
	   Silverlight 1.0 (most likely a bug). It works fine in Silverlight 2.0 
	   though. We currently stick to the 2.0 behavior, not replicating the bug
	   from 1.0. 
	*/
	TimeSpan t;
	if (!time_span_from_str (str, &t))
		return false;

	*res = RepeatBehavior (t);

	return true;
}

bool
duration_from_str (const char *str, Duration *res)
{
	if (!g_strcasecmp ("Automatic", str)) {
		*res = Duration::Automatic;
		return true;
	}

	if (!g_strcasecmp ("Forever", str)) {
		*res = Duration::Forever;
		return true;
	}

	TimeSpan ts;
	if (!time_span_from_str (str, &ts))
		return false;

	*res = Duration (ts);
	return true;
}

bool
keytime_from_str (const char *str, KeyTime *res)
{
	if (!g_strcasecmp ("Uniform", str)) {
		*res = KeyTime::Uniform;
		return true;
	}

	if (!g_strcasecmp ("Paced", str)) {
		*res = KeyTime::Paced;
		return true;
	}

	/* check for a percentage first */
	const char *last = str + strlen(str) - 1;
	if (*last == '%') {
		char *ep;
		double pct = g_ascii_strtod (str, &ep);
		if (ep == last) {
			*res = KeyTime (pct);
			return true;
		}
	}

	TimeSpan ts;
	if (!time_span_from_str (str, &ts))
		return false;

	*res = KeyTime (ts);
	return true;
}

bool
key_spline_from_str (const char *str, KeySpline **res)
{	
	PointCollection *pts = PointCollection::FromStr (str);

	if (!pts)
		return false;

	if (pts->GetCount () != 2) { 
		pts->unref ();
		return false;
	}

	*res = new KeySpline (*pts->GetValueAt (0)->AsPoint (), *pts->GetValueAt (1)->AsPoint ());
	
	pts->unref ();
	
	return true;
}

Matrix *
matrix_from_str (const char *str)
{
	DoubleCollection *values = DoubleCollection::FromStr (str);
	Matrix *matrix;
	
	if (!values)
		return new Matrix ();

	if (values->GetCount () < 6) {
		values->unref ();
		return NULL;
	}

	matrix = new Matrix ();
	matrix->SetM11 (values->GetValueAt (0)->AsDouble ());
	matrix->SetM12 (values->GetValueAt (1)->AsDouble ());
	matrix->SetM21 (values->GetValueAt (2)->AsDouble ());
	matrix->SetM22 (values->GetValueAt (3)->AsDouble ());
	matrix->SetOffsetX (values->GetValueAt (4)->AsDouble ());
	matrix->SetOffsetY (values->GetValueAt (5)->AsDouble ());
	
	values->unref ();

	return matrix;
}

#if SL_2_0
bool
grid_length_from_str (const char *str, GridLength *grid_length)
{
	if (str [0] == '*') {
		*grid_length = GridLength (0, GridUnitTypeStar);
		return true;
	}
		
	if (!strcmp (str, "Auto")) {
		*grid_length = GridLength ();
		return true;
	}

	char *endptr;
	errno = 0;
	double d = g_ascii_strtod (str, &endptr);

	if (errno || endptr == str)
		return false;

	*grid_length = GridLength (d, GridUnitTypePixel);
	return true;
}
#endif

static void
advance (char **in)
{
	char *inptr = *in;
	
	while (*inptr && !g_ascii_isalnum (*inptr) && *inptr != '.' && *inptr != '-' && *inptr != '+')
		inptr = g_utf8_next_char (inptr);
	
	*in = inptr;
}

static bool
get_point (Point *p, char **in)
{
	char *end, *inptr = *in;
	double x, y;
	
	x = g_ascii_strtod (inptr, &end);
	if (end == inptr)
		return false;
	
	inptr = end;
	while (g_ascii_isspace (*inptr))
		inptr++;
	
	if (*inptr == ',')
		inptr++;
	
	while (g_ascii_isspace (*inptr))
		inptr++;
	
	y = g_ascii_strtod (inptr, &end);
	if (end == inptr)
		return false;
	
	p->x = x;
	p->y = y;
	
	*in = end;
	
	return true;
}

static void
make_relative (const Point *cp, Point *mv)
{
	mv->x += cp->x;
	mv->y += cp->y;
}

static bool
more_points_available (char **in)
{
	char *inptr = *in;
	
	while (g_ascii_isspace (*inptr) || *inptr == ',')
		inptr++;
	
	*in = inptr;
	
	return (g_ascii_isdigit (*inptr) || *inptr == '.' || *inptr == '-' || *inptr == '+');
}

Point *
get_point_array (char *in, GSList *pl, int *count, bool relative, Point *cp, Point *last)
{
	int c = *count;

	while (more_points_available (&in)) {
		Point *n = new Point ();
		
		if (!get_point (n, &in)) {
			delete n;
			break;
		}
		
		advance (&in);
		
		if (relative) make_relative (cp, n);

		pl = g_slist_append (pl, n);
		c++;
	}

	Point *t;
	Point *pts = new Point [c];
	for (int i = 0; i < c; i++) {
		t = (Point *) pl->data;
		pts [i].x = t->x;
		pts [i].y = t->y;

		// locally allocated points needs to be deleted
		if (i >= *count)
			delete t;
		pl = pl->next;
	}

	last = &pts [c - 1];
	*count = c;

	return pts;
}

Geometry *
geometry_from_str (const char *str)
{
	char *inptr = (char *) str;
	Point cp = Point (0, 0);
	Point cp1, cp2, cp3;
	Point start;
	char *end;
	PathGeometry *pg = NULL;
	FillRule fill_rule = FillRuleEvenOdd;
	bool cbz = false; // last figure is a cubic bezier curve
	bool qbz = false; // last figure is a quadratic bezier curve
	Point cbzp, qbzp; // points needed to create "smooth" beziers

	moon_path *path = moon_path_new (10);

	while (*inptr) {
		if (g_ascii_isspace (*inptr))
			inptr++;
		
		if (!inptr[0])
			break;
		
		bool relative = false;
		
		char c = *inptr;
		inptr = g_utf8_next_char (inptr);
		
		switch (c) {
		case 'f':
		case 'F':
			advance (&inptr);

			if (*inptr == '0')
				fill_rule = FillRuleEvenOdd;
			else if (*inptr == '1')
				fill_rule = FillRuleNonzero;
			else
				// FIXME: else it's a bad value and nothing should be rendered
				goto bad_pml; // partial: only this Path won't be rendered

			inptr = g_utf8_next_char (inptr);
			break;
		case 'm':
			relative = true;
		case 'M':
			if (!get_point (&cp1, &inptr))
				break;
			
			if (relative)
				make_relative (&cp, &cp1);

			// start point
			moon_move_to (path, cp1.x, cp1.y);

			start.x = cp.x = cp1.x;
			start.y = cp.y = cp1.y;

			advance (&inptr);
			while (more_points_available (&inptr)) {
				if (!get_point (&cp1, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp1);
				
				moon_line_to (path, cp1.x, cp1.y);
			}

			cp.x = cp1.x;
			cp.y = cp1.y;
			cbz = qbz = false;
			break;

		case 'l':
			relative = true;
		case 'L':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp1, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp1);

				moon_line_to (path, cp1.x, cp1.y);

				cp.x = cp1.x;
				cp.y = cp1.y;

				advance (&inptr);
			}
			cbz = qbz = false;
			break;
		}
		
		case 'h':
			relative = true;
		case 'H':
		{
			double x = g_ascii_strtod (inptr, &end);
			if (end == inptr)
				break;
			
			inptr = end;
			
			if (relative)
				x += cp.x;
			cp = Point (x, cp.y);

			moon_line_to (path, cp.x, cp.y);
			cbz = qbz = false;
			break;
		}
		
		case 'v':
			relative = true;
		case 'V':
		{
			double y = g_ascii_strtod (inptr, &end);
			if (end == inptr)
				break;
			
			inptr = end;
			
			if (relative)
				y += cp.y;
			cp = Point (cp.x, y);

			moon_line_to (path, cp.x, cp.y);
			cbz = qbz = false;
			break;
		}
		
		case 'c':
			relative = true;
		case 'C':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp1, &inptr))
					break;

				if (relative)
					make_relative (&cp, &cp1);
			
				advance (&inptr);

				if (!get_point (&cp2, &inptr))
					break;
			
				if (relative)
					make_relative (&cp, &cp2);
			
				advance (&inptr);
			
				if (!get_point (&cp3, &inptr))
					break;
			
				if (relative)
					make_relative (&cp, &cp3);
			
				advance (&inptr);
			
				moon_curve_to (path, cp1.x, cp1.y, cp2.x, cp2.y, cp3.x, cp3.y);

				cp1.x = cp3.x;
				cp1.y = cp3.y;
			}
			cp.x = cp3.x;
			cp.y = cp3.y;
			cbz = true;
			cbzp.x = cp2.x;
			cbzp.y = cp2.y;
			qbz = false;
			break;
		}
		case 's':
			relative = true;
		case 'S':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp2, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp2);

				advance (&inptr);
				
				if (!get_point (&cp3, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp3);

				if (cbz) {
					cp1.x = 2 * cp.x - cbzp.x;
					cp1.y = 2 * cp.y - cbzp.y;
				} else
					cp1 = cp;

				moon_curve_to (path, cp1.x, cp1.y, cp2.x, cp2.y, cp3.x, cp3.y);
				cbz = true;
				cbzp.x = cp2.x;
				cbzp.y = cp2.y;

				cp.x = cp3.x;
				cp.y = cp3.y;

				advance (&inptr);
			}
			qbz = false;
			break;
		}
		case 'q':
			relative = true;
		case 'Q':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp1, &inptr))
					break;
			
				if (relative)
					make_relative (&cp, &cp1);

				advance (&inptr);
			
				if (!get_point (&cp2, &inptr))
					break;
			
				if (relative)
					make_relative (&cp, &cp2);
			
				advance (&inptr);

				moon_quad_curve_to (path, cp1.x, cp1.y, cp2.x, cp2.y);

				cp.x = cp2.x;
				cp.y = cp2.y;
			}
			qbz = true;
			qbzp.x = cp1.x;
			qbzp.y = cp1.y;
			cbz = false;
			break;
		}
		case 't':
			relative = true;
		case 'T':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp2, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp2);

				if (qbz) {
					cp1.x = 2 * cp.x - qbzp.x;
					cp1.y = 2 * cp.y - qbzp.y;
				} else
					cp1 = cp;

				moon_quad_curve_to (path, cp1.x, cp1.y, cp2.x, cp2.y);
				qbz = true;
				qbzp.x = cp1.x;
				qbzp.y = cp1.y;
				
				cp.x = cp2.x;
				cp.y = cp2.y;

				advance (&inptr);
			}
			cbz = false;
			break;
		}
		case 'a':
			relative = true;
		case 'A':
		{
			while (more_points_available (&inptr)) {
				if (!get_point (&cp1, &inptr))
					break;
				
				advance (&inptr);
				
				double angle = g_ascii_strtod (inptr, &end);
				if (end == inptr)
					break;
				
				inptr = end;
				advance (&inptr);
				
				int is_large = strtol (inptr, &end, 10);
				if (end == inptr)
					break;
				
				inptr = end;
				advance (&inptr);
				
				int sweep = strtol (inptr, &end, 10);
				if (end == inptr)
					break;
				
				inptr = end;
				advance (&inptr);
				
				if (!get_point (&cp2, &inptr))
					break;
				
				if (relative)
					make_relative (&cp, &cp2);

				moon_arc_to (path, cp1.x, cp1.y, angle, is_large, sweep, cp2.x, cp2.y);
					
				cp.x = cp2.x;
				cp.y = cp2.y;

				advance (&inptr);
			}
			cbz = qbz = false;
			break;
		}
		case 'z':
		case 'Z':
			moon_line_to (path, start.x, start.y);
			moon_close_path (path);
			moon_move_to (path, start.x, start.y);

			cp.x = start.x;
			cp.y = start.y;
			cbz = qbz = false;
			break;
		default:
			break;
		}
	}

	pg = new PathGeometry (path);
	pg->SetFillRule (fill_rule);
	return pg;

bad_pml:
	moon_path_destroy (path);
	return NULL;
}

bool
value_from_str_with_typename (const char *type_name, const char *prop_name, const char *str, Value **v, bool sl2)
{
	Type *t = Type::Find (type_name);
	if (!t)
		return false;

	return value_from_str (t->type, prop_name, str, v, sl2);
}

#define IS_NULL_OR_EMPTY(str)	(!str || (*str == 0))

bool
value_from_str (Type::Kind type, const char *prop_name, const char *str, Value** v, bool sl2)
{
	char *endptr;
	*v = NULL;
	
	if (!strcmp ("{x:Null}", str)) {
		*v = NULL;
		return true;
	}

	switch (type) {
	case Type::BOOL: {
		bool b;
		if (!g_strcasecmp ("true", str))
			b = true;
		else if (!g_strcasecmp ("false", str))
			b = false;
		else {
			// Check if it's a string representing a decimal value
			gint64 l;

			errno = 0;
			l = strtol (str, &endptr, 10);

			if (errno || endptr == str || *endptr || l > G_MAXINT32 || l < G_MININT32)
				return false;

			if (l == 0)
				b = false;
			else
				b = true;
		}
				

		*v = new Value (b);
		break;
	}
	case Type::DOUBLE: {
		double d;

		// empty string should not reset default values with 0
		if (IS_NULL_OR_EMPTY(str))
			return true;

		errno = 0;
		d = g_ascii_strtod (str, &endptr); 
		
		
		if (errno || endptr == str || *endptr) {
			if ((!strcmp (prop_name, "Width") || !strcmp (prop_name, "Height"))
			    && !strcmp (str, "Auto"))
				d = NAN;
			else 
				return false;
		}
		
		*v = new Value (d);
		break;
	}
	case Type::INT64: {
		gint64 l;

		errno = 0;
		l = strtol (str, &endptr, 10);

		if (errno || endptr == str || *endptr)
			return false;

		*v = new Value (l, Type::INT64);
		break;
	}
	case Type::TIMESPAN: {
		TimeSpan ts;

		if (!time_span_from_str (str, &ts))
			return false;

		*v = new Value (ts, Type::TIMESPAN);
		break;
	}
	case Type::INT32: {
		int i;

		if (isalpha (str [0]) && prop_name) {
			i = enums_str_to_int (prop_name, str, sl2);
			if (i == -1) {
				g_warning ("'%s' enum is not valid on '%s' property", str, prop_name);
				return false;
			}
		} else {
			errno = 0;
			long l = strtol (str, &endptr, 10);

			if (errno || endptr == str || *endptr)
				return false;

			i = (int) l;
		}

		*v = new Value (i);
		break;
	}
	case Type::STRING: {
		*v = new Value (str);
		break;
	}
	case Type::COLOR: {
		Color *c = color_from_str (str);
		if (c == NULL)
			return false;
		*v = new Value (*c);
		delete c;
		break;
	}
	case Type::REPEATBEHAVIOR: {
		RepeatBehavior rb = RepeatBehavior::Forever;

		if (!repeat_behavior_from_str (str, &rb))
			return false;

		*v = new Value (rb);
		break;
	}
	case Type::DURATION: {
		Duration d = Duration::Forever;

		if (!duration_from_str (str, &d))
			return false;

		*v = new Value (d);
		break;
	}
	case Type::KEYTIME: {
		KeyTime kt = KeyTime::Paced;

		if (!keytime_from_str (str, &kt))
			return false;

		*v = new Value (kt);
		break;
	}
	case Type::KEYSPLINE: {
		KeySpline *ks;

		if (!key_spline_from_str (str, &ks))
			return false;

		*v = Value::CreateUnrefPtr (ks);
		break;
	}
	case Type::BRUSH:
	case Type::SOLIDCOLORBRUSH: {
		// Only solid color brushes can be specified using attribute syntax
		SolidColorBrush *scb = new SolidColorBrush ();
		Color *c = color_from_str (str);
		
		if (c == NULL)
			return false;
		
		scb->SetColor (c);
		delete c;
		
		*v = new Value (scb);
		scb->unref ();
		break;
	}
	case Type::POINT: {
		Point p;

		if (!Point::FromStr (str, &p))
			return false;

		*v = new Value (p);
		break;
	}
	case Type::RECT: {
		Rect rect;

		if (!Rect::FromStr (str, &rect))
			return false;

		*v = new Value (rect);
		break;
	}
	case Type::DOUBLE_COLLECTION: {
		DoubleCollection *doubles = DoubleCollection::FromStr (str);
		if (!doubles)
			return false;

		*v = new Value (doubles);
		doubles->unref ();
		break;
	}
	case Type::POINT_COLLECTION: {
		PointCollection *points = PointCollection::FromStr (str);
		if (!points)
			return false;

		*v = new Value (points);
		points->unref ();
		break;
	}
	case Type::TRANSFORM: {
		if (IS_NULL_OR_EMPTY(str))
			return true;

		Matrix *mv = matrix_from_str (str);
		if (!mv)
			return false;

		MatrixTransform *t = new MatrixTransform ();
		t->SetValue (MatrixTransform::MatrixProperty, Value (mv));

		*v = new Value (t);
		t->unref ();
		mv->unref ();
		break;
	}
	case Type::MATRIX: {
		// note: unlike TRANSFORM this creates an empty, identity, matrix for an empty string
		Matrix *matrix = matrix_from_str (str);
		if (!matrix)
			return false;

		*v = new Value (matrix);
		matrix->unref ();
		break;
	}
	case Type::GEOMETRY: {
		Geometry *geometry = geometry_from_str (str);

		if (!geometry)
			return false;

		*v = new Value (geometry);
		geometry->unref ();
		break;
	}
#if SL_2_0
	case Type::THICKNESS: {
		Thickness t;

		if (!Thickness::FromStr (str, &t))
			return false;

		*v = new Value (t);
		break;
	}

	case Type::CORNERRADIUS: {
		CornerRadius c;

		if (!CornerRadius::FromStr (str, &c))
			return false;

		*v = new Value (c);
		break;
	}

	case Type::GRIDLENGTH: {
		GridLength grid_length;

		if (!grid_length_from_str (str, &grid_length))
			return false;

		*v = new Value (grid_length);
		break;
	}

	case Type::MULTISCALETILESOURCE:
	case Type::DEEPZOOMIMAGETILESOURCE: {
		// As far as I know the only thing you can create here is a URI based DeepZoomImageTileSource

		Uri *uri = new Uri ();
		if (!uri->Parse (str)) {
			delete uri;
			return false;
		}
		*v = new Value (new DeepZoomImageTileSource (str));
		delete uri;

		break;
	}
#endif
	default:
		// we don't care about NULL or empty values
		return IS_NULL_OR_EMPTY(str);
	}

	return true;
}

bool
XamlElementInstance::TrySetContentProperty (XamlParserInfo *p, XamlElementInstance *value)
{
	const char* prop_name = info->GetContentProperty (p);

	if (!prop_name)
		return false;

	DependencyProperty *dep = DependencyProperty::GetDependencyProperty (info->GetKind (), prop_name);
	if (!dep)
		return false;

	Type *prop_type = Type::Find (dep->GetPropertyType());
	bool is_collection = prop_type->IsSubclassOf (Type::DEPENDENCY_OBJECT_COLLECTION);

	if (!is_collection && Type::Find (value->info->GetKind ())->IsSubclassOf (dep->GetPropertyType())) {
		MoonError err;
		if (!item->SetValueWithError (NULL /* XXX */, dep, value->GetAsValue (), &err)) {
		    parser_error (p, value->element_name, NULL, err.code, err.message);
		    return false;
		}
		return true;
	}

	// We only want to enter this if statement if we are NOT dealing with the content property element,
	// otherwise, attempting to use explicit property setting, would add the content property element
	// to the content property element collection
	if (is_collection && dep->GetPropertyType() != value->info->GetKind ()) {
		Value *col_v = item->GetValue (dep);
		Collection *col;
			
		if (!col_v) {
			col = collection_new (dep->GetPropertyType ());
			item->SetValue (dep, Value (col));
			col->unref ();
		} else {
			col = (Collection *) col_v->AsCollection ();
		}

		MoonError err;
		if (-1 == col->AddWithError (value->GetAsValue (), &err)) {
			parser_error (p, value->element_name, NULL, err.code, err.message);
			return false;
		}
			
		return true;
	}

	return false;
}

bool
XamlElementInstance::TrySetContentProperty (XamlParserInfo *p, const char *value)
{
	const char* prop_name = info->GetContentProperty (p);
	if (!prop_name)
		return false;

	Type::Kind prop_type = p->current_element->info->GetKind ();
	DependencyProperty *content = DependencyProperty::GetDependencyProperty (prop_type, prop_name);
	
	// TODO: There might be other types that can be specified here,
	// but string is all i have found so far.  If you can specify other
	// types, i should pull the property setting out of set_attributes
	// and use that code

	if (content && (content->GetPropertyType ()) == Type::STRING && value) {
		item->SetValue (content, Value (g_strstrip (p->cdata->str)));
		return true;
	} else if (Type::Find (info->GetKind ())->IsSubclassOf (Type::TEXTBLOCK)) {
		TextBlock *textblock = (TextBlock *) item;
		InlineCollection *inlines = textblock->GetInlines ();
		Inline *last = NULL;
		
		if (inlines && inlines->GetCount () > 0)
			last = inlines->GetValueAt (inlines->GetCount () - 1)->AsInline ();
		
		if (!p->cdata_content) {
			if (p->next_element && !strcmp (p->next_element, "Run") && last && last->GetObjectType () == Type::RUN &&
			    !last->autogen) {
				// LWSP between <Run> elements is to be treated as a single-SPACE <Run> element
				// Note: p->cdata is already canonicalized
			} else {
				// This is one of the following cases:
				//
				// 1. LWSP before the first <Run> element
				// 2. LWSP after the last <Run> element
				// 3. LWSP between <Run> and <LineBreak> elements
				return true;
			}
		} else {
			if (!p->next_element)
				g_strchomp (p->cdata->str);

			if (!last || last->GetObjectType () != Type::RUN || last->autogen)
				g_strchug (p->cdata->str);
		}

		Run *run = new Run ();
		run->SetText (p->cdata->str);
		
		if (!inlines) {
			inlines = new InlineCollection ();
			textblock->SetInlines (inlines);
			inlines->unref ();
		}
		
		inlines->Add (run);
		run->unref ();
		return true;
	}

	return false;
}


static XamlElementInfo *
create_element_info_from_imported_managed_type (XamlParserInfo *p, const char *name)
{
	if (!p->loader)
		return NULL;

	Value *v = new Value ();
	if (!p->loader->CreateObject (NULL, NULL, name, v) /*|| v->Is (Type::DEPENDENCY_OBJECT)*/) {
		delete v;
		return NULL;
	}

	XamlElementInfoImportedManaged *info = new  XamlElementInfoImportedManaged (g_strdup (name), NULL, v);
	DependencyObject *dob = v->AsDependencyObject ();
	dob->SetSurface (p->loader->GetSurface ());

	return info;
}


const char *
XamlElementInfoNative::GetContentProperty (XamlParserInfo *p)
{
	return type->GetContentPropertyName ();
}

XamlElementInstance *
XamlElementInfoNative::CreateElementInstance (XamlParserInfo *p)
{
	if (type->IsValueType ())
		return new XamlElementInstanceValueType (this, p, GetName (), XamlElementInstance::ELEMENT);
	else if (type->IsSubclassOf (Type::FRAMEWORKTEMPLATE))
		return new XamlElementInstanceTemplate (this, p, GetName (), XamlElementInstance::ELEMENT);
	else
		return new XamlElementInstanceNative (this, p, GetName (), XamlElementInstance::ELEMENT);
}

XamlElementInstance *
XamlElementInfoNative::CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o)
{
	XamlElementInstance *res = new XamlElementInstanceNative (this, p, GetName (), XamlElementInstance::ELEMENT, false);
	res->SetDependencyObject (o);

	return res;
}

XamlElementInstance *
XamlElementInfoNative::CreatePropertyElementInstance (XamlParserInfo *p, const char *name)
{
	return new XamlElementInstanceNative (this, p, name, XamlElementInstance::PROPERTY, false);
}

XamlElementInstanceNative::XamlElementInstanceNative (XamlElementInfoNative *element_info, XamlParserInfo *parser_info, const char *name, ElementType type, bool create_item) :
	XamlElementInstance (element_info, name, type)
{
	this->element_info = element_info;
	this->parser_info = parser_info;
	if (create_item)
		SetDependencyObject (CreateItem ());
}



DependencyObject *
XamlElementInstanceNative::CreateItem ()
{
	XamlElementInstance *walk = parser_info->current_element;
	Type *type = element_info->GetType ();

	DependencyObject *item = NULL;
	DependencyProperty *dep = NULL;

	if (type->IsSubclassOf (Type::COLLECTION) || type->IsSubclassOf (Type::RESOURCE_DICTIONARY)) {
		// If we are creating a collection, try walking up the element tree,
		// to find the parent that we belong to and using that instance for
		// our collection, instead of creating a new one

		// We attempt to advance past the property setter, because we might be dealing with a
		// content element
		
		if (walk && walk->element_type == XamlElementInstance::PROPERTY) {
			char **prop_name = g_strsplit (walk->element_name, ".", -1);
			
			walk = walk->parent;
			dep = DependencyProperty::GetDependencyProperty (walk->info->GetKind (), prop_name [1]);

			g_strfreev (prop_name);
		} else if (walk && walk->info->GetContentProperty (parser_info)) {
			dep = DependencyProperty::GetDependencyProperty (walk->info->GetKind (),
					(char *) walk->info->GetContentProperty (parser_info));			
		}

		if (dep && Type::Find (dep->GetPropertyType())->IsSubclassOf (type->type)) {
			Value *v = ((DependencyObject * ) walk->GetAsDependencyObject ())->GetValue (dep);
			if (v) {
				item = v->AsDependencyObject ();
				dep = NULL;
			}
			// note: if !v then the default collection is NULL (e.g. PathFigureCollection)
		}
	}

	if (!item) {
		item = element_info->GetType ()->CreateInstance ();

		if (item) {
			if (parser_info->loader)
				item->SetSurface (parser_info->loader->GetSurface ());
			
			// in case we must store the collection into the parent
			if (dep && dep->GetPropertyType() == type->type) {
				MoonError err;
				Value item_value (item);
				if (!((DependencyObject * ) walk->GetAsDependencyObject ())->SetValueWithError (NULL/* XXX */, dep, &item_value, &err))
					parser_error (parser_info, element_name, NULL, err.code, err.message);
			}
			
			parser_info->AddCreatedElement (item);
		}
		else {
			parser_error (parser_info, element_name, NULL, 2007,
				      g_strdup_printf ("Unknown element: %s.", element_name));
		}
	}

	return item;
}

bool
XamlElementInstanceNative::SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value)
{
	return dependency_object_set_property (p, this, property, value);
}

bool
XamlElementInstanceNative::SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char *value)
{
	char **prop_name = g_strsplit (property->element_name, ".", -1);
	Type *owner = Type::Find (prop_name [0]);
	DependencyProperty *dep;

	if (!owner)
		return false;

	dep = item->GetDependencyProperty (prop_name [1]);
	if (!dep)
		return false;

	return xaml_set_property_from_str (item, dep, value, p->loader->GetSurface()->IsSilverlight2 ());
}

void
XamlElementInstanceNative::AddChild (XamlParserInfo *p, XamlElementInstance *child)
{
	dependency_object_add_child (p, this, child);
}

void
XamlElementInstanceNative::SetAttributes (XamlParserInfo *p, const char **attr)
{
	dependency_object_set_attributes (p, this, attr);
}


XamlElementInstanceValueType::XamlElementInstanceValueType (XamlElementInfoNative *element_info, XamlParserInfo *parser_info, const char *name, ElementType type) :
	XamlElementInstance (element_info, name, type)
{
	this->element_info = element_info;
	this->parser_info = parser_info;
}

bool
XamlElementInstanceValueType::CreateValueItemFromString (const char* str)
{

	bool res = value_from_str (element_info->GetType ()->GetKind (), NULL, str, &value_item, parser_info->loader->GetSurface()->IsSilverlight2 ());
	return res;
}

void
XamlElementInstanceValueType::SetAttributes (XamlParserInfo *p, const char **attr)
{
	value_type_set_attributes (p, this, attr);
}

const char *
XamlElementInfoManaged::GetContentProperty (XamlParserInfo *p)
{
	if (!p->loader)
		return NULL;

	// TODO: We could cache this, but for now lets keep things as simple as possible.
	const char *res = p->loader->GetContentPropertyName (obj);
	if (res)
		return res;
	return XamlElementInfo::GetContentProperty (p);
}

XamlElementInstance *
XamlElementInfoManaged::CreateElementInstance (XamlParserInfo *p)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, GetName (), XamlElementInstance::ELEMENT, obj);

	if (obj->Is (Type::DEPENDENCY_OBJECT)) {
		if (p->loader)
			inst->GetAsDependencyObject ()->SetSurface (p->loader->GetSurface ());
		p->AddCreatedElement (inst->GetAsDependencyObject ());
	}

	return inst;
}

XamlElementInstance *
XamlElementInfoManaged::CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, GetName (), XamlElementInstance::ELEMENT, new Value (o));

	if (p->loader)
		inst->GetAsDependencyObject ()->SetSurface (p->loader->GetSurface ());
	p->AddCreatedElement (inst->GetAsDependencyObject ());

	return inst;
}

XamlElementInstance *
XamlElementInfoManaged::CreatePropertyElementInstance (XamlParserInfo *p, const char *name)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, name, XamlElementInstance::PROPERTY, obj);

	if (obj->Is (Type::DEPENDENCY_OBJECT)) {
		if (p->loader)
			inst->GetAsDependencyObject ()->SetSurface (p->loader->GetSurface ());
		p->AddCreatedElement (inst->GetAsDependencyObject ());
	}

	return inst;
}

XamlElementInstanceManaged::XamlElementInstanceManaged (XamlElementInfo *info, const char *name, ElementType type, Value *obj) :
	XamlElementInstance (info, name, type)
{
	this->obj = obj;

	if (obj->Is (Type::DEPENDENCY_OBJECT))
		this->SetDependencyObject (obj->AsDependencyObject ());
}

void *
XamlElementInstanceManaged::GetManagedPointer ()
{
	if (obj->Is (Type::DEPENDENCY_OBJECT)) {
		
		return obj->AsDependencyObject ();
	}
	return obj->AsManagedObject ();
}

bool
XamlElementInstanceManaged::SetProperty (XamlParserInfo *p, XamlElementInstance *property, XamlElementInstance *value)
{
	if (GetAsDependencyObject () != NULL && dependency_object_set_property (p, this, property, value))
		return true;
	return p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, ((XamlElementInfoManaged *) info)->xmlns, GetManagedPointer (), property->element_name, value->GetAsValue ());
}

bool
XamlElementInstanceManaged::SetProperty (XamlParserInfo *p, XamlElementInstance *property, const char *value)
{
	Value v = Value (value);
	return p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, ((XamlElementInfoManaged *) info)->xmlns, GetManagedPointer (), property->element_name, &v);
}

void
XamlElementInstanceManaged::AddChild (XamlParserInfo *p, XamlElementInstance *child)
{
	dependency_object_add_child (p, this, child);
}

void
XamlElementInstanceManaged::SetAttributes (XamlParserInfo *p, const char **attr)
{
	dependency_object_set_attributes (p, this, attr);
}

bool
XamlElementInstanceManaged::TrySetContentProperty (XamlParserInfo *p, const char *value)
{
	if (!XamlElementInstance::TrySetContentProperty (p, value)) {
		const char* prop_name = info->GetContentProperty (p);
		if (!prop_name || !p->cdata_content)
			return false;
		Value v = Value (value);
		return p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, ((XamlElementInfoManaged *) info)->xmlns, GetManagedPointer (), prop_name, &v);
	}
	return true;
}

bool
XamlElementInstanceManaged::SetAttachedProperty (XamlParserInfo *p, XamlElementInstance *target, XamlElementInstance *value)
{
	if (!element_type == PROPERTY) {
		printf ("somehow we are setting an attached property on an ELEMENT instance (%s).", element_name);
		return false;
	}

	p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, ((XamlElementInfoManaged *) info)->xmlns, target->GetManagedPointer (), element_name, value->GetAsValue ());
}

XamlElementInstance *
XamlElementInfoImportedManaged::CreateElementInstance (XamlParserInfo *p)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, name, XamlElementInstance::ELEMENT, obj);

	return inst;
}

const char *
XamlElementInfoImportedManaged::GetContentProperty (XamlParserInfo *p)
{
	if (!p->loader)
		return NULL;

	// TODO: Test, it's possible that managed objects that aren't DOs are allowed to have content properties.
	if (!obj->Is (Type::DEPENDENCY_OBJECT))
		return XamlElementInfo::GetContentProperty (p);

	
	// TODO: We could cache this, but for now lets keep things as simple as possible.
	const char *res = p->loader->GetContentPropertyName (obj->AsDependencyObject ());
	if (res)
		return res;
	
	return XamlElementInfo::GetContentProperty (p);
}

XamlElementInstance *
XamlElementInfoImportedManaged::CreateWrappedElementInstance (XamlParserInfo *p, DependencyObject *o)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, o->GetTypeName (), XamlElementInstance::ELEMENT, new Value (o));

	return inst;
}

XamlElementInstance *
XamlElementInfoImportedManaged::CreatePropertyElementInstance (XamlParserInfo *p, const char *name)
{
	XamlElementInstanceManaged *inst = new XamlElementInstanceManaged (this, name, XamlElementInstance::PROPERTY, obj);

	return inst;
}


///
/// Add Child funcs
///

static void
dependency_object_add_child (XamlParserInfo *p, XamlElementInstance *parent, XamlElementInstance *child)
{
	if (parent->element_type == XamlElementInstance::PROPERTY) {
		char **prop_name = g_strsplit (parent->element_name, ".", -1);
		Type *owner = Type::Find (prop_name [0]);

		if (owner) {
			DependencyProperty *dep = DependencyProperty::GetDependencyProperty (owner->type, prop_name [1]);

			g_strfreev (prop_name);

			if (!dep) {
				g_warning ("Unknown element: %s.", parent->element_name);
				return parser_error (p, parent->element_name, NULL, 2007,
						     g_strdup_printf ("Unknown element: %s.", parent->element_name));
			}

			// Don't add the child element, if it is the entire collection
			if (dep->GetPropertyType() == child->info->GetKind ())
				return;

			Type *col_type = Type::Find (dep->GetPropertyType());
			if (!col_type->IsSubclassOf (Type::DEPENDENCY_OBJECT_COLLECTION)
			    && !col_type->IsSubclassOf (Type::RESOURCE_DICTIONARY))
				return;

			// Most common case, we will have a parent that we can snag the collection from
			DependencyObject *obj = (DependencyObject *) parent->parent->GetAsDependencyObject ();
			if (!obj)
				return;

			Value *col_v = obj->GetValue (dep);
			MoonError err;

			if (col_type->IsSubclassOf (Type::DEPENDENCY_OBJECT_COLLECTION)) {
				Collection *col = NULL;

				if (!col_v) {
					col = (Collection *) col_type->CreateInstance ();
					obj->SetValue (dep, Value (col));
				} else {
					col = (Collection *) col_v->AsCollection ();
				}

				Value child_val (child->GetAsDependencyObject ());
				if (-1 == col->AddWithError (&child_val, &err))
					return parser_error (p, child->element_name, NULL, err.code, err.message);
			}
			else if (col_type->IsSubclassOf (Type::RESOURCE_DICTIONARY)) {
				ResourceDictionary *dict = NULL;

				if (!col_v) {
					dict = (ResourceDictionary*) col_type->CreateInstance ();
					obj->SetValue (dep, Value (dict));
				}
				else {
					dict = (ResourceDictionary*) col_v->AsResourceDictionary ();
				}

				char *key = child->GetKey ();

				if (key == NULL) {
					// XXX don't know the proper values here...
					return parser_error (p, child->element_name, NULL, 2007,
							     "You must specify an x:Key or x:Name for elements in a ResourceDictionary");
				}


				Value *child_as_value = child->GetAsValue ();

				bool added = dict->AddWithError (key, child_as_value, &err);
				delete child_as_value;
				if (!added)
					return parser_error (p, child->element_name, NULL, err.code, err.message);
			}

			return;
		}
		else {
			g_strfreev (prop_name);

			/* 
			parent->SetProperty (p, 

			void *parent_ptr = parent->GetKind () == Type::MANAGED ? parent->GetManagedPointer () : parent->item;
			void *child_ptr = child->GetKind () == Type::MANAGED ? child->GetManagedPointer () : child->item;

			bool
XamlLoader::
			// This might be a property of a managed object
			if (p->loader && p->loader->SetProperty (p->top_element->item, const char* xmlns, void *target, const char *name, Value *value)) {
				return;
			}
			

			g_warning ("Unknown element 3: %s.", parent->element_name);
			return parser_error (p, parent->element_name, NULL, 2007,
					     g_strdup_printf ("Unknown element: %s.", parent->element_name));
			*/
		}

	}

#if SL_2_0
	if (Type::Find (parent->info->GetKind ())->IsSubclassOf (Type::FRAMEWORKTEMPLATE)) {
		FrameworkTemplate *t = (FrameworkTemplate*) parent->GetAsDependencyObject ();

		// XXX a SetVisualTreeWithError, maybe?
		t->SetVisualTree ((FrameworkElement*) child->GetAsDependencyObject ());
		return;
	}
	else 
#endif
	if (Type::Find (parent->info->GetKind ())->IsSubclassOf (Type::DEPENDENCY_OBJECT_COLLECTION)) {
		Collection *col = (Collection *) parent->GetAsDependencyObject ();
		MoonError err;
		Value child_val ((DependencyObject*)child->GetAsDependencyObject ());

		if (-1 == col->AddWithError (&child_val, &err))
			return parser_error (p, child->element_name, NULL, err.code, err.message);
		return;
	}
	else if (Type::Find (parent->info->GetKind ())->IsSubclassOf (Type::RESOURCE_DICTIONARY)) {
		ResourceDictionary *dict = (ResourceDictionary *) parent->GetAsDependencyObject ();
		MoonError err;
		char *key = child->GetKey ();

		Value *child_as_value = child->GetAsValue ();
		bool added = dict->AddWithError (key, child_as_value, &err);
		delete child_as_value;

		if (!added)
			return parser_error (p, child->element_name, NULL, err.code, err.message);
	}


	if (parent->element_type != XamlElementInstance::PROPERTY)
		parent->TrySetContentProperty (p, child);

	// Do nothing if we aren't adding to a collection, or a content property collection
}


///
/// set property funcs
///

// these are just a bunch of special cases
static void
dependency_object_missed_property (XamlElementInstance *item, XamlElementInstance *prop, XamlElementInstance *value, char **prop_name)
{

}

static bool
dependency_object_set_property (XamlParserInfo *p, XamlElementInstance *item, XamlElementInstance *property, XamlElementInstance *value)
{
	char **prop_name = g_strsplit (property->element_name, ".", -1);
	DependencyObject *dep = item->GetAsDependencyObject ();
	DependencyProperty *prop = NULL;
	bool res;

	if (Type::Find (property->info->GetKind ())->IsValueType ()) {
		parser_error (p, item->element_name, NULL, -1, g_strdup_printf ("Value types (%s) do not have properties.", property->element_name));
		g_strfreev (prop_name);
		return false;
	}

	if (!property->IsDependencyObject ()) {
		// Kinda an ugly assumption: It's not a DO and it's not a ValueType so it must be Managed.
		XamlElementInstanceManaged *mp = (XamlElementInstanceManaged *) property;
		g_strfreev (prop_name);

		return mp->SetAttachedProperty (p, item, value);
	}

	if (!dep) {
		// FIXME is this really where this check should live
		parser_error (p, item->element_name, NULL, 2030,
			      g_strdup_printf ("Property element %s cannot be used inside another property element.",
					       property->element_name));

		g_strfreev (prop_name);
		return false;
	}

	prop = DependencyProperty::GetDependencyProperty (item->info->GetKind (), prop_name [1]);

	if (prop) {
		if (prop->IsReadOnly ()) {
			parser_error (p, item->element_name, NULL, 2014,
				      g_strdup_printf ("The attribute %s is read only and cannot be set.", prop->GetName()));
			res = false;
		} else if (Type::Find (value->info->GetKind ())->IsSubclassOf (prop->GetPropertyType())) {
			// an empty collection can be NULL and valid
			if (value->GetAsDependencyObject ()) {
				if (item->IsPropertySet (prop->GetName())) {
					parser_error (p, item->element_name, NULL, 2033,
						g_strdup_printf ("Cannot specify the value multiple times for property: %s.",
								 property->element_name));
					res = false;
				} else {
					MoonError err;

					if (!dep->SetValueWithError (NULL, prop, value->GetAsValue (), &err))
						parser_error (p, item->element_name, NULL, err.code, err.message);
					
					item->MarkPropertyAsSet (prop->GetName());
					res = true;
				}
			}
		} else {
			// TODO: Do some error checking in here, this is a valid place to be
			// if we are adding a non collection to a collection, so the non collection
			// item will have already been added to the collection in add_child
			res = false;
		}
	} else {
		dependency_object_missed_property (item, property, value, prop_name);
		res = false;
	}

	g_strfreev (prop_name);
	return res;
}

bool
xaml_set_property_from_str (DependencyObject *obj, DependencyProperty *prop, const char *value, bool sl2)
{
	Value *v = NULL;
	
	if (!value_from_str (prop->GetPropertyType(), prop->GetName(), value, &v, sl2))
		return false;
	
	// it's possible for (a valid) value to be NULL (and we must keep the default value)
	if (v) {
		obj->SetValue (prop, v);
		delete v;
	}
	
	return true;
}

bool
xaml_is_valid_event_name (const char *name)
{
	return (!strcmp (name, "ImageFailed") ||
		!strcmp (name, "MediaEnded") ||
		!strcmp (name, "MediaFailed") ||
		!strcmp (name, "MediaOpened") ||
		!strcmp (name, "BufferingProgressChanged") ||
		!strcmp (name, "CurrentStateChanged") ||
		!strcmp (name, "DownloadProgressChanged") ||
		!strcmp (name, "MarkerReached") ||
		!strcmp (name, "Completed") ||
		!strcmp (name, "DownloadFailed") ||
		!strcmp (name, "FullScreenChange") ||
		!strcmp (name, "Resize") ||
		!strcmp (name, "Error") ||
		!strcmp (name, "Completed") ||
		!strcmp (name, "ImageFailed") ||
		!strcmp (name, "GotFocus") ||
		!strcmp (name, "KeyDown") ||
		!strcmp (name, "KeyUp") ||
		!strcmp (name, "Loaded") ||
		!strcmp (name, "LostFocus") ||
		!strcmp (name, "MouseEnter") ||
		!strcmp (name, "MouseLeave") ||
		!strcmp (name, "MouseLeftButtonDown") ||
		!strcmp (name, "MouseLeftButtonUp") ||
		!strcmp (name, "MouseMove"));
}

/*
static bool
dependency_object_hookup_event (XamlParserInfo *p, XamlElementInstance *item, const char *name, const char *value)
{
	if (is_valid_event_name (name)) {
		if (!strncmp (value, "javascript:", strlen ("javascript:"))) {
			parser_error (p, item->element_name, name, 2024,
				      g_strdup_printf ("Invalid attribute value %s for property %s.",
						       value, name));
			return true;
		}

		if (!p->loader) {
			parser_error (p, item->element_name, name, -1,
				      g_strdup_printf ("No hookup event callback handler installed '%s' event will not be hooked up\n", name));
			return true;
		}

		if (p->loader)
		// TODO: Do a set property here to hookup events, lets just think of events as proeprties and make the managed code handle it.
		// p->loader->HookupEvent (item->item, p->top_element->item, name, value);
		
		return false;
	}

	return true;
}
*/

static void
value_type_set_attributes (XamlParserInfo *p, XamlElementInstance *item, const char **attr)
{
	// the only attributes value on value types seem to be x:Key
	// and x:Name, but reuse the generic namespace attribute stuff
	// anyway.
	//
	for (int i = 0; attr [i]; i += 2) {
		// Skip empty attrs
		if (attr[i + 1] == NULL || attr[i + 1][0] == '\0')
			continue;

		char **attr_name = g_strsplit (attr [i], "|", -1);

		if (attr_name [1]) {
			
			XamlNamespace *ns = (XamlNamespace *) g_hash_table_lookup (p->namespace_map, attr_name [0]);

			if (!ns)
				return parser_error (p, item->element_name, attr [i], 5055,
						g_strdup ("undeclared prefix"));

			bool reparse = false;
			ns->SetAttribute (p, item, attr_name [1], attr [i + 1], &reparse);

			g_strfreev (attr_name);

			// Setting managed attributes can cause errors galore
			if (p->error_args)
				return;

			continue;
		}

		g_strfreev (attr_name);
	}
}

static void
dependency_object_set_attributes (XamlParserInfo *p, XamlElementInstance *item, const char **attr)
{
	int skip_attribute = -1;

start_parse:
	for (int i = 0; attr [i]; i += 2) {
		if (i == skip_attribute)
			continue;

		// Skip empty attrs
		if (attr[i + 1] == NULL || attr[i + 1][0] == '\0')
			continue;
		
		// Setting attributes like x:Class can change item->item, so we
		// need to make sure we have an up to date pointer
		DependencyObject *dep = item->GetAsDependencyObject ();
		char **attr_name = g_strsplit (attr [i], "|", -1);

		if (attr_name [1]) {
			XamlNamespace *ns = (XamlNamespace *) g_hash_table_lookup (p->namespace_map, attr_name [0]);

			if (!ns)
				return parser_error (p, item->element_name, attr [i], 5055,
						g_strdup ("undeclared prefix"));

			bool reparse = false;
			ns->SetAttribute (p, item, attr_name [1], attr [i + 1], &reparse);

			g_strfreev (attr_name);

			// Setting managed attributes can cause errors galore
			if (p->error_args)
				return;

			if (reparse) {
				skip_attribute = i;
				i = 0;
				item->ClearSetProperties ();
				goto start_parse;
			}
			continue;
		}

		g_strfreev (attr_name);

		const char *pname = attr [i];
		char *atchname = NULL;
		for (int a = 0; attr [i][a]; a++) {
			if (attr [i][a] != '.')
				continue;
			atchname = g_strndup (attr [i], a);
			pname = attr [i] + a + 1;
			break;
		}

		DependencyProperty *prop = NULL;
		if (atchname) {
			Type *attached_type = Type::Find (atchname);
			if (attached_type)
				prop = DependencyProperty::GetDependencyProperty (attached_type->type, pname);
		} else {
			prop = DependencyProperty::GetDependencyProperty (item->info->GetKind (), pname);
		}

		if (prop) {
			if (prop == DependencyObject::NameProperty) {
				// XXX toshok - I don't like doing this here... but it fixes airlines.
				item->SetKey (attr[i+1]);
			}

			if (prop->IsReadOnly ()) {
				parser_error (p, item->element_name, NULL, 2014,
					      g_strdup_printf ("The attribute %s is read only and cannot be set.", prop->GetName()));
				if (atchname)
					g_free (atchname);
				return;
			} 

			if (item->IsPropertySet (prop->GetName())) {
				parser_error (p, item->element_name, attr [i], 2033,
					      g_strdup_printf ("Cannot specify the value multiple times for property: %s.", prop->GetName()));
				if (atchname)
					g_free (atchname);
				return;
			}

			Value *v = NULL;
			if (attr[i+1][0] == '{' && attr[i+1][strlen(attr[i+1]) - 1] == '}') {
				const char *start = attr[i+1] + 1; // skip the initial {
				while (*start && isspace (*start)) start++;

				if (!strncmp (start, "StaticResource ", strlen ("StaticResource "))) {
					start += strlen ("StaticResource ");

					while (*start && isspace (*start)) start++;

					if (*start == '}') {
						parser_error (p, item->element_name, attr[i], 2024,
							      g_strdup_printf ("Empty StaticResource reference for property %s.",
									       attr[i]));
						return;
					}

					char *resource_name = g_strdup (start);

					/* now trim off the trailing } and any spaces after the resource name */
					char *end = resource_name + (strlen (resource_name) - 2);
					while (end != resource_name && isspace (*end))
						end --;
					end++;
					*end = '\0';

					if (p->current_element)
						p->current_element->LookupNamedResource (resource_name, &v);

					if (!v) {
						// XXX don't know the proper values here...
						parser_error (p, item->element_name, attr[i], 2024,
							      g_strdup_printf ("Could not locate StaticResource %s for property %s.",
									       resource_name,
									       attr[i]));
						g_free (resource_name);
						return;
					}
				}
#if SL_2_0
				else if (!strncmp (start, "TemplateBinding ", strlen ("TemplateBinding "))) {
					XamlElementInstance *parent = item->parent;

					while (parent && !parent->IsTemplate())
						parent = parent->parent;

					if (!parent) {
						parser_error (p, item->element_name, attr[i], 2024,
							      g_strdup ("TemplateBinding expression found outside of a template"));
						return;
					}

					XamlElementInstanceTemplate *template_parent = (XamlElementInstanceTemplate*)parent;

					start += strlen ("TemplateBinding ");

					while (*start && isspace (*start)) start++;

					if (*start == '}') {
						parser_error (p, item->element_name, attr[i], 2024,
							      g_strdup_printf ("Empty TemplateBinding reference for property %s.",
									       attr[i]));
						return;
					}

					char *argument = g_strdup (start);

					/* now trim off the trailing } and any spaces after the resource name */
					char *end = argument + (strlen (argument) - 2);
					while (end != argument && isspace (*end))
						end --;
					end++;
					*end = '\0';

					template_parent->AddTemplateBinding (item, argument, attr[i]);
					return;
				}
#endif
			}

			if (!v && !value_from_str (prop->GetPropertyType(), prop->GetName(), attr [i + 1], &v, p->loader->GetSurface()->IsSilverlight2())) {
				if (prop->GetPropertyType () == Type::MANAGED || prop->GetPropertyType() == Type::OBJECT) {
					Value v = Value (g_strdup (attr [i + 1]));
					if (p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, NULL, item->GetManagedPointer (), g_strdup (prop->GetName ()), &v))
						return;
				}

				parser_error (p, item->element_name, attr [i], 2024,
					      g_strdup_printf ("Invalid attribute value %s for property %s.",
							       attr [i + 1], attr [i]));
				if (atchname)
					g_free (atchname);
				return;
			}

			if (v) {
				MoonError err;

				if (!dep->SetValueWithError (NULL, prop, v, &err))
					parser_error (p, item->element_name, attr [i], err.code, err.message);
				else
					item->MarkPropertyAsSet (prop->GetName());
				
				delete v;
			} else {
				if (!prop->IsNullable ())
					parser_error (p, item->element_name, attr [i], 2017, g_strdup_printf ("Null is not a legal value for attribute %s.", attr [i]));
				else
					dep->SetValue (prop, NULL);
			}
		} else {
			Value v = Value (attr [i + 1]);
			if (!p->loader || !p->loader->SetProperty (p->top_element ? p->top_element->GetManagedPointer () : NULL, NULL, item->GetManagedPointer (), attr [i], &v)) {
				if (atchname)
					g_free (atchname);
				parser_error (p, item->element_name, attr [i], 2012,
					      g_strdup_printf ("Unknown attribute %s on element %s.",
							       attr [i], item->element_name));
				continue;
			}
		}

		if (atchname)
			g_free (atchname);
	}
}

void
xaml_init (void)
{
	default_namespace = new DefaultNamespace ();
	x_namespace = new XNamespace ();
}
